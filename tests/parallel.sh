#!/bin/bash

PORT=12345
HOST=localhost
DIR=/tmp


print_status()
{
	if [ $? -eq 0 ]; then
		echo -e "\e[0;32m[ OK: No diffrence found $1 ]\e[0m"   
	else 
		echo -e "\e[0;31m[ Error: files differ $1 ]\e[0m"   
	fi
}
# first argument should be number of MB (less than 32)
send_file()
{
	echo -e "\e[0;36m[========= gen client_file $1 KB =========] \e[0m"   
	dd bs=K count=$1 </dev/urandom > client_file_$1kb 2>/dev/null
	echo -e "\e[0;36m[========= DONE gen client_file $1 KB =========] \e[0m"   

	echo -e "\e[0;33m[========= Put file $1 =========] \e[0m"   
	tftp -m octet $HOST $PORT -c put client_file_$1kb
	echo -e "\e[0;33m[========= End of transmission $1 =========] \e[0m"   

	cmp client_file_$1kb $DIR/client_file_$1kb
	print_status $1
}

# first argument should be number of MB (less than 32)
recieve_file()
{
	echo -e "\e[0;36m[========= gen server_file $1 KB =========] \e[0m"   
	dd bs=K count=$1 </dev/urandom > $DIR/server_file_$1kb 2>/dev/null
	echo -e "\e[0;36m[========= DONE gen server_file $1 kB =========] \e[0m"   

	echo -e "\e[0;33m[========= Get file $1 =========] \e[0m"   
	tftp -m octet $HOST $PORT -c get server_file_$1kb
	echo -e "\e[0;33m[========= End of transmission $1 =========] \e[0m"   

	cmp  $DIR/server_file_$1kb server_file_$1kb 
	print_status $1
}

echo -e "\033c"

for i in {1..20}; do

	recieve_file $RANDOM & 
	send_file $RANDOM & 
done

for job in `jobs -p`; do
    wait $job 
done

