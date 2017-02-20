#include "flist.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

fnode_t *fhead = NULL;
fnode_t *ftail = NULL;

pthread_mutex_t fmutex = PTHREAD_MUTEX_INITIALIZER;

pthread_rwlock_t *
flist_add_file(const char *filename)
{
	fnode_t *fnode_p = NULL;

	pthread_mutex_lock(&fmutex);
	if (!fhead) {

		fnode_p = malloc(sizeof (fnode_t));

		fhead = fnode_p;
		ftail = fnode_p;

		ftail->next    = NULL;
		ftail->prvs    = NULL;
		ftail->fd_list = NULL;

		ftail->cnt		= 1;
		ftail->filename = strdup(filename);
		pthread_rwlock_init(&ftail->rw_lock, NULL);
	} else {

		fnode_p = fhead;
		do {
			if (strcmp(fnode_p->filename, filename) == 0)
			break;

			fnode_p = fnode_p->next;

		} while (fnode_p);

		if (!fnode_p) { // not found, create new

			fnode_p = malloc(sizeof (fnode_t));

			fnode_p->cnt	= 1;
			fnode_p->filename = strdup(filename);
			pthread_rwlock_init(&fnode_p->rw_lock, NULL);

			fnode_p->prvs  = ftail;
			fnode_p->next  = NULL;
			ftail->fd_list = NULL;
			ftail->next	= fnode_p;
			ftail		= fnode_p;
		} else {
			fnode_p->cnt++;
		}
	}
	pthread_mutex_unlock(&fmutex);

	return (&fnode_p->rw_lock);
}

void
free_fnode(fnode_t *fnode_p)
{
	free(fnode_p->filename);
	pthread_rwlock_destroy(&fnode_p->rw_lock);

	fd_node_t *fd_node_p = fnode_p->fd_list;

	while (fd_node_p) {
		fd_node_t *tmp = fd_node_p;

		close(fd_node_p->fd);

		fd_node_p = fd_node_p->next;
		free(tmp);
	}

	free(fnode_p);
}

// 1 means it was the last occurrence in list, 0 not the last
// if there is more node with same filename,
// add fd to list (the last filename release fds)
int
flist_rm_file(int fd, const char *filename)
{
	pthread_mutex_lock(&fmutex);

	assert(fhead);

	fnode_t *fnode_p = fhead;
	do {
		if (strcmp(fnode_p->filename, filename) == 0) {
			break;
		}

		fnode_p = fnode_p->next;

	} while (fnode_p);

	assert(fnode_p);

	if (fnode_p->cnt > 1) {

		fd_node_t *fd_node_p = malloc(sizeof (fd_node_t));
		fd_node_p->next		= fnode_p->fd_list; // NULL or nonempty
		fd_node_p->fd		= fd;

		fnode_p->fd_list = fd_node_p;

		fnode_p->cnt--;
		pthread_mutex_unlock(&fmutex);
		return (0);
	}

	// rm node
	if (fnode_p->next && fnode_p->prvs) {
		fnode_p->prvs->next = fnode_p->next;
		fnode_p->next->prvs = fnode_p->prvs;
		free_fnode(fnode_p);
	} else if (!fnode_p->prvs) {
		if (!fnode_p->next) {
			free_fnode(fnode_p);
			fhead = NULL;
		} else {
			fnode_p->next->prvs = NULL;
			fhead = fnode_p->next;
			free_fnode(fnode_p);
		}
	} else if (!fnode_p->next) {
		if (!fnode_p->prvs) {
			free_fnode(fnode_p);
			fhead = NULL;
		} else {
			fnode_p->prvs->next = NULL;
			ftail = fnode_p->prvs;
			free_fnode(fnode_p);
		}
	}
	pthread_mutex_unlock(&fmutex);

	return (1);
}
