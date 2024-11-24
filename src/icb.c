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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include "icb.h"
#include "irc.h"

extern int	 sync_write(int, const char *, int);

static unsigned char	 icb_args(const unsigned char *, unsigned char, char [255][255]);
static void		 icb_cmd(const unsigned char *, unsigned char, int, int);
static void		 icb_ico(int, const char *);
static void		 icb_iwl(int, const char *, const char *, long,
			    long, const char *, const char *);
static void		 icb_send_hw(int, const char *);

extern int terminate_client;
int icb_logged_in = 0;

static char icb_protolevel[256];
static char icb_hostid[256];
static char icb_serverid[256];
static char icb_moderator[256];
enum { imode_none, imode_list, imode_names, imode_whois, imode_who };
static int imode = imode_none;
static char icurgroup[256];
static char igroup[256];
static char inick[256];
static char ihostmask[256];
static unsigned off;

/*
 * A single ICB packet consists of a length byte, a command byte and
 * variable arguments. The length includes command and arguments, but
 * not the length byte itself. Since length is at most 255, the entire
 * packet is at most 256 bytes long.
 *
 * icb_recv() gets passed read(2) chunks and assembles a complete packet
 * (including the length byte) in cmd. Once complete, the packet is
 * passed to icb_cmd() without the length byte. Hence, arguments to
 * icb_cmd() are at most 255 bytes long.
 *
 * icb_cmd() skips the command byte and passes only the variable
 * arguments to icb_args(). Hence, arguments to icb_args() are at most
 * 254 octects long.
 *
 * Variable arguments consist of zero or more strings separated by
 * \001 characters. The strings need not be null-terminated and may
 * be empty. Hence, there can be at most 255 strings and a string can
 * be at most 254 bytes long. icb_args() fills the array argument,
 * null-terminating each argument.
 *
 * This (together with the comments below) should be convincing proof
 * that the char [255][255] as well as the unsigned char variables
 * cannot overflow.
 *
 * Further argument parsing in icb_cmd() and icb_ico() relies on the
 * fact that any argument can be at most 255 bytes long (including
 * null-termination).
 *
 * The icb_send_*() functions may get arbitrarily long arguments from
 * IRC, they may generate packets of at most 256 bytes size. Overlong
 * arguments are truncated, except for open and personal messages,
 * which are split across multiple packets, if needed (generating
 * separate messages on ICB).
 *
 * The ICB protocol definition is not very clear about null-termination
 * of arguments for packets generated by the client. Without any
 * termination, at least one common server implementation shows a
 * buffer re-use bug. Terminating all arguments, however, causes
 * another server implementation to refuse certain commands. The
 * best approach seems to be to null-terminate only the last
 * argument. Where the code below violates that rule, that was done
 * intentionally after testing.
 *
 */

void
scan(const unsigned char **s, char *d, size_t siz, const char *skip, const char *term)
{
	while (**s && strchr(skip, **s) != NULL)
		(*s)++;
	while (**s && strchr(term, **s) == NULL) {
		if (siz > 1) {
			*d++ = **s;
			siz--;
		}
		(*s)++;
	}
	if (siz > 0)
		*d = '\0';
}

void
icb_init(void)
{
	memset(icb_protolevel, 0, sizeof(icb_protolevel));
	memset(icb_hostid, 0, sizeof(icb_hostid));
	memset(icb_serverid, 0, sizeof(icb_serverid));
	memset(icb_moderator, 0, sizeof(icb_moderator));
	imode = imode_none;
	memset(icurgroup, 0, sizeof(icurgroup));
	memset(igroup, 0, sizeof(igroup));
	memset(inick, 0, sizeof(inick));
	memset(ihostmask, 0, sizeof(ihostmask));
	off = 0;
}

