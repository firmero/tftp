#include "workers.h"
#include "flist.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

pthread_mutex_t query_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t query_finished = PTHREAD_COND_INITIALIZER;

node_t *head = NULL;
node_t *tail = NULL;

static const int timeout_cnt_rrq = 3;
static const int timeout_cnt_wrq = 3;

static const int timeout_ms_wrq = 900;
static const int timeout_ms_rrq = 900;

static struct error_t tftp_error[] = {
			{"Not defined, see error message (if any)."},
			{"File not found."},
			{"Access violation."},
			{"Disk full or allocation exceeded."},
			{"Illegal TFTP operation."},
			{"Unknown transfer ID."},
			{"File already exists."},
			{"No such user."}};
void
dump(node_t *node_p)
{
	printf("VVVVVV start of dump\n");
	do {
	size_t i;
	for (i = 0; i < node_p->sz; i++)
	    printf("%c", node_p->buff[i]);
	printf("\n");

	node_p = node_p->next;

	} while (node_p);

	printf("^^^^^^ end of dump\n");
}

void
close_file(int ffd, char *filename)
{
	if (flist_rm_file(ffd, filename))
		close(ffd);
	// else ffd is added to waitng list for closing
}

// if success return 1, otherwise 0
// expect NULL filename, mode
int
get_filename_mode(const char *buff, const size_t sz,
			char **filename, char **mode)
{
	size_t inxe = 0, inxs = 0;

	while (inxe < sz && buff[inxe++])
		;

	if (inxe == sz) {
		fprintf(stderr, "PE::Cannot get filename field from packet\n");
		return (0);
	}

	*filename = strdup(buff);

	inxs = inxe;
	while (inxe < sz && buff[inxe++])
		;

	if (buff[inxe - 1]) {
		free(filename);
		fprintf(stderr, "PE::Cannot get mode field from packet\n");
		return (0);
	}

	*mode = strdup(buff + inxs);

	for (char *ptr = *mode; *ptr; ptr++)
		*ptr = tolower(*ptr);

	return (1);
}

void
print_buff(char *buff, size_t sz)
{
	size_t i;
	for (i = 0; i < sz; i++)
		printf("%c|", buff[i]);
	printf("#\n");
}

int
get_socket()
{
	int fd = -1;
	struct addrinfo *r, *rorig, hi;

	memset(&hi, 0, sizeof (hi));
	hi.ai_family = AF_INET6;
	// hi.ai_family   = AF_UNSPEC;
	hi.ai_socktype = SOCK_DGRAM;
	hi.ai_flags = AI_PASSIVE;

	// port number will not be used,, no calling of the bind function
	if (getaddrinfo(NULL, "2233", &hi, &rorig) != 0) {
		warn("getaddrinfo in get_socket");
		return (-1);
	}

	for (r = rorig; r != NULL; r = r->ai_next) {

		if ((fd = socket(r->ai_family, r->ai_socktype,
						r->ai_protocol)) == -1) {
			warn("socket");
			continue;
		}

		int onoff = 0;
		if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &onoff,
					sizeof (onoff)) == -1) {
			warn("setsockopt");
			close(fd);
			fd = -1;
			continue;
		}
		break;
	}

	freeaddrinfo(rorig);

	return (fd);
}

void
send_ack(int fd, struct sockaddr_storage saddr_st, uint16_t bn)
{
	char ack[4];
	uint16_t opcode = htons(OP_ACK);
	uint16_t nbn = htons(bn);

	memcpy(ack, &opcode, 2);
	memcpy(ack + 2, &nbn, 2);

	if (sendto(fd, ack, 4, 0, (struct sockaddr *)&saddr_st,
				sizeof (saddr_st)) == -1)
		warn("couldn't send ack packet");
}

void
send_err(int fd, struct sockaddr_storage saddr_st, enum EE error, char *msg)
{
	if (!msg)
		msg = tftp_error[error].msg;

	int sz = strlen(msg) + 1; // for '\0'

	char *buff = malloc(sz + 4); // opcode, errcode

	uint16_t opcode = htons(OP_ERR);
	uint16_t errcode = htons(error);

	memcpy(buff, &opcode, OPCODE_SIZE);
	memcpy(buff + 2, &errcode, 2);
	memcpy(buff + 4, msg, sz);

	if (sendto(fd, buff, sz + 4, 0, (struct sockaddr *)&saddr_st,
				sizeof (saddr_st)) == -1) {
		warn("couldn't send error packet");
	}

	free(buff);
}

int
send_block(int ffd, int fd, char *buff, int bn, node_t *node_p,
		struct pollfd fds, int block_size)
{
	int rcnt = block_size;

