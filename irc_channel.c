/*
 *  irc_channel.c: Keeps track of channels active on IRC.
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

struct irc_channel *first_chan = NULL, *last_chan = NULL;
struct irc_pend_channel *first_pend_chan = NULL, *last_pend_chan = NULL;
struct irc_reg_channel *first_reg_chan = NULL, *last_reg_chan = NULL;
struct suspend_channel *first_sus = NULL, *last_sus = NULL;
void link_registered_channel (struct irc_reg_channel *ipc);
void delete_registered_channel (struct irc_reg_channel *ipc);
void save_channels (void);

struct irc_channel *
find_channel (char *name)
{
#if 1
  struct hash_entry *he;
  if (!(he = hash[hash_text (name)]))
    return NULL;
  for (; he; he = he->next)
    if (he->type == 1 && !strcasecmp (
				      ((struct irc_channel *) he->ptr)->name,
				      name))
      return ((struct irc_channel *) he->ptr);
  return NULL;
#else
  struct irc_channel *this_chan;
  for (this_chan = first_chan; this_chan; this_chan = this_chan->next)
    if (!strcasecmp (this_chan->name, name))
      return this_chan;
  return NULL;
#endif
}

void
link_in_channel (struct irc_channel *ic)
{
  struct hash_entry *he, *phe, *fhe;
  unsigned short hv;
  ic->next = NULL;
  ic->prev = last_chan;
  if (first_chan)
    {
      last_chan->next = ic;
    }
  else
    first_chan = ic;
  last_chan = ic;
  he = alloc_block (bh_hash_entry);
  he->type = 1;
  he->next = NULL;
  he->ptr = ic;
  if (!(fhe = hash[(hv = hash_text (ic->name))]))
    {
      hash[hv] = he;
      return;
    }
  for (; fhe; fhe = fhe->next)
    phe = fhe;
  phe->next = he;
}

struct irc_chan_user *
get_user_on_channel (struct irc_channel *ic, struct irc_user *iu)
{
  struct irc_chan_user *ticu;
  for (ticu = ic->first_u; ticu; ticu = ticu->next)
    if (ticu->user == iu)
      return ticu;
  return NULL;
}

void
add_chanu (struct irc_channel *chan, struct irc_chan_user *u)
{
  u->next = NULL;
  if (chan->last_u)
    {
      chan->last_u->next = u;
    }
  else
    chan->first_u = u;
  chan->last_u = u;
}

void
delete_chanu (struct irc_channel *chan, struct irc_chan_user *u)
{
  struct irc_chan_user *prev_cu = NULL, *cur_cu;
  for (cur_cu = chan->first_u; cur_cu != u && cur_cu; cur_cu = cur_cu->next)
    prev_cu = cur_cu;
  if (!cur_cu)
    return;
  if (prev_cu)
    {
      prev_cu->next = u->next;
    }
  else
    chan->first_u = u->next;
  if (!u->next)
    chan->last_u = NULL;
  free_block (bh_irc_chan_user, u);
}

void
destroy_channel (struct irc_channel *ic)
{
  struct hash_entry *fhe, *phe;
  unsigned short hv;
  if (ic->next)
    {
      ic->next->prev = ic->prev;
    }
  else
    last_chan = ic->prev;
  if (ic->prev)
    {
      ic->prev->next = ic->next;
    }
  else
    first_chan = ic->next;
  while (ic->first_u)
    delete_chanu (ic, ic->first_u);
  fhe = hash[(hv = hash_text (ic->name))];
  for (phe = NULL; fhe; fhe = fhe->next)
    {
      if (fhe->type == 1 && fhe->ptr == ic)
	break;
      phe = fhe;
    }
  phe ? phe->next : hash[hv] = fhe->next;
  free_block (bh_hash_entry, fhe);
  free_block (bh_irc_channel, ic);
}

void
expire_pending_chans (void)
{
  struct irc_pend_channel *ipc, *npc;
  for (ipc = first_pend_chan; ipc; ipc = npc)
    {
      struct irc_reg_channel *irgc;
      npc = ipc->next;
      if (cur_time - ipc->reg_ts < 10)
	continue;
      irgc = alloc_block (bh_irc_reg_channel);
      irgc->reg_ts = ipc->reg_ts;
      irgc->chan_ts = ipc->chan_ts;
      irgc->score = ipc->chan_size + 10;
      strcpy (irgc->password, ipc->pass);
      strcpy (irgc->name, ipc->chan_name);
      send_client (serv, ":%s NOTICE %s :The channel %s is now registered.",
		   servicesname, ipc->nick_name, ipc->chan_name);
      link_registered_channel (irgc);
      delete_pending_channel (ipc);
    }
}

/* The judgement hour occurs every 3600s, and at this time all regchans with >4 members go up 1 point,
   and all with <4 members go down 10 points. This is also the time when all channels below 4 points
   are destroyed. */