void
icb_recv(const char *buf, unsigned len, int fd, int server_fd)
{
	static unsigned char cmd[256];

	while (len > 0) {
		if (off == 0) {
			cmd[off++] = *buf++;
			/* 0 < cmd[0] <= 255 */
			len--;
		}
		/* off > 0, 0 < cmd[0] <= 255 */
		while (len > 0 && (off - 1) < cmd[0]) {
			cmd[off++] = *buf++;
			len--;
		}
		/* len == 0 || (off - 1) == cmd[0] */
		if ((off - 1) == cmd[0]) {
			icb_cmd(cmd + 1, off - 1 /* <= 255 */, fd, server_fd);
			off = 0;
		}
	}
}

static unsigned char
icb_args(const unsigned char *data, unsigned char len, char args[255][255])
{
	unsigned char i = 0, j = 0, k = 0;

	/* 0 < len < 255 */
	while (i < len) {
		/* 0 <= i, j, k < 255 */
		if (data[i] == '\001') {
			args[j++][k] = 0;
			k = 0;
		} else if (data[i] == '\r' || data[i] == '\n')
			args[j][k++] = '?';
		else
			args[j][k++] = data[i];
		i++;
	}
	/* i, j, k < 255 */
	if (k > 0)
		args[j++][k] = 0;
	/* j <= 255 */
	for (i = j; i < 255; ++i)
		args[i][0] = 0;
	return (j);
}

