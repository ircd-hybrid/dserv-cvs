
/*
 *  config.c - Reads the configuration file.
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

struct access_level *acl_first = NULL, *acl_last = NULL;
struct services_op *sop_first = NULL, *sop_last = NULL;

int services_up = 0;

int
config_error (char *l, unsigned long i)
{
  if (services_up)
    {
      send_client (serv, ":%s WALLOPS :Configuration error, line %lu: %s",
		   servicesname, i, l);
    }
  else
    {
      fprintf (stderr, "Configuration error, line %lu: %s\n", i, l);
      exit (1);
    }
  return -1;
}

int
parse_config_line (char *l, unsigned long i)
{
  char *command, *rest, *st, *ep;
  int ic = 0;
  char *names[40], *values[40];
  if (!(command = strtok (l, " \r\n\t")))
    return 0;
  rest = strtok (NULL, "\r\n");
  if (!rest)
    rest = "";
  st = rest;
  while (-1)
    {
      while (*st && (*st == ' ' || *st == '\t'))
	st++;
      if (!*st)
	break;
      rest = st;
      while (*st && *st != ' ')
	st++;
      if (*rest == '\"')
	{
	  while (*st && *st != '\"')
	    st++;
	  rest++;
	  if (*st)
	    *st = 0;
	}
      if (*rest == '\'')
	rest++;
      names[ic] = rest;
      if ((ep = strchr (rest, '=')))
	{
	  *ep++ = 0;
	  values[ic++] = *ep ? ep : "";
	}
      else
	values[ic++] = "";
      if (!*st)
	break;
      *st++ = 0;
    }
  names[ic] = NULL;
  values[ic] = NULL;
  if (!strcasecmp (command, "LEVEL"))
    {
      char *levname;
      struct access_level *acl_new;
      int iv, acc_fl = 0;
      for (iv = 0; names[iv] && strcasecmp (names[iv], "NAME"); iv++);
      if (!names[iv])
	return config_error ("Access keyword needs name=...", i);
      if (strlen (values[iv]) > 10)
	return config_error ("Access name must not exceed 10b long.", i);
      levname = values[iv];
      for (iv = 0; names[iv] && strcasecmp (names[iv], "DEBUG"); iv++);
      if (names[iv])
	acc_fl |= OPERFLAG_DEBUG;
      for (iv = 0; names[iv] && strcasecmp (names[iv], "SUSPEND"); iv++);
      if (names[iv])
	acc_fl |= OPERFLAG_SUSPEND;
      for (iv = 0; names[iv] && strcasecmp (names[iv], "CONSOLE"); iv++);
      if (names[iv])
	acc_fl |= OPERFLAG_CONSOLE;
      for (iv = 0; names[iv] && strcasecmp (names[iv], "FOUNDER"); iv++);
      if (names[iv])
	acc_fl |= OPERFLAG_FOUNDER;
      for (iv = 0; names[iv] && strcasecmp (names[iv], "CHANINFO"); iv++);
      if (names[iv])
	acc_fl |= OPERFLAG_CHANINFO;
      for (iv = 0; names[iv] && strcasecmp (names[iv], "SYNC"); iv++);
      if (names[iv])
	acc_fl |= OPERFLAG_SYNC;
      acl_new = alloc_block (bh_access_level);
      strcpy (acl_new->name, levname);
      acl_new->flags = acc_fl;
      acl_new->next = NULL;
      if (acl_last)
	{
	  acl_last->next = acl_new;
	}
      else
	acl_first = acl_new;
      acl_last = acl_new;
    }
  else if (!strcasecmp (command, "OPERATOR"))
    {
      char *opname, *pass, *access;
      struct access_level *leva;
      struct services_op *sop;
      int iv;
      for (iv = 0; names[iv] && strcasecmp (names[iv], "NAME"); iv++);
      opname = values[iv];
      if (opname && strlen (opname) > 20)
	return config_error ("Op names must not exceed 20b long.", i);
      for (iv = 0; names[iv] && strcasecmp (names[iv], "PASSWORD"); iv++);
      pass = values[iv];
      if (pass && strlen (pass) > 20)
	return config_error ("Op passwords must not exceed 20b long.", i);
      for (iv = 0; names[iv] && strcasecmp (names[iv], "ACCESS"); iv++);
      access = values[iv];
      if (!opname || !pass || !access)
	return
	  config_error ("Operator keyword needs NAME, PASSWORD and access "
			"tags.", i);
      for (leva = acl_first; leva && strcasecmp (access, leva->name);
	   leva = leva->next);
      if (!leva)
	return config_error ("No such access level.", i);
      sop = alloc_block (bh_services_op);
      strcpy (sop->name, opname);
      strcpy (sop->pass, pass);
      sop->flags = leva->flags;
      sop->next = NULL;
      if (sop_last)
	{
	  sop_last->next = sop;
	}
      else
	sop_first = sop;
      sop_last = sop;
    }
  else if (!strcasecmp (command, "SERVER"))
    {
      char *srv_host, *srv_pass, *srv_ports, *srv_sports;
      unsigned long srv_port, srv_sport;
      int iv = 0;
      if (services_up)
	return 0;
      for (iv = 0; names[iv] && strcasecmp (names[iv], "HOST"); iv++);
      srv_host = values[iv];
      for (iv = 0; names[iv] && strcasecmp (names[iv], "PASS"); iv++);
      srv_pass = values[iv];
      for (iv = 0; names[iv] && strcasecmp (names[iv], "PORT"); iv++);
      srv_ports = values[iv];
      for (iv = 0; names[iv] && strcasecmp (names[iv], "SPORT"); iv++);
      srv_sports = values[iv];

      if (!srv_ports || !srv_host || !srv_pass || !srv_sports)
	return config_error ("SERVER needs HOST, PASS and PORT tags.", i);
      if (!(srv_port = strtoul (srv_ports, NULL, 10))
	  || srv_port >= (1 << 16))
	return config_error ("Port number must be between 1 and 65535.", i);
      if (!(srv_sport = strtoul (srv_sports, NULL, 10))
	  || srv_sport >= (1 << 16))
	return config_error ("Port number must be between 1 and 65535.", i);

      serv_port = (unsigned short) srv_port;
      serv_host = strdup (srv_host);
      serv_password = strdup (srv_pass);
      serv_sport = (unsigned short) srv_sport;
    }
  return 0;
}

void
read_config_file (void)
{
  FILE *conf;
  int i = 0;
  char buffer[200];
  puts ("Config-read");
  snprintf (buffer, 2000, "%s.dserv.conf", servicesname);
  conf = fopen (buffer, "r");
  if (!conf)
    return;
  while (fgets (buffer, 2000, conf))
    {
      parse_config_line (buffer, ++i);
    }
  fclose (conf);		/* Lets not be messy. */
}

void
rehash (void)
{
  struct access_level *alc, *alcn;
  struct services_op *sop, *sopn;
  /* Delete all levels and SOPs. */
  for (alc = acl_first; alc; alc = alcn)
    {
      alcn = alc->next;
      free_block (bh_access_level, alc);
    }
  acl_first = NULL;
  acl_last = NULL;
  for (sop = sop_first; sop; sop = sopn)
    {
      sopn = sop->next;
      free_block (bh_services_op, sop);
    }
  sop_first = NULL;
  sop_last = NULL;
  read_config_file ();
}
