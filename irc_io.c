/*
 *  irc_io.c: Handles the input and output from the IRC server.
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

void read_server_line (struct net_connection *, char *);
void close_server_connection (struct net_connection *, char *);

extern unsigned long server_addr;
extern struct command commands[];

void
resolve_server (void)
{
  struct hostent *host_res;
  unsigned long i_a;
  puts ("Resolving server host...");
  if ((i_a = inet_addr (serv_host)) == 0xFFFFFFFF)
    {
      if (!(host_res = gethostbyname (serv_host)))
	{
	  fprintf (stderr,
		   "Could not resolve server hostname to an IP address.\n");
	  exit (1);
	}
      i_a = *(unsigned long *) host_res->h_addr;
    }
  printf ("Host resolved to %lu.\n", i_a);
  if (i_a == 0xFFFFFFFF)
    {
      fprintf (stderr,
	       "Could not resolve server hostname to an IP address.\n");
      exit (1);
    }
  server_addr = i_a;
}

void
connect_server (void)
{
  long conn;
  struct sockaddr_in sai;
  struct net_connection *tnc;
  sai.sin_family = AF_INET;
  sai.sin_port = htons (serv_sport);
  sai.sin_addr.s_addr = server_addr;
  errno = 0;
  if ((conn = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
    {
      fprintf (stderr, "Could not create a socket.");
      exit (1);
    }
  if ((connect (conn, (struct sockaddr *) &sai, sizeof (struct sockaddr))) ==
      -1)
    {
      fprintf (stderr, "Could not connect to server.\n");
      exit (1);
    }
  if (conn <= 0)
    return;
  tnc = alloc_block (bh_net_connection);
  serv = tnc;			/* Move up to here, other functions(like close_connection) need it sooner - A1kmm. */
  tnc->prev = last_nc;
  if (last_nc)
    {
      last_nc->next = tnc;
    }
  else
    first_nc = tnc;
  last_nc = tnc;
  tnc->flags = 0;
  tnc->read_raw_data = read_raw_data;
  tnc->write_raw_data = write_raw_data;
  tnc->read_line = read_server_line;
  tnc->write_line = write_line;
  tnc->close_connection = close_server_connection;
  tnc->fd = conn;
  printf ("New fd = %ld.\n", conn);
  tnc->next = NULL;
  tnc->outbuff_pos = 0;
  tnc->inbuff_pos = 0;
/* We could be serving international links, so make
   this fairly big. */
  tnc->outbuff_size = 100000;
  tnc->inbuff_size = 100000;
  tnc->out_buffer = malloc (100000);
  if (!tnc->out_buffer)
    {
      fprintf (stderr, "Out of memory!\n");
      exit (1);
    }
  tnc->in_buffer = malloc (100000);
  if (!tnc->in_buffer)
    {
      fprintf (stderr, "Out of memory!\n");
      exit (1);
    }
  tnc->data = NULL;
  send_client (tnc, "PASS %s :TS", serv_password);
  send_client (tnc, "CAPAB :QS EX CHW DE");
  send_client (tnc, "SERVER %s.dserv 1 :Distributed Services v1.0",
	       servicesname);
  /* Send queued data. */
  // write_raw_data (tnc);
  /* I moved this down, so we can send things in the right order. */
  fcntl (conn, F_SETFL, O_NONBLOCK);
  send_client (tnc, "SVINFO 1 1 0 %lu", time (0));
  /* Use TS=1 so we always win in the event of a collision. We
     are an IRCOp so people know we are the real services. */
  send_client (tnc, "NICK %s 1 1 +o dserv %s.dserv %s.dserv :"
	       "Distributed services", servicesname, servicesname,
	       servicesname);
}

void
close_server_connection (struct net_connection *nc, char *m)
{
  printf ("Disconnected from server(%s), sleeping 5s...\n", m);
  services_up = 0;
  sleep (5);
  printf ("Reconnecting...\n");
  /* Now we *must* delete all clients and servers before we reconnect, and split off the
     SVCnet.  */
  while (first_conn_serv)
    process_server_quit (first_conn_serv);
  while (first_user)
    process_client_quit (first_user);
  my_connect = NULL;
  /* Ensure garbage collection is done, for a short time both structures co-exist. */
  serv->flags |= NCFLAG_DEAD;
  /* close (serv->fd); - Don't close it! It gets closed later anyay. - A1kmm. */
  connect_server ();
  services_up = 1;
}


void
read_server_line (struct net_connection *nc, char *l)
{
  char *args[30], *line;
  struct command *ccur;
  int argc = 0;
  printf ("From server: %s\n", l);
  if (*l != ':')
    {
      args[argc++] = my_connect ? my_connect->name : "";
    }
  else
    l++;
  for (line = strtok (l, " ");
       line && (*line != ':') && argc < 29; line = strtok (NULL, " "))
    {
      args[argc++] = line;
    }
  if (line)
    {
      char *l2;
      if ((l2 = strtok (NULL, "")))
	*--l2 = ' ';
      if (*line == ':')
	line++;
      args[argc++] = line;
    }
  if (argc < 2)
    return;
  for (ccur = commands; ccur->func; ccur++)
    if (!strcmp (ccur->name, args[1]))
      {
	ccur->func (find_server (args[0]), argc, args);
	return;
      }
}
