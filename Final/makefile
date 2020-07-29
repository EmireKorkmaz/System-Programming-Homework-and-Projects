all: client server
	
client: client.c
	gcc -o client client.c -lrt -pthread -Wall -pedantic

server: server.c
	gcc -o server server.c -lrt -pthread -Wall -pedantic

clean:
	rm -f client client.o server server.o