unsigned long last_judgement_hour = 0;

void
judge_channels (void)
{
  struct irc_reg_channel *ircrc, *nircrc;
  struct irc_channel *ircc;
  struct irc_chan_user *irccu;
  unsigned long count;
  if ((cur_time - last_judgement_hour) < 3600)
    return;
  /* Do not run a judgement hour on startup. */
  if (!last_judgement_hour)
    {
      last_judgement_hour = cur_time;
      return;
    }
  last_judgement_hour = cur_time;
  send_client (serv, ":%s WALLOPS :Now judging channels on %s.dserv.",
	       servicesname, servicesname);
  /* Now we go through the registered channels and change the score. */
  for (ircrc = first_reg_chan; ircrc; ircrc = nircrc)
    {
      nircrc = ircrc->next;
      ircc = find_channel (ircrc->name);
      if (!ircc)
	{
	  ircrc->score -= 10;
	  if (ircrc->score < 4)
	    delete_registered_channel (ircrc);
	  continue;
	}
      count = 0;
      for (irccu = ircc->first_u; irccu; irccu = irccu->next)
	count++;
      if (count < 4)
	{
	  ircrc->score -= 10;
	  if (ircrc->score < 4)
	    delete_registered_channel (ircrc);

	}
      else if (ircrc->score > 4)
	{
	  ircrc->score++;
	  /* Prevent it from getting unreasonably large. This cap does, however, raise problems
	     if huge channels ever arise, because very low(<< 0) could come if 2000 users depart
	     rapidly when we have capped the score at 1000, which is unfair, for this channel could
	     get dropped when they have hundreds of members still. Below is a kludge for this problem.
	   */
	  if (ircrc->score < 5)
	    ircrc->score = (count > 900) ? 1000 : (count + 10);
	  if (ircrc->score > 1000)
	    ircrc->score = 1000;
	}
    }
  save_channels ();
}

void
expire_suspended_chans (void)
{
  struct suspend_channel *susc, *susn;
  for (susc = first_sus; susc; susc = susn)
    {
      susn = susc->next;
      if (susc->expire_ts < cur_time)
	{
	  /* Tell every other service it has expired. */
	  send_all_services ("3 SUSPEND %s 0", susc->name);
	  delete_suspended_channel (susc);
	}
    }
}

void
destroy_empty_channels (void)
{
  static unsigned long last_gc = 0;
  struct irc_channel *tchan, *nchan;
  /* Only garbage collect every 5 seconds. */
  if (cur_time - last_gc < 5)
    return;
  last_gc = cur_time;
  for (tchan = first_chan; tchan; tchan = nchan)
    {
      nchan = tchan->next;
      if (!tchan->first_u)
	destroy_channel (tchan);
    }
  expire_suspended_chans ();
  expire_pending_chans ();
  judge_channels ();
}

void
delete_all_user_channels (struct irc_user *iu)
{
  struct irc_channel *ic;
  struct irc_chan_user *icu;
  for (ic = first_chan; ic; ic = ic->next)
    if ((icu = get_user_on_channel (ic, iu)))
      delete_chanu (ic, icu);
}

