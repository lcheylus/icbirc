/*
 * Copyright (c) 2003-2004 Daniel Hartmeier
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

static void	 irc_cmd(const char *, int, int);

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
irc_cmd(const char *cmd, int client_fd, int server_fd)
{
	if (!strncasecmp(cmd, "PASS ", 5)) {
		cmd += 5;
		scan(&cmd, irc_pass, sizeof(irc_pass), " ", " ");
	} else if (!strncasecmp(cmd, "USER ", 5)) {
		cmd += 5;
		scan(&cmd, irc_ident, sizeof(irc_ident), " ", " ");
		if (!icb_logged_in && irc_nick[0] && irc_ident[0])
			icb_send_login(server_fd, irc_nick,
			    irc_ident, irc_pass);
	} else if (!strncasecmp(cmd, "NICK ", 5)) {
		cmd += 5;
		scan(&cmd, irc_nick, sizeof(irc_nick), " ", " ");
		if (icb_logged_in)
			icb_send_name(server_fd, irc_nick);
		else if (irc_nick[0] && irc_ident[0])
			icb_send_login(server_fd, irc_nick,
			    irc_ident, irc_pass);
	} else if (!strncasecmp(cmd, "JOIN ", 5)) {
		char group[128];

		cmd += 5;
		if (*cmd == '#')
			cmd++;
		scan(&cmd, group, sizeof(group), " ", " ");
		icb_send_group(server_fd, group);
	} else if (!strncasecmp(cmd, "PART ", 5)) {
		in_irc_channel = 0;
	} else if (!strncasecmp(cmd, "PRIVMSG ", 8) ||
	    !strncasecmp(cmd, "NOTICE ", 7)) {
		char dst[128];
		char msg[8192];
		unsigned i, j;

		cmd += strncasecmp(cmd, "NOTICE ", 7) ? 8 : 7;
		scan(&cmd, dst, sizeof(dst), " ", " ");
		scan(&cmd, msg, sizeof(msg), " ", "");
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
		if (!strcmp(dst, irc_channel))
			icb_send_openmsg(server_fd,
			    msg + (msg[0] == ':' ? 1 : 0));
		else
			icb_send_privmsg(server_fd, dst,
			    msg + (msg[0] == ':' ? 1 : 0));
	} else if (!strncasecmp(cmd, "MODE ", 5)) {
		cmd += 5;
		if (!strcmp(cmd, irc_channel))
			icb_send_names(server_fd, irc_channel);
		else if (!strncmp(cmd, irc_channel, strlen(irc_channel))) {
			cmd += strlen(irc_channel);
			if (strncmp(cmd, " +o ", 4)) {
				printf("irc_cmd: invalid MODE args '%s'\n",
				    cmd);
				return;
			}
			cmd += 4;
			icb_send_pass(server_fd, cmd);
		}
	} else if (!strncasecmp(cmd, "TOPIC ", 6)) {
		cmd += 6;
		if (strncmp(cmd, irc_channel, strlen(irc_channel))) {
			printf("irc_cmd: invalid TOPIC args '%s'\n", cmd);
			return;
		}
		cmd += strlen(irc_channel);
		if (strncmp(cmd, " :", 2)) {
			printf("irc_cmd: invalid TOPIC args '%s'\n", cmd);
			return;
		}
		cmd += 2;
		icb_send_topic(server_fd, cmd);
	} else if (!strcasecmp(cmd, "LIST")) {
		icb_send_list(server_fd);
	} else if (!strncasecmp(cmd, "NAMES ", 6)) {
		cmd += 6;
		icb_send_names(server_fd, cmd);
	} else if (!strncasecmp(cmd, "WHOIS ", 6)) {
		cmd += 6;
		icb_send_whois(server_fd, cmd);
	} else if (!strncasecmp(cmd, "WHO ", 4)) {
		cmd += 4;
		icb_send_who(server_fd, cmd);
	} else if (!strncasecmp(cmd, "KICK ", 5)) {
		char channel[128], nick[128];

		cmd += 5;
		scan(&cmd, channel, sizeof(channel), " ", " ");
		scan(&cmd, nick, sizeof(nick), " ", " ");
		if (strcmp(channel, irc_channel)) {
			printf("irc_cmd: invalid KICK args '%s'\n", cmd);
			return;
		}
		icb_send_boot(server_fd, nick);
	} else if (!strncasecmp(cmd, "PING ", 5)) {
		icb_send_noop(server_fd);
		cmd += 5;
		irc_send_pong(client_fd, cmd);
	} else if (!strncasecmp(cmd, "RAWICB ", 7)) {
		cmd += 7;
		icb_send_raw(server_fd, cmd);
	} else if (!strncasecmp(cmd, "QUIT ", 5)) {
		printf("client QUIT\n");
		terminate_client = 1;
	} else
		printf("irc_cmd: unknown cmd '%s'\n", cmd);
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