static void
icb_cmd(const unsigned char *cmd, unsigned char len, int fd, int server_fd)
{
	char args[255][255];
	const unsigned char *a = (unsigned char *)args[1];
	unsigned char i, j;
	char s[8192];

	if (len == 0)
		return;

	/* 0 < len <= 255 */
	i = icb_args(cmd + 1, len - 1 /* < 255 */, args);
	/* 0 <= i <= 255 */
	switch (cmd[0]) {
	case 'a':	/* Login OK */
		irc_send_code(fd, icb_hostid, irc_nick, "001",
		    "Welcome to icbirc %s", irc_nick);
		irc_send_code(fd, icb_hostid, irc_nick, "002",
		    "Your host is %s running %s protocol %s",
		    icb_hostid, icb_serverid, icb_protolevel);
		irc_send_code(fd, icb_hostid, irc_nick, "003",
		    "This server was created recently");
		irc_send_code(fd, icb_hostid, irc_nick, "004",
		    "%s %d", icb_serverid, icb_protolevel);
		/* some clients really want to see a MOTD */
		irc_send_code(fd, icb_hostid, irc_nick, "375",
		    "ICB server: %s", icb_serverid);
		irc_send_code(fd, icb_hostid, irc_nick, "376",
		    "End of MOTD");
		icb_logged_in = 1;
		break;
	case 'b':	/* Open Message */
		if (!in_irc_channel) {
			irc_send_join(fd, irc_nick, irc_channel);
			icb_send_names(server_fd, irc_channel);
		}
		irc_send_msg(fd, args[0], irc_channel, args[1]);
		break;
	case 'c':	/* Personal Message */
		irc_send_msg(fd, args[0], irc_nick, args[1]);
		break;
	case 'd':	/* Status Message */
		if (!strcmp(args[0], "Status") && !strncmp(args[1],
		    "You are now in group ", 21)) {
			if (irc_channel[0])
				irc_send_part(fd, irc_nick, irc_channel);
			irc_channel[0] = '#';
			a += 21;
			scan(&a, irc_channel + 1, sizeof(irc_channel) - 1,
			    " ", " ");
			irc_send_join(fd, irc_nick, irc_channel);
			icb_send_names(server_fd, irc_channel);
		} else if (!strcmp(args[0], "Arrive") ||
		    !strcmp(args[0], "Sign-on")) {
			char nick[256], host[256];

			scan(&a, nick, sizeof(nick), " ", " ");
			scan(&a, host, sizeof(host), " (", ")");
			snprintf(s, sizeof(s), "%s!%s", nick, host);
			irc_send_join(fd, s, irc_channel);
		} else if (!strcmp(args[0], "Depart")) {
			char nick[256], host[256];

			scan(&a, nick, sizeof(nick), " ", " ");
			scan(&a, host, sizeof(host), " (", ")");
			snprintf(s, sizeof(s), "%s!%s", nick, host);
			irc_send_part(fd, s, irc_channel);
		} else if (!strcmp(args[0], "Sign-off")) {
			char nick[256], host[256], reason[256];

			scan(&a, nick, sizeof(nick), " ", " ");
			scan(&a, host, sizeof(host), " (", ")");
			scan(&a, reason, sizeof(reason), " )", "");
			if (strlen(reason) > 0 &&
			    reason[strlen(reason) - 1] == '.')
				reason[strlen(reason) - 1] = 0;
			snprintf(s, sizeof(s), ":%s!%s QUIT :%s\r\n",
			    nick, host, reason);
			sync_write(fd, s, strlen(s));
		} else if (!strcmp(args[0], "Name")) {
			char old_nick[256], new_nick[256];

			scan(&a, old_nick, sizeof(old_nick), " ", " ");
			if (strncmp((const char *)a, " changed nickname to ", 21))
				return;
			a += 21;
			scan(&a, new_nick, sizeof(new_nick), " ", " ");
			snprintf(s, sizeof(s), ":%s NICK :%s\r\n",
			    old_nick, new_nick);
			sync_write(fd, s, strlen(s));
			if (!strcmp(old_nick, irc_nick))
				strlcpy(irc_nick, new_nick,
				    sizeof(irc_nick));
		} else if (!strcmp(args[0], "Topic")) {
			char nick[256], topic[256];

			scan(&a, nick, sizeof(nick), " ", " ");
			if (strncmp((const char *)a, " changed the topic to \"", 23))
				return;
			a += 23;
			scan(&a, topic, sizeof(topic), "", "\"");
			snprintf(s, sizeof(s), ":%s TOPIC %s :%s\r\n",
			    nick, irc_channel, topic);
			sync_write(fd, s, strlen(s));
		} else if (!strcmp(args[0], "Pass")) {
			char old_mod[256], new_mod[256];

			scan(&a, old_mod, sizeof(old_mod), " ", " ");
			if (!strncmp((const char *)a, " has passed moderation to ", 26)) {
				a += 26;
				scan(&a, new_mod, sizeof(new_mod), " ", " ");
				snprintf(s, sizeof(s),
				    ":%s MODE %s -o+o %s %s\r\n",
				    old_mod, irc_channel, old_mod, new_mod);
			} else if (!strcmp((const char *)a, " is now mod.")) {
				snprintf(s, sizeof(s),
				    ":%s MODE %s +o %s\r\n",
				    icb_hostid, irc_channel, old_mod);
			} else
				return;
			sync_write(fd, s, strlen(s));
			strlcpy(icb_moderator, new_mod, sizeof(icb_moderator));
		} else if (!strcmp(args[0], "Boot")) {
			char nick[256];

			scan(&a, nick, sizeof(nick), " ", " ");
			if (strcmp((const char *)a, " was booted."))
				return;
			snprintf(s, sizeof(s), ":%s KICK %s %s :booted\r\n",
			    icb_moderator, irc_channel, nick);
			sync_write(fd, s, strlen(s));
		} else
			irc_send_notice(fd, "ICB Status Message: %s: %s",
			    args[0], args[1]);
		break;
	case 'e':	/* Error Message */
		irc_send_notice(fd, "ICB Error Message: %s", args[0]);
		break;
	case 'f':	/* Important Message */
		irc_send_notice(fd, "ICB Important Message: %s: %s",
		    args[0], args[1]);
		break;
	case 'g':	/* Exit */
		irc_send_notice(fd, "ICB Exit");
		printf("server Exit\n");
		terminate_client = 1;
		break;
	case 'i':	/* Command Output */
		if (!strcmp(args[0], "co")) {
			for (j = 1; j < i; ++j)
				icb_ico(fd, args[j]);
		} else if (!strcmp(args[0], "wl")) {
			icb_iwl(fd, args[1], args[2], atol(args[3]),
			    atol(args[5]), args[6], args[7]);
		} else if (!strcmp(args[0], "wh")) {
			/* display whois header, deprecated */
		} else
			irc_send_notice(fd, "ICB Command Output: %s: %u args",
			    args[0], i - 1);
		break;
	case 'j':	/* Protocol */
		strlcpy(icb_protolevel, args[0], sizeof(icb_protolevel));
		strlcpy(icb_hostid, args[1], sizeof(icb_hostid));
		strlcpy(icb_serverid, args[2], sizeof(icb_serverid));
		break;
	case 'k':	/* Beep */
		irc_send_notice(fd, "ICB Beep from %s", args[0]);
		break;
	case 'l':	/* Ping */
		irc_send_notice(fd, "ICB Ping '%s'", args[0]);
		break;
	case 'm':	/* Pong */
		irc_send_notice(fd, "ICB Pong '%s'", args[0]);
		break;
	case 'n':	/* No-op */
		irc_send_notice(fd, "ICB No-op");
		break;
	default:
		irc_send_notice(fd, "ICB unknown command %d: %u args",
		    (int)cmd[0], i);
	}
}

