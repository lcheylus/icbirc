/*
 * Copyright (c) 2003-2004 Daniel Hartmeier
 * Copyright (c) 2023-2024 Laurent Cheylus <foxy@free.fr>
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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "irc.h"
#include "icb.h"

extern void	 scan(const char **, char *, size_t, const char *,
		    const char *);
extern int	 sync_write(int, const char *, int);

static void	 irc_cmd(char *, int, int);

static void	 irc_send_pong(int, const char *);

extern int terminate_client;

char irc_pass[256];
char irc_ident[256];
char irc_nick[256];
char irc_channel[256];
int in_irc_channel;

/*
 * irc_recv() receives read(2) chunks and assembles complete lines, which are
 * passed to irc_cmd(). Overlong lines are truncated after 65kB.
 *
 * XXX: argument checking is not as strong as for ICB (trusting the client)
 *
 */

void
irc_recv(const char *buf, unsigned len, int client_fd, int server_fd)
{
	static char cmd[65535];
	static unsigned off = 0;

	while (len > 0) {
		while (len > 0 && off < (sizeof(cmd) - 1) && *buf != '\n') {
			cmd[off++] = *buf++;
			len--;
		}
		if (off == (sizeof(cmd) - 1))
			while (len > 0 && *buf != '\n') {
				buf++;
				len--;
			}
		/* off <= sizeof(cmd) - 1 */
		if (len > 0 && *buf == '\n') {
			buf++;
			len--;
			if (off > 0 && cmd[off - 1] == '\r')
				cmd[off - 1] = 0;
			else
				cmd[off] = 0;
			irc_cmd(cmd, client_fd, server_fd);
			off = 0;
		}
	}
}

static void
irc_cmd(char *cmd, int client_fd, int server_fd)
{
	if (!strncasecmp(cmd, "RAWICB ", 7)) {
		icb_send_raw(server_fd, cmd + 7);
		return;
	}

	char *argv[10], *p;
	int argc = 1;

	for (p = cmd, argv[0] = p; argc < 10 && (p = strchr(p, ' ')) != NULL;
	    argc++) {
		*p++ = '\0';
		while (*p == ' ')
			p++;
		if (*p == ':') {
			argv[argc] = p + 1;
			argc++;
			break;
		}
		argv[argc] = p;
	}

	if (!strcasecmp(argv[0], "PASS")) {
		strlcpy(irc_pass, argv[1], sizeof(irc_pass));
	} else if (!strcasecmp(argv[0], "USER")) {
		strlcpy(irc_ident, argv[1], sizeof(irc_ident));
		if (!icb_logged_in && irc_nick[0] && irc_ident[0])
			icb_send_login(server_fd, irc_nick,
			    irc_ident, irc_pass);
	} else if (!strcasecmp(argv[0], "NICK")) {
		strlcpy(irc_nick, argv[1], sizeof(irc_nick));
		if (icb_logged_in)
			icb_send_name(server_fd, irc_nick);
		else if (irc_nick[0] && irc_ident[0])
			icb_send_login(server_fd, irc_nick,
			    irc_ident, irc_pass);
	} else if (!strcasecmp(argv[0], "JOIN")) {
		icb_send_group(server_fd,
		    argv[1] + (argv[1][0] == '#' ? 1 : 0));
	} else if (!strcasecmp(argv[0], "PART")) {
		in_irc_channel = 0;
	} else if (!strcasecmp(argv[0], "PRIVMSG") ||
	    !strcasecmp(argv[0], "NOTICE")) {
		char msg[8192];
		unsigned i, j;

		strlcpy(msg, argv[2], sizeof(msg));
		/* strip \001 found in CTCP messages */
		i = 0;
		while (msg[i]) {
			if (msg[i] == '\001') {
				for (j = i; msg[j + 1]; ++j)
					msg[j] = msg[j + 1];
				msg[j] = 0;
			} else
				i++;
		}
		if (!strcmp(argv[1], irc_channel))
			icb_send_openmsg(server_fd, msg);
		else
			icb_send_privmsg(server_fd, argv[1], msg);
	} else if (!strcasecmp(argv[0], "MODE")) {
		if (strcmp(argv[1], irc_channel))
			return;
		if (argc == 2)
			icb_send_names(server_fd, irc_channel);
		else {
			if (strcmp(argv[2], "+o")) {
				printf("irc_cmd: invalid MODE args '%s'\n",
				    argv[2]);
				return;
			}
			icb_send_pass(server_fd, argv[3]);
		}
	} else if (!strcasecmp(argv[0], "TOPIC")) {
		if (strcmp(argv[1], irc_channel)) {
			printf("irc_cmd: invalid TOPIC channel '%s'\n",
			    argv[1]);
			return;
		}
		icb_send_topic(server_fd, argv[2]);
	} else if (!strcasecmp(argv[0], "LIST")) {
		icb_send_list(server_fd);
	} else if (!strcasecmp(argv[0], "NAMES")) {
		icb_send_names(server_fd, argv[1]);
	} else if (!strcasecmp(argv[0], "WHOIS")) {
		icb_send_whois(server_fd, argv[1]);
	} else if (!strcasecmp(argv[0], "WHO")) {
		icb_send_who(server_fd, argv[1]);
	} else if (!strcasecmp(argv[0], "KICK")) {
		if (strcmp(argv[1], irc_channel)) {
			printf("irc_cmd: invalid KICK args '%s'\n", argv[1]);
			return;
		}
		icb_send_boot(server_fd, argv[2]);
	} else if (!strcasecmp(argv[0], "PING")) {
		icb_send_noop(server_fd);
		irc_send_pong(client_fd, argv[1]);
	} else if (!strcasecmp(argv[0], "QUIT")) {
		printf("client QUIT\n");
		terminate_client = 1;
	} else if (!strcasecmp(argv[0], "CAP")) {
		/*
		 * avoid printing "unknown command 'CAP'"
		 * https://ircv3.net/specs/extensions/capability-negotiation.html
		 *
		 * there is nothing to do if the server (icbirc) doesn't support
		 * capability negotiation.
		 */
	} else
		printf("irc_cmd: unknown command '%s'\n", argv[0]);
}

