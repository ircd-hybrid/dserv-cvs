/*
 *  irc_commands.c: Handles commands from the IRC server.
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

extern unsigned long die_c_done;

/* Real is the name of the sender, lt is a pointer to a static recording the last use timestamp,
   c is the cost(If a user uses 1000 cost units in 30 seconds, we kill them). T is the minimum time
   between messages from any user before we refuse to serve them. */
int
check_flood (char *real, unsigned long *lt, unsigned long c, unsigned long T)
{
  struct irc_user *ircu;
  if (!(ircu = find_user (real)))
    return -1;
  if (cur_time - ircu->last_f_time < 30)
    {
      ircu->f_count += c;
    }
  else
    {
      ircu->f_count = c;
      ircu->last_f_time = cur_time;
    }
  if (ircu->f_count > 1000)
    {
      send_client (serv, ":%s KILL %s :Do not flood services.", servicesname,
		   real);
      process_client_quit (ircu);
      return -1;
    }
  if (cur_time - *lt < T || ircu->f_count > 500)
    {
      send_client (serv, ":%s NOTICE %s :Services load temporarily too high, "
		   "try again %s.", servicesname, real,
		   (ircu->f_count > 500) ? "in 35 seconds" : "later");
      *lt = cur_time;
      return -1;
    }
  *lt = cur_time;
  return 0;
}

char *create_services_password (struct net_server *);
void
m_ping (struct net_server *ns, int argc, char **args)
{
  /* If args[3]="NoDistributed" we respond with our IP and the session
     password. :ourserver PONG sender :Distributed ip sessionpass */
  if (argc < 3)
    return;
  if (argc == 3)
    {
      send_client (serv, ":%s.dserv PONG %s :%s", servicesname, args[2],
		   args[0]);
      return;
    }
  if (!strcmp (args[2], "NoDistributed"))
    {
      char buffer[100], *svname, *postfix, *pass;
      strncpy (buffer, args[0], 99);
      /* Filter out the kiddies. */
      if (!(svname = strtok (buffer, ".")) || !(postfix = strtok (NULL, ""))
	  || strcmp (postfix, "dserv"))
	{
	  /* If it was a kiddy(client sending 'NoDistributed' ping), kill it. */
	  if (!postfix)
	    send_client (serv, ":%s KILL %s : You must be a service to do "
			 "that and live.", servicesname, args[0]);
	  process_client_quit (find_user (args[0]));
	  return;
	}
      /* Filter out services who should not be pinging us(we should be
         pinging them!) */
      if (strcmp (svname, servicesname) <= 0)
	return;
      pass = create_services_password (ns);
      send_client (serv, ":%s.dserv PONG Distributed-%u-%u-%s %s",
		   servicesname, 0x100007F, htons (serv_port), pass, args[0]);
    }
  else
    send_client (serv, ":%s.dserv PONG %s :%s", servicesname, args[3],
		 args[0]);
}

void
m_pong (struct net_server *ns, int argc, char **args)
{
  char buffer[100], *svcname, *postfix = NULL, *tok;
  unsigned long ip;
  unsigned short port;
  if (argc < 4)
    return;
  strncpy (buffer, args[0], 100);
  /* Ignore unless in form x.dserv. */
  if (!(svcname = strtok (buffer, ".")) || !(postfix = strtok (NULL, ""))
      || strcmp (postfix, "dserv"))
    return;
  /* If we should not be receiving this pong, then ignore. */
  if (strcmp (svcname, servicesname) >= 0)
    return;
  if (!strcmp (args[2], "NoDistributed"))
    {
      /* They are not a distributed services server. */
      send_client (serv,
		   ":%s.dserv SQUIT %s :Servernames ending in .dserv are"
		   " reserved for distributed services.", servicesname,
		   args[0]);
      return;
    }
  if (!(tok = strtok (args[2], "-")) || strcmp (tok, "Distributed"))
    return;
  if (!(tok = strtok (NULL, "-")) || !(svcname = strtok (NULL, "-")) ||
      !(postfix = strtok (NULL, "")))
    return;
  /* Now tok has the ip number(net order), svcname has the port(net order),
     postfix has the password. */
  ip = strtoul (tok, &tok, 10);
  port = (short) strtoul (svcname, &svcname, 10);
  /* Save the password in case we need to reconnect. */
  strncpy (ns->pass, postfix, 20);
  connect_services (ns, ip, port, postfix);
}

