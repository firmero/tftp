#include "flist.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

fnode_t *
flist_find_filename(const char *filename, flist_t *flist)
{
	fnode_t *fnode_p = flist->head;

	while (fnode_p) {

		if (strcmp(fnode_p->filename, filename) == 0)
			break;

		fnode_p = fnode_p->next;
	}

	// returns NULL if not found
	return (fnode_p);
}

fnode_t *
flist_add_file(const char *filename, flist_t *flist)
{
	pthread_mutex_t *fmutex = &flist->mutex;

	pthread_mutex_lock(fmutex);

	fnode_t *fnode_p = flist_find_filename(filename, flist);

	if (fnode_p) {

		fnode_p->cnt++;

	} else {

		fnode_p = malloc(sizeof (fnode_t));

		fnode_p->filename = strdup(filename);
		fnode_p->cnt	= 1;

		fnode_p->next	 = NULL;
		fnode_p->fd_list = NULL;

		pthread_rwlock_init(&fnode_p->rw_lock, NULL);

		if (!flist->head) {

			fnode_p->prvs    = NULL;

			flist->head = fnode_p;
			flist->tail = fnode_p;

		} else {

			fnode_p->prvs    = flist->tail;
			flist->tail->next	= fnode_p;
			flist->tail		= fnode_p;
		}
	}

	pthread_mutex_unlock(fmutex);

	return (fnode_p);
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

	// free(fnode_p);
}

// 1 means it was the last occurrence in list, 0 not the last
// if there is more node with same filename,
// add fd to list (the last filename release fds)
int
flist_rm_file(int fd, const char *filename, flist_t *flist)
{
	pthread_mutex_t *fmutex = &flist->mutex;

	pthread_mutex_lock(fmutex);

	assert(flist->head);

	fnode_t *fnode_p = flist->head;

	do {
		if (strcmp(fnode_p->filename, filename) == 0) {
			break;
		}

		fnode_p = fnode_p->next;

	} while (fnode_p);

	// there must exist node with same filename
	assert(fnode_p);

	if (fnode_p->cnt > 1) {

		fd_node_t *fd_node_p = malloc(sizeof (fd_node_t));

		// append current list after node
		fd_node_p->next		= fnode_p->fd_list; // NULL or nonempty
		fd_node_p->fd		= fd;

		fnode_p->fd_list = fd_node_p;

		fnode_p->cnt--;
		pthread_mutex_unlock(fmutex);
		return (0);
	}

	// rm node
	if (fnode_p->next)
		fnode_p->next->prvs = fnode_p->prvs;
	else
		flist->tail = fnode_p->prvs;

	if (fnode_p->prvs)
		fnode_p->prvs->next = fnode_p->next;
	else
		flist->head = fnode_p->next;

	free_fnode(fnode_p);

	pthread_mutex_unlock(fmutex);

	return (1);
}