void
irc_send_notice(int fd, const char *format, ...)
{
	char cmd[16384], msg[8192];
	va_list ap;

	va_start(ap, format);
	vsnprintf(msg, sizeof(msg), format, ap);
	va_end(ap);
	snprintf(cmd, sizeof(cmd), "NOTICE %s\r\n", msg);
	sync_write(fd, cmd, strlen(cmd));
}

void
irc_send_code(int fd, const char *from, const char *nick, const char *code,
    const char *format, ...)
{
	char cmd[16384], msg[8192];
	va_list ap;

	va_start(ap, format);
	vsnprintf(msg, sizeof(msg), format, ap);
	va_end(ap);
	snprintf(cmd, sizeof(cmd), ":%s %s %s :%s\r\n", from, code, nick, msg);
	sync_write(fd, cmd, strlen(cmd));
}

void
irc_send_msg(int fd, const char *src, const char *dst, const char *msg)
{
	char cmd[8192];

	snprintf(cmd, sizeof(cmd), ":%s PRIVMSG %s :%s\r\n", src, dst, msg);
	sync_write(fd, cmd, strlen(cmd));
}

void
irc_send_join(int fd, const char *src, const char *dst)
{
	char cmd[8192];

	snprintf(cmd, sizeof(cmd), ":%s JOIN :%s\r\n", src, dst);
	sync_write(fd, cmd, strlen(cmd));
	in_irc_channel = 1;
}

void
irc_send_part(int fd, const char *src, const char *dst)
{
	char cmd[8192];

	snprintf(cmd, sizeof(cmd), ":%s PART :%s\r\n", src, dst);
	sync_write(fd, cmd, strlen(cmd));
}

void
irc_send_pong(int fd, const char *daemon)
{
	char cmd[8192];

	snprintf(cmd, sizeof(cmd), "PONG %s\r\n", daemon);
	sync_write(fd, cmd, strlen(cmd));
}
