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

// for query packet
#define	BUF_LEN 1024

#define	BLOCK_SIZE 512

#define	ERROR_GETADDRINFO 3
#define	ERROR_OPENDIR 4
#define	ERROR_CANNOT_GET_SOCKET 5

// # of bytes
#define	OPCODE_SIZE 2
#define	BLOCK_NUM_SIZE 2

enum EE {

    EE_NOTDEFINED = 0,
    EE_FILENFOUND = 1,
    EE_ACCESSVIOLATION = 2,
    EE_DISKFULL	= 3,
    EE_ILLEGALOP = 4,
    EE_UNKNOWNBN = 5,
    EE_FILEEXIST = 6,
    EE_NOSUCHUSER = 7
};
enum SS { SS_RQ, SS_WQ };

enum opcodes_t {

    OP_RRQ  = 1,
    OP_WRQ  = 2,
    OP_DATA = 3,
    OP_ACK  = 4,
    OP_ERR  = 5
};

struct error_t {
    char *msg;
};

struct node_tt {

    char *buff; // whole packet except first 2 bytest (opcode)
    size_t	sz;
    struct sockaddr_storage saddr_st;
    pthread_t		    tid; // not used

    struct node_tt *next;
    struct node_tt *prvs;
};

typedef struct node_tt node_t;

void* rrq_serve(void *x);
void* wrq_serve(void *x);

void
print_info(struct sockaddr_storage *saddr_st,
			char *filename, char *mode, enum SS wr);

void dump(node_t *);
void append_node(node_t *node_p);

node_t *create_node(size_t sz, char *buff, struct sockaddr_storage ca);
void free_node(node_t *node_p);
void remove_node(node_t *node_p);

void cleanup(node_t *node_p, char *filename, char *mode);

extern pthread_mutex_t query_list_mutex;

// signal when is node unregistred from list
extern pthread_cond_t query_finished;
extern char	*dir;
extern node_t *head, *tail;
#endif
