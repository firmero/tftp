# TFTP server
Simple implementation of tftp server. The main purpose of this project
is demonstation of some unix system calls.

## Usage

tftp\_server [OPTION]...  
   
####Options:
-p, --port  
&nbsp;port number, IANA TFTP port is 69  
  
-d, --dir  
&nbsp;server root directory, /tmp/

## Implementation details

#### general:
* when server get an udp packet, then needed data are stored in a new node
* node is then appended to the list, which uses mutex to preserve consistency
* a new thread is created to serve query and takes the node as an argument
* there are 2 function to server query: wrq\_serve and rwq\_serve
* thread then try to get filename and mode from packet, which is stored in node
* while finishing thread, thread releases node data and notifies conditinal  
variable (see signals handling)

#### flist structure:
* program uses pthread\_rwlocks for files acces which are managed by flist
* after parsing packet and having filename's fd, the filename is registred in flist
* flist has global mutex and has two method: flist\_add\_file and flist\_rm\_file
* flist node should has items as filename, rw\_lock and cnt (as count references)  
but ~~also list of fd waiting for closing for that filename (the consequence of using  
rw\_locks)~~
* ~~the fd for filename in flist is closed if cnt is 1, if cnt is greater, then fd is  
appended to list of waiting fd~~

#### signals handling:
* catching signals are SIGINT and SIGTERM
* main loop use volatile sig\_atomic\_t accept\_query variable
* signal handler set this variable to false
* then the server wait while all threads finish their job,  
for that purpose is used conditional variable

## Limitations and suggestions 
* tftp connection can transport 2^16\*BLOCK\_SIZE bytes,  
BLOCK\_SIZE is 512B, so that makes 32MB
* program do not use function chroot(), that needs root permission, in general  
this application is not secure
* sending error packets doesn't cover all error situations
* server works with data only in binary mode, but protocol suggests mail mode  
which is deprecated but also netascii mode