	int r, rr = 0;
	do {
		r = read(ffd, buff + 4 + rr, (size_t)(block_size - rr));
		if (r < 0) {
			warn("read in send_block");
			return (0);
		}

	rr += r;
	} while (rr < block_size);

	uint16_t opcode = htons(OP_DATA);
	uint16_t nbn = htons(bn);

	memcpy(buff, &opcode, 2);
	memcpy(buff + 2, &nbn, 2);

	sendto(fd, buff, rcnt + 4, 0, (struct sockaddr *)&node_p->saddr_st,
			sizeof (node_p->saddr_st));

	// == wait for ack

	int done = 0;
	int nth_timeout = 1;
	char ack[4]; // 2B opcode, 2B block number
	int n;

	uint16_t ack_opcode;
	uint16_t ack_bn;

	do {
		int rp = poll(&fds, 1, timeout_ms_rrq);

		switch (rp) {
		case -1:
			warn("pool::");
			return (0);
		case 0: // timeout
			if (nth_timeout < timeout_cnt_rrq) {

				// retransmit
			sendto(fd, buff, rcnt + 4, 0,
				(struct sockaddr *)&node_p->saddr_st,
				sizeof (node_p->saddr_st));

			nth_timeout++;
			} else // timeouts runs out
				return (0);

			break;
		case 1: // want ack packet

			n = recvfrom(fd, ack, 4, 0, NULL, 0);

			if (n != 4)
				return (0);

			ack_opcode = ((uint8_t)ack[0] << 8) | (uint8_t)ack[1];

			ack_bn = (uint8_t)ack[3] | ((uint8_t)ack[2] << 8);

			if (ack_opcode != OP_ACK)  // ignore, dupl
				break;

			if (ack_bn == bn)
				done = 1;

			break;
		default:
			fprintf(stderr, "Unexpected return value "
					"in send_block switch");
			return (0);
		}

	} while (!done);

