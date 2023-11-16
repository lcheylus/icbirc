/*
 * Copyright (c) 2003-2004 Daniel Hartmeier
 * Copyright (c) 2023 Laurent Cheylus <foxy@free.fr>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "icb.h"
#include "irc.h"

int		sync_write(int, const char *, int);
static void	usage(void);
static void	handle_client(int);

int terminate_client;
static struct sockaddr_in sa_connect;

static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-d] -c conffile | [-l address] [-p port] "
	    "-s server [-P port]\n", __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int debug = 0;
	const char *addr_listen = NULL, *addr_connect = NULL;
	const char *conf_file = NULL;
	unsigned port_listen = 6667, port_connect = 7326;
	int ch;
	int listen_fd = -1;
	struct sockaddr_in sa;
	socklen_t len;
	int val;

	while ((ch = getopt(argc, argv, "dc:l:p:s:P:")) != -1) {
		switch (ch) {
		case 'd':
			debug++;
			break;
		case 'c':
			conf_file = optarg;
			break;
		case 'l':
			addr_listen = optarg;
			break;
		case 'p':
			port_listen = atoi(optarg);
			break;
		case 's':
			addr_connect = optarg;
			break;
		case 'P':
			port_connect = atoi(optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	if (argc || ((conf_file == NULL) && (addr_connect == NULL)))
		usage();

	if ((conf_file != NULL) && (addr_connect != NULL)) {
		printf("Use only configuration file or server address, not both\n");
		goto error;
	}

	if (conf_file != NULL) {
		printf("Configuration file: %s\n", conf_file);
		goto error;
	}

	memset(&sa_connect, 0, sizeof(sa_connect));
	sa_connect.sin_family = AF_INET;
	sa_connect.sin_addr.s_addr = inet_addr(addr_connect);
	if (sa_connect.sin_addr.s_addr == INADDR_NONE) {
		struct hostent *h;

		if ((h = gethostbyname(addr_connect)) == NULL) {
			fprintf(stderr, "gethostbyname: %s: %s\n",
			    addr_connect, hstrerror(h_errno));
			goto error;
		}
		memcpy(&sa_connect.sin_addr.s_addr, h->h_addr,
		    sizeof(in_addr_t));
	}
	sa_connect.sin_port = htons(port_connect);

	if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		goto error;
	}

	if (fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFL) |
	    O_NONBLOCK)) {
		perror("fcntl");
		goto error;
	}

        val = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
	    (const char *)&val, sizeof(val))) {
		perror("setsockopt");
		goto error;
        }

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	if (addr_listen != NULL)
		sa.sin_addr.s_addr = inet_addr(addr_listen);
	else
		sa.sin_addr.s_addr = INADDR_ANY;
	sa.sin_port = htons(port_listen);
        if (bind(listen_fd, (const struct sockaddr *)&sa, sizeof(sa))) {
		fprintf(stderr, "bind %s:%u: %s\n", inet_ntoa(sa.sin_addr),
		    ntohs(sa.sin_port), strerror(errno));
		goto error;
        }

        if (listen(listen_fd, 1)) {
		perror("listen");
		goto error;
        }

	if (!debug && daemon(0, 0)) {
		perror("daemon");
		goto error;
	}
	signal(SIGPIPE, SIG_IGN);

#ifdef __OpenBSD__
	if (pledge("stdio inet dns", NULL) == -1) {
		perror("pledge");
		goto error;
	}
#endif /* __OpenBSD__ */

	/* handle incoming client connections */
	while (1) {
		fd_set readfds;
		struct timeval tv;
		int r;

		FD_ZERO(&readfds);
		FD_SET(listen_fd, &readfds);
		memset(&tv, 0, sizeof(tv));
		tv.tv_sec = 10;
		r = select(listen_fd + 1, &readfds, NULL, NULL, &tv);
		if (r < 0) {
			if (errno != EINTR) {
				perror("select");
				break;
			}
			continue;
		}
		if (r > 0 && FD_ISSET(listen_fd, &readfds)) {
			int client_fd;

			memset(&sa, 0, sizeof(sa));
			len = sizeof(sa);
			client_fd = accept(listen_fd,
			    (struct sockaddr *)&sa, &len);
			if (client_fd < 0) {
				if (errno != ECONNABORTED) {
					perror("accept");
					break;
				}
				continue;
			}
			printf("client connection from %s:%i\n",
			    inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
			handle_client(client_fd);
			close(client_fd);
		}
	}

	close(listen_fd);
	return (0);

error:
	if (listen_fd)
		close(listen_fd);

	return (1);
}

