
/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2013, Guilhem Bonnefille <guilhem.bonnefille@gmail.com>
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
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include "viking.h"
#include "viktrwlayer_export.h"

void trw_layer_export ( VikTrwLayer *vtl, const gchar *title, const gchar* default_name, VikTrack* trk, guint file_type )
{
  GtkWidget *file_selector;
  const gchar *fn;
  gboolean failed = FALSE;
  file_selector = gtk_file_chooser_dialog_new (title,
                                               NULL,
                                               GTK_FILE_CHOOSER_ACTION_SAVE,
                                               GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                               GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
                                               NULL);
  gchar *cwd = g_get_current_dir();
  if ( cwd ) {
    gtk_file_chooser_set_current_folder ( GTK_FILE_CHOOSER(file_selector), cwd );
    g_free ( cwd );
  }

  gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER(file_selector), default_name);

  while ( gtk_dialog_run ( GTK_DIALOG(file_selector) ) == GTK_RESPONSE_ACCEPT )
  {
    fn = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(file_selector) );
    if ( g_file_test ( fn, G_FILE_TEST_EXISTS ) == FALSE ||
         a_dialog_yes_or_no ( GTK_WINDOW(file_selector), _("The file \"%s\" exists, do you wish to overwrite it?"), a_file_basename ( fn ) ) )
    {
      gtk_widget_hide ( file_selector );
      vik_window_set_busy_cursor ( VIK_WINDOW(VIK_GTK_WINDOW_FROM_LAYER(vtl)) );
      failed = ! a_file_export ( vtl, fn, file_type, trk, TRUE );
      vik_window_clear_busy_cursor ( VIK_WINDOW(VIK_GTK_WINDOW_FROM_LAYER(vtl)) );
      break;
    }
  }
  gtk_widget_destroy ( file_selector );
  if ( failed )
    a_dialog_error_msg ( VIK_GTK_WINDOW_FROM_LAYER(vtl), _("The filename you requested could not be opened for writing.") );
}