static void
icb_iwl(int fd, const char *flags, const char *nick, long idle,
    long signon, const char *ident, const char *host)
{
	char s[8192];
	int chanop = strchr(flags, 'm') != NULL;

	if (imode == imode_whois && !strcmp(nick, inick)) {
		snprintf(s, sizeof(s), ":%s 311 %s %s %s %s * :\r\n",
		    icb_hostid, irc_nick, nick, ident, host);
		sync_write(fd, s, strlen(s));
		if (icurgroup[0]) {
			snprintf(s, sizeof(s), ":%s 319 %s %s :%s%s\r\n",
			    icb_hostid, irc_nick, nick, chanop ? "@" : "",
			    icurgroup);
			sync_write(fd, s, strlen(s));
		}
		snprintf(s, sizeof(s), ":%s 312 %s %s %s :\r\n",
		    icb_hostid, irc_nick, nick, icb_hostid);
		sync_write(fd, s, strlen(s));
		snprintf(s, sizeof(s), ":%s 317 %s %s %ld %ld :seconds idle, "
		    "signon time\r\n",
		    icb_hostid, irc_nick, nick, idle, signon);
		sync_write(fd, s, strlen(s));
		snprintf(s, sizeof(s), ":%s 318 %s %s :End of /WHOIS list.\r\n",
		    icb_hostid, irc_nick, nick);
		sync_write(fd, s, strlen(s));
	} else if (imode == imode_names && !strcmp(icurgroup, igroup)) {
		snprintf(s, sizeof(s), ":%s 353 %s @ %s :%s%s \r\n",
		    icb_hostid, irc_nick, icurgroup, chanop ? "@" : "", nick);
		sync_write(fd, s, strlen(s));
		snprintf(s, sizeof(s), ":%s 352 %s %s %s %s %s %s H :5 %s\r\n",
		    icb_hostid, irc_nick, icurgroup, nick, host, icb_hostid,
		    nick, ident);
		sync_write(fd, s, strlen(s));
	} else if (imode == imode_who) {
		int match;

		if (ihostmask[0] == '#')
			match = !strcmp(icurgroup, ihostmask);
		else {
			char hostmask[1024];

			snprintf(hostmask, sizeof(hostmask), "%s!%s@%s",
			    nick, ident, host);
			match = strstr(hostmask, ihostmask) != NULL;
		}
		if (match) {
			snprintf(s, sizeof(s), ":%s 352 %s %s %s %s %s %s "
			    "H :5 %s\r\n",
			    icb_hostid, irc_nick, icurgroup, nick, host,
			    icb_hostid, nick, ident);
			sync_write(fd, s, strlen(s));
		}
	}

	if (chanop && !strcmp(icurgroup, irc_channel))
		strlcpy(icb_moderator, nick, sizeof(icb_moderator));
}

