/*
 *  net.c: A generalised dispatcher for 'select' calls.
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
destroy_client (struct net_connection *nc)
{
  if (nc->prev)
    {
      nc->prev->next = nc->next;
    }
  else
    first_nc = nc->next;
  if (nc->next)
    {
      nc->next->prev = nc->prev;
    }
  else
    last_nc = nc->prev;
  close (nc->fd);
  if (nc->data)
    {
      if (nc->blockh)
	free_block (nc->blockh, nc->data);
      else
	free (nc->data);
    }
  free (nc->in_buffer);
  free (nc->out_buffer);
  free_block (bh_net_connection, nc);
}

void destroy_empty_channels (void);
void cleanup_hosts (void);

/* This happens whenever we write out/read in, as well as every 4s when idle. */
void
do_cyclic (void)
{
  /* Cleanup empty channels(and judge channels if judgement hour, and expire pending channels
     and suspensions). Defer these if services not up. - A1kmm */
  if (services_up)
    {
     destroy_empty_channels ();
     cleanup_hosts();
    }
}

unsigned long die_c_done;

void
do_main_loop (void)
{
  fd_set rdfds, wrfds;
  struct net_connection *cnc, *nnc;
  struct timeval tv;
  die_c_done = 0;
  while (-1)
    {
      if (!first_nc)
	return;
      if (die_c_done >= 2)
	return;
      if (die_c_done > 0)
	{
	  die_c_done++;
	}
      FD_ZERO (&rdfds);
      FD_ZERO (&wrfds);
      nnc = first_nc;
      while (-1)
	{
	scan_ncs:
	  cnc = nnc;
	  if (!cnc)
	    goto bl1d;
	  nnc = cnc->next;
	  nnc = cnc->next;
	  if (cnc->flags & NCFLAG_DEAD)
	    goto scan_ncs;
	  FD_SET (cnc->fd, &rdfds);
	  if (cnc->outbuff_pos)
	    FD_SET (cnc->fd, &wrfds);
	}
    bl1d:
      tv.tv_sec = 2;
      tv.tv_usec = 0;
      select (FD_SETSIZE, &rdfds, &wrfds, NULL, &tv);
      nnc = first_nc;
      cur_time = time (0);
      while (-1)
	{
	scan_ncs2:
	  cnc = nnc;
	  if (!cnc)
	    break;
	  nnc = cnc->next;
	  if (cnc->flags & NCFLAG_DEAD)
	    goto scan_ncs2;
	  if (FD_ISSET (cnc->fd, &rdfds))
	    {
	      int rsp;
	      char buffer[2000];
	      if (cnc->flags & NCFLAG_LISTEN)
		{
		  cnc->read_raw_data (cnc, NULL, 0);
		  goto scan_ncs2;
		}
	      rsp = read (cnc->fd, buffer, 2000);
	      if (rsp == 0)
		{
		  //cnc->close_connection (cnc, "Connection reset by peer.");
		  //cnc->flags |= NCFLAG_DEAD;
		  //cnc = nnc;
		  goto scan_ncs2;
		}
	      if (rsp < 0 && errno != EAGAIN && errno != EINTR)
		{
		  cnc->close_connection (cnc, "Read error.");
		  cnc->flags |= NCFLAG_DEAD;
		  cnc = nnc;
		  goto scan_ncs2;
		}
	      cnc->read_raw_data (cnc, buffer, rsp);
	    }
	  if (FD_ISSET (cnc->fd, &wrfds))
	    cnc->write_raw_data (cnc);
	}
      for (cnc = first_nc; cnc; cnc = nnc)
	{
	  nnc = cnc->next;
	  if (cnc->flags & NCFLAG_DEAD)
	    destroy_client (cnc);
	}
      do_cyclic ();
    }
}

void
read_raw_data (struct net_connection *nc, char *d, unsigned long l)
{
  char *ep;
  ep = d + l;
  assert (!(nc->flags & NCFLAG_DEAD));
  while (d < ep)
    {
      if (nc->inbuff_pos > nc->inbuff_size - 1)
	{
	  nc->in_buffer[nc->inbuff_size - 1] = 0;
	  nc->read_line (nc, nc->in_buffer);
	  nc->inbuff_pos = 0;
	  return;
	}
      if (*d == '\r')
	{
	  d++;
	  continue;
	}
      if (*d == '\n')
	{
	  nc->in_buffer[nc->inbuff_pos] = 0;
	  nc->read_line (nc, nc->in_buffer);
	  nc->inbuff_pos = 0;
	  d++;
	  continue;
	}
      nc->in_buffer[nc->inbuff_pos++] = *d++;
    }
}

void
write_raw_data (struct net_connection *nc)
{
  int i, err, x;
  assert (!(nc->flags & NCFLAG_DEAD));
  if (!(i = nc->outbuff_pos + 1))
    return;
  x = nc->outbuff_pos;
  while (--i && (err = write (nc->fd, &nc->out_buffer[x - i], 1)) == 1);
  if (err == 0)
    {
      nc->close_connection (nc, "Connection reset by peer.");
      nc->flags |= NCFLAG_DEAD;
      return;
    }
  if (err < 0 && errno != EAGAIN && errno != EINTR)
    {
      nc->close_connection (nc, "Write error.");
      nc->flags |= NCFLAG_DEAD;
      return;
    }
  /* Move nc->outbuff_pos - i bytes at nc->in_buffer[i] back to
     nc->inbuffer[0] */
  memmove (&nc->in_buffer[i], &nc->in_buffer[0], nc->outbuff_pos - i);
  nc->outbuff_pos = i;
}

void
write_line (struct net_connection *nc, char *l)
{
  printf ("Outgoing: %s", l);
  if (nc->outbuff_pos + strlen (l) > nc->outbuff_size)
    return;			/* perhaps it would be better to close the connection. */
  strcpy (&nc->out_buffer[nc->outbuff_pos], l);
  nc->outbuff_pos += strlen (l);
}

void
send_client (struct net_connection *nc, char *m, ...)
{
  va_list val;
  int i;
  char buffer[5000];
  va_start (val, m);
  vsnprintf (buffer, 4998, m, val);
  buffer[4999] = 0;
  i = strlen (buffer);
  buffer[i] = '\n';
  buffer[i + 1] = 0;
  nc->write_line (nc, buffer);
  va_end (val);
}
