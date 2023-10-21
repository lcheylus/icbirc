/*	$Id: icb.h,v 1.3 2015/08/21 19:01:12 dhartmei Exp $ */

#ifndef _ICB_H_
#define _ICB_H_

void	 icb_init(void);
void	 icb_recv(const char *, unsigned, int, int);
void	 icb_send_login(int, const char *, const char *, const char *);
void	 icb_send_openmsg(int, const char *);
void	 icb_send_privmsg(int, const char *, const char *);
void	 icb_send_group(int, const char *);
void	 icb_send_list(int);
void	 icb_send_names(int, const char *);
void	 icb_send_whois(int, const char *);
void	 icb_send_who(int, const char *);
void	 icb_send_pass(int, const char *);
void	 icb_send_topic(int, const char *);
void	 icb_send_boot(int, const char *);
void	 icb_send_name(int, const char *);
void	 icb_send_raw(int, const char *);
void	 icb_send_noop(int);

extern int icb_logged_in;

#endif
