/*
 *  svc_io.c: Handles data from other dservs on the IRC network.
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

void
close_svc_connection (struct net_connection *nc, char *m)
{
  switch (((struct link_from_services *) nc->data)->phase)
    {
    case LINKPHASE_AUTH:
      printf ("Closing auth services connection: %s\n", m);
      return;
    case LINKPHASE_ESTABLISHED:
      /* Something must be wrong with the network if the link was dropped in the first place,
         so it might be more useful to drop the services here - A1kmm. */
      send_client (serv, ":%s SQUIT %s :Services broke from SVC net.",
		   servicesname,
		   ((struct link_from_services *) nc->data)->server->name);
      process_server_quit (((struct link_from_services *) nc->data)->server);
      return;
    case LINKPHASE_ROONIN:
      printf ("Dropped link: %s (This services is split from network).\n", m);
      return;
    }
}

void
check_pendchan (struct link_from_services *lfs, int c, char **p)
{
  struct suspend_channel *sc;
  if (c < 2)
    return;
  /* Complain if duplicate found. */
  if (find_pending_channel (p[1]))
    {
      send_client (lfs->services, "2 DUPPEND %s", p[1]);
    }
  else if (find_registered_channel (p[1]))
    {
      send_client (lfs->services, "2 DUPREG %s", p[1]);
    }
  else if ((sc = find_suspended_channel (p[1])))
    {
      send_client (lfs->services, "3 SUSPEND %s %u", p[1], sc->type);
    }
}

void
process_duppend (struct link_from_services *lfs, int c, char **p)
{
  struct irc_pend_channel *ipc;
  if (c < 2)
    return;
  ipc = find_pending_channel (p[1]);
  /* What are they talking about? Is it now registered and we are so lagged that they missed the 30s
     window, or are they trying to trick us? */
  if (!ipc)
    return;
  send_client (serv,
	       ":%s NOTICE %s :The channel %s is already pending registration "
	       "on services %s so your pending registration was declined.",
	       servicesname, ipc->nick_name, ipc->chan_name,
	       lfs->server->name);
  delete_pending_channel (ipc);
}

void
process_dupreg (struct link_from_services *lfs, int c, char **p)
{
  struct irc_pend_channel *ipc;
  if (c < 2)
    return;
  ipc = find_pending_channel (p[1]);
  /* What are they talking about? Is it now registered and we are so lagged that they missed the 30s
     window, or are they trying to trick us? */
  if (!ipc)
    return;
  send_client (serv, ":%s NOTICE %s :The channel %s is already registered "
	       "on services %s so your pending registration was declined.",
	       servicesname, ipc->nick_name, ipc->chan_name,
	       lfs->server->name);
  delete_pending_channel (ipc);
}


/* CLASH channel chan_ts reg_ts */
void
process_clash (struct link_from_services *lfs, int c, char **p)
{
  struct irc_reg_channel *ircrc;
  unsigned long chan_ts, reg_ts, x;
  if (c < 4)
    return;
  if (!(ircrc = find_registered_channel (p[1])))
    return;
  chan_ts = strtoul (p[2], NULL, 10);
  reg_ts = strtoul (p[3], NULL, 10);
  if (!chan_ts || !reg_ts)
    return;
  if (ircrc->chan_ts < chan_ts)
    {
      x = 0;			/* Keep our channel, drop theirs. */
    }
  else if (ircrc->chan_ts > chan_ts)
    {
      x = 1;			/* Keep their channel, drop ours. */
    }
  else if (ircrc->reg_ts < reg_ts)
    {
      x = 0;
      /* I hope this doesn't happen too often, it means that both channels have exactly
         the same chan_ts and the same reg_ts!!!! - A1kmm. */
    }
  else
    x = 1;
  if (!x)
    {
      /* Okay, they need to delete theirs. */
      send_client (lfs->services, "4 CLASH %s %lu %lu", p[1], ircrc->chan_ts,
		   ircrc->reg_ts);
    }
  else
    {
      send_client (serv,
		   ":%s WALLOPS :Registered channel %s collision(%s<->%s.dserv)"
		   " %s.dserv version deleted", servicesname, p[1],
		   lfs->server->name, servicesname, servicesname);
      delete_registered_channel (ircrc);
    }
}

