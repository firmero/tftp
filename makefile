
all: tftp_server run

run:
	./tftp_server --port 12345 --dir /tmp/


tftp_server: server.o workers.o workers_data.o flist.o
	cc -lpthread $^ -o $@ 

server.o: server.c
	cc $^ -c -o $@

workers.o: workers.c workers.h workers_data.o flist.o
	cc $< -c -o $@

workers_data.o: workers_data.c flist.o
	cc $< -c -o $@

flist.o: flist.c  flist.h
	cc $< -c -o $@

clean:
	rm flist.o workers.o workers_data.o server.o tftp_server 
