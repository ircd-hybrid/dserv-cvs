/*
 *  dserv.h - The ditributed services main header.
 *  This file is part of the Distributed Services by Andrew Miller(A1kmm), 27/8/2000.
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>

 /* By default, ircd sets max channel names far too long(200b!).
    We will not register anything exceeding MAX_CHAN_LEN, and all
    other channels cannot be used with services. */
#define MAX_CHAN_LEN 30

typedef struct __BlockHeap BlockHeap;

struct net_server
{
  char name[40], pass[20];
  struct net_server *uplink, *next, *prev;
  /* Pointer to the services connection if present. */
  struct link_from_services *lts;
};

struct irc_user
{
  char name[25];
  int hops;
  unsigned long last_f_time, f_count, flags;
  unsigned long op_flags;
  struct irc_user *next, *prev;
  struct host_online *ho;
  struct net_server *server;
};

struct hash_entry
{
  char type;
  struct hash_entry *next;
  void *ptr;
};

struct suspend_channel
{
  char name[MAX_CHAN_LEN + 1];
  char type;
  /* If the server is still up later, we will SUSPEND 0 the network, so that the channels get fixed,
     but otherwise the channel stays suspended until explicit SUSPEND 0 */
  unsigned long expire_ts;
  struct suspend_channel *next;
};

struct command
{
  char *name;
  void (*func) (struct net_server *, int argc, char **args);
};

struct net_connection
{
  struct net_connection *next, *prev;
  BlockHeap *blockh;
  void *data;
  unsigned long fd, inbuff_pos, outbuff_pos, flags;
  void (*read_raw_data) (struct net_connection *, char *, unsigned long);
  void (*write_raw_data) (struct net_connection *);
  void (*read_line) (struct net_connection *, char *);
  void (*write_line) (struct net_connection *, char *);
  void (*close_connection) (struct net_connection *, char *);
  char *in_buffer, *out_buffer;
  unsigned long inbuff_size, outbuff_size;
};

struct link_from_services
{
  struct net_connection *services;
  unsigned long phase;
  struct net_server *server;
};

struct irc_chan_user
{
  unsigned long flags;
  struct irc_chan_user *next;
  struct irc_user *user;
};

struct irc_channel
{
  unsigned long chan_ts, flags;
  char name[MAX_CHAN_LEN + 1];
  struct irc_chan_user *first_u, *last_u;
  struct irc_channel *next, *prev;
};

/* Registration is pending on this channel. */
struct irc_pend_channel
{
  /* Channels pend for 30s after they are registered, and if they hear no complaints from any other
     services, they time out and become a fully registered channel. The chan_ts is the value the
     channel ts is set to when ops are hacked. chan_score starts at the number of people in the
     channel+10, and goes up by 1 on join and down by 1 on part, and also drops by 10 every hour
     there is less than 4 people in the channel and rises by 1 everytime there is more than 4 people
     in the channel. It has a maximum value of 1000, and if it ever falls below 4 the channel is dropped.
     The effect of this scheme is that any neglected channel will disppear within 4 days 6 hours no
     matter how old it is, but channels which are stockpiled by bringing in 4 users, registering and
     leaving the channel will disappear within 2 hours. If an abnormal situation arises and a big channel
     which has had a good following for at least 41 days, 39 hours becomes empty or very near to empty,
     then they have 4 days 5 hours to fix it up. - A1kmm */
  unsigned long reg_ts, chan_ts;
  int chan_size;
  /* We store these as strings rather than pointers to the structures in case the structures disappear
     while the registration is pending. We do need to keep the nick_name so we can get back to them when
     something happens, e.g. the the pend times out or another services knows the channel. */
  char chan_name[MAX_CHAN_LEN + 1], nick_name[25], pass[11];
  struct irc_pend_channel *next, *prev;
};

struct irc_reg_channel
{
  unsigned long reg_ts, chan_ts;
  int score;
  char password[10], name[MAX_CHAN_LEN + 1];
  char suspend_t;
  /* This _MUST_ stay at the end of the struct! The end of the struct is not written to disk. */
  struct irc_reg_channel *next, *prev;
};

struct access_level
{
  char name[11];
  unsigned long flags;
  struct access_level *next;
};

struct services_op
{
  char name[21], pass[21];
  unsigned long flags;
  struct services_op *next;
};

struct oomon_link
{
  char name[21];
  struct net_connection *nc;
  struct oomon_link *next;
};

struct tcm_link
{
  char name[21];
  struct net_connection *nc;
  struct tcm_link *next;
};

