#ifndef	FLIST_H
#define	FLIST_H

#include <pthread.h>

// used as node in pending fds list
struct fd_node_tt {

    int fd;

    struct fd_node_tt *next;
};
typedef struct fd_node_tt fd_node_t;


// represents a node in the list of files,
struct fnode_tt {

	// number of references
    size_t	cnt;
    char	*filename;
    pthread_rwlock_t rw_lock;

	// pending fds, waiting for close, consenquence of using rwlocks
    fd_node_t *fd_list;

    struct fnode_tt *next;
    struct fnode_tt *prvs;
};
typedef struct fnode_tt fnode_t;


// return value:
// 1 means it was the last occurrence in list, 0 not the last
int flist_rm_file(int fd, const char *filename);

pthread_rwlock_t *flist_add_file(const char *filename);

#endif
