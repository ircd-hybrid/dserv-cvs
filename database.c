/*
 *  database.c: Stores the channel database.
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

/*
I think we would be best just to write each registered channel structure without the next and
previous pointers straight to the file, and read them in as we load. It does not need to be a
hash or anything if we just load and unload them as we go. - A1kmm
*/

#define SIZE_TO_DISK \
   (sizeof(struct irc_reg_channel) - 2*sizeof(struct irc_reg_channel *))

void
load_channels (void)
{
  FILE *chan_file;
  struct irc_reg_channel *ip, *pp = NULL, **cp;
  char chan_file_name[255];
  snprintf (chan_file_name, 255, "%s.channels.db", servicesname);
  /* If there is no file, we start with no registered channels. */
  if (!(chan_file = fopen (chan_file_name, "r")))
    return;
  ip = alloc_block (bh_irc_reg_channel);
  for (cp = &first_reg_chan; fread (ip, SIZE_TO_DISK, 1, chan_file) > 0;
       cp = &pp->next)
    {
      struct hash_entry *he, *che, *phe;
      unsigned short hv;
      *cp = ip;
      cp = &ip;
      /* Now enter a value into the hash for it. */
      he = alloc_block (bh_hash_entry);
      he->next = NULL;
      he->ptr = ip;
      he->type = 2;
      if (!(che = hash[(hv = hash_text (ip->name))]))
	{
	  hash[hv] = he;
	  goto hash_add_done;
	}
      for (; che; che = che->next)
	phe = che;
      phe->next = he;
    hash_add_done:
      ip->prev = pp;
      pp = ip;
      ip = alloc_block (bh_irc_reg_channel);
    }
  /* The last ip is not needed, and we should set the last pointer to NULL. */
  free_block (bh_irc_reg_channel, ip);
  if (pp)
    pp->next = NULL;
  last_reg_chan = pp;
  /* And finally, close the channel file. */
  fclose (chan_file);
}

void
save_channels (void)
{
  /* Open the temporary channel file. */
  struct irc_reg_channel *regc;
  FILE *tchan_file;
  char chan_file_name[255], chan_temp_name[255];
  snprintf (chan_file_name, 255, "%s.channels.db", servicesname);
  snprintf (chan_temp_name, 255, "%s.channels.tmp", servicesname);
  tchan_file = fopen (chan_temp_name, "w");
  for (regc = first_reg_chan; regc; regc = regc->next)
    {
      fwrite (regc, SIZE_TO_DISK, 1, tchan_file);
    }
  fclose (tchan_file);
  unlink (chan_file_name);
  rename (chan_temp_name, chan_file_name);
}