/*
 :sender SJOIN <TS> #<channel> <modes> :[@][+]<nick_1> ...  [@][+]<nick_n>
*/
void
m_sjoin (struct net_server *ns, int argc, char **argp)
{
  struct irc_user *users[5000];
  unsigned long u_flags[5000];
  struct irc_channel *chan;
  struct irc_chan_user *chanu;
  struct irc_reg_channel *ircrc;
  struct suspend_channel *susc;
  char *t_iu, c;
  int modes[2], mode, dir = 0;
  int uc, cf;
  cf = 0;
  if (argc < 6)
    return;
  if (*argp[3] != '#')
    return;
  /* We don't know about channels > MAX_CHAN_LEN. */
  if (strlen (argp[3]) > MAX_CHAN_LEN)
    return;
  t_iu = strtok (argp[argc - 1], " ");
  for (uc = 0; uc < 5000 && t_iu; uc++)
    {
      cf = 0;
      if (*t_iu == '@')
	{
	  t_iu++;
	  cf |= 1;
	}
      if (*t_iu == '+')
	{
	  t_iu++;
	  cf |= 2;
	}
      users[uc] = find_user (t_iu);
      u_flags[uc] = cf;
      if (!users[uc])
	{
	  puts ("Channel desynched with user!!!!!");
	  exit (1);
	  return;
	}
      t_iu = strtok (NULL, " ");
    }
  if (uc == 5000)
    return;
  while ((c = *argp[4]++))
    switch (c)
      {
      case '+':
	dir = 0;
	break;
      case '-':
	dir = 1;
	break;
      case 's':
	modes[dir] |= CHANFLAG_SECRET;
	break;
      case 'p':
	modes[dir] |= CHANFLAG_PRIVATE;
	break;
      case 'n':
	modes[dir] |= CHANFLAG_NOEXTERNAL;
	break;
      case 't':
	modes[dir] |= CHANFLAG_TOPICOP;
	break;
      case 'k':		/* We don't store the key, just record its existance. */
	modes[dir] |= CHANFLAG_KEY;
	break;
      case 'i':
	modes[dir] |= CHANFLAG_INVITE;
	break;
      case 'l':		/* We don't store the limit, just record its existance. */
	modes[dir] |= CHANFLAG_LIMIT;
	break;
      }
  chan = find_channel (argp[3]);
  if (chan)
    {
      mode = chan->flags;
    }
  else
    mode = 0;
  mode |= modes[0];
  mode &= ~modes[1];
  if (!chan)
    {
      chan = alloc_block (bh_irc_channel);
      /* This is safe - we checked the length above. */
      strcpy (chan->name, argp[3]);
      link_in_channel (chan);
      chan->first_u = NULL;
      chan->last_u = NULL;
    }
  chan->flags = mode;
  for (cf = 0; cf < uc; cf++)
    {
      /* Skip users we know are on the channel. */
      if (get_user_on_channel (chan, users[uc]))
	{
	  puts ("Duplicate user on channel for SJOIN received.");
	  continue;
	}
      chanu = alloc_block (bh_irc_chan_user);
      chanu->user = users[cf];
      chanu->flags = u_flags[cf];
      add_chanu (chan, chanu);
    }
  chan->chan_ts = strtoul (argp[2], NULL, 10);
  /* We need TS to work... */
  if (!(chan->chan_ts))
    return;
  /* If the channel is suspended, tell the sender if from services, if it is massdeop then do the
     deop/devoice unconditionally, and set +psmnti if SILENCE. Note that we still preserve the 
     ts and delete duplicates. - A1kmm */
  if ((susc = find_suspended_channel (chan->name)))
    {
      if (ns->lts)
	send_client (ns->lts->services, "3 SUSPEND %s %u", chan->name,
		     susc->type);
      if (susc->type > SUSPEND_DONTMANAGE)
	for (chanu = chan->first_u; chanu; chanu = chanu->next)
	  {
	    char modeb[3];
	    int modep = 0;
	    if ((chanu->flags & CHANUFLAG_OPPED))
	      modeb[modep++] = 'o';
	    if ((chanu->flags & CHANUFLAG_VOICED))
	      modeb[modep++] = 'v';
	    if (!modep)
	      continue;
	    modeb[modep++] = 0;
	    send_client (serv, ":%s MODE %s -%s :%s %s", servicesname,
			 chan->name, modeb, chanu->user->name,
			 chanu->user->name);
	    chanu->flags &= ~(CHANUFLAG_OPPED | CHANUFLAG_VOICED);
	  }
      if (susc->type == SUSPEND_SILENCE)
	send_client (serv, ":%s MODE %s +psmnti", servicesname, chan->name);
    }
  if ((ircrc = find_registered_channel (argp[3])))
    {
      if (chan->chan_ts < ircrc->chan_ts)
	{
	  delete_registered_channel (ircrc);
	  return;
	}
      /* If it is from another services, we announce it to the services network with the
         time of registration, and they tell us to delete it if needed, or they delete it. */
      if (ns->lts)
	{
	  send_client (ns->lts->services, "4 CLASH %s %lu %lu", argp[3],
		       ircrc->chan_ts, ircrc->reg_ts);
	}
      /* Deop/devoice all joinees if they cycled the channel. */
      if (chan->chan_ts > ircrc->chan_ts)
	{
	  /*
	     :sender SJOIN <TS> #<channel> <modes> :[@][+]<nick_1> ...  [@][+]<nick_n>
	   */
	  send_client (serv, "SJOIN %lu %s +nt :@%s", ircrc->chan_ts, argp[3],
		       servicesname);
	  send_client (serv, ":%s PART %s", servicesname, argp[3]);
	  for (chanu = chan->first_u; chanu; chanu = chanu->next)
	    chanu->flags &= ~(CHANUFLAG_OPPED & CHANUFLAG_VOICED);
	}
    }
}

