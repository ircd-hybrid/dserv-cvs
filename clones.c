/*
 *  clones.c: A clone/nick flood detector.
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

struct host_online *ho_first=NULL, *ho_last=NULL;

struct host_online *
find_host(char *host)
{
 struct hash_entry *he;
 if (!(he = hash[hash_text(host)]))
   return NULL;
 for (;he;he=he->next)
    if (!strcasecmp(((struct host_online*)he->ptr)->host, host))
      return (struct host_online*)he->ptr;
 return NULL;
}

void
delete_host(struct host_online *host)
{
 unsigned short ht;
 struct hash_entry *he, *phe = NULL;
 if (!(he=hash[(ht=hash_text(host->host))]))
   return;
 for (;he&&he->ptr!=host;he=he->next)
    phe = he;
 if(!he) return;
 if (phe)
   phe->next = he->next;
 else
   hash[ht] = he->next;
 free_block(bh_host_online, host);
 free_block(bh_hash_entry, he);
}

struct host_online *
create_host(char *host)
{
 struct host_online *ho;
 struct hash_entry *he, *phe;
 unsigned short hv;
 ho = alloc_block(bh_host_online);
 strncpy(ho->host, host, 40)[39] = 0;
 ho->conn_freq = 1; ho->last_conn_time = cur_time;
 ho->conn_count = 0;
 ho->next = NULL;
 if (ho_last)
   ho_last->next = ho;
 else
   ho_first = ho;
 ho_last = ho;
 if (!(he = hash[(hv=hash_text(host))]))
   {
    hash[hv] = alloc_block(bh_hash_entry);
    hash[hv]->next = NULL;
    hash[hv]->type = 3;
    hash[hv]->ptr = ho;
    return (struct host_online*)hash[hv]->ptr;
   }
 else
   {
    for (;he;he=he->next) phe = he;
    phe->next = alloc_block(bh_hash_entry);
    phe->next->next = NULL;
    phe->next->type = 3;
    phe->next->ptr = ho;
    return (struct host_online*)phe->next->ptr;
   } 
}

struct host_online *
connect_host(char *host, unsigned long ts)
{
 struct host_online *ho;
 if (!(ho = find_host(host)))
   ho = create_host(host);
 else
   {
    long delta;
    /* We don't count bringing newly linked clients as nick floods, but they
        are clones if there are many of them. - A1kmm. */
    if ((delta = (cur_time - ho->last_conn_time) / 30)>0)
      {
       ho->last_conn_time = cur_time - 
         (cur_time - ho->last_conn_time) % 30;
       ho->conn_freq -= delta-1;
      } else {
       ho->conn_freq++;
       ho->last_conn_time = cur_time;
      }
    /* 5+ intros in 30s? They're flooding. - A1kmm. */
    if (ho->conn_freq > 4)
      {
       char kbuff[50];
       snprintf(kbuff, 50, "*@*%s", host);
       suggest_klines(kbuff, "Nick flooding.", 60);
       return ho;
      }
   }
    /* Okay, we avoid klining old clients here rather than above - A1kmm. */
  if ((cur_time - ts) > 30)
    ho->conn_freq = 0;
 /* 5+ clients in irc now(counting the new client.) */
 if (ho->conn_count++ > 3)
   {
    char kbuff[50];
    snprintf(kbuff, 50, "*@*%s", host);
    suggest_klines(kbuff, "Cloning.", 60);
    return ho;
   }
 return ho;
}

void
cleanup_hosts(void)
{
 struct host_online *ho, *nho;
 static unsigned long last_ts=0;
 if (cur_time-last_ts < 30)
   return;
 last_ts = cur_time; 
 for (ho=ho_first; ho; ho=nho)
    {
    	unsigned long delta;
    	nho = ho->next;
    	if ((cur_time-ho->last_conn_time) < 30||ho->conn_count)
    	  continue;
    	delta = (cur_time-ho->last_conn_time) / 30;
    	ho->last_conn_time = cur_time - (cur_time-ho->last_conn_time) % 30;
    	if (ho->conn_freq > delta)
    	  ho->conn_freq -= delta;
    	else
    	  delete_host(ho);
    }
}