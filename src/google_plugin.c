/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2015, Guilhem Bonnefille <guilhem.bonnefille@gmail.com>
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

#include <gmodule.h>
#include "google.h"

const gchar *
g_module_check_init (GModule *module)
{
  g_debug("module loading: %s", g_module_name(module));
  google_init();
  return NULL;
}

const gchar *
g_module_post_init (GModule *module)
{
  g_debug("module post init: %s", g_module_name(module));
  google_post_init();
  return NULL;
}

void
g_module_unload (GModule *module)
{
  g_debug("module unloading: %s", g_module_name(module));
}