static void
handle_client(int client_fd)
{
	int server_fd;
	int max_fd;
	time_t t;
	unsigned long bytes_in, bytes_out;

	t = time(NULL);
	bytes_in = bytes_out = 0;
	irc_pass[0] = irc_nick[0] = irc_ident[0] = irc_channel[0] = 0;
	icb_logged_in = 0;
	terminate_client = 1;

	printf("connecting to server %s:%u\n",
	    inet_ntoa(sa_connect.sin_addr), ntohs(sa_connect.sin_port));
	irc_send_notice(client_fd, "*** Connecting to server %s:%u",
	    inet_ntoa(sa_connect.sin_addr), ntohs(sa_connect.sin_port));
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		goto done;
	}
	if (connect(server_fd, (struct sockaddr *)&sa_connect,
	    sizeof(sa_connect))) {
		perror("connect");
		irc_send_notice(client_fd, "*** Error: connect: %s",
		    strerror(errno));
		close(server_fd);
		goto done;
	}

	if (fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL) | O_NONBLOCK) ||
	    fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL) | O_NONBLOCK)) {
		perror("fcntl");
		goto done;
	}

	if (client_fd > server_fd)
		max_fd = client_fd;
	else
		max_fd = server_fd;

	irc_send_notice(client_fd, "*** Connected");
	terminate_client = 0;
	icb_init();
	while (!terminate_client) {
		fd_set readfds;
		struct timeval tv;
		int r;

		FD_ZERO(&readfds);
		FD_SET(server_fd, &readfds);
		FD_SET(client_fd, &readfds);
		memset(&tv, 0, sizeof(tv));
                tv.tv_sec = 10;
                r = select(max_fd + 1, &readfds, NULL, NULL, &tv);
                if (r < 0) {
			if (errno != EINTR) {
				perror("select");
				break;
			}
			continue;
		}
		if (r > 0) {
			char buf[65535];
			int len;

			if (FD_ISSET(server_fd, &readfds)) {
				len = read(server_fd, buf, sizeof(buf));
				if (len < 0) {
					if (errno == EINTR)
						continue;
					perror("read");
					len = 0;
				}
				if (len == 0) {
					printf("connection closed by server\n");
					irc_send_notice(client_fd,
					    "*** Connection closed by server");
					break;
				}
				icb_recv(buf, len, client_fd, server_fd);
				bytes_in += len;
			}
			if (FD_ISSET(client_fd, &readfds)) {
				len = read(client_fd, buf, sizeof(buf));
				if (len < 0) {
					if (errno == EINTR)
						continue;
					perror("read");
					len = 0;
				}
				if (len == 0) {
					printf("connection closed by client\n");
					break;
				}
				irc_recv(buf, len, client_fd, server_fd);
				bytes_out += len;
			}
		}
	}

done:
	if (server_fd >= 0)
		close(server_fd);
	printf("(%lu seconds, %lu:%lu bytes)\n",
	    (unsigned long)(time(NULL) - t), bytes_out, bytes_in);
	if (terminate_client)
		irc_send_notice(client_fd, "*** Closing connection "
		    "(%u seconds, %lu:%lu bytes)",
		    time(NULL) - t, bytes_out, bytes_in);
}

int
sync_write(int fd, const char *buf, int len)
{
	int off = 0;

	while (len > off) {
		fd_set writefds;
		struct timeval tv;
		int r;

		FD_ZERO(&writefds);
		FD_SET(fd, &writefds);
		memset(&tv, 0, sizeof(tv));
		tv.tv_sec = 10;
		r = select(fd + 1, NULL, &writefds, NULL, &tv);
		if (r < 0) {
			if (errno != EINTR) {
				perror("select");
				return (1);
			}
			continue;
		}
		if (r > 0 && FD_ISSET(fd, &writefds)) {
			r = write(fd, buf + off, len - off);
			if (r < 0) {
				perror("write");
				return (1);
			}
			off += r;
		}
	}
	return (0);
}