/* USERFIND channel sender */
void
process_userfind (struct link_from_services *lfs, int c, char **p)
{
  struct irc_reg_channel *regc;
  struct irc_user *ircu;
  if (c < 3)
    return;
  if (!(regc = find_registered_channel (p[1])))
    return;
  if (!(ircu = find_user (p[2])))
    return;
  if (ircu->op_flags & OPERFLAG_CHANINFO)
    {
      send_client (serv,
		   ":%s NOTICE %s :Channel %s with TS %lu Registered %lu with "
		   "%s, score %u password %s", servicesname, p[2], p[1],
		   regc->chan_ts, regc->reg_ts, servicesname, regc->score,
		   regc->password);
    }
  else
    {
      send_client (serv,
		   ":%s NOTICE %s :Channel %s with TS %lu Registered %lu with "
		   "%s", servicesname, p[2], p[1], regc->chan_ts,
		   regc->reg_ts, servicesname);
    }
}

void
process_oper (struct link_from_services *lfs, int c, char **p)
{
  struct irc_user *ircu;
  unsigned long flags;
  if (c < 5)
    return;

  /* Out of luck, op, if wrong version of modeflags. */
  if (strcmp (p[2], "1"))
    return;
  if (!(ircu = find_user (p[1])))
    return;
  if (!(flags = strtoul (p[3], NULL, 10)))
    return;
  ircu->op_flags = flags;
  /* If we previously were in charge of sending out their modes we are not anymore. */
  ircu->flags &= ~UFLAG_MYMODE;
}

void
process_suspend (struct link_from_services *lfs, int c, char **p)
{
  /* SUSPEND channel type */
  char *chan_n, t;
  struct irc_reg_channel *irrc;
  struct suspend_channel *susc;
  struct irc_pend_channel *pc;
  if (c < 3)
    return;
  chan_n = p[1];
  t = (char) strtoul (p[2], NULL, 10);
  if (t && (pc = find_pending_channel (chan_n)))
    {
      send_client (serv,
		   ":%s NOTICE %s : The channel %s has been suspended and "
		   "cannot be registered.", servicesname, pc->nick_name,
		   pc->chan_name);
      delete_pending_channel (pc);
    }
  /* Let them now hold the record for the suspension. */
  if ((susc = find_suspended_channel (chan_n)))
    {
      delete_suspended_channel (susc);
    }
  if (!(irrc = find_registered_channel (chan_n)))
    return;
  irrc->suspend_t = t;
}

void
parse_services_line (struct link_from_services *lfs, char *l)
{
  char *params[200], *cl;
  unsigned long i, j;
  cl = strtok (l, " ");
  if (!cl || !(i = strtoul (cl, NULL, 10)))
    return;
  i--;
  for (j = 0; j < i; j++)
    {
      cl = strtok (NULL, " ");
      if (!cl)
	return;			/* Parameter count wrong. */
      params[j] = cl;
    }
  /* Join all of the last parameter into one. */
  if (!(params[i++] = strtok (NULL, "")))
    return;
  if (!strcasecmp (params[0], "PENDCHAN"))
    {
      check_pendchan (lfs, i, params);
    }
  else if (!strcasecmp (params[0], "DUPPEND"))
    {
      process_duppend (lfs, i, params);
    }
  else if (!strcasecmp (params[0], "DUPREG"))
    {
      process_dupreg (lfs, i, params);
    }
  else if (!strcasecmp (params[0], "CLASH"))
    {
      process_clash (lfs, i, params);
    }
  else if (!strcasecmp (params[0], "USERFIND"))
    {
      process_userfind (lfs, i, params);
    }
  else if (!strcasecmp (params[0], "OPER"))
    {
      process_oper (lfs, i, params);
    }
  else if (!strcasecmp (params[0], "SUSPEND"))
    {
      process_suspend (lfs, i, params);
    }
}

void check_oo (struct net_connection *);
void check_tcm (struct net_connection *);


