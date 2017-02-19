#!/bin/bash

PORT=12345
HOST=localhost
DIR=/tmp


print_status()
{
	if [ $? -eq 0 ]; then
		echo -e "\e[0;32m[ OK: No diffrence found ]\e[0m"   
	else 
		echo -e "\e[0;31m[ Error: files differ ]\e[0m"   
	fi
}
# first argument should be number of MB (less than 32)
send_file()
{
	echo -e "\e[0;36m[========= gen client_file $1 MB =========] \e[0m"   
	dd bs=M count=$1 </dev/urandom > client_file_$1mb
	echo -e "\e[0;36m[========= DONE gen client_file $1 MB =========] \e[0m"   

	echo -e "\e[0;33m[========= Put file =========] \e[0m"   
	tftp -m octet $HOST $PORT -c put client_file_$1mb
	echo -e "\e[0;33m[========= End of transmission =========] \e[0m"   

	cmp client_file_$1mb $DIR/client_file_$1mb
	print_status
}

# first argument should be number of MB (less than 32)
recieve_file()
{
	echo -e "\e[0;36m[========= gen server_file $1 MB =========] \e[0m"   
	dd bs=M count=$1 </dev/urandom > $DIR/server_file_$1mb
	echo -e "\e[0;36m[========= DONE gen server_file $1 MB =========] \e[0m"   

	echo -e "\e[0;33m[========= Get file =========] \e[0m"   
	tftp -m octet $HOST $PORT -c get server_file_$1mb
	echo -e "\e[0;33m[========= End of transmission =========] \e[0m"   

	cmp  $DIR/server_file_$1mb server_file_$1mb 
	print_status
}

echo -e "\033c"
send_file 2
recieve_file 2

send_file 31
recieve_file 31