static void
icb_ico(int fd, const char *arg)
{
	char s[8192];

	if (!strncmp(arg, "Group: ", 7)) {
		char group[256];
		int i = 0;
		char *topic;

		arg += 7;
		group[i++] = '#';
		while (*arg && *arg != ' ')
			group[i++] = *arg++;
		group[i] = 0;
		strlcpy(icurgroup, group, sizeof(icurgroup));
		topic = strstr(arg, "Topic: ");
		if (topic == NULL)
			topic = "(None)";
		else
			topic += 7;
		if (imode == imode_list) {
			snprintf(s, sizeof(s), ":%s 322 %s %s 1 :%s\r\n",
			    icb_hostid, irc_nick, group, topic);
			sync_write(fd, s, strlen(s));
		} else if (imode == imode_names &&
		    !strcmp(icurgroup, igroup)) {
			snprintf(s, sizeof(s), ":%s 332 %s %s :%s\r\n",
			    icb_hostid, irc_nick, icurgroup, topic);
			sync_write(fd, s, strlen(s));
		}
	} else if (!strncmp(arg, "Total: ", 7)) {
		if (imode == imode_list) {
			snprintf(s, sizeof(s), ":%s 323 %s :End of /LIST\r\n",
			    icb_hostid, irc_nick);
			sync_write(fd, s, strlen(s));
		} else if (imode == imode_names) {
			snprintf(s, sizeof(s), ":%s 366 %s %s :End of "
			    "/NAMES list.\r\n",
			    icb_hostid, irc_nick, igroup);
			sync_write(fd, s, strlen(s));
		} else if (imode == imode_who) {
			snprintf(s, sizeof(s), ":%s 315 %s %s :End of "
			    "/WHO list.\r\n",
			    icb_hostid, irc_nick, ihostmask);
			sync_write(fd, s, strlen(s));
		}
		imode = imode_none;
	} else if (strcmp(arg, " "))
		irc_send_notice(fd, "*** Unknown ico: %s", arg);
}

#define MAX_MSG_SIZE 246

void
icb_send_login(int fd, const char *nick, const char *ident, const char *group)
{
	char cmd[256];
	unsigned off = 1;
	const char *login_cmd = "login";

	cmd[off++] = 'a';
	while (*ident && off < MAX_MSG_SIZE)
		cmd[off++] = *ident++;
	cmd[off++] = '\001';
	while (*nick && off < MAX_MSG_SIZE)
		cmd[off++] = *nick++;
	cmd[off++] = '\001';
	while (*group && off < MAX_MSG_SIZE)
		cmd[off++] = *group++;
	cmd[off++] = '\001';
	while (*login_cmd)
		cmd[off++] = *login_cmd++;
	cmd[off++] = '\001';
	cmd[off++] = '\001';
	cmd[off++] = '\001';
	cmd[0] = off - 1;
	sync_write(fd, cmd, off);
}

void
icb_send_openmsg(int fd, const char *msg)
{
	unsigned char cmd[256];
	unsigned off;

	while (*msg) {
		off = 1;
		cmd[off++] = 'b';
		while (*msg && off < MAX_MSG_SIZE)
			cmd[off++] = *msg++;
		cmd[off++] = 0;
		cmd[0] = off - 1;
		/* cmd[0] <= MAX_MSG_SIZE */
		sync_write(fd, (const char *)cmd, off);
	}
}

void
icb_send_privmsg(int fd, const char *nick, const char *msg)
{
	unsigned char cmd[256];
	unsigned off;

	while (*msg) {
		const char *n = nick;

		off = 1;
		cmd[off++] = 'h';
		cmd[off++] = 'm';
		cmd[off++] = '\001';
		while (*n && off < MAX_MSG_SIZE)
			cmd[off++] = *n++;
		cmd[off++] = ' ';
		while (*msg && off < MAX_MSG_SIZE)
			cmd[off++] = *msg++;
		cmd[off++] = 0;
		cmd[0] = off - 1;
		/* cmd[0] <= MAX_MSG_SIZE */
		sync_write(fd, (const char *)cmd, off);
	}
}

void
icb_send_group(int fd, const char *group)
{
	char cmd[256];
	unsigned off = 1;

	cmd[off++] = 'h';
	cmd[off++] = 'g';
	cmd[off++] = '\001';
	while (*group && off < MAX_MSG_SIZE)
		cmd[off++] = *group++;
	cmd[off++] = 0;
	cmd[0] = off - 1;
	sync_write(fd, cmd, off);
}

