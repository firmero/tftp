
CC=gcc
CFLAGS= -Wall -std=gnu99 -pthread #-g -DDEBUG
LDFLAGS= -std=gnu99 -lpthread #-g

all: tftp_server run

run: tftp_server
	./tftp_server --port 12345 --dir /tmp

tests: simple parallel

simple: tftp_server
	./tftp_server --port 12345 --dir /tmp &
	./tests/simple.sh
	pkill tftp_server
	rm -f client_file* server_file* /tmp/client_file* /tmp/server_file*

parallel: tftp_server
	./tftp_server --port 12345 --dir /tmp &
	./tests/parallel.sh
	pkill tftp_server
	rm -f client_file* server_file* /tmp/client_file* /tmp/server_file*
	
tftp_server: server.o workers.o workers_data.o flist.o
	$(CC) $^ -o $@ $(LDFLAGS) 

server.o: server.c
	$(CC) $(CFLAGS) $^ -c -o $@

workers.o: workers.c workers.h workers_data.o flist.o
	$(CC) $(CFLAGS) $< -c -o $@

workers_data.o: workers_data.c flist.o
	$(CC) $(CFLAGS) $< -c -o $@

flist.o: flist.c  flist.h
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -f flist.o workers.o workers_data.o server.o tftp_server client_file* server_file* /tmp/client_file* /tmp/server_file*
