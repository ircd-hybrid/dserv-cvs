
/*
 *  dserv.c - The ditributed services main file.
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

#include "dserv.h"

struct net_server *first_conn_serv = NULL, *last_conn_serv,
  *my_connect = NULL;
struct irc_user *first_user = NULL, *last_user = NULL;
struct net_connection *first_nc = NULL, *last_nc = NULL, *serv;
unsigned short serv_port, serv_sport;
unsigned long cur_time, server_addr;

char *servicesname, *serv_password, *serv_host = NULL;

void connect_server (void);
void do_main_loop (void);
void listen_services_connections (unsigned short);
void load_channels (void);
void read_config_file (void);
void resolve_server (void);
void handle_signals (void);
void setup_blockheaps (void);

int
usage (void)
{
  /*puts ("Usage: dserv servicesname password port"); */
  puts ("Usage: dserv servicesname\n"
	"servicesname is the name you give your services. You must have C/N lines for"
	"\nservicesname.dserv in the server configuration file.");
  return -1;
}

int
main (int argc, char *argv[])
{
  if (argc < 2)
    return usage ();
  servicesname = argv[1];
  /* We no longer pass the password or port on the commandline - A1kmm. */
  /*serv_password = argv[2]; */
  if (strchr (servicesname, '.'))
    {
      puts ("The servicesname may not contain a .");
      return -1;
    }
  /*port = (unsigned short) strtoul (argv[3], NULL, 10);
     if (!port)
     return usage (); */
  handle_signals ();
  setup_blockheaps ();
  read_config_file ();
  if (!serv_host)
    {
      fprintf (stderr, "No SERVER directive found in the config file.\n");
      exit (1);
    }
  resolve_server ();
  listen_services_connections (serv_port);
  load_channels ();
  connect_server ();
  services_up = 1;
  do_main_loop ();
  return 0;
}

void
handle_sighup (int signum)
{
  if (services_up)
    send_client (serv, ":%s WALLOPS :Received HUP signal, rehashing.",
		 servicesname);
  rehash ();
}
void
handle_signals (void)
{
  struct sigaction sa_s;
  sa_s.sa_handler = handle_sighup;
  sigemptyset (&sa_s.sa_mask);
  sa_s.sa_flags = SA_RESTART;
  sigaction (SIGHUP, &sa_s, NULL);
#ifdef BLOCK_SIGPIPE
  {
    sigset_t sset;
    sigemptyset (&sset);
    sigaddset (&sset, SIGPIPE);
    sigprocmask (SIG_BLOCK, &sset, &sset);
  }
#endif
}