void
m_part (struct net_server *ns, int argc, char **argp)
{
  struct irc_user *ircu;
  struct irc_channel *ircc;
  struct irc_chan_user *irccu;
  if (argc < 3)
    return;
  if (!(ircu = find_user (argp[0])))
    return;
  if (!(ircc = find_channel (argp[2])))
    return;
  if (!(irccu = get_user_on_channel (ircc, ircu)))
    return;
  delete_chanu (ircc, irccu);
}

void
do_channel_mode (int argc, char **argv)
 /* :sender MODE #channel +...-... ... */
{
  char c;
  int th_arg = 4;
  int modes[2], dir;
  struct irc_chan_user *irccu;
  struct irc_channel *ircc;
  struct irc_user *ircu;
  if (!(ircc = find_channel (argv[2])))
    return;
  while ((c = *argv[3]++))
    switch (c)
      {
      case '+':
	dir = 0;
	break;
      case '-':
	dir = 1;
	break;
      case 's':
	modes[dir] |= CHANFLAG_SECRET;
	break;
      case 'p':
	modes[dir] |= CHANFLAG_PRIVATE;
	break;
      case 'n':
	modes[dir] |= CHANFLAG_NOEXTERNAL;
	break;
      case 't':
	modes[dir] |= CHANFLAG_TOPICOP;
	break;
      case 'k':		/* We don't store the key, just record its existance. */
	modes[dir] |= CHANFLAG_KEY;
	break;
      case 'i':
	modes[dir] |= CHANFLAG_INVITE;
	break;
      case 'l':		/* We don't store the limit, just record its existance. */
	modes[dir] |= CHANFLAG_LIMIT;
	break;
      case 'o':
	if (th_arg >= argc)
	  continue;
	if (!(ircu = find_user (argv[th_arg++])))
	  continue;
	if (!(irccu = get_user_on_channel (ircc, ircu)))
	  continue;
	if (dir)
	  {
	    irccu->flags &= ~CHANUFLAG_OPPED;
	  }
	else
	  irccu->flags |= CHANUFLAG_OPPED;
	break;
      case 'v':
	if (th_arg >= argc)
	  continue;
	if (!(ircu = find_user (argv[th_arg++])))
	  continue;
	if (!(irccu = get_user_on_channel (ircc, ircu)))
	  continue;
	if (dir)
	  {
	    irccu->flags &= ~CHANUFLAG_VOICED;
	  }
	else
	  irccu->flags |= CHANUFLAG_VOICED;
	break;
      }
  ircc->flags |= modes[0];
  ircc->flags &= ~modes[1];
}

struct irc_pend_channel *
find_pending_channel (char *name)
{
  struct irc_pend_channel *cc;
  for (cc = first_pend_chan; cc; cc = cc->next)
    if (!strcasecmp (cc->chan_name, name))
      return cc;
  return NULL;
}