struct host_online
{
  char host[40];
  /* Goes down by 1 every 60s, up 1 on connect. If this reaches 0, when gc is done
     every 30s, delete it(if conn_count==0). last_conn_time is the last time conn_count
     should have gone down(So if we find 130s have elapsed, we set conn_freq down by 2 and set
     last_conn_time to cur_time - 10), and conn_count is the number of active(open)
     connections. - A1kmm. */
  unsigned long conn_freq, last_conn_time, conn_count;
  struct host_online *next;
};

/* Types of suspensions. */
enum
{
  SUSPEND_NONE,			/* Not suspended. (Code '0') */
  SUSPEND_DONTMANAGE,		/* Do not allow ON commands to be used. (Code 's') */
  SUSPEND_MASSDEOP,		/* Do not allow user to get ops or voice. (Code 'm') */
  SUSPEND_SILENCE,		/* Maintain +m on channel as well, and set TOPIC to "Channel Suspended"
				   (Code 'c') */
};

extern struct irc_channel *first_chan, *last_chan;
extern struct irc_reg_channel *first_reg_chan, *last_reg_chan;
extern struct net_server *first_conn_serv, *last_conn_serv, *my_connect;
extern struct irc_user *first_user, *last_user;
extern struct net_connection *first_nc, *last_nc, *serv;
extern struct suspend_channel *first_sus, *last_sus;
extern unsigned long cur_time;
extern char *servicesname, *serv_password, *serv_host;
extern unsigned short serv_port, serv_sport;
extern struct hash_entry *hash[];
extern int services_up;
extern BlockHeap *bh_access_level, *bh_services_op, *bh_hash_entry,
  *bh_irc_reg_channel, *bh_irc_channel, *bh_irc_chan_user,
  *bh_suspend_channel, *bh_net_server, *bh_irc_user, *bh_irc_pend_channel,
  *bh_tcm_link, *bh_oomon_link, *bh_net_connection,
  *bh_link_from_services, *bh_host_online;


void process_client_quit (struct irc_user *);
void process_server_quit (struct net_server *);
void send_client (struct net_connection *, char *, ...)
  __attribute__ ((format (printf, 2, 3)));
void send_all_services (char *m, ...) __attribute__ ((format (printf, 1, 2)));
void connect_services (struct net_server *, unsigned long, unsigned short,
		       char *);
void write_line (struct net_connection *nc, char *l);
void read_raw_data (struct net_connection *, char *, unsigned long);
void write_raw_data (struct net_connection *);
struct irc_user *find_user (char *);
struct net_server *find_server (char *);
struct irc_channel *find_channel (char *);
struct irc_chan_user *get_user_on_channel (struct irc_channel *,
					   struct irc_user *);
struct irc_pend_channel *find_pending_channel (char *);
struct irc_reg_channel *find_registered_channel (char *);
void link_pending_channel (struct irc_pend_channel *);
void delete_pending_channel (struct irc_pend_channel *);
void delete_registered_channel (struct irc_reg_channel *);
void suggest_klines (char *, char *, int);
struct suspend_channel *find_suspended_channel (char *);
void create_suspend_channel (char *, char type);
void rehash (void);
void delete_suspended_channel (struct suspend_channel *);
BlockHeap *create_blockheap (unsigned long, unsigned long);
void *alloc_block (BlockHeap *);
void free_block (BlockHeap *, void *);


inline unsigned short hash_text (unsigned char *txt);

enum
{
  LINKPHASE_AUTH,
  LINKPHASE_ESTABLISHED,
  LINKPHASE_ROONIN,		/* The server it is on has split. */
};

#define NCFLAG_DEAD 1
#define NCFLAG_LISTEN 2

#define CHANUFLAG_OPPED 1
#define CHANUFLAG_VOICED 2
#define CHANFLAG_SECRET 1
#define CHANFLAG_PRIVATE 2
#define CHANFLAG_MODERATED 4
#define CHANFLAG_NOEXTERNAL 8
#define CHANFLAG_TOPICOP 16
#define CHANFLAG_KEY 32
#define CHANFLAG_INVITE 64
#define CHANFLAG_LIMIT 128

#define OPERFLAG_DEBUG		0x00000001
#define OPERFLAG_SUSPEND		0x00000002
#define OPERFLAG_CONSOLE		0x00000004	/* REHASH, DIE, ... */
#define OPERFLAG_FOUNDER		0x00000008	/* Can do ON #channel OPACCESS ... */
#define OPERFLAG_CHANINFO	0x00000010
#define OPERFLAG_SYNC		0x00000020

#define UFLAG_OPER 1
#define UFLAG_MYMODE 2