void
m_server (struct net_server *ns, int argc, char **args)
{
  struct net_server *fns;
  if (argc < 4)
    return;
  fns = find_server (args[2]);
  if (fns)
    return;
  /* The first server command announces the server we are connected to. */
  if (!my_connect)
    {
      my_connect = alloc_block (bh_net_server);
      strncpy (my_connect->name, args[2], 39);
      my_connect->name[39] = 0;
      my_connect->uplink = NULL;
      my_connect->next = NULL;
      my_connect->prev = NULL;
      first_conn_serv = my_connect;
      last_conn_serv = my_connect;
      return;
    }
  fns = alloc_block (bh_net_server);
  last_conn_serv->next = fns;
  fns->prev = last_conn_serv;
  last_conn_serv = fns;
  fns->uplink = ns;
  fns->next = NULL;
  fns->lts = NULL;
  strncpy (fns->name, args[2], 39);
  fns->name[39] = 0;
  {
    char buffer[100], *svsname, *postfix;
    strncpy (buffer, fns->name, 99);
    buffer[99] = 0;
    if (!(svsname = strtok (buffer, ".")) || !(postfix = strtok (NULL, "")) ||
	strcmp (postfix, "dserv") || (strcmp (svsname, servicesname) >= 0))
      return;
    /* We ask them for their information. */
    send_client (serv, "PING NoDistributed :%s.dserv", svsname);
  }
  return;
}

struct host_online *connect_host(char *, unsigned long);

void
m_nick (struct net_server *ns, int argc, char **args)
{
  int hc;
  struct net_server *nsu;
  struct hash_entry *he, *phe, *fhe;
  unsigned short hv;
  struct irc_user *ircu;
  if (!ns && argc == 4)
    {				/* Nick change. */
      if (!(ircu = find_user (args[0])))
	return;
      strncpy (ircu->name, args[2], 24);
      ircu->name[24] = 0;
      /* Now we have to delete it from the old hash and add it to the new :( - A1kmm 
         Better hash the _OLD_ name here, not the new. */
      he = hash[(hv = hash_text (args[0]))];
      for (phe = NULL; he; he = he->next)
	{
	  if (he->type == 0
	      && !strcasecmp (((struct irc_user *) he->ptr)->name,
			      ircu->name))
	    break;
	  phe = he;
	}
      if (!phe)
	{
	  hash[hv] = he->next;
	}
      else
	phe->next = he->next;
      /* Now scan the new hash */
      fhe = hash[(hv = hash_text (ircu->name))];
      if (!fhe)
	{
	  hash[hv] = he;
	  return;
	}
      while ((fhe = fhe->next))
	phe = fhe;
      phe->next = he;
      return;
    }
  if (argc < 10)
    return;
  /* :sender NICK nickname hopcount ts umode username hostname server  
     realname */
  /* Count hops(calculate it ourself, don't trust the sender). This is
     used to determine if they can register channels or are too far
     away. */
  if ((ircu = find_user (args[2])))
    {
      process_client_quit (ircu);
    }
  nsu = ns;
  for (hc = 0; nsu; hc++)
    nsu = nsu->uplink;
  ircu = alloc_block (bh_irc_user);
  strncpy (ircu->name, args[2], 24);
  if (strchr (args[5], 'o'))
    {
      ircu->flags = UFLAG_OPER;
    }
  else
    ircu->flags = 0;
  ircu->name[24] = 0;
  ircu->hops = hc;
  ircu->last_f_time = cur_time;
  ircu->f_count = 0;
  if (!ircu->flags & UFLAG_OPER)
    ircu->ho = connect_host(args[7], strtoul(args[4], NULL, 10));
  else
   ircu->ho = NULL;
  ircu->server = find_server (args[8]);
  ircu->next = NULL;
  ircu->prev = last_user;
  ircu->op_flags = 0;
  if (first_user)
    {
      last_user->next = ircu;
    }
  else
    first_user = ircu;
  last_user = ircu;
  he = alloc_block (bh_hash_entry);
  he->type = 0;
  he->next = NULL;
  he->ptr = ircu;
  if (!(fhe = hash[(hv = hash_text (ircu->name))]))
    {
      hash[hv] = he;
      return;
    }
  phe = fhe;
  while ((fhe = fhe->next))
    phe = fhe;
  phe->next = he;
  return;
}

void
m_squit (struct net_server *ns, int argc, char **args)
{
  /* :sender SQUIT server1 :reason */
  struct net_server *nsq;
  if (argc < 3)
    return;
  if (!(nsq = find_server (args[2])))
    return;
  process_server_quit (nsq);
}

void
m_quit (struct net_server *ns, int argc, char **args)
{
  struct irc_user *ircu;
  if (!(ircu = find_user (args[0])))
    return;
  process_client_quit (ircu);
}

void
m_kill (struct net_server *ns, int argc, char **args)
{
  if (argc < 3)
    return;
  if (!strcasecmp (args[2], servicesname))
    {
      struct irc_user *ircu;
      send_client (serv, "NICK %s 1 1 +o dserv %s.dserv %s.dserv :"
		   "Distributed services", servicesname, servicesname,
		   servicesname);
      if (!ns)
	send_client (serv, ":%s KILL %s : Please do not kill services!",
		     servicesname, args[0]);
      if ((ircu = find_user (args[0])))
	process_client_quit (ircu);
    }
}

