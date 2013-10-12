
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include "babel.h"
#include "viking.h"
#include "viktrwlayer_export.h"

void vik_trw_layer_export ( VikTrwLayer *vtl, const gchar *title, const gchar* default_name, VikTrack* trk, guint file_type )
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


/**
 * Convert the given TRW layer into a temporary GPX file and open it with the specified program
 *
 */
void vik_trw_layer_export_external_gpx ( VikTrwLayer *vtl, const gchar* external_program )
{
  gchar *name_used = NULL;
  int fd;

  if ((fd = g_file_open_tmp("tmp-viking.XXXXXX.gpx", &name_used, NULL)) >= 0) {
    vik_window_set_busy_cursor ( VIK_WINDOW(VIK_GTK_WINDOW_FROM_LAYER(vtl)) );
    gboolean failed = ! a_file_export ( VIK_TRW_LAYER(vtl), name_used, FILE_TYPE_GPX, NULL, TRUE);
    vik_window_clear_busy_cursor ( VIK_WINDOW(VIK_GTK_WINDOW_FROM_LAYER(vtl)) );
    if (failed) {
      a_dialog_error_msg (VIK_GTK_WINDOW_FROM_LAYER(vtl), _("Could not create temporary file for export.") );
    }
    else {
      GError *err = NULL;
      gchar *quoted_file = g_shell_quote ( name_used );
      gchar *cmd = g_strdup_printf ( "%s %s", external_program, quoted_file );
      g_free ( quoted_file );
      if ( ! g_spawn_command_line_async ( cmd, &err ) )
        {
          a_dialog_error_msg_extra ( VIK_GTK_WINDOW_FROM_LAYER( vtl), _("Could not launch %s."), external_program );
          g_error_free ( err );
        }
      g_free ( cmd );
    }
    // Note ATM the 'temporary' file is not deleted, as loading via another program is not instantaneous
    //g_remove ( name_used );
    // Perhaps should be deleted when the program ends?
    // For now leave it to the user to delete it / use system temp cleanup methods.
    g_free ( name_used );
  }
}

void babel_ui_selector_add_entry_cb ( gpointer data, gpointer user_data )
{
  BabelFile *file = (BabelFile*)data;
  GtkWidget *combo = GTK_WIDGET (user_data);

  GList *formats = g_object_get_data ( G_OBJECT ( combo ), "formats");
  formats = g_list_append(formats, file);
  g_object_set_data ( G_OBJECT ( combo ), "formats", formats );

  const gchar *label = file->label;
  vik_combo_box_text_append (combo, label);
}

GtkWidget *babel_ui_selector_new ( BabelMode mode )
{
  /* Create the combo */
  GtkWidget * combo = vik_combo_box_text_new ();

  /* Prepare space for file format list */
  g_object_set_data ( G_OBJECT ( combo ), "formats", NULL );

  a_babel_foreach_file_with_mode (mode, babel_ui_selector_add_entry_cb, combo);

  return combo;
}

void babel_ui_selector_destroy ( GtkWidget *selector )
{
  GList *formats = g_object_get_data ( G_OBJECT ( selector ), "formats");
  g_free (formats);
}

BabelFile *babel_ui_selector_get ( GtkWidget *selector )
{
  gint active = gtk_combo_box_get_active (GTK_COMBO_BOX(selector));
  GList *formats = g_object_get_data ( G_OBJECT ( selector ), "formats");
  return (BabelFile*)g_list_nth_data(formats, active);
}

GtkWidget *babel_ui_modes_new ( gboolean tracks, gboolean routes, gboolean waypoints )
{
  GtkWidget *hbox = gtk_hbox_new(FALSE, 0);
  GtkWidget *button = NULL;

  button = gtk_check_button_new_with_label ( _("Tracks"));
  gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(button), tracks );
  gtk_box_pack_start ( GTK_BOX(hbox), button, TRUE, TRUE, 0 );
  gtk_widget_show (button);

  button = gtk_check_button_new_with_label ( _("Routes"));
  gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(button), routes );
  gtk_box_pack_start ( GTK_BOX(hbox), button, TRUE, TRUE, 0 );
  gtk_widget_show (button);

  button = gtk_check_button_new_with_label ( _("Waypoints"));
  gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(button), waypoints );
  gtk_box_pack_start ( GTK_BOX(hbox), button, TRUE, TRUE, 0 );
  gtk_widget_show (button);

  return hbox;
}

void vik_trw_layer_export_gpsbabel ( VikTrwLayer *vtl )
{
  BabelMode mode = { 0, 0, 0, 0, 0, 0 };
  if ( g_hash_table_size (vik_trw_layer_get_routes(vtl)) ) {
      mode.routesWrite = 1;
  }
  if ( g_hash_table_size (vik_trw_layer_get_tracks(vtl)) ) {
      mode.tracksWrite = 1;
  }
  if ( g_hash_table_size (vik_trw_layer_get_waypoints(vtl)) ) {
      mode.waypointsWrite = 1;
  }

  GtkWidget *file_selector;
  const gchar *fn;
  gboolean failed = FALSE;
  const gchar *title = _("Export via GPSbabel");
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
  GtkWidget *babel_selector = babel_ui_selector_new ( mode );
  GtkWidget *label = gtk_label_new(_("File format:"));
  GtkWidget *hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start ( GTK_BOX(hbox), label, TRUE, TRUE, 0 );
  gtk_box_pack_start ( GTK_BOX(hbox), babel_selector, TRUE, TRUE, 0 );
  gtk_widget_show (babel_selector);
  gtk_widget_show (label);
  gtk_widget_show_all (hbox);

  GtkWidget *babel_modes = babel_ui_modes_new(mode.tracksWrite, mode.routesWrite, mode.waypointsWrite);
  gtk_widget_show (babel_modes);

  GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start ( GTK_BOX(vbox), hbox, TRUE, TRUE, 0 );
  gtk_box_pack_start ( GTK_BOX(vbox), babel_modes, TRUE, TRUE, 0 );
  gtk_widget_show_all (vbox);

  gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER(file_selector), vbox);

  // TODO gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER(file_selector), default_name);

  while ( gtk_dialog_run ( GTK_DIALOG(file_selector) ) == GTK_RESPONSE_ACCEPT )
  {
    fn = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(file_selector) );
    if ( g_file_test ( fn, G_FILE_TEST_EXISTS ) == FALSE ||
         a_dialog_yes_or_no ( GTK_WINDOW(file_selector), _("The file \"%s\" exists, do you wish to overwrite it?"), a_file_basename ( fn ) ) )
    {
      gtk_widget_hide ( file_selector );
      vik_window_set_busy_cursor ( VIK_WINDOW(VIK_GTK_WINDOW_FROM_LAYER(vtl)) );
      // FIXME sublayer
      BabelFile *active = babel_ui_selector_get(babel_selector);
      failed = ! a_file_export_babel ( vtl, fn, active->name );
      vik_window_clear_busy_cursor ( VIK_WINDOW(VIK_GTK_WINDOW_FROM_LAYER(vtl)) );
      break;
    }
  }
  //babel_ui_selector_destroy(babel_selector);
  gtk_widget_destroy ( file_selector );
  if ( failed )
    a_dialog_error_msg ( VIK_GTK_WINDOW_FROM_LAYER(vtl), _("The filename you requested could not be opened for writing.") );
}
