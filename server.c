#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <memory.h>
#include <netdb.h> //getaddrinfo..
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "workers.h"

char*                 dir          = "/tmp/";
volatile sig_atomic_t accept_query = 1;

static struct option longopts[] = {{"port", required_argument, NULL, 'p'},
                                   {"dir", required_argument, NULL, 'd'},
                                   {"help", no_argument, NULL, 'h'},
                                   {NULL, 0, NULL, 0}};
void
usage(char* prg_name)
{
    fprintf(stderr, "Usage: %s [OPTION]...\n\n"
                    "\t-p, --port    port number           [ 69 ] \n"
                    "\t-d, --dir     src and dst directory [ /tmp/ ]\n\n",
            prg_name);
    exit(2);
}

void
sig_handler(int s)
{
    accept_query = 0;
}

void
print_nameinfo(struct addrinfo* r)
{

    char ip_str[NI_MAXHOST];
    char port_str[NI_MAXSERV]; // not in posix

    int error;
    if ((error = getnameinfo(r->ai_addr, r->ai_addrlen, ip_str, sizeof(ip_str), port_str,
                             sizeof(port_str), NI_NUMERICHOST | NI_NUMERICSERV)) != 0) {
        warn("%s", gai_strerror(error));
    }
    fprintf(stderr, "\tIP: %s\t Port: %s\n", ip_str, port_str);
}

int
get_server_socket(char* portstr)
{
    int              fd = -1;
    struct addrinfo *r, *rorig, hi;

    /*
    With that said, if you are running on a platform that supports dual-stack sockets, your server does
    not have to create any IPv4 listening sockets at all. It can create IPv6 sockets only, and then
    disable their IPV6_V6ONLY option. This will allow them to accept both IPv4 and IPv6 clients. The
    client address returned by accept() will tell you whether an IPv4 or IPv6 client has connected.
        // source: stackoverflow.com/
     */

    memset(&hi, 0, sizeof(hi));
    hi.ai_family = AF_INET6;
    // hi.ai_family   = AF_UNSPEC;
    hi.ai_socktype = SOCK_DGRAM;
    hi.ai_flags    = AI_PASSIVE;

    if (getaddrinfo(NULL, portstr, &hi, &rorig) != 0)
        err(6, "getaddrinfo");

    for (r = rorig; r != NULL; r = r->ai_next) {

        print_nameinfo(r);

        if ((fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) == -1) {
            warn("socket");
            continue;
        }

        int onoff = 0;
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &onoff, sizeof(onoff)) == -1) {
            close(fd);
            fd = -1;
            warn("setsockopt");
			continue;
        }

        if (bind(fd, r->ai_addr, r->ai_addrlen) == -1) {
            close(fd);
            fd = -1;
            warn("bind");
        }
        else
            break;
    }

    freeaddrinfo(rorig);

    return fd;
}

int
main(int argc, char** argv)
{

    char* portstr = "69";

    int        ch;
    extern int errno;

    while ((ch = getopt_long(argc, argv, "hp:d:", longopts, NULL)) != -1) {
        switch (ch) {
        case 'd':
            dir            = strdup(optarg);
            DIR* directory = opendir(dir);

            if (directory) {
                closedir(directory);
            }
            else if (ENOENT == errno) {
                fprintf(stderr, "%s dir does not exist\n", dir);
                usage(argv[0]);
            }
            else
                err(6, "opendir");
            break;
        case 'p':
            portstr = strdup(optarg);
            int j;
            for (j = 0; portstr[j]; j++) {
                if (!isdigit(portstr[j]))
                    usage(argv[0]);
            }
            break;
        default:
            usage(argv[0]);
        }
    }

    int fd = get_server_socket(portstr);
    if (fd == -1) {
        return 3;
    }

    struct sigaction act;
    bzero(&act, sizeof(act));
    act.sa_handler = sig_handler;

    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    // printf("main thread id : %lu\n", pthread_self());

    struct sockaddr_storage ca;

    int  st_sz = sizeof(ca);
    char buff[BUF_LEN + 1];

    while (accept_query) {

        int n = recvfrom(fd, buff, BUF_LEN, 0, (struct sockaddr*)&ca, (socklen_t*)&st_sz);
        if (n == -1) {
            warn("recvfrom ");
            continue;
        }

        if (n < 2) // nonvalid packet
            continue;

        uint16_t opcode = (buff[0] << 8) | buff[1];

        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);

        switch (opcode) {

        case OP_RRQ:;

            node_t* node_p = create_node(n - 2, buff, ca);
            append_node(node_p);

            pthread_create(&node_p->tid, &a, rrq_serve, node_p);

            break;
        case OP_WRQ:

            node_p = create_node(n - 2, buff, ca);
            append_node(node_p);

            pthread_create(&node_p->tid, &a, wrq_serve, node_p);

            break;
        default:;
            fprintf(stderr, "main:: packet has wrong opcode\n");
        }
    }

    pthread_mutex_lock(&mutex);
    while (head) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);

    fprintf(stderr, "===== tftp server exit ======\n");

    exit(0);
}
// nc -6  fe80::69ec:192b:cc77:f220%wlp3s0 33333
