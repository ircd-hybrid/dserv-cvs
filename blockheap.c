/*
 *  blockheap.c: Allocates blocks of memory at a time.
 *  This file is part of the Distributed Services by Andrew Miller(A1kmm), 20/9/2000.
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

typedef struct __BH_Block
{
  unsigned char *alloc_space;
  void *data_space, *data_space_end;
  struct __BH_Block *next;
}
BH_Block;

struct __BlockHeap
{
  BH_Block *first_block, *last_block;
  unsigned long block_size, alloc_at_once;
};

BlockHeap *bh_access_level, *bh_services_op, *bh_hash_entry,
  *bh_irc_reg_channel, *bh_irc_channel, *bh_irc_chan_user,
  *bh_suspend_channel, *bh_net_server, *bh_irc_user, *bh_irc_pend_channel,
  *bh_tcm_link, *bh_oomon_link, *bh_net_connection,
  *bh_link_from_services, *bh_host_online;

void
setup_blockheaps (void)
{
#define SetupHeap(x, y) bh_ ## x = create_blockheap(sizeof(struct x), y)
  SetupHeap (access_level, 32);
  SetupHeap (services_op, 32);
  SetupHeap (hash_entry, 6400);
  SetupHeap (irc_reg_channel, 800);
  SetupHeap (irc_channel, 1600);
  SetupHeap (irc_chan_user, 3200);
  SetupHeap (suspend_channel, 256);
  SetupHeap (net_server, 32);
  SetupHeap (irc_user, 3200);
  SetupHeap (host_online, 3200);  
  SetupHeap (irc_pend_channel, 256);
  SetupHeap (tcm_link, 16);
  SetupHeap (oomon_link, 16);
  SetupHeap (net_connection, 32);
  SetupHeap (link_from_services, 32);
}

#if 1
BlockHeap *
create_blockheap (unsigned long block_size, unsigned long alloc_at_once)
{
 return (BlockHeap*)block_size;
}

void *
alloc_block(BlockHeap *bh)
{
 return malloc((unsigned long)bh);
}

void
free_block(BlockHeap *bh, void *ptr)
{
 free(ptr);
}
#else
BlockHeap *
create_blockheap (unsigned long block_size, unsigned long alloc_at_once)
{
  BlockHeap *new_bh;
  if (!(new_bh = malloc (sizeof (BlockHeap))))
    {
      fprintf (stderr, "Out of memory!\n");
      exit (1);
    }
  new_bh->block_size = block_size;
  new_bh->alloc_at_once = alloc_at_once;
  if (!(new_bh->first_block = malloc (sizeof (BH_Block))))
    {
      fprintf (stderr, "Out of memory!\n");
      exit (1);
    }
  if (!
      (new_bh->first_block->data_space = malloc (block_size * alloc_at_once)))
    {
      fprintf (stderr, "Out of memory!\n");
      exit (1);
    }
  if (!(new_bh->first_block->alloc_space = malloc (alloc_at_once / 8 + 1)))
    {
      fprintf (stderr, "Out of memory!\n");
      exit (1);
    }
  memset (new_bh->first_block->alloc_space, 0, alloc_at_once / 8 + 1);
  new_bh->first_block->data_space_end =
    new_bh->first_block->data_space + block_size * alloc_at_once;
  new_bh->first_block->next = NULL;
  return new_bh;
}

void *
alloc_block (BlockHeap * bh)
{
  BH_Block *bt, *bp;
  char v;
  unsigned long sze, p, r;
  sze = bh->alloc_at_once / 8 + 1;
  for (bt = bh->first_block; bt; bt = bt->next)
    {
    cwtb:
      for (p = 0; (bt->alloc_space[p] == 0xFF) && p < sze; p++);
      if (p == sze)
	continue;
      v = bt->alloc_space[p];
      if (!(v & 0x1))
	{
	  bt->alloc_space[p] |= 0x1;
	  r = p * 8 * bh->block_size;
	}
      else if (!(v & 0x2))
	{
	  bt->alloc_space[p] |= 0x2;
	  r = (p * 8 + 1) * bh->block_size;
	}
      else if (!(v & 0x4))
	{
	  bt->alloc_space[p] |= 0x4;
	  r = (p * 8 + 2) * bh->block_size;
	}
      else if (!(v & 0x8))
	{
	  bt->alloc_space[p] |= 0x8;
	  r = (p * 8 + 3) * bh->block_size;
	}
      else if (!(v & 0x10))
	{
	  bt->alloc_space[p] |= 0x10;
	  r = (p * 8 + 4) * bh->block_size;
	}
      else if (!(v & 0x20))
	{
	  bt->alloc_space[p] |= 0x20;
	  r = (p * 8 + 5) * bh->block_size;
	}
      else if (!(v & 0x40))
	{
	  bt->alloc_space[p] |= 0x40;
	  r = (p * 8 + 6) * bh->block_size;
	}
      else
	{
	  bt->alloc_space[p] |= 0x80;
	  r = (p * 8 + 7) * bh->block_size;
	}
      /* This can happen if we have sizes that don't divide by 8. - A1kmm */
      if (r > bh->alloc_at_once * bh->block_size)
	goto cwtb;
      return r + bt->data_space;
    }
  /* If we get here, we need to allocate a new block. */
  for (bt = bh->first_block; bt; bt = bt->next)
    bp = bt;
  bt = malloc (sizeof (BlockHeap));
  if (!bt)
    {
      fprintf (stderr, "Out of memory!\n");
      exit (1);
    }
  bp->next = bt;
  if (!(bt->alloc_space = malloc (sze)))
    {
      fprintf (stderr, "Out of memory!\n");
      exit (1);
    }
  if (!(bt->data_space = malloc (bh->block_size * bh->alloc_at_once)))
    {
      fprintf (stderr, "Out of memory!\n");
      exit (1);
    }
  bt->data_space_end = bt->data_space + bh->block_size * bh->alloc_at_once;
  memset (bt->alloc_space, 0, bh->alloc_at_once / 8 + 1);
  /* First block in use. */
  *bt->alloc_space = 1;
  return bt->data_space;
}

void
free_block (BlockHeap * bh, void *ptr)
{
  BH_Block *bt;
  char flg;
  unsigned long pos;
  for (bt = bh->first_block;
       bt && (bt->data_space > ptr || ptr > bt->data_space_end);
       bt = bt->next);
  if (!bt)			/* We didn't allocate that! - A1kmm */
    assert (0);
  pos = (ptr - bt->data_space) / bh->block_size;
  flg = 1 << (pos & 0x7);
  bt->alloc_space[pos / 8] &= ~flg;
  /* I don't see too much point in shrinking the blockheaps if they are empty - they will probably only
     rise again to the old size in the lifetime of the services, and if the number of clients settles
     near to exactly filling the blockheaps, malloc/free could be called many times. The problem with
     this approach is that if the number of clients drifts downwards we keep a lot more memory in use
     than we need(like if the network becomes less popular). But that is probably not too much of a
     problem - A1kmm.
   */
}
#endif