void
m_version (struct net_server *ns, int argc, char **args)
{
  if (*args[0])
    send_client (serv, ":%s NOTICE %s :Distributed Services v1.0, "
		 "by Andrew Miller.", servicesname, args[0]);
}

void
pm_help (struct net_server *ns, int argc, char **args, char *real)
{
  static unsigned long last_t = 0;
  unsigned char buffer[0xFF], *cp;
  int x = 0, pos, l;
  FILE *fle;
  /* Kill after 10 tries, min pace 1s */
  if (check_flood (real, &last_t, 100, 1))
    return;
  if (argc < 2 || strchr (args[1], '.') || strchr (args[1], '/')
      || strchr (args[1], '\\'))
    {
      args[1] = "main";
    }
  strcpy (buffer, "help/");
  pos = 5;
  for (x = 1; args[x]; x++)
    {
      if (!strchr (args[x], '.') && !strchr (args[x], '/')
	  && !strchr (args[x], '\\'))
	{
	  /* The 6 is for the '/'(1) and the ".hlp"(4) and the '\0'(1).
	    1+4+1 = 6 -A1kmm */
	  if (((l = strlen (args[x])) + pos) >= (sizeof (buffer) - 6))
	    break;
	  buffer[pos++] = '/';
	  strcpy (buffer + pos, args[x]);
	  pos += l;
	}
    }
  /* We need this prefix so that we can have a command and a directory
     with the same name. */
  strcpy (buffer + pos, ".hlp");
  for (cp = buffer; *cp; cp++)
    if ((*cp >= 'A') && (*cp <= 'Z'))
      *cp |= 0x20;
  fle = fopen (buffer, "r");
  if (!fle)
    {
      send_client (serv, ":%s NOTICE %s :Sorry, there is no help on that.",
		   servicesname, real);
      return;
    }
  while (fgets (buffer, 80, fle))
    {
      buffer[strlen (buffer) - 1] = 0;	/* Remove \n. */
      send_client (serv, ":%s NOTICE %s :%s", servicesname, real, buffer);
    }
  fclose (fle);
}

void
pm_register (struct net_server *ns, int argc, char **args, char *real)
{
  static unsigned long load_m = 0;
  struct irc_user *ircu;
  struct irc_channel *chan;
  struct irc_pend_channel *ircrc;
  struct irc_chan_user *chanu;
  unsigned long size;
  /* Allow 20 register commands in 30s. This includes failed attempts. */
  if (check_flood (real, &load_m, 50, 1))
    return;
  if (argc < 3)
    {
      send_client (serv, ":%s NOTICE %s :Usage: REGISTER channel password",
		   servicesname, real);
      return;
    }
  if (strlen (args[2]) > 10)
    return;
  if (!(ircu = find_user (real)))
    return;
  if (!(chan = find_channel (args[1])))
    {
      send_client (serv, ":%s NOTICE %s :No such channel.",
		   servicesname, real);
      return;
    }
  /* For easy testing, allow operators to register young channels. */
  if (!chan->chan_ts)
    {
      send_client (serv,
		   ":%s NOTICE %s :Sorry, your channel has a zero timestamp.",
		   servicesname, real);
      return;
    }
  if ((cur_time - chan->chan_ts) < 60 * 60 * 4 &&
      !(ircu->op_flags & OPERFLAG_DEBUG))
    {
      send_client (serv,
		   ":%s NOTICE %s :The channel is not old enough(%lu/%us old)",
		   servicesname, real, cur_time - chan->chan_ts, 60 * 60 * 4);
      return;
    }
  if (!(chanu = get_user_on_channel (chan, ircu)))
    {
      send_client (serv, ":%s NOTICE %s :You are not on that channel.",
		   servicesname, real);
      return;
    }
  if (!chanu->flags & CHANUFLAG_OPPED)
    {
      send_client (serv,
		   ":%s NOTICE %s :You are not a chanop on that channel.",
		   servicesname, real);
      return;
    }
  /* Now check the size... */
  size = 0;
  for (chanu = chan->first_u; chanu; chanu = chanu->next)
    size++;
  /* Once again, grace the IRCops for ease of testing etc...
     Okay, changed that, now it is services ops with DEBUG access. */
  if (size < 4 && !(ircu->op_flags & OPERFLAG_DEBUG))
    {
      send_client (serv,
		   ":%s NOTICE %s :There must be at least 4 people in the channel"
		   " before you may register it.", servicesname, real);
      return;
    }
  /* Check for any existing pending registrations on this channel. */
  if (find_pending_channel (args[1]))
    {
      send_client (serv,
		   ":%s NOTICE %s :The channel is already pending (local) "
		   "registration.", servicesname, real);
      return;
    }
  if (find_registered_channel (args[1]))
    {
      send_client (serv, ":%s NOTICE %s :The channel is already (locally) "
		   "registered.", servicesname, real);
      return;
    }
  /* Now we broadcast to the network and create a 'registration pending' structure. */
  send_client (serv, ":%s NOTICE %s :Channel %s will pend registration.",
	       servicesname, real, args[1]);
  send_all_services ("2 PENDCHAN %s", args[1]);
  ircrc = alloc_block (bh_irc_pend_channel);
  strcpy (ircrc->nick_name, real);
  strcpy (ircrc->chan_name, args[1]);
  strcpy (ircrc->pass, args[2]);
  ircrc->chan_size = size;
  ircrc->reg_ts = cur_time;
  ircrc->chan_ts = chan->chan_ts;
  link_pending_channel (ircrc);
}