static void
icb_send_hw(int fd, const char *arg)
{
	char cmd[256];
	unsigned off = 1;

	icurgroup[0] = 0;
	cmd[off++] = 'h';
	cmd[off++] = 'w';
	cmd[off++] = '\001';
	while (*arg && off < MAX_MSG_SIZE)
		cmd[off++] = *arg++;
	cmd[off++] = 0;
	cmd[0] = off - 1;
	sync_write(fd, cmd, off);
}

void
icb_send_list(int fd)
{
	if (imode != imode_none)
		return;
	imode = imode_list;
	icb_send_hw(fd, "-g");
}

void
icb_send_names(int fd, const char *group)
{
	if (imode != imode_none)
		return;
	imode = imode_names;
	strlcpy(igroup, group, sizeof(igroup));
	icb_send_hw(fd, "");
}

void
icb_send_whois(int fd, const char *nick)
{
	if (imode != imode_none)
		return;
	imode = imode_whois;
	strlcpy(inick, nick, sizeof(inick));
	icb_send_hw(fd, "");
}

void
icb_send_who(int fd, const char *hostmask)
{
	if (imode != imode_none)
		return;
	imode = imode_who;
	strlcpy(ihostmask, hostmask, sizeof(ihostmask));
	icb_send_hw(fd, "");
}

void
icb_send_pass(int fd, const char *nick)
{
	char cmd[256];
	unsigned off = 1;
	const char *pass_cmd = "pass";

	cmd[off++] = 'h';
	while (*pass_cmd)
		cmd[off++] = *pass_cmd++;
	cmd[off++] = '\001';
	while (*nick && off < MAX_MSG_SIZE)
		cmd[off++] = *nick++;
	cmd[off++] = 0;
	cmd[0] = off - 1;
	sync_write(fd, cmd, off);
}

void
icb_send_topic(int fd, const char *topic)
{
	char cmd[256];
	unsigned off = 1;
	const char *topic_cmd = "topic";

	cmd[off++] = 'h';
	while (*topic_cmd)
		cmd[off++] = *topic_cmd++;
	cmd[off++] = '\001';
	while (*topic && off < MAX_MSG_SIZE)
		cmd[off++] = *topic++;
	cmd[off++] = 0;
	cmd[0] = off - 1;
	sync_write(fd, cmd, off);
}

void
icb_send_boot(int fd, const char *nick)
{
	char cmd[256];
	unsigned off = 1;
	const char *boot_cmd = "boot";

	cmd[off++] = 'h';
	while (*boot_cmd)
		cmd[off++] = *boot_cmd++;
	cmd[off++] = '\001';
	while (*nick && off < MAX_MSG_SIZE)
		cmd[off++] = *nick++;
	cmd[off++] = 0;
	cmd[0] = off - 1;
	sync_write(fd, cmd, off);
}

void
icb_send_name(int fd, const char *nick)
{
	char cmd[256];
	unsigned off = 1;
	const char *name_cmd = "name";

	cmd[off++] = 'h';
	while (*name_cmd)
		cmd[off++] = *name_cmd++;
	cmd[off++] = '\001';
	while (*nick && off < MAX_MSG_SIZE)
		cmd[off++] = *nick++;
	cmd[off++] = 0;
	cmd[0] = off - 1;
	sync_write(fd, cmd, off);
}

void
icb_send_raw(int fd, const char *data)
{
	char cmd[256];
	unsigned off = 1;

	while (*data && off < MAX_MSG_SIZE) {
		if (*data == ',')
			cmd[off++] = '\001';
		else if (*data == '\\')
			cmd[off++] = 0;
		else
			cmd[off++] = *data;
		data++;
	}
	cmd[off++] = 0;
	cmd[0] = off - 1;
	sync_write(fd, cmd, off);
}

void
icb_send_noop(int fd)
{
	char cmd[256];
	unsigned off = 1;

	cmd[off++] = 'n';
	cmd[off++] = 0;
	cmd[0] = off - 1;
	sync_write(fd, cmd, off);
}
