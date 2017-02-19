
CC=gcc
CFLAGS= -Wall -pthread
LDFLAGS= -lpthread

all: tftp_server run

run: tftp_server
	./tftp_server --port 12345 --dir /tmp

test: tftp_server
	./tftp_server --port 12345 --dir /tmp &
	./tests/run.sh
	pkill tftp_server
	rm -f client_file* server_file*

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
	rm -f flist.o workers.o workers_data.o server.o tftp_server client_file* server_file*
