/**
NAME: Nevin Liang
EMAIL: nliang868@g.ucla.edu
ID: *********
**/

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <netdb.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <zlib.h>
#include <signal.h>

void handler(int signal) {
    fprintf(stderr, "error: SIGPIPE has been raised with signal %d\n", signal);
    return;
}

int main(int argc, char **argv) {
    static struct option opts[] = {
        {"port",        required_argument,  0, 'p'},
        {"compress",    no_argument, 0, 'c'},
        {0, 0, 0, 0}
    };
    int opt_it = 0;
    int comp = 0;
    int port = -1;
    while (1) {
        int opt = getopt_long(argc, argv, "p:c", opts, &opt_it);
        if (opt == -1) break;
        switch (opt) {
            case 'p':
                port = atoi(optarg);
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

    z_stream zbash, zclient;
    zbash.zalloc = Z_NULL, zbash.zfree = Z_NULL, zbash.opaque = Z_NULL;
    zbash.avail_in = 0, zbash.next_in = Z_NULL;
    if (deflateInit(&zbash, Z_DEFAULT_COMPRESSION) != Z_OK) {
        fprintf(stderr, "error completing deflateInit for zbash in server\n");
        exit(1);
    }
    zclient.zalloc = Z_NULL, zclient.zfree = Z_NULL, zclient.opaque = Z_NULL;
    zclient.avail_in = 0, zclient.next_in = Z_NULL;
    if (inflateInit(&zclient) != Z_OK) {
        fprintf(stderr, "error completing inflateInit for zclient in server\n");
        exit(1);
    }

    struct sockaddr_in serv_addr, client_addr;
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    int backlog = 5;
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    bind(listenfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    listen(listenfd, backlog);
    socklen_t addrlen;
    int newfd = accept(listenfd, (struct sockaddr *) &client_addr, &addrlen);

    signal(SIGPIPE, handler);
    int pipePC[2], pipeCP[2];
    if (pipe(pipePC) < 0) {
        fprintf(stderr, "error in pipe parent->child creation: %s\n", strerror(errno));
        exit(1);
    }
    if (pipe(pipeCP) < 0) {
        fprintf(stderr, "error in pipe child->parent creation: %s\n", strerror(errno));
        exit(1);
    }
    int pid;
    if ((pid = fork()) < 0) {
        fprintf(stderr, "error forking: %s\n", strerror(errno));
        exit(1);
    }
    if (pid == 0) {
        if (close(pipePC[1]) < 0) {
            fprintf(stderr, "error closing PC pipe write fd: %s\n", strerror(errno));
            exit(1);
        }
        if (close(pipeCP[0]) < 0) {
            fprintf(stderr, "error closing CP pipe read fd: %s\n", strerror(errno));
            exit(1);
        }
        if (close(STDIN_FILENO) < 0) {
            fprintf(stderr, "error closing stdin fd: %s\n", strerror(errno));
            exit(1);
        }
        if (dup(pipePC[0]) < 0) {
            fprintf(stderr, "error duping PC pipe read fd: %s\n", strerror(errno));
            exit(1);
        }
        if (close(pipePC[0]) < 0) {
            fprintf(stderr, "error closing PC pipe read fd: %s\n", strerror(errno));
            exit(1);
        }
        
        if (close(STDOUT_FILENO) < 0) {
            fprintf(stderr, "error closing stdout fd: %s\n", strerror(errno));
            exit(1);
        }
        if (close(STDERR_FILENO) < 0) {
            fprintf(stderr, "error closing stderr fd: %s\n", strerror(errno));
            exit(1);
        }
        if (dup(pipeCP[1]) < 0) {
            fprintf(stderr, "error duping CP pipe write fd: %s\n", strerror(errno));
            exit(1);
        }
        if (dup(pipeCP[1]) < 0) {
            fprintf(stderr, "error duping CP pipe write fd: %s\n", strerror(errno));
            exit(1);
        }
        if (close(pipeCP[1]) < 0) {
            fprintf(stderr, "error closing CP pipe write fd: %s\n", strerror(errno));
            exit(1);
        }

        execlp("/bin/bash", "/bin/bash", (char*)NULL);
        fprintf(stderr, "error exec bash process: %s\n", strerror(errno));
        exit(1);
    }
    else {
        if (close(pipePC[0]) < 0) {
            fprintf(stderr, "error closing PC pipe read fd: %s\n", strerror(errno));
            exit(1);
        }
        if (close(pipeCP[1]) < 0) {
            fprintf(stderr, "error closing CP pipe write fd: %s\n", strerror(errno));
            exit(1);
        }

        struct pollfd term[2];
        int fromBash = 0, fromSock = 1;

        term[fromBash].fd = pipeCP[0];
        term[fromSock].fd = newfd;

        term[fromBash].events = term[fromSock].events = POLLIN | POLLHUP | POLLERR;

        int polling = 1;
        while (polling) {
            int retval = poll(term, (unsigned long)2, -1);
            if (retval < 0) {
                fprintf(stderr,"error while polling: %s\n", strerror(errno));
                exit(1);
            }
            for (int i = 0; i < 2; i++) {
                if (term[i].revents != 0) {
                    if (term[i].revents & POLLIN) {
                        unsigned char buf[256];
                        if (i == fromBash) {
                            int inp;
                            if ((inp = read(pipeCP[0], buf, 256)) < 0) {
                                fprintf(stderr, "error reading from CP pipe: %s\n", strerror(errno));
                                exit(1);
                            }
                            if (comp) {
                                unsigned char outbuf[256];
                                zbash.avail_in = inp;
                                zbash.next_in = buf;
                                zbash.avail_out = sizeof outbuf;
                                zbash.next_out = outbuf;
                                do {
                                    (void) deflate(&zbash, Z_SYNC_FLUSH);
                                    int newinp = sizeof outbuf - zbash.avail_out;
                                    for (unsigned char* e = outbuf; e < outbuf + newinp; e++) {
                                        if (write(newfd, e, 1) < 0) {
                                            fprintf(stderr, "error writing to stdout: %s\n", strerror(errno));
                                            exit(1);
                                        }
                                    }
                                } while (zbash.avail_in > 0);
                            }
                            else {
                                for (unsigned char* e = buf; e < buf + inp; e++) {
                                    if (*e == 0x04) {
                                        polling = 0;
                                    }
                                    else {
                                        if (write(newfd, e, 1) < 0) {
                                            fprintf(stderr, "error writing to stdout: %s\n", strerror(errno));
                                            exit(1);
                                        }
                                    }
                                }
                            }
                        }
                        if (i == fromSock) {
                            int inp;
                            if ((inp = read(newfd, buf, 256)) < 0) {
                                fprintf(stderr, "error reading from socket fd: %s\n", strerror(errno));
                                exit(1);
                            }
                            if (comp) {
                                unsigned char outbuf[256];
                                zclient.avail_in = inp;
                                zclient.next_in = buf;
                                zclient.avail_out = sizeof outbuf;
                                zclient.next_out = outbuf;
                                do {
                                    (void) inflate(&zclient, Z_SYNC_FLUSH);
                                    int newinp = sizeof outbuf - zclient.avail_out;
                                    for (unsigned char* e = outbuf; e < outbuf + newinp; e++) {
                                        if (*e == 0x0A || *e == 0x0D) {
                                            if (write(pipePC[1], "\n", 1) < 0) {
                                                fprintf(stderr, "error writing to stdout: %s\n", strerror(errno));
                                                exit(1);
                                            }
                                        }
                                        else if (*e == 0x04) {
                                            if (pipePC[1] != -1) {
                                                if (close(pipePC[1]) < 0) {
                                                    fprintf(stderr, "error closing PC pipe write fd: %s\n", strerror(errno));
                                                    exit(1);
                                                }
                                                pipePC[1] = -1;
                                            }
                                        }
                                        else if (*e == 0x03) {
                                            if (kill(pid, SIGINT) < 0) {
                                                fprintf(stderr, "error sending kill signal: %s\n", strerror(errno));
                                                exit(1);
                                            }
                                        }
                                        else {
                                            if (write(pipePC[1], e, 1) < 0) {
                                                fprintf(stderr, "error writing to pipePC: %s\n", strerror(errno));
                                                exit(1);
                                            }
                                        }
                                    }
                                } while (zclient.avail_in > 0);
                            }
                            else {
                                for (unsigned char* e = buf; e < buf + inp; e++) {
                                    if (*e == 0x0A || *e == 0x0D) {
                                        if (write(pipePC[1], "\n", 1) < 0) {
                                            fprintf(stderr, "error writing to stdout: %s\n", strerror(errno));
                                            exit(1);
                                        }
                                    }
                                    else if (*e == 0x04) {
                                        if (pipePC[1] != -1) {
                                            if (close(pipePC[1]) < 0) {
                                                fprintf(stderr, "error closing PC pipe write fd: %s\n", strerror(errno));
                                                exit(1);
                                            }
                                            pipePC[1] = -1;
                                        }
                                    }
                                    else if (*e == 0x03) {
                                        if (kill(pid, SIGINT) < 0) {
                                            fprintf(stderr, "error sending kill signal: %s\n", strerror(errno));
                                            exit(1);
                                        }
                                    }
                                    else {
                                        if (write(pipePC[1], e, 1) < 0) {
                                            fprintf(stderr, "error writing to pipePC: %s\n", strerror(errno));
                                            exit(1);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (term[i].revents & (POLLHUP | POLLERR))
                        polling = 0;
                }
            }
        }
        if (pipePC[1] != -1) {
            if (close(pipePC[1]) < 0) {
                fprintf(stderr, "error closing write end of pipePC: %s\n", strerror(errno));
                exit(1);
            }
        }
        int retstat;
        if (waitpid(pid, &retstat, 0) < 0) {
            fprintf(stderr, "error child has not exited: %s\n", strerror(errno));
            exit(1);
        }
        int exitsig = retstat & 0x007f, status = (retstat & 0xff00) >> 8;
        fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\r\n", exitsig, status);

        shutdown(listenfd, SHUT_RDWR);
        if (deflateEnd(&zbash) == Z_STREAM_ERROR) {
            fprintf(stderr, "error in deflateEnd\n");
        }
        if (inflateEnd(&zclient) == Z_STREAM_ERROR) {
            fprintf(stderr, "error in deflateEnd\n");
        }
        exit(0);
    }
}
