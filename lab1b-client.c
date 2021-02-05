/**
NAME: Nevin Liang
EMAIL: nliang868@g.ucla.edu
ID: 705575353
**/

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <poll.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <zlib.h>

int main(int argc, char **argv) {

    static struct option opts[] = {
        {"port",        required_argument,  0, 'p'},
        {"log",         required_argument,  0, 'l'},
        {"compress",    no_argument,        0, 'c'},
        {0, 0, 0, 0}
    };
    int opt_it = 0;
    int port = -1;
    int comp = 0;
    char* filename = NULL;
    while (1) {
        int opt = getopt_long(argc, argv, "p:l:c", opts, &opt_it);
        if (opt == -1) break;
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'l':
                filename = optarg;
                break;
            case 'c':
                comp = 1;
                break;
            default:
                fprintf(stderr, "incorrect usage\n");
                exit(1);
        }
    }
    if (port == -1) {
        fprintf(stderr, "--port flag is mandatory!\n");
        exit(1);
    }
    int logfd = -1;
    if (filename != NULL) {
        if ((logfd = open(filename, O_WRONLY | O_CREAT | O_APPEND, S_IRWXU)) < 0) {
            fprintf(stderr, "error opening log file\n");
            exit(1);
        }
        struct rlimit l_limit = {10000, 10000};
        setrlimit(RLIMIT_FSIZE, &l_limit);
    }
    
    struct termios new, orig;
    if (tcgetattr(STDIN_FILENO, &orig) < 0) {
        fprintf(stderr, "error getting attribute for stdin: %s\n", strerror(errno));
        exit(1);
    }
    new = orig;
    new.c_iflag = ISTRIP, new.c_oflag = new.c_lflag = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new) < 0) {
        fprintf(stderr, "error setting attribute for stdin: %s\n", strerror(errno));
        exit(1);
    }

    z_stream zstdin, zserver;
    zstdin.zalloc = Z_NULL;
    zstdin.zfree = Z_NULL;
    zstdin.opaque = Z_NULL;
    zstdin.avail_in = 0, zstdin.next_in = Z_NULL;
    if (deflateInit(&zstdin, Z_DEFAULT_COMPRESSION) != Z_OK) {
        fprintf(stderr, "error completing deflateInit for zstdin in client\n");
        exit(1);
    }
    zserver.zalloc = Z_NULL;
    zserver.zfree = Z_NULL;
    zserver.opaque = Z_NULL;
    zserver.avail_in = 0, zserver.next_in = Z_NULL;
    if (inflateInit(&zserver) != Z_OK) {
        fprintf(stderr, "error completing inflateInit for zserver in client\n");
        exit(1);
    }
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    char *host = "localhost";
    struct hostent *server = gethostbyname(host);
    struct sockaddr_in serv_addr;
    bzero((char *) &serv_addr, sizeof(serv_addr));
    bcopy((char *) server->h_addr, (char *) &serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));

    struct pollfd cpoll[2];
    int fromSock = 0, fromKeyb = 1;
    cpoll[fromSock].fd = sockfd;
    cpoll[fromKeyb].fd = STDIN_FILENO;
    cpoll[fromSock].events = cpoll[fromKeyb].events = POLLIN | POLLHUP | POLLERR;

    int polling = 1;
    while (polling) {
        int retval = poll(cpoll, (unsigned long)2, -1);
        if (retval < 0) {
            fprintf(stderr,"error while polling: %s\n", strerror(errno));
            exit(1);
        }
        for (int i = 0; i < 2; i++) {
            if (cpoll[i].revents != 0) {
                if (cpoll[i].revents & POLLIN) {
                    unsigned char buf[256];
                    if (i == fromSock) {
                        int inp = read(sockfd, buf, 256);
                        if (inp < 0) {
                            fprintf(stderr, "error reading from socket (client end): %s\n", strerror(errno));
                            exit(1);
                        }
                        if (inp == 0) {
                            if (tcsetattr(STDIN_FILENO, TCSANOW, &orig) < 0) {
                                fprintf(stderr, "error setting attribute for stdin: %s\n", strerror(errno));
                                exit(1);
                            }
                            exit(0);
                        }
                        if (logfd != -1) {
                            dprintf(logfd, "RECEIVED %d bytes: ", inp);
                            write(logfd, buf, inp);
                            dprintf(logfd, "\n");
                        }
                        if (comp) {
                            unsigned char outbuf[256];
                            zserver.avail_in = inp;
                            zserver.next_in = buf;
                            zserver.avail_out = sizeof outbuf;
                            zserver.next_out = outbuf;
                            do {
                                (void) inflate(&zserver, Z_SYNC_FLUSH);
                                int newinp = sizeof outbuf - zserver.avail_out;
                                for (unsigned char* e = outbuf; e < outbuf + newinp; e++) {
                                    if (*e == 0x0A) {
                                        if (write(STDOUT_FILENO, "\r\n", 2) < 0) {
                                            fprintf(stderr, "error writing to stdout: %s\n", strerror(errno));
                                            exit(1);
                                        }
                                    }
                                    else {
                                        if (write(STDOUT_FILENO, e, 1) < 0) {
                                            fprintf(stderr, "error writing to stdout: %s\n", strerror(errno));
                                            exit(1);
                                        }
                                    }  
                                }
                            } while (zserver.avail_in > 0);
                            
                        }
                        else {
                            for (unsigned char* e = buf; e < buf + inp; e++) {
                                if (*e == 0x0A) {
                                    if (write(STDOUT_FILENO, "\r\n", 2) < 0) {
                                        fprintf(stderr, "error writing to stdout: %s\n", strerror(errno));
                                        exit(1);
                                    }
                                }
                                else {
                                    if (write(STDOUT_FILENO, e, 1) < 0) {
                                        fprintf(stderr, "error writing to stdout: %s\n", strerror(errno));
                                        exit(1);
                                    }
                                }  
                            }
                        }
                        
                    }
                    if (i == fromKeyb) {
                        int inp = read(STDIN_FILENO, buf, 256);
                        if (inp < 0) {
                            fprintf(stderr, "error reading from stdin: %s\n", strerror(errno));
                            exit(1);
                        }
                        for (unsigned char* e = buf; e < buf + inp; e++) {
                            if (*e == 0x0A || *e == 0x0D) {
                                if (write(STDOUT_FILENO, "\n\r", 2) < 0) {
                                    fprintf(stderr, "error writing to stdout: %s\n", strerror(errno));
                                    exit(1);
                                }
                            }
                            else if (*e == 0x04) {
                                if (write(STDOUT_FILENO, "^D", 2) < 0) {
                                    fprintf(stderr, "error writing to stdout: %s\n", strerror(errno));
                                    exit(1);
                                }
                            }
                            else if (*e == 0x03) {
                                if (write(STDOUT_FILENO, "^C", 2) < 0) {
                                    fprintf(stderr, "error writing to stdout: %s\n", strerror(errno));
                                    exit(1);
                                }
                            }
                            else {
                                if (write(STDOUT_FILENO, e, 1) < 0) {
                                    fprintf(stderr, "error writing to stdout: %s\n", strerror(errno));
                                    exit(1);
                                }
                            }
                        }
                        if (comp) {
                            unsigned char outbuf[256];
                            zstdin.avail_in = inp;
                            zstdin.next_in = buf;
                            zstdin.avail_out = sizeof outbuf;
                            zstdin.next_out = outbuf;
                            do {
                                (void) deflate(&zstdin, Z_SYNC_FLUSH);
                                int newinp = sizeof outbuf - zstdin.avail_out;
                                if (write(sockfd, outbuf, newinp) < 0) {
                                    fprintf(stderr, "error writing to socket fd: %s\n", strerror(errno));
                                    exit(1);
                                }
                                if (logfd != -1) {
                                    dprintf(logfd, "SENT %d bytes: ", newinp);
                                    write(logfd, outbuf, newinp);
                                    dprintf(logfd, "\n");
                                }
                            } while (zstdin.avail_in > 0);
                        }
                        else {
                            if (write(sockfd, buf, inp) < 0) {
                                fprintf(stderr, "error writing to socket fd: %s\n", strerror(errno));
                                exit(1);
                            }
                            if (logfd != -1) {
                                dprintf(logfd, "SENT %d bytes: ", inp);
                                write(logfd, buf, inp);
                                dprintf(logfd, "\n");
                            }
                        }
                    }
                }
                else if (cpoll[i].revents & (POLLHUP | POLLERR))
                    polling = 0;
            }
        }
    }
    if (deflateEnd(&zstdin) == Z_STREAM_ERROR) {
        fprintf(stderr, "error in deflateEnd\n");
    }
    if (inflateEnd(&zserver) == Z_STREAM_ERROR) {
        fprintf(stderr, "error in deflateEnd\n");
    }
    if (tcsetattr(STDIN_FILENO, TCSANOW, &orig) < 0) {
        fprintf(stderr, "error setting attribute for stdin: %s\n", strerror(errno));
        exit(1);
    }
}