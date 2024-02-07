all: client server
client.o : client.c
	$(CC) -c client.c
client: client.o
	$(CC) -o client client.o -lpthread -lm -D_GNU_SOURCE -lgsl -lgslcblas
server.o: server.c
	$(CC) -c server.c
server: server.o
	$(CC) -o server server.o -D_GNU_SOURCE -lhiredis
clean:
	rm *.o client server