extern unsigned long last_judgement_hour;	/* We need this for DEBUG JUDGE */

void
pm_debug (struct net_server *ns, int argc, char **args, char *real)
{
  struct irc_user *ircu;
  if (!(ircu = find_user (real)))
    return;
  if (!ircu->op_flags & OPERFLAG_DEBUG)
    {
      /* Skip the access denied and we need not worry about flooding. - A1kmm
         send_client(serv,":%s NOTICE %s :Access Denied",servicesname,real); */
      return;
    }
  if (argc < 2)
    {
      send_client (serv, ":%s NOTICE %s :Usage: DEBUG what ...", servicesname,
		   real);
      return;
    }
  if (!strcasecmp (args[1], "JUDGE"))
    {
      send_client (serv,
		   ":%s NOTICE %s :Judgement hour will now occur within 5 "
		   "seconds.", servicesname, real);
      last_judgement_hour = cur_time - 3600;
    }
  else if (!strcasecmp (args[1], "TWEAK"))
    {
      struct irc_reg_channel *ircrc;
      /* DEBUG TWEAK chan score */
      if (argc < 4)
	{
	  send_client (serv, ":%s NOTICE %s :Use DEBUG tweak #chan score",
		       servicesname, real);
	  return;
	}
      if (!(ircrc = find_registered_channel (args[2])))
	{
	  send_client (serv, ":%s NOTICE %s :No such registered channel.",
		       servicesname, real);
	  return;
	}
      ircrc->score = strtoul (args[3], NULL, 10);
      send_client (serv, ":%s NOTICE %s :Score tweak complete(%lu)",
		   servicesname, real, strtoul (args[3], NULL, 10));
    }
  else
    send_client (serv, ":%s NOTICE %s :No such DEBUG command.", servicesname,
		 real);
}

void
pm_drop (struct net_server *ns, int argc, char **args, char *real)
{
  static unsigned long flood_mon = 0;
  struct irc_reg_channel *chan;
  struct irc_user *ircu;
  /* DROP channel password */
  /* We have a low load value because we send only 1 message only occasionally, even though a user need
     not use the drop command a lot - A1kmm. */
  if (check_flood (real, &flood_mon, 50, 0))
    return;
  if (argc < 3)
    {
      send_client (serv, ":%s NOTICE %s :Usage: DROP #channel password",
		   servicesname, real);
      return;
    }
  if (!(chan = find_registered_channel (args[1])))
    {
      send_client (serv, ":%s NOTICE %s :No such channel.", servicesname,
		   real);
      return;
    }
  if (chan->suspend_t)
    {
      send_client (serv, ":%s NOTICE %s :That channel is suspended.",
		   servicesname, real);
      return;
    }
  if (!(ircu = find_user (real)))
    return;
  if ((ircu->op_flags & OPERFLAG_FOUNDER) && !strcasecmp (args[2], "FOUNDER"))
    {
      send_client (serv, ":%s OPERWALL :Services op %s used FOUNDER access to"
		   " drop %s.", servicesname, real, args[1]);
    }
  else if (strcasecmp (chan->password, args[2]))
    {
      send_client (serv, ":%s NOTICE %s :Password mismatch.", servicesname,
		   real);
      return;
    }
  delete_registered_channel (chan);
}

