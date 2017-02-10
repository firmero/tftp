#ifndef FLIST_H
#define FLIST_H

#include <pthread.h>

struct fd_node_tt {

    int fd;

    struct fd_node_tt* next;
};
typedef struct fd_node_tt fd_node_t;

struct fnode_tt {

    size_t           cnt;
    char*            filename;
    pthread_rwlock_t rw_lock;

    // pending fd, waiting for close, consenquence of using rwlocks
    fd_node_t* fd_list;

    struct fnode_tt* next;
    struct fnode_tt* prvs;
};
typedef struct fnode_tt fnode_t;


// 1 means it was the last occurrence in list, 0 not the last
int flist_rm_file(int fd, char* filename);

pthread_rwlock_t* flist_add_file(char* filename);

#endif