	return (1); // ok
}
void
print_info(struct sockaddr_storage *saddr_st, char *filename,
		char *mode, enum SS wr)
{
	char ip_str[NI_MAXHOST];
	char port_str[NI_MAXSERV]; // not in posix
	int error;
	if ((error = getnameinfo((struct sockaddr *)&*saddr_st,
				sizeof (struct sockaddr_storage), ip_str,
				sizeof (ip_str), port_str, sizeof (port_str),
				NI_NUMERICSERV | NI_NUMERICHOST)) != 0) {
		errx(1, "%s", gai_strerror(error));
	}

	printf("name: %s\n", filename);
	printf("mode: %s\n", mode);
	char *prefix = wr == SS_WQ ? "WRQ" : "RRQ";

	fprintf(stderr, "%s_packet_from: %s %s \n", prefix, ip_str, port_str);
}
// RRQ
void *
rrq_serve(void *p_node)
{
	node_t *node_p = (node_t *)p_node;
	// print_buff(node_p->buff, node_p->sz);

	char *filename = NULL;
	char *mode = NULL;

	// filename and mode release function cleanup(node_p, filename, mode)
	if (!get_filename_mode(node_p->buff, node_p->sz, &filename, &mode)) {
		remove_node(node_p);
		return (NULL);
	}

	int fd = get_socket();
	if (fd == -1) {
		remove_node(node_p);
		return (NULL);
	}

	print_info(&node_p->saddr_st, filename, mode, SS_RQ);
	if (strcmp(mode, "octet") != 0) {
		send_err(fd, node_p->saddr_st, EE_NOTDEFINED,
				"Server support only octet mode transmission.");
		remove_node(node_p);
		return (NULL);
	}

	// ===========================

	char buff[BLOCK_SIZE + 4]; // 2B opcode; 2B block number

	struct pollfd fds;
	fds.fd = fd;
	fds.events = POLLIN;

	uint16_t bn = 1; // block number

	char *tmp = malloc(strlen(filename) + strlen(dir) + 1);
	strcpy(tmp, dir);
	strcat(tmp, filename);
	free(filename);
	filename = tmp;

	int ffd = open(filename, O_RDONLY);

	extern int errno;
	if (ffd == -1) {

		if (errno == ENOENT)
			send_err(fd, node_p->saddr_st, EE_FILENFOUND, NULL);
		else
			send_err(fd, node_p->saddr_st,
					EE_NOTDEFINED, strerror(errno));

		close(fd);
		cleanup(node_p, filename, mode);
		return (NULL);
	}

	// ========= ffd

	pthread_rwlock_t *rwlock = flist_add_file(filename);

	int fsz = lseek(ffd, 0, SEEK_END);
	lseek(ffd, 0, SEEK_SET);

	int p, m;

	p = fsz / BLOCK_SIZE;
	m = fsz % BLOCK_SIZE;

	pthread_rwlock_rdlock(rwlock);

	for (bn = 1; bn <= p; bn++) {

		if (!send_block(ffd, fd, buff, bn, node_p, fds, BLOCK_SIZE)) {
			pthread_rwlock_unlock(rwlock);
			close_file(ffd, filename);
			cleanup(node_p, filename, mode);
			close(fd);
			return (NULL);
		}
	}

	// may not be successfull;;
	// the last block must have size < BLOCK_SIZE,
	// so if size of file is multiple of BLOCK_SIZE,
	// then is needed to send packet with empty payload
	send_block(ffd, fd, buff, bn, node_p, fds, m);
	pthread_rwlock_unlock(rwlock);

	close_file(ffd, filename);
	cleanup(node_p, filename, mode);
	close(fd);
	printf(" [RRQ thread %lu done ]\n", node_p->tid);
	return (NULL);
}
// WRQ
void *wrq_serve(void *p_node)	{

	node_t *node_p = (node_t *)p_node;
	// print_buff(node_p->buff, node_p->sz);

	char *filename = NULL;
	char *mode = NULL;

	// filename and mode release function cleanup(node_p, filename, mode)
	if (!get_filename_mode(node_p->buff, node_p->sz, &filename, &mode)) {
		remove_node(node_p);
		return (NULL);
	}

	int fd = get_socket();
	if (fd == -1) {
		remove_node(node_p);
		return (NULL);
	}

	print_info(&node_p->saddr_st, filename, mode, SS_WQ);

	// ===========================

	char buff[BLOCK_SIZE + 4]; // 2B opcode; 2B block number

	struct pollfd fds;
	fds.fd = fd;
    fds.events = POLLIN;
	uint16_t bn = 0; // block number

	char *tmp = malloc(strlen(filename) + strlen(dir) + 1);
	strcpy(tmp, dir);
    strcat(tmp, filename);
	free(filename);
	filename = tmp;

	int ffd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    extern int errno;
	if (ffd == -1) {
		if (errno == EACCES)
			send_err(fd, node_p->saddr_st,
					EE_ACCESSVIOLATION, NULL);
		else
			send_err(fd, node_p->saddr_st,
					EE_NOTDEFINED, strerror(errno));
		close(fd);
		cleanup(node_p, filename, mode);
		return (NULL);
	}

	pthread_rwlock_t *rwlock = flist_add_file(filename);

	// ========= accept wrq

	send_ack(fd, node_p->saddr_st, bn);
    int done = 0;
	int nth_timeout = 1;
	int n;

	uint16_t get_opcode, get_bn;
    do {
		int rp = poll(&fds, 1, timeout_ms_wrq);
		switch (rp) {
		case -1:
			warn("poll::");
			done = 1;
			break;
		case 0: // timeout

			if (nth_timeout < timeout_cnt_wrq) {
				// retransmit
				send_ack(fd, node_p->saddr_st, bn);
				nth_timeout++;
			} else // timeouts runs out
				done = 1;
			break;
		case 1: // want data packet
			n = recvfrom(fd, buff, BLOCK_SIZE + 4, 0, NULL, 0);
			if (n < 4) { // malformed packet
				break;
			}
			get_opcode = ((uint8_t)buff[0] << 8) | (uint8_t)buff[1];
			get_bn = (uint8_t)buff[3] | ((uint8_t)buff[2] << 8);
			if (get_opcode != OP_DATA) { // ignore
				break;
			}

			if (get_bn == bn + 1) {
				bn++;
				int wcnt = n - 4;
				int w, rw = 0;

				pthread_rwlock_wrlock(rwlock);

			do {

				w = write(ffd, buff + 4 + rw, (wcnt - rw));
				if (w < 0) {
					if (errno == EDQUOT)
						send_err(fd, node_p->saddr_st,
						EE_DISKFULL, NULL);
					else
						send_err(fd, node_p->saddr_st,
						EE_NOTDEFINED, strerror(errno));

					pthread_rwlock_unlock(rwlock);
					goto CL;
				}
				rw += w;

			} while (rw < wcnt);

				pthread_rwlock_unlock(rwlock);

				send_ack(fd, node_p->saddr_st, bn);
				if (n < BLOCK_SIZE + 4) { // get final block
					done = 1;
				}
			}
			break;
		default:
			fprintf(stderr, "Unexpected return value "
					"in send_block switch");
			break;
		}
	} while (!done);

CL:
    close_file(ffd, filename);
	cleanup(node_p, filename, mode);
	close(fd);

	printf(" [WRQ thread %lu done ]\n", node_p->tid);

	return (NULL);
}