void
pm_info (struct net_server *ns, int argc, char **args, char *real)
{
  static unsigned long flood_mon = 0;
  struct irc_reg_channel *regc;
  /* This sends stuff to the servicesnet. */
  if (check_flood (real, &flood_mon, 100, 0))
    return;
  /* INFO #channel - Finds out where channels are registered. */
  if (argc < 2 || *args[1] != '#')
    {
      send_client (serv, ":%s NOTICE %s :Usage: FIND #channel", servicesname,
		   real);
    }
  if ((regc = find_registered_channel (args[1])))
    {
      struct irc_user *ircu;
      ircu = find_user (real);
      if (ircu->op_flags & OPERFLAG_CHANINFO)
	{
	  send_client (serv,
		       ":%s NOTICE %s :Channel %s with TS %lu Registered %lu with "
		       "%s, score %u password %s", servicesname, real,
		       args[1], regc->chan_ts, regc->reg_ts, servicesname,
		       regc->score, regc->password);
	}
      else
	{
	  send_client (serv,
		       ":%s NOTICE %s :Channel %s with TS %lu Registered %lu with "
		       "%s", servicesname, real, args[1], regc->chan_ts,
		       regc->reg_ts, servicesname);
	}
      return;
    }
  send_all_services ("3 USERFIND %s %s", args[1], real);
}

struct services_op *sop_first;

void
pm_oper (struct net_server *ns, int argc, char **args, char *real)
{
  static unsigned long last_f_time = 0;
  struct services_op *sop;
  struct irc_user *ircu;
  if (argc < 3)
    {
      if (check_flood (real, &last_f_time, 100, 1))
	return;
      send_client (serv, ":%s NOTICE %s :Usage: OPER opname password",
		   servicesname, real);
      return;
    }
  for (sop = sop_first; sop && strcasecmp (args[1], sop->name);
       sop = sop->next);
  if (!sop || strcasecmp (sop->pass, args[2]))
    {
      /* Heavy flood penalty doubles as a protection against password guessing. */
      if (check_flood (real, &last_f_time, 300, 1))
	return;
      send_client (serv, ":%s NOTICE %s :Authorization failed.", servicesname,
		   real);
      return;
    }
  if (check_flood (real, &last_f_time, 100, 1))
    return;
  ircu = find_user (real);
  if (!ircu)
    return;
  ircu->op_flags |= sop->flags;
  ircu->flags |= UFLAG_MYMODE;
  send_client (serv, ":%s NOTICE %s :OPER command succeeded.", servicesname,
	       real);
  send_all_services ("5 OPER %s 1 %lu 0", real, ircu->op_flags);
}

void save_channels (void);

void
pm_sync (struct net_server *ns, int argc, char **args, char *real)
{
  /* Keep 2 times to stop normal users(normal only by not having SYNC access :) )
     blocking the sync command. */
  static unsigned long last_sync_f = 0, last_sync = 0;
  struct irc_user *ircu;
  if (!(ircu = find_user (real)))
    return;
  if (!(ircu->op_flags & OPERFLAG_SYNC))
    {
      if (check_flood (real, &last_sync_f, 100, 1))
	return;
      send_client (serv, ":%s NOTICE %s : You don't have SYNC access.",
		   servicesname, real);
      return;
    }
  /* Wide spacing, but only SYNC privileged people can get this. */
  if (check_flood (real, &last_sync, 100, 20))
    return;
  send_client (serv, ":%s WALLOPS : Services operator %s synched server.",
	       servicesname, real);
  save_channels ();
}

void
pm_die (struct net_server *ns, int argc, char **args, char *real)
{
  static unsigned long last_die_f = 0;
  struct irc_user *ircu;
  if (!(ircu = find_user (real)))
    return;
  if (!(ircu->op_flags & OPERFLAG_CONSOLE))
    {
      if (check_flood (real, &last_die_f, 100, 1))
	return;
      send_client (serv, ":%s NOTICE %s : You don't have CONSOLE access.",
		   servicesname, real);
      return;
    }
  send_client (serv, ":%s WALLOPS : Services operator %s DIEd %s.dserv.",
	       servicesname, real, servicesname);
  save_channels ();
  die_c_done = 1;
}
void rehash (void);

void
pm_rehash (struct net_server *ns, int argc, char **args, char *real)
{
  /* Keep 2 times to stop normal users(normal only by not having CONSOLE access :) )
     blocking the rehash command. */
  static unsigned long last_rehash_f = 0, last_rehash = 0;
  struct irc_user *ircu;
  if (!(ircu = find_user (real)))
    return;
  if (!(ircu->op_flags & OPERFLAG_CONSOLE))
    {
      if (check_flood (real, &last_rehash_f, 100, 1))
	return;
      send_client (serv, ":%s NOTICE %s : You don't have CONSOLE access.",
		   servicesname, real);
      return;
    }
  if (check_flood (real, &last_rehash, 100, 5))
    return;
  send_client (serv, ":%s WALLOPS : Services Operator %s REHASHed %s.dserv.",
	       servicesname, real, servicesname);
  rehash ();
}

