/*
 *  oomon_io.c: Lets us talk to oomons, and now also tcms.
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

struct oomon_link *first_ooml = NULL, *last_ooml = NULL;
struct tcm_link *first_tcml = NULL, *last_tcml = NULL;

void
read_oom_line (struct net_connection *nc, char *l)
{
  if (*l && *l == ':' && !(*((struct oomon_link *) nc->data)->name))
    {
      char *c;
      l++;
      if (!(c = strchr (l, ' ')))
	return;
      *c = 0;
      strncpy (((struct oomon_link *) nc->data)->name, l, 20)[20] = 0;
    }
}

void
read_tcm_line (struct net_connection *nc, char *l)
{
  printf ("From a tcm: %s\n", l);
}

void
close_oom_connection (struct net_connection *nc, char *m)
{
  struct oomon_link *ooml, *pooml;
  ooml = (struct oomon_link *) (nc->data);
  for (pooml = first_ooml; pooml != ooml && pooml; pooml = pooml->next);
  if (!ooml->next)
    {
      last_ooml = pooml;
    }
  else
    last_ooml = NULL;
  if (pooml)
    {
      pooml->next = ooml->next;
    }
  else
    first_ooml = ooml->next;
}

void
close_tcm_connection (struct net_connection *nc, char *m)
{
  struct tcm_link *ooml, *pooml;
  ooml = (struct tcm_link *) (nc->data);
  for (pooml = first_tcml; pooml != ooml && pooml; pooml = pooml->next);
  if (!ooml->next)
    {
      last_tcml = pooml;
    }
  else
    last_tcml = NULL;
  if (pooml)
    {
      pooml->next = ooml->next;
    }
  else
    first_tcml = ooml->next;
}

void
check_oo (struct net_connection *nc)
{
  struct oomon_link *ooml;
  ooml = alloc_block (bh_oomon_link);
  ooml->nc = nc;
  ooml->next = NULL;
  *ooml->name = 0;
  if (last_ooml)
    {
      last_ooml->next = ooml;
    }
  else
    first_ooml = ooml;
  nc->blockh = bh_oomon_link;
  nc->data = ooml;
  nc->close_connection = close_oom_connection;
  nc->read_line = read_oom_line;
  send_client (nc, ":services 001 * : Welcome to the dserv<->OOMon link.");
  send_client (nc, ":services 010 * : Ready.");
}

void
check_tcm (struct net_connection *nc)
{
  struct tcm_link *tcml;
  nc->close_connection = close_tcm_connection;
  nc->read_line = read_tcm_line;
  tcml = alloc_block (bh_tcm_link);
  tcml->next = NULL;
  if (last_tcml)
    {
      last_tcml->next = tcml;
    }
  else
    first_tcml = tcml;
  last_tcml = tcml;
  nc->blockh = bh_tcm_link;
  nc->data = tcml;
  tcml->nc = nc;
}

void
suggest_klines (char *mask, char *reason, int t)
{
  struct oomon_link *ooml;
  struct tcm_link *tcml;
  for (ooml = first_ooml; ooml; ooml = ooml->next)
    {
      if (*ooml->name)
	send_client (ooml->nc, "KLINE %s %d %s :%s", ooml->name, t,
		     mask, reason);
    }
  for (tcml = first_tcml; tcml; tcml = tcml->next)
    {
      send_client (tcml->nc, ".kline %d %s :%s", t, mask, reason);
    }
}
