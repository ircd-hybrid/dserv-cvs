/*
 *  irc_users.c: Keeps track of what users and servers are on IRC.
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

void delete_all_user_channels (struct irc_user *);

inline unsigned short
hash_text (unsigned char *txt)
{
  register unsigned char c, c1 = 'd', c2 = 's';
  while (-1)
    {
      if ((c = *txt++))
	{
	  /* Make it case insensitive. */
	  c1 ^= ((c >= 'a' && c <= 'z') ? (c & ~0x20) : c);
	}
      else
	return (c1 << 8 | c2);
      if ((c = *txt++))
	{
	  /* Make it case insensitive. */
	  c2 ^= ((c >= 'a' && c <= 'z') ? (c & ~0x20) : c);
	}
      else
	return (c1 << 8 | c2);
    }
}

/* Its big but it serves nicks and all types of channels and should offer a performance
   boost. */
struct hash_entry *hash[0xFFFF];

void
process_client_quit (struct irc_user *ircu)
{
  struct hash_entry *he, *phe;
  unsigned short hv;
  if (!ircu)
    return;
  if (ircu->next)
    {
      ircu->next->prev = ircu->prev;
    }
  else
    last_user = ircu->prev;
  if (ircu->prev)
    {
      ircu->prev->next = ircu->next;
    }
  else
    first_user = ircu->next;
  if (ircu->ho)
    ircu->ho->conn_count--;
#if 1				/* Hash code stuff... */
  he = hash[(hv = hash_text (ircu->name))];
  for (phe = NULL; he; he = he->next)
    {
      if (he->type == 0 && he->ptr == ircu)
	break;
      phe = he;
    }
  if (!phe)
    {
      hash[hv] = he->next;
    }
  else
    phe->next = he->next;
  free_block (bh_hash_entry, he);
#endif
  delete_all_user_channels (ircu);
  free_block (bh_irc_user, ircu);
}

void
process_one_server_quit (struct net_server *ns)
{
  struct irc_user *ircu, *nircu;
  if (!ns)
    return;
  for (ircu = first_user; ircu; ircu = nircu)
    {
      nircu = ircu->next;
      if (ircu->server == ns)
	    process_client_quit (ircu);
    }
  if (ns->prev)
    {
      ns->prev->next = ns->next;
    }
  else
    first_conn_serv = ns->next;
  if (ns->next)
    {
      ns->next->prev = ns->prev;
    }
  else
    last_conn_serv = ns->prev;
  if (ns->lts)
    {
      struct link_from_services *lts;
      lts = ns->lts;
      lts->phase = LINKPHASE_ROONIN;
      lts->services->close_connection (lts->services,
				       "Owning server delinked.");
      close (lts->services->fd);
      lts->services->flags &= NCFLAG_DEAD;
    }
  /* And free it!! - A1kmm */
  free_block (bh_net_server, ns);
}

void
process_server_quit (struct net_server *ns)
{
 struct net_server *cns, *c2ns, *ncns;
 for (cns = first_conn_serv; cns; cns = ncns)
   {
    ncns = cns->next;
    for (c2ns = cns; c2ns && c2ns != ns; c2ns = c2ns->uplink);
    if (c2ns)
      process_one_server_quit(c2ns);
   }
}

struct irc_user *
find_user (char *name)
{
#if 1				/* New, faster way, I hope. - A1kmm */
  struct hash_entry *he;
  if (!(he = hash[hash_text (name)]))
    return NULL;
  for (; he; he = he->next)
    /* he->type = 0 means it is a user, not a channel... */
    if (he->type == 0
	&& !strcasecmp ((((struct irc_user *) he->ptr)->name), name))
      return (struct irc_user *) he->ptr;
  return NULL;
#else /* Old slow way. - A1kmm */
  struct irc_user *cu;
  for (cu = first_user; cu; cu = cu->next)
    /* strcasecmp here! - A1kmm. */
    if (!strcasecmp (cu->name, name))
      return cu;
  return NULL;
#endif
}

struct net_server *
find_server (char *name)
{
  struct net_server *ns;
  for (ns = first_conn_serv; ns; ns = ns->next)
    /* strcasecmp here! - A1kmm */
    if (!strcasecmp (ns->name, name))
      return ns;
  return NULL;
}
