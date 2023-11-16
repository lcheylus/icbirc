#ifndef _IRC_H_
#define _IRC_H_

void	 irc_recv(const char *, unsigned, int, int);
void	 irc_send_notice(int, const char *, ...);
void	 irc_send_code(int, const char *, const char *, const char *,
	    const char *, ...);
void	 irc_send_msg(int, const char *, const char *, const char *);
void	 irc_send_join(int, const char *, const char *);
void	 irc_send_part(int, const char *, const char *);

extern char irc_pass[256];
extern char irc_ident[256];
extern char irc_nick[256];
extern char irc_channel[256];
extern int in_irc_channel;

#endif
