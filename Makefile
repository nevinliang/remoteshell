# NAME: Nevin Liang
# EMAIL: nliang868@g.ucla.edu
# ID: 705575353

lab1b: client server

client: lab1b-client.c
	gcc lab1b-client.c -o lab1b-client -Wall -Wextra -lz

server: lab1b-server.c
	gcc lab1b-server.c -o lab1b-server -Wall -Wextra -lz

clean:
	rm -f lab1b-server lab1b-client
	rm -f lab1b-705575353.tar.gz

dist: lab1b-client.c lab1b-server.c README Makefile
	tar -czf lab1b-705575353.tar.gz lab1b-client.c lab1b-server.c README Makefile