void
pm_suspend (struct net_server *ns, int argc, char **args, char *real)
{
  static unsigned long flood_mon = 0;
  struct irc_user *ircu;
  struct irc_reg_channel *ircrc;
  struct irc_channel *ircc;
  struct suspend_channel *sus_c;
  unsigned char type;
  char *chan;
  /* 20 of these in 30s will get you killed. - A1kmm */
  if (check_flood (real, &flood_mon, 100, 0))
    return;
  if (!(ircu = find_user (real)))
    return;
  if (!(ircu->op_flags & OPERFLAG_SUSPEND))
    {
      send_client (serv, ":%s NOTICE %s :You do not have SUSPEND access.",
		   servicesname, real);
      return;
    }
  if (argc < 3)
    {
      send_client (serv, ":%s NOTICE %s :Use SUSPEND #channel [off|manage|op|"
		   "silence]", servicesname, real);
      return;
    }
  chan = args[1];
  if (*chan != '#')
    {
      send_client (serv, ":%s NOTICE %s :Use SUSPEND #channel [off|manage|op|"
		   "silence]", servicesname, real);
      return;
    }
  if (!strcasecmp (args[2], "OFF"))
    {
      type = SUSPEND_NONE;
    }
  else if (!strcasecmp (args[2], "MANAGE"))
    {
      type = SUSPEND_DONTMANAGE;
    }
  else if (!strcasecmp (args[2], "OP"))
    {
      type = SUSPEND_MASSDEOP;
    }
  else if (!strcasecmp (args[2], "SILENCE"))
    {
      type = SUSPEND_SILENCE;
    }
  else
    {
      send_client (serv, ":%s NOTICE %s :Use SUSPEND #channel [off|manage|op|"
		   "silence]", servicesname, real);
      return;
    }
  /* Now broadcast to the network to set the suspend status everywhere else: */
  send_all_services ("3 SUSPEND %s %u", args[1], type);
  if ((sus_c = find_suspended_channel (args[1])))
    {
      if (type)
	{
	  sus_c->type = type;
	  /* Extend the suspension. */
	  sus_c->expire_ts = cur_time + 24 * 60 * 60;
	}
      else
	delete_suspended_channel (sus_c);
    }
  if ((ircrc = find_registered_channel (args[1])))
    ircrc->suspend_t = type;
  send_client (serv, ":%s OPERWALL :Channel %s suspended(%s) by %s.",
	       servicesname, args[1], args[2], real);
  if (type > SUSPEND_DONTMANAGE && (ircc = find_channel (args[1])))
    {
      struct irc_chan_user *icu;
      for (icu = ircc->first_u; icu; icu = icu->next)
	{
	  char change_m[3];
	  int cpcm = 0;
	  if (icu->flags & CHANUFLAG_OPPED)
	    change_m[cpcm++] = 'o';
	  if (icu->flags & CHANUFLAG_VOICED)
	    change_m[cpcm++] = 'v';
	  if (!cpcm)
	    continue;
	  change_m[cpcm++] = 0;
	  send_client (serv, ":%s MODE %s -%s %s %s", servicesname, args[1],
		       change_m, icu->user->name, icu->user->name);
	  icu->flags &= ~(CHANUFLAG_OPPED & CHANUFLAG_VOICED);
	}
      if (type == SUSPEND_SILENCE)
	send_client (serv, ":%s MODE %s +psmnti", servicesname, args[1]);
    }
  /* Should we record this suspension? */
  if (!type || sus_c)
    return;
  create_suspend_channel (args[1], type);
}

