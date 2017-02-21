#ifndef	WK_H
#define	WK_H

#include <arpa/inet.h>
#include <err.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

// default args
#define	DEFAULT_PORT "69"
#define	DEFAULT_DIR  "/tmp/"

// for query packet
#define	BUFF_LEN 1024

#define	BLOCK_SIZE 512


// exit errors
#define	ERROR_GETADDRINFO 3
#define	ERROR_OPENDIR 4
#define	ERROR_CANNOT_GET_SOCKET 5

// # of bytes
#define	OPCODE_SIZE 2
#define	BLOCK_NUM_SIZE 2
#define	ERROR_CODE_SIZE 2

#define	ACK_SIZE  (OPCODE_SIZE + BLOCK_NUM_SIZE)

// upper bound of received data
#define	RCV_UDP_SIZE (BLOCK_SIZE + OPCODE_SIZE + BLOCK_NUM_SIZE)
#define	RCV_HDR_SIZE (OPCODE_SIZE + BLOCK_NUM_SIZE)

// send data header
#define	SND_HDR_SIZE (OPCODE_SIZE + BLOCK_NUM_SIZE)

enum TFTP_ERROR {

    ERR_NOTDEFINED = 0,
    ERR_FILENFOUND = 1,
    ERR_ACCESSVIOLATION = 2,
    ERR_DISKFULL	= 3,
    ERR_ILLEGALOP = 4,
    ERR_UNKNOWNBN = 5,
    ERR_FILEEXIST = 6,
    ERR_NOSUCHUSER = 7
};
enum PRINT_INFO { INFO_RQ, INFO_WQ };

enum opcodes_t {

    OPCODE_RRQ  = 1,
    OPCODE_WRQ  = 2,
    OPCODE_DATA = 3,
    OPCODE_ACK  = 4,
    OPCODE_ERR  = 5
};

struct error_t {
    char *msg;
};

// control thread uses list of queries,
// query is represent by this node
struct node_tt {

    char *buff; // copy of whole packet except first 2 bytest (opcode)
    size_t	sz;

    struct sockaddr_storage saddr_st;
    pthread_t tid; // not used

    struct node_tt *next;
    struct node_tt *prvs;
};
typedef struct node_tt node_t;

struct query_list_tt {

	node_t *head;
	node_t *tail;

	pthread_mutex_t mutex;
	pthread_cond_t query_finished;
};
typedef struct query_list_tt query_list_t;

void* rrq_serve(void *x);
void* wrq_serve(void *x);

void
print_info(const struct sockaddr_storage *saddr_st,
			const char *filename, const char *mode,
			enum PRINT_INFO wr);

void dump(const node_t *);
void append_node(node_t *node_p, query_list_t *qlist);

node_t *create_node(size_t sz, const char *buff, struct sockaddr_storage ca);
void free_node(node_t *node_p);
void remove_node(node_t *node_p, query_list_t *qlist);

void cleanup(node_t *node_p, char *filename, char *mode, query_list_t *qlist);

extern char	*dir;


#endif
