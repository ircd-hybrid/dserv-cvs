/*
 *  commands.c: A table of all IRC commands we know.
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

void m_ping (struct net_server *, int, char **);
void m_pong (struct net_server *, int, char **);
void m_server (struct net_server *, int, char **);
void m_version (struct net_server *, int, char **);
void m_nick (struct net_server *, int, char **);
void m_squit (struct net_server *, int, char **);
void m_kill (struct net_server *, int, char **);
void m_privmsg (struct net_server *, int, char **);
void m_sjoin (struct net_server *, int, char **);
void m_part (struct net_server *, int, char **);
void m_mode (struct net_server *, int, char **);
void m_quit (struct net_server *, int, char **);
/* Keep these in the approximate order of descending frequency of use. */
struct command commands[] = {
  {"NICK", m_nick},
  {"PRIVMSG", m_privmsg},
  {"MODE", m_mode},
  {"SJOIN", m_sjoin},
  {"PART", m_part},
  {"QUIT", m_quit},
  {"KILL", m_kill},
  {"SERVER", m_server},
  {"SQUIT", m_squit},
  {"VERSION", m_version},
  {"INFO", m_version},
/* We normally only get these when everything is quiet, so send them to the end. */
  {"PING", m_ping},
  {"PONG", m_pong},
  {0, 0}
};