void
pm_on (struct net_server *ns, int argc, char **args, char *real)
{
/* ON #channel password [MASSDEOP|OP who|DEOP who|MODE +/-spmntkli|UNBAN who] */
  struct irc_reg_channel *irrc;
  struct irc_channel *ircc;
  struct irc_user *ircu;
  static unsigned long flood_mon = 0;
  /* 20 of these in 30s will get you killed. - A1kmm
     One issue with this command is whether or not we want an invite option. I am worried that if I do
     that, people will start to use services instead of a bot, and it will get too much load. - A1kmm
   */
  if (check_flood (real, &flood_mon, 100, 0))
    return;
  if (argc < 4)
    {
      send_client (serv,
		   ":%s NOTICE %s :Usage: ON #channel password [MASSDEOP|OP who|"
		   "DEOP who|MODE -[k][l][i]|UNBAN who]", servicesname, real);
      return;
    }
  if (!(ircu = find_user (real)))
    return;
  if (!(irrc = find_registered_channel (args[1])))
    {
      send_client (serv, ":%s NOTICE %s :That channel is not registered.",
		   servicesname, real);
      return;
    }
  if (irrc->suspend_t)
    {
      send_client (serv, ":%s NOTICE %s :That channel is suspended.",
		   servicesname, real);
      return;
    }
  if (!(ircc = find_channel (args[1])))
    {
      send_client (serv, ":%s NOTICE %s :Join the channel first.",
		   servicesname, real);
      return;
    }
  /* "FOUNDER" access to ops. */
  if (!strcasecmp (args[2], "FOUNDER") && ircu->op_flags & OPERFLAG_FOUNDER)
    {
      send_client (serv,
		   ":%s WALLOPS : Access via 'FOUNDER' on channel %s by "
		   "operator %s.", servicesname, args[1], real);
    }
  else if (strcasecmp (irrc->password, args[2]))
    {
      send_client (serv, ":%s NOTICE %s :Password mismatch - access denied.",
		   servicesname, real);
      return;
    }
  if (!strcasecmp (args[3], "OP"))
    {
      struct irc_user *ircut;
      struct irc_chan_user *irccu;
      if (argc < 5)
	{
	  send_client (serv,
		       ":%s NOTICE %s :Usage: ON #channel password [MASSDEOP|OP who"
		       "|DEOP who|MODE -[k][l][i]|UNBAN who]", servicesname,
		       real);
	  return;
	}
      if (!(ircut = find_user (args[4])))
	{
	  send_client (serv, ":%s NOTICE %s :No such user.",
		       servicesname, real);
	  return;
	}
      if (!(irccu = get_user_on_channel (ircc, ircut)))
	{
	  send_client (serv, ":%s NOTICE %s :No such user.",
		       servicesname, real);
	  return;
	}
      if (irccu->flags & CHANUFLAG_OPPED)
	{
	  send_client (serv, ":%s NOTICE %s :That user already has ops.",
		       servicesname, real);
	  return;
	}
      send_client (serv, ":%s MODE %s +o %s", servicesname, ircc->name,
		   ircut->name);
      irccu->flags |= CHANUFLAG_OPPED;
    }
  else if (!strcasecmp (args[3], "MASSDEOP"))
    {
      struct irc_chan_user *chanu;
      if (argc < 4)
	{
	  send_client (serv,
		       ":%s NOTICE %s :Usage: ON #channel password [MASSDEOP|OP who"
		       "|DEOP who|MODE -[k][l][i]|UNBAN who]", servicesname,
		       real);
	  return;
	}
      for (chanu = ircc->first_u; chanu; chanu = chanu->next)
	if (chanu->flags & CHANUFLAG_OPPED)
	  {
	    chanu->flags &= ~CHANUFLAG_OPPED;
	    send_client (serv, ":%s MODE %s -o %s", servicesname, ircc->name,
			 chanu->user->name);
	  }
    }
  else if (!strcasecmp (args[3], "DEOP"))
    {
      char *cdo;
      unsigned long i;
      struct irc_user *ircut;
      struct irc_chan_user *irccu;
      if (argc < 5)
	{
	  send_client (serv,
		       ":%s NOTICE %s :Usage: ON #channel password [MASSDEOP|OP who"
		       "|DEOP who|MODE -[k][l][i]|UNBAN who]", servicesname,
		       real);
	  return;
	}
      /* More than one deop works. */
      i = 4;
    deop_next_user:
      for (cdo = args[i++]; i < argc + 1; cdo = args[i++])
	{
	  if (!(ircut = find_user (cdo)))
	    goto deop_next_user;
	  if (!(irccu = get_user_on_channel (ircc, ircut)))
	    goto deop_next_user;
	  if (!(irccu->flags & CHANUFLAG_OPPED))
	    goto deop_next_user;
	  send_client (serv, ":%s MODE %s -o %s", servicesname, ircc->name,
		       ircut->name);
	  irccu->flags &= ~CHANUFLAG_OPPED;
	}
    }
  else if (!strcasecmp (args[3], "MODE"))
    {
      unsigned long modes_off = 0;
      char *cps, c;
      char change[5], *fcs;
      if (argc < 5)
	{
	  send_client (serv,
		       ":%s NOTICE %s :Usage: ON #channel password [MASSDEOP|OP who"
		       "|DEOP who|MODE -[k][l][i]|UNBAN who]",
		       servicesname, real);
	  return;
	}
      fcs = change;
      cps = args[4];
      while ((c = *(cps++)))
	switch (c)
	  {
	  case 'k':
	    if (!(modes_off & CHANFLAG_KEY) && (ircc->flags & CHANFLAG_KEY))
	      {
		*fcs++ = 'k';
		modes_off |= CHANFLAG_KEY;
	      }
	    break;
	  case 'l':
	    if (!(modes_off & CHANFLAG_LIMIT)
		&& (ircc->flags & CHANFLAG_LIMIT))
	      {
		*fcs++ = 'l';
		modes_off |= CHANFLAG_LIMIT;
	      }
	    break;
	  case 'i':
	    if (!(modes_off & CHANFLAG_INVITE)
		&& (ircc->flags & CHANFLAG_INVITE))
	      {
		*fcs++ = 'i';
		modes_off |= CHANFLAG_INVITE;
	      }
	    break;
	  }
      if (!modes_off)
	return;
      *fcs = 0;
      ircc->flags &= ~modes_off;
      send_client (serv, ":%s MODE %s -%s", servicesname, ircc->name, change);
    }
  else if (!strcasecmp (args[3], "UNBAN"))
    {
      if (argc < 5)
	{
	  send_client (serv,
		       ":%s NOTICE %s :Usage: ON #channel password [MASSDEOP|OP who"
		       "|DEOP who|MODE -[k][l][i]|UNBAN who]", servicesname,
		       real);
	  return;
	}
      send_client (serv, ":%s MODE %s -b %s", servicesname, ircc->name,
		   args[4]);
    }
#ifdef USE_UNDENY
  else if (!strcasecmp (args[3], "UNDENY"))
    {
      if (argc < 5)
	{
	  send_client (serv,
		       ":%s NOTICE %s :Usage: ON #channel password [MASSDEOP|OP who"
		       "|DEOP who|MODE -[k][l][i]|UNBAN who]", servicesname,
		       real);
	  return;
	}
      send_client (serv, ":%s MODE %s -d %s", servicesname, ircc->name,
		   args[4]);
    }
#endif
  else
    {
      send_client (serv,
		   ":%s NOTICE %s :Usage: ON #channel password [MASSDEOP|OP who"
		   "|DEOP who|MODE -[k][l][i]|UNBAN who]", servicesname,
		   real);
    }
}

