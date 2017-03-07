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

flist_t flist = {	.head = NULL, .tail = NULL,
					.mutex = PTHREAD_MUTEX_INITIALIZER };

query_list_t qlist = {
				.head = NULL, .tail = NULL,
				.mutex = PTHREAD_MUTEX_INITIALIZER,
				.query_finished = PTHREAD_COND_INITIALIZER};


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
dump(const node_t *node_p)
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
close_file(int ffd, const char *filename)
{
	if (flist_rm_file(ffd, filename, &flist))
		close(ffd);
	// else ffd is added to waitng list for closing
}

// if success return 1, otherwise 0
// function expects NULL filename and mode
int
get_filename_mode(const char *buff, const size_t sz,
			char **filename, char **mode)
{
	// index start, index end
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
print_buff(const char *buff, size_t sz)
{
	size_t i;
	for (i = 0; i < sz; i++)
		printf("%c|", buff[i]);
	printf("#\n");
}

// on error return -1
int
get_socket()
{
	// returned socket
	int fd = -1;
	struct addrinfo *addrinfo, *addrinfo_orig, addr_hint;

	memset(&addr_hint, 0, sizeof (addr_hint));
	addr_hint.ai_family = AF_INET6;
	// addr_hint.ai_family   = AF_UNSPEC;
	addr_hint.ai_socktype = SOCK_DGRAM;
	addr_hint.ai_flags = AI_PASSIVE;

	// port number will not be used,, no calling of the bind function
	if (getaddrinfo(NULL, "9999", &addr_hint, &addrinfo_orig) != 0) {
		warn("getaddrinfo in get_socket");
		return (-1);
	}

	for (addrinfo = addrinfo_orig; addrinfo != NULL;
					addrinfo = addrinfo->ai_next) {

		if ((fd = socket(addrinfo->ai_family, addrinfo->ai_socktype,
						addrinfo->ai_protocol)) == -1) {
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

	freeaddrinfo(addrinfo_orig);

	return (fd);
}

void
send_ack(int fd, struct sockaddr_storage *saddr_st, uint16_t block_number)
{
	char ack[ACK_SIZE];
	uint16_t opcode = htons(OPCODE_ACK);
	uint16_t nbn = htons(block_number);

	memcpy(ack, &opcode, OPCODE_SIZE);
	memcpy(ack + OPCODE_SIZE, &nbn, BLOCK_NUM_SIZE);

	if (sendto(fd, ack, ACK_SIZE, 0, (struct sockaddr *)saddr_st,
				sizeof (*saddr_st)) == -1)
		warn("couldn't send ack packet");
}

void
send_err(int fd, struct sockaddr_storage *saddr_st,
			enum TFTP_ERROR error, const char *msg)
{
	if (!msg)
		msg = tftp_error[error].msg;

	int sz = strlen(msg) + 1; // for '\0'
	int header_sz = OPCODE_SIZE + ERROR_CODE_SIZE;

	char *buff = malloc(sz + header_sz); // opcode, errcode

	uint16_t opcode = htons(OPCODE_ERR);
	uint16_t errcode = htons(error);


	memcpy(buff, &opcode, OPCODE_SIZE);
	memcpy(buff + OPCODE_SIZE, &errcode, ERROR_CODE_SIZE);
	memcpy(buff + header_sz, msg, sz);

	if (sendto(fd, buff, sz + header_sz, 0, (struct sockaddr *)saddr_st,
				sizeof (*saddr_st)) == -1) {
		warn("couldn't send error packet");
	}

	free(buff);
}

int
send_block(int file_fd, int socket, char *buff, int block_number,
		    const node_t *node_p, struct pollfd *fds, int block_size)
{
	int r, sum_read = 0;
	do {
		r = read(file_fd, buff + SND_HDR_SIZE + sum_read,
				(size_t)(block_size - sum_read));

		if (r < 0) {
			warn("read in send_block");
			return (0);
		}

		sum_read += r;
	} while (sum_read < block_size);

	uint16_t opcode = htons(OPCODE_DATA);
	uint16_t nbn = htons(block_number);

	memcpy(buff, &opcode, OPCODE_SIZE);
	memcpy(buff + OPCODE_SIZE, &nbn, BLOCK_NUM_SIZE);

	if (sendto(socket, buff, block_size + SND_HDR_SIZE, 0,
				(struct sockaddr *)&node_p->saddr_st,
				sizeof (node_p->saddr_st)) == -1) {
		warn("couldn't send first data packet");
	}

	// == wait for ack

	int done = 0;
	int nth_timeout = 1;

	char ack[ACK_SIZE]; // 2B opcode, 2B block number
	int receive_sz;

	uint16_t ack_opcode;
	uint16_t ack_block_number;

	do {
		int poll_events = poll(fds, 1, timeout_ms_rrq);

		switch (poll_events) {
		case -1:
			warn("poll::");
			return (0);
		case 0: // timeout
			if (nth_timeout < timeout_cnt_rrq) {

				// retransmit
			if (sendto(socket, buff, block_size + SND_HDR_SIZE, 0,
				(struct sockaddr *)&node_p->saddr_st,
				sizeof (node_p->saddr_st)) == -1) {
				warn("couldn't send retransmit data packet");
			}


				nth_timeout++;
			} else // timeouts runs out
				return (0);

			break;
		case 1: // want ack packet

			receive_sz = recvfrom(socket, ack, ACK_SIZE,
								0, NULL, 0);

			if (receive_sz != ACK_SIZE)
				return (0);

			ack_opcode = ntohs(((uint8_t)ack[1] << 8)
							| (uint8_t)ack[0]);

			ack_block_number = ntohs((uint8_t)ack[2]
						    | ((uint8_t)ack[3] << 8));


			if (ack_opcode != OPCODE_ACK)  // ignore, dupl
				break;

			if (ack_block_number == block_number)
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
print_info(const struct sockaddr_storage *saddr_st, const char *filename,
			const char *mode, enum PRINT_INFO wr)
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
	char *prefix = wr == INFO_WQ ? "WRQ" : "RRQ";

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

	// filename and mode are released
	// by function cleanup(node_p, filename, mode)
	if (!get_filename_mode(node_p->buff, node_p->sz, &filename, &mode)) {
		remove_node(node_p, &qlist);
		return (NULL);
	}

	int socket = get_socket();
	if (socket == -1) {
		remove_node(node_p, &qlist);
		return (NULL);
	}

#ifdef DEBUG
	print_info(&node_p->saddr_st, filename, mode, INFO_RQ);
#endif

	if (strcmp(mode, "octet") != 0) {
		send_err(socket, &node_p->saddr_st, ERR_NOTDEFINED,
				"Server support only octet mode transmission.");
		remove_node(node_p, &qlist);
		close(socket);
		return (NULL);
	}

	// ===========================

	char buff[RCV_UDP_SIZE];

	struct pollfd poll_fds;
	poll_fds.fd = socket;
	poll_fds.events = POLLIN;

	char *tmp = malloc(strlen(filename) + strlen(dir) + 1);
	strcpy(tmp, dir);
	strcat(tmp, filename);
	free(filename);
	filename = tmp;

	int file_fd = open(filename, O_RDONLY);

	extern int errno;
	if (file_fd == -1) {

		if (errno == ENOENT)
			send_err(socket, &node_p->saddr_st,
					ERR_FILENFOUND, NULL);
		else
			send_err(socket, &node_p->saddr_st,
					ERR_NOTDEFINED, strerror(errno));

		close(socket);
		cleanup(node_p, filename, mode, &qlist);
		return (NULL);
	}

	// ========= file_fd

	pthread_rwlock_t *rwlock = &flist_add_file(filename, &flist)->rw_lock;

	int file_sz = lseek(file_fd, 0, SEEK_END);
	lseek(file_fd, 0, SEEK_SET);

	int p, m;

	p = file_sz / BLOCK_SIZE;
	m = file_sz % BLOCK_SIZE;

	pthread_rwlock_rdlock(rwlock);

	uint16_t block_number;
	for (block_number = 1; block_number <= p; block_number++) {

		if (!send_block(file_fd, socket, buff, block_number,
					node_p, &poll_fds, BLOCK_SIZE)) {

			pthread_rwlock_unlock(rwlock);
			close_file(file_fd, filename);
			cleanup(node_p, filename, mode, &qlist);
			close(socket);
			return (NULL);
		}
	}

	// may not be successfull;;
	// the last block must have size < BLOCK_SIZE,
	// so if size of file is multiple of BLOCK_SIZE,
	// then is needed to send packet with empty payload
	send_block(file_fd, socket, buff, block_number, node_p, &poll_fds, m);
	pthread_rwlock_unlock(rwlock);

#ifdef DEBUG
	char *dbg_filename = strdup(filename);
#endif

	close_file(file_fd, filename);
	cleanup(node_p, filename, mode, &qlist);
	close(socket);

#ifdef DEBUG
	printf(" [RRQ file %s done ]\n", dbg_filename);
	free(dbg_filename);
#endif

	return (NULL);
}
// WRQ
void *wrq_serve(void *p_node)	{

	node_t *node_p = (node_t *)p_node;
	// print_buff(node_p->buff, node_p->sz);

	char *filename = NULL;
	char *mode = NULL;

	// filename and mode are released
	// by function cleanup(node_p, filename, mode)
	if (!get_filename_mode(node_p->buff, node_p->sz, &filename, &mode)) {
		remove_node(node_p, &qlist);
		return (NULL);
	}

	int socket = get_socket();
	if (socket == -1) {
		remove_node(node_p, &qlist);
		return (NULL);
	}

#ifdef DEBUG
	print_info(&node_p->saddr_st, filename, mode, INFO_WQ);
#endif

	// ===========================

	// buffer for received data
	char buff[RCV_UDP_SIZE];

	struct pollfd poll_fds;
	poll_fds.fd = socket;
    poll_fds.events = POLLIN;

	char *tmp = malloc(strlen(filename) + strlen(dir) + 1);
	strcpy(tmp, dir);
    strcat(tmp, filename);
	free(filename);
	filename = tmp;

	int file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    extern int errno;
	if (file_fd == -1) {
		if (errno == EACCES)
			send_err(socket, &node_p->saddr_st,
					ERR_ACCESSVIOLATION, NULL);
		else
			send_err(socket, &node_p->saddr_st,
					ERR_NOTDEFINED, strerror(errno));
		close(socket);
		cleanup(node_p, filename, mode, &qlist);
		return (NULL);
	}

	pthread_rwlock_t *rwlock = &flist_add_file(filename, &flist)->rw_lock;

	// ========= accept wrq

	uint16_t block_number = 0;

	send_ack(socket, &node_p->saddr_st, block_number);
    int done = 0;
	int nth_timeout = 1;
	int receive_sz;

	uint16_t get_opcode, get_block_number;

    do {
		int poll_events = poll(&poll_fds, 1, timeout_ms_wrq);
		switch (poll_events) {
		case -1:
			warn("poll::");
			done = 1;
			break;
		case 0: // timeout

			if (nth_timeout < timeout_cnt_wrq) {
				// retransmit
				send_ack(socket, &node_p->saddr_st,
							block_number);

				nth_timeout++;
			} else // timeouts runs out
				done = 1;
			break;
		case 1: // want data packet
			receive_sz = recvfrom(socket, buff,
							RCV_UDP_SIZE, 0,
							NULL, 0);

			if (receive_sz < RCV_HDR_SIZE) { // malformed packet
				break;
			}

			get_opcode = ntohs(((uint8_t)buff[1] << 8)
							| (uint8_t)buff[0]);

			get_block_number = ntohs((uint8_t)buff[2]
						| ((uint8_t)buff[3] << 8));

			if (get_opcode != OPCODE_DATA) { // ignore
				break;
			}

			if (get_block_number == block_number + 1) {
				block_number++;
				int wcnt = receive_sz - RCV_HDR_SIZE;
				int w, sum_write = 0;

				pthread_rwlock_wrlock(rwlock);

				do {

				w = write(file_fd,
						buff + RCV_HDR_SIZE + sum_write,
						(wcnt - sum_write));

				if (w < 0) {
					if (errno == EDQUOT)
						send_err(socket,
							&node_p->saddr_st,
							ERR_DISKFULL, NULL);
					else
						send_err(socket,
							&node_p->saddr_st,
							ERR_NOTDEFINED,
							strerror(errno));

					pthread_rwlock_unlock(rwlock);
					goto CL;
				}
				sum_write += w;

				} while (sum_write < wcnt);

				pthread_rwlock_unlock(rwlock);

				send_ack(socket, &node_p->saddr_st,
							block_number);

				// if got final block
				if (receive_sz < RCV_UDP_SIZE) {
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
	;
#ifdef DEBUG
	char * dbg_filename = strdup(filename);
#endif

    close_file(file_fd, filename);
	cleanup(node_p, filename, mode, &qlist);
	close(socket);

#ifdef DEBUG
	printf(" [WRQ file %s done ]\n", dbg_filename);
	free(dbg_filename);
#endif

	return (NULL);
}