void
read_services_line (struct net_connection *nc, char *l)
{
  struct link_from_services *lfs;
  struct irc_user *ircu;
  struct net_server *ns;
  char buffer[100];
  lfs = (struct link_from_services *) nc->data;
  /* Note: We now share the same port for oomon access. */
  if (lfs->phase == LINKPHASE_AUTH)
    {
      char *services, *password, *version, *ve;
      unsigned long vno;
      services = strtok (l, " ");
#define CCCSX(t) {\
   puts(t); \
   nc->flags|=NCFLAG_DEAD; return;\
  }
      if (!services)
	CCCSX ("Bad connect string.");
      if (!strcmp (services, "OOMon"))
	{
	  free_block (bh_link_from_services, lfs);
	  nc->data = NULL;
	  check_oo (nc);
	  return;
	}
      password = strtok (NULL, " ");
      if (!password)
	CCCSX ("Bad connect string.");
      version = strtok (NULL, " ");
      /* We don't need security here, so use the password to identify TCMs. */
      if (!strcasecmp (version, "TCMLINK"))
	{
	  free_block (bh_link_from_services, lfs);
	  nc->data = NULL;
	  check_tcm (nc);
	  return;
	}
      if (!version)
	CCCSX ("Bad connect string.");
      vno = strtoul (version, &ve, 10);
      if (*ve)
	CCCSX ("Bad connect string.");
      if (vno != 1)
	CCCSX ("Wrong version.");
      snprintf (buffer, 99, "%s.dserv", services);
      buffer[99] = 0;
      ns = find_server (buffer);
      if (!ns)
	CCCSX ("Bad servicesname.");
      if (strcmp (password, ns->pass))
	CCCSX ("Bad password.");
      lfs->server = ns;
      ns->lts = lfs;
      nc->outbuff_size = 100000;
      nc->inbuff_size = 100000;
      free(nc->out_buffer);
      free(nc->in_buffer);
      nc->out_buffer = malloc (100000);
      nc->in_buffer = malloc (100000);
      printf ("Established link with %s\n", lfs->server->name);
      for (ircu = first_user; ircu; ircu = ircu->next)
	if (ircu->op_flags && ircu->flags & UFLAG_MYMODE)
	  send_client (lfs->services, "5 OPER %s 1 %lu 0", ircu->name,
		       ircu->op_flags);
      lfs->phase = LINKPHASE_ESTABLISHED;
      return;
    }
  if (lfs->phase != LINKPHASE_ESTABLISHED)
    return;
  printf ("From services: %s\n", l);
  parse_services_line (lfs, l);
}

void
accept_services_connection (struct net_connection *nc, char *s,
			    unsigned long l)
{
  long conn;
  int len;
  struct sockaddr_in sai;
  struct net_connection *tnc;
  len = sizeof (sai);
  conn = accept (nc->fd, (struct sockaddr *) &sai, &len);
  if (conn <= 0)
    return;
  fcntl (conn, F_SETFL, O_NONBLOCK);
  tnc = alloc_block (bh_net_connection);
  assert (tnc);
  tnc->prev = last_nc;
  if (last_nc)
    {
      last_nc->next = tnc;
    }
  else
    first_nc = tnc;
  last_nc = tnc;
  tnc->read_raw_data = &read_raw_data;
  tnc->write_raw_data = write_raw_data;
  tnc->read_line = &read_services_line;
  tnc->write_line = write_line;
  tnc->close_connection = close_svc_connection;
  tnc->fd = conn;
  tnc->next = NULL;
  tnc->outbuff_pos = 0;
  tnc->inbuff_pos = 0;
/* We could be serving international links, so make this fairly big. - A1kmm.
   On second thoughts, why not start small, and leave it small for oomons and tcms, but for services,
   once we get the password we can put it back up to 100000. - A1kmm.
*/
  tnc->outbuff_size = 400;
  tnc->flags = 0;
  tnc->inbuff_size = 400;
  tnc->out_buffer = malloc (400);
  if (!tnc->out_buffer)
    {
      fprintf (stderr, "Out of memory!\n");
      exit (1);
    }
  tnc->in_buffer = malloc (400);
  if (!tnc->in_buffer)
    {
      fprintf (stderr, "Out of memory!\n");
      exit (1);
    }
  tnc->data = alloc_block (bh_link_from_services);
  tnc->blockh = bh_link_from_services;
  ((struct link_from_services *) tnc->data)->phase = LINKPHASE_AUTH;
  ((struct link_from_services *) tnc->data)->services = tnc;
  ((struct link_from_services *) tnc->data)->server = NULL;
}