#if 0
void
pm_chanop (struct net_server *ns, int argc, char **args, char *real)
{
 unsigned long flood_mon;
 struct irc_user *ircu;
 if (check_flood(real, &flood_mon, 50, 5))
   return;
 if (!(ircu = find_user(real)))
   return;
 if (!(ircu->flags & 
}
#endif
void do_channel_mode (int, char **);

void
m_mode (struct net_server *ns, int argc, char **args)
{
  char c;
  int dir, f;
  struct irc_user *targ;
  if (argc < 4)
    return;
  if (*args[2] == '#')
    {
      do_channel_mode (argc, args);
      return;
    }
  if (!(targ = find_user (args[2])))
    return;
  while ((c = *args[3]++))
    switch (c)
      {
      case '+':
	dir = 0;
	break;
      case '-':
	dir = 1;
	break;
      case 'o':
	f = UFLAG_OPER;
	if (dir)
	  f = 0;
	break;
      }
  targ->flags = f;
}

void
m_privmsg (struct net_server *ns, int argc, char **args)
{
  struct irc_user *ircu;
  char *pargs[20], *ls;
  int i = 0;
  if (argc < 4)
    return;
  if (!(ircu = find_user (args[0])))
    return;
  ls = "";
  for (ls = strtok (args[3], " "); ls && ++i <= 20; ls = strtok (NULL, " "))
    {
      pargs[i - 1] = ls;
    }
  if (!i)
    return;
  if (ls)
    i--;
  /* Now check the commands against the table. */
  if (!strcasecmp (pargs[0], "ON"))
    {
      pm_on (ns, i, pargs, args[0]);
    }
  else if (!strcasecmp (pargs[0], "INFO"))
    {
      pm_info (ns, i, pargs, args[0]);
    }
  else if (!strcasecmp (pargs[0], "HELP"))
    {
      pm_help (ns, i, pargs, args[0]);
    }
  else if (!strcasecmp (pargs[0], "REGISTER"))
    {
      pm_register (ns, i, pargs, args[0]);
    }
  else if (!strcasecmp (pargs[0], "DROP"))
    {
      pm_drop (ns, i, pargs, args[0]);
    }
  else if (!strcasecmp (pargs[0], "OPER"))
    {
      pm_oper (ns, i, pargs, args[0]);
    }
  else if (!strcasecmp (pargs[0], "SUSPEND"))
    {
      pm_suspend (ns, i, pargs, args[0]);
    }
  else if (!strcasecmp (pargs[0], "SYNC"))
    {
      pm_sync (ns, i, pargs, args[0]);
    }
  else if (!strcasecmp (pargs[0], "REHASH"))
    {
      pm_rehash (ns, i, pargs, args[0]);
    }
  else if (!strcasecmp (pargs[0], "DEBUG"))
    {
      pm_debug (ns, i, pargs, args[0]);
    }
  else if (!strcasecmp (pargs[0], "DIE"))
    {
      pm_die (ns, i, pargs, args[0]);
    }
  else
    {
      /* We should probably pace this too... - A1kmm */
      static unsigned long last_fail_pw = 0;
      if (check_flood (args[0], &last_fail_pw, 100, 1))
	return;
      send_client (serv, ":%s NOTICE %s :No such command.", servicesname,
		   args[0]);
    }
}
