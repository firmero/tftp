#include "workers.h"

void
append_node(node_t *node_p, query_list_t *qlist)
{
	pthread_mutex_lock(&qlist->mutex);
	if (!qlist->head) {

		qlist->head = node_p;
		qlist->tail = node_p;

		// qlist->tail->next = NULL;
		// qlist->tail->prvs = NULL;
	} else {

		node_p->prvs = qlist->tail;
		// node_p->next = NULL;
		qlist->tail->next	= node_p;
		qlist->tail		= node_p;
	}
	// dump(qlist->head);
	pthread_mutex_unlock(&qlist->mutex);
}

// buff has whole packet except OPCODE_SIZE bytes
node_t *
create_node(size_t sz, const char *buff, struct sockaddr_storage *ca)
{
	node_t *node_p = malloc(sizeof (node_t));

	node_p->sz   = sz;
	node_p->buff = malloc(sz);
	// release by free_node()
	memcpy(node_p->buff, buff, sz);

	node_p->saddr_st = *ca;
	// init there, not in append_node()
	node_p->next = NULL;
	node_p->prvs = NULL;

	return (node_p);
}
void
free_node(node_t *node_p)
{
	free(node_p->buff);
	free(node_p);
}
void
remove_node(node_t *node_p, query_list_t *qlist)
{
	pthread_mutex_lock(&qlist->mutex);

	if (node_p->next)
		node_p->next->prvs = node_p->prvs;
	else
		qlist->tail = node_p->prvs;

	if (node_p->prvs)
		node_p->prvs->next = node_p->next;
	else
		qlist->head = node_p->next;

	free_node(node_p);

	// if (qlist->head) dump(qlist->head);
	pthread_cond_signal(&qlist->query_finished);
	pthread_mutex_unlock(&qlist->mutex);
}
void
cleanup(node_t *node_p, char *filename, char *mode, query_list_t *qlist)
{
	remove_node(node_p, qlist);
	free(filename);
	free(mode);
}
