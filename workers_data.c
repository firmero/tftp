#include "workers.h"

void
append_node(node_t* node_p)
{

    pthread_mutex_lock(&query_list_mutex);
    if (!head) {

        head = node_p;
        tail = node_p;

        tail->next = NULL;
        tail->prvs = NULL;
    }
    else {

        node_p->prvs = tail;
        node_p->next = NULL;
        tail->next   = node_p;
        tail         = node_p;
    }
    // dump(head);
    pthread_mutex_unlock(&query_list_mutex);
}

// buff has whole packet except OPCODE_SIZE bytes
node_t*
create_node(size_t sz, char* buff, struct sockaddr_storage ca)
{
    node_t* node_p = malloc(sizeof(node_t));

    node_p->sz   = sz;
    node_p->buff = malloc(sz);
    memcpy(node_p->buff, buff, sz);

    node_p->saddr_st = ca;

    return node_p;
}
void
free_node(node_t* node_p)
{
    free(node_p->buff);
    free(node_p);
}
void
remove_node(node_t* node_p)
{

    pthread_mutex_lock(&query_list_mutex);
    if (node_p->next && node_p->prvs) {
        node_p->prvs->next = node_p->next;
        node_p->next->prvs = node_p->prvs;
        free_node(node_p);
    }
    else if (!node_p->prvs) {
        if (!node_p->next) {
            free_node(node_p);
            head = NULL;
        }
        else {
            node_p->next->prvs = NULL;
            head               = node_p->next;
            free_node(node_p);
        }
    }
    else if (!node_p->next) {
        if (!node_p->prvs) {
            free_node(node_p);
            head = NULL;
        }
        else {
            node_p->prvs->next = NULL;
            tail               = node_p->prvs;
            free_node(node_p);
        }
    }

    // if (head) dump(head);
    pthread_cond_signal(&query_finished);
    pthread_mutex_unlock(&query_list_mutex);
}
void
cleanup(node_t* node_p, char* filename, char* mode)
{
    remove_node(node_p);
    free(filename);
    free(mode);
}