struct irc_reg_channel *
find_registered_channel (char *name)
{
#if 1
  struct hash_entry *he;
  if (!(he = hash[hash_text (name)]))
    return NULL;
  for (; he; he = he->next)
    if (he->type == 2 && !strcasecmp (
				      ((struct irc_reg_channel *) he->ptr)->
				      name, name))
      return ((struct irc_reg_channel *) he->ptr);
  return NULL;
#else
  struct irc_reg_channel *cc;
  for (cc = first_reg_chan; cc; cc = cc->next)
    if (!strcasecmp (cc->name, name))
      return cc;
  return NULL;
#endif
}

void
link_pending_channel (struct irc_pend_channel *ipc)
{
  /* Note: unlike channels and registered channels, pending channels are _not_ hashed. This is because
     they only last a short time and there are not usually a large number of them. */
  ipc->next = NULL;
  ipc->prev = last_pend_chan;
  if (last_pend_chan)
    {
      last_pend_chan->next = ipc;
    }
  else
    first_pend_chan = ipc;
  last_pend_chan = ipc;
}

void
link_registered_channel (struct irc_reg_channel *ipc)
{
  struct hash_entry *he, *phe, *fhe;
  unsigned short hv;
  ipc->next = NULL;
  ipc->prev = last_reg_chan;
  if (last_reg_chan)
    {
      last_reg_chan->next = ipc;
    }
  else
    first_reg_chan = ipc;
  last_reg_chan = ipc;
  he = alloc_block (bh_hash_entry);
  he->type = 2;
  he->next = NULL;
  he->ptr = ipc;
  if (!(fhe = hash[(hv = hash_text (ipc->name))]))
    {
      hash[hv] = he;
      return;
    }
  for (; fhe; fhe = fhe->next)
    phe = fhe;
  phe->next = he;
}

void
delete_registered_channel (struct irc_reg_channel *ipc)
{
  struct hash_entry *fhe, *phe;
  unsigned short hv;
  if (ipc->next)
    {
      ipc->next->prev = ipc->prev;
    }
  else
    last_reg_chan = ipc->prev;
  if (ipc->prev)
    {
      ipc->prev->next = ipc->next;
    }
  else
    first_reg_chan = ipc->next;
  fhe = hash[(hv = hash_text (ipc->name))];
  for (phe = NULL; fhe; fhe = fhe->next)
    {
      if (fhe->type == 2 && fhe->ptr == ipc)
	break;
      phe = fhe;
    }
  phe ? phe->next : hash[hv] = fhe->next;
  free_block (bh_hash_entry, fhe);
  free_block (bh_irc_pend_channel, ipc);
}

void
delete_pending_channel (struct irc_pend_channel *ipc)
{
  if (ipc->next)
    {
      ipc->next->prev = ipc->prev;
    }
  else
    last_pend_chan = ipc->prev;
  if (ipc->prev)
    {
      ipc->prev->next = ipc->next;
    }
  else
    first_pend_chan = ipc->next;
  free_block (bh_irc_pend_channel, ipc);
}

void
create_suspend_channel (char *name, char type)
{
  struct suspend_channel *susrec;
  susrec = alloc_block (bh_suspend_channel);
  susrec->next = NULL;
  susrec->type = type;
  if (strlen (name) > MAX_CHAN_LEN)
    return;
  /* Suspend for a day. */
  susrec->expire_ts = cur_time + 60 * 60 * 24;
  strcpy (susrec->name, name);
  if (last_sus)
    {
      last_sus->next = susrec;
    }
  else
    first_sus = susrec;
  last_sus = susrec;
}

struct suspend_channel *
find_suspended_channel (char *name)
{
  struct suspend_channel *sus;
  for (sus = first_sus; sus; sus = sus->next)
    if (!strcasecmp (name, sus->name))
      return sus;
  return NULL;
}

void
delete_suspended_channel (struct suspend_channel *sd)
{
  struct suspend_channel *sus, *lsus;
  lsus = NULL;
  for (sus = first_sus; sus != sd && sus; sus = sus->next)
    lsus = sus;
  if (!sus)
    return;
  if (last_sus == sd)
    {
      last_sus = lsus;
    }
  if (first_sus == sd)
    {
      first_sus = sd->next;
    }
  else
    lsus->next = sd->next;
  free_block (bh_suspend_channel, sd);
}
