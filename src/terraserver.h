/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2003-2005, Evan Battaglia <gtoevan@gmx.net>
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

#ifndef __VIKING_TERRASERVER_H
#define __VIKING_TERRASERVER_H

#include <glib.h>

#include "vikcoord.h"
#include "mapcoord.h"

gboolean terraserver_topo_coord_to_mapcoord ( const VikCoord *src, gdouble xmpp, gdouble ympp, MapCoord *dest );
void terraserver_topo_download ( MapCoord *src, const gchar *dest_fn );

gboolean terraserver_aerial_coord_to_mapcoord ( const VikCoord *src, gdouble xmpp, gdouble ympp, MapCoord *dest );
void terraserver_aerial_download ( MapCoord *src, const gchar *dest_fn );

gboolean terraserver_urban_coord_to_mapcoord ( const VikCoord *src, gdouble xmpp, gdouble ympp, MapCoord *dest );
void terraserver_urban_download ( MapCoord *src, const gchar *dest_fn );

void terraserver_mapcoord_to_center_coord ( MapCoord *src, VikCoord *dest );

#endif
