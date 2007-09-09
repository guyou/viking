/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2003-2005, Evan Battaglia <gtoevan@gmx.net>
 * Copyright (C) 2007, Guilhem Bonnefille <guilhem.bonnefille@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdlib.h>

#include <glib.h>
#include <glib/gstdio.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "viking.h"

const gchar *a_get_viking_base_cache_dir()
{
  static gchar *viking_dir = NULL;

  if (!viking_dir) {
    const gchar *cache_dir = g_get_user_cache_dir();
#ifdef HAVE_MKDTEMP
    if (!cache_dir || access(cache_dir, W_OK))
    {
      static gchar temp[] = {"/tmp/vikXXXXXX"};
      cache_dir = mkdtemp(temp);
    }
#endif
    if (!cache_dir || access(cache_dir, W_OK))
    {
      /* Fatal error */
      g_critical("Unable to find a base directory");
      exit(1);
    }

    /* Build the name of the directory */
    viking_dir = g_build_filename(cache_dir, "viking", NULL);
    if (access(viking_dir, F_OK))
      g_mkdir(viking_dir, 0755);
  }

  return viking_dir;
}

const gchar *a_get_viking_cookies_dir()
{
  static gchar *viking_dir = NULL;

  if (!viking_dir) {
    const gchar *cache_dir = a_get_viking_base_cache_dir();

    /* Build the name of the directory */
    viking_dir = g_build_filename(cache_dir, "cookies", NULL);
    if (access(viking_dir, F_OK))
      g_mkdir(viking_dir, 0755);
  }

  return viking_dir;
}

const gchar *a_get_viking_maps_dir()
{
  static gchar *viking_dir = NULL;

  if (!viking_dir) {
    const gchar *cache_dir = a_get_viking_base_cache_dir();

    /* Build the name of the directory */
    viking_dir = g_build_filename(cache_dir, "maps", NULL);
    if (access(viking_dir, F_OK))
      g_mkdir(viking_dir, 0755);
  }

  return viking_dir;
}