void
listen_services_connections (unsigned short port)
{
  long fd;
  int val = 1;
  struct sockaddr_in sai;
  struct net_connection *tnc;
  fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
  setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &val, 4);
  if (!fd)
    {
      perror ("socket()");
      return;
    }
  sai.sin_family = AF_INET;
  sai.sin_port = htons (port);
  sai.sin_addr.s_addr = INADDR_ANY;
  if (bind (fd, (struct sockaddr *) &sai, sizeof (struct sockaddr)))
    {
      perror ("bind()");
      return;
    };
  if (listen (fd, 20))
    {
      perror ("listen()");
      return;
    }
  fcntl (fd, F_SETFL, O_NONBLOCK);
  tnc = alloc_block (bh_net_connection);
  assert (tnc);
  tnc->prev = last_nc;
  if (first_nc)
    {
      last_nc->next = tnc;
    }
  else
    first_nc = tnc;
  last_nc = tnc;
  tnc->fd = fd;
  tnc->read_raw_data = &accept_services_connection;
  tnc->flags = NCFLAG_LISTEN;
  tnc->next = NULL;
  tnc->outbuff_pos = 0;
  tnc->inbuff_pos = 0;
}

char *
create_services_password (struct net_server *nc)
{
  int i;
  for (i = 0; i < 19; i++)
    nc->pass[i] = (char) rand ();
  for (i = 0; i < 19; i++)
    if (((unsigned char) nc->pass[i]) < 30)
      nc->pass[i] += 30;
  nc->pass[19] = 0;
  return nc->pass;
}

void
connect_services (struct net_server *ns, unsigned long host,
		  unsigned short portn, char *password)
{
  struct sockaddr_in sai;
  struct irc_user *ircu;
  struct net_connection *ncn;
  struct link_from_services *lfs;
  unsigned long sk;
  sai.sin_addr.s_addr = host;
  sai.sin_port = portn;
  sai.sin_family = AF_INET;
  sk = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
  fcntl (sk, F_SETFL, O_NONBLOCK);
  printf ("Opening link to services %s.\n", ns->name);
  connect (sk, (struct sockaddr *) &sai, sizeof (struct sockaddr));
  ncn = alloc_block (bh_net_connection);
  lfs = alloc_block (bh_link_from_services);
  ncn->prev = last_nc;
  ncn->next = NULL;
  if (first_nc)
    {
      last_nc->next = ncn;
    }
  else
    first_nc = ncn;
  last_nc = ncn;
  ncn->data = lfs;
  ncn->blockh = bh_link_from_services;	/* So it is deleted correctly. */
  ncn->read_raw_data = &read_raw_data;
  ncn->write_raw_data = &write_raw_data;
  ncn->read_line = &read_services_line;
  ncn->write_line = &write_line;
  ncn->close_connection = &close_svc_connection;
  if (!(ncn->in_buffer = malloc (200000)))
    {
      fprintf (stderr, "Out of memory!\n");
      exit (1);
    }
  if (!(ncn->out_buffer = malloc (200000)))
    {
      fprintf (stderr, "Out of memory!\n");
      exit (1);
    }
  ncn->outbuff_size = 20000;
  ncn->inbuff_size = 200000;
  ncn->fd = sk;
  ns->lts = lfs;
  lfs->services = ncn;
  lfs->server = ns;
  lfs->phase = LINKPHASE_ESTABLISHED;
  send_client (ncn, "%s %s 1", servicesname, password);
  for (ircu = first_user; ircu; ircu = ircu->next)
    if (ircu->op_flags && ircu->flags & UFLAG_MYMODE)
      send_client (lfs->services, "5 OPER %s 1 %lu 0", ircu->name,
		   ircu->op_flags);
}

void
send_all_services (char *m, ...)
{
  va_list val;
  struct net_server *cns;
  int i;
  char buffer[5000];
  va_start (val, m);
  vsnprintf (buffer, 4998, m, val);
  buffer[4999] = 0;
  i = strlen (buffer);
  buffer[i] = '\n';
  buffer[i + 1] = 0;
  printf ("Outgoing to all services: %s", buffer);
  for (cns = first_conn_serv; cns; cns = cns->next)
    if (cns->lts)
      cns->lts->services->write_line (cns->lts->services, buffer);
  va_end (val);
}
