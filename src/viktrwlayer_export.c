
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
#include "babel_ui.h"
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

  gtk_file_chooser_set_current_name ( GTK_FILE_CHOOSER(file_selector), default_name );

  while ( gtk_dialog_run ( GTK_DIALOG(file_selector) ) == GTK_RESPONSE_ACCEPT )
  {
    fn = gtk_file_chooser_get_filename ( GTK_FILE_CHOOSER(file_selector) );
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
 * export_to:
 *
 * Export all TRW Layers in the list to individual files in the specified directory
 *
 * Returns: %TRUE on success
 */
static gboolean export_to ( VikWindow *vw, GList *gl, VikFileType_t vft, const gchar *dir, const gchar *extension )
{
  gboolean success = TRUE;

  gint export_count = 0;

  vik_window_set_busy_cursor ( vw );

  while ( gl ) {

    gchar *fn = g_strconcat ( dir, G_DIR_SEPARATOR_S, VIK_LAYER(gl->data)->name, extension, NULL );

    // Some protection in attempting to write too many same named files
    // As this will get horribly slow...
    gboolean safe = FALSE;
    gint ii = 2;
    while ( ii < 5000 ) {
      if ( g_file_test ( fn, G_FILE_TEST_EXISTS ) ) {
        // Try rename
        g_free ( fn );
        fn = g_strdup_printf ( "%s%s%s#%03d%s", dir, G_DIR_SEPARATOR_S, VIK_LAYER(gl->data)->name, ii, extension );
          }
          else {
                  safe = TRUE;
                  break;
          }
          ii++;
    }
    if ( ii == 5000 )
      success = FALSE;

    // NB: We allow exporting empty layers
    if ( safe ) {
      gboolean this_success = a_file_export ( VIK_TRW_LAYER(gl->data), fn, vft, NULL, TRUE );

      // Show some progress
      if ( this_success ) {
        export_count++;
        gchar *message = g_strdup_printf ( _("Exporting to file: %s"), fn );
        vik_statusbar_set_message ( vik_window_get_statusbar ( vw ), VIK_STATUSBAR_INFO, message );
        while ( gtk_events_pending() )
          gtk_main_iteration ();
        g_free ( message );
      }

      success = success && this_success;
    }

    g_free ( fn );
    gl = g_list_next ( gl );
  }

  vik_window_clear_busy_cursor ( vw );

  // Confirm what happened.
  gchar *message = g_strdup_printf ( _("Exported files: %d"), export_count );
  vik_statusbar_set_message ( vik_window_get_statusbar ( vw ), VIK_STATUSBAR_INFO, message );
  g_free ( message );

  return success;
}

void vik_trw_layer_export_all ( VikWindow *vw, VikFileType_t vft, const gchar *extension )
{
  GList *gl = vik_layers_panel_get_all_layers_of_type ( vik_window_layers_panel ( vw ), VIK_LAYER_TRW, TRUE );

  if ( !gl ) {
    a_dialog_info_msg ( GTK_WINDOW(vw), _("Nothing to Export!") );
    return;
  }

  GtkWidget *dialog = gtk_file_chooser_dialog_new ( _("Export to directory"),
                                                    GTK_WINDOW(vw),
                                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                    GTK_STOCK_CANCEL,
                                                    GTK_RESPONSE_REJECT,
                                                    GTK_STOCK_OK,
                                                    GTK_RESPONSE_ACCEPT,
                                                    NULL );
  gtk_window_set_transient_for ( GTK_WINDOW(dialog), GTK_WINDOW(vw) );
  gtk_window_set_destroy_with_parent ( GTK_WINDOW(dialog), TRUE );
  gtk_window_set_modal ( GTK_WINDOW(dialog), TRUE );

  gtk_widget_show_all ( dialog );

  if ( gtk_dialog_run ( GTK_DIALOG(dialog) ) == GTK_RESPONSE_ACCEPT ) {
    gchar *dir = gtk_file_chooser_get_filename ( GTK_FILE_CHOOSER(dialog) );
    gtk_widget_destroy ( dialog );
    if ( dir ) {
      if ( !export_to ( vw, gl, vft, dir, extension ) )
        a_dialog_error_msg ( GTK_WINDOW(vw),_("Could not convert all files") );
      g_free ( dir );
    }
  }
  else
    gtk_widget_destroy ( dialog );

  g_list_free ( gl );
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


void vik_trw_layer_export_gpsbabel ( VikTrwLayer *vtl, const gchar *title, const gchar* default_name )
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

  /* Build the extra part of the widget */
  GtkWidget *babel_selector = a_babel_ui_file_type_selector_new ( mode );
  GtkWidget *label = gtk_label_new(_("File format:"));
  GtkWidget *hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start ( GTK_BOX(hbox), label, TRUE, TRUE, 0 );
  gtk_box_pack_start ( GTK_BOX(hbox), babel_selector, TRUE, TRUE, 0 );
  gtk_widget_show (babel_selector);
  gtk_widget_show (label);
  gtk_widget_show_all (hbox);

  gtk_widget_set_tooltip_text( babel_selector, _("Select the file format.") );

  GtkWidget *babel_modes = a_babel_ui_modes_new(mode.tracksWrite, mode.routesWrite, mode.waypointsWrite);
  gtk_widget_show (babel_modes);

  gtk_widget_set_tooltip_text( babel_modes, _("Select the information to process.\n"
      "Warning: the behavior of these switches is highly dependent of the file format selected.\n"
      "Please, refer to GPSbabel if unsure.") );

  GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start ( GTK_BOX(vbox), hbox, TRUE, TRUE, 0 );
  gtk_box_pack_start ( GTK_BOX(vbox), babel_modes, TRUE, TRUE, 0 );
  gtk_widget_show_all (vbox);

  gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER(file_selector), vbox);

  /* Add some dynamic: only allow dialog's validation when format selection is done */
  g_signal_connect (babel_selector, "changed", G_CALLBACK(a_babel_ui_type_selector_dialog_sensitivity_cb), file_selector);
  /* Manually call the callback to fix the state */
  a_babel_ui_type_selector_dialog_sensitivity_cb (babel_selector, file_selector);

  /* Set possible name of the file */
  gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER(file_selector), default_name);

  while ( gtk_dialog_run ( GTK_DIALOG(file_selector) ) == GTK_RESPONSE_ACCEPT )
  {
    fn = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(file_selector) );
    if ( g_file_test ( fn, G_FILE_TEST_EXISTS ) == FALSE ||
         a_dialog_yes_or_no ( GTK_WINDOW(file_selector), _("The file \"%s\" exists, do you wish to overwrite it?"), a_file_basename ( fn ) ) )
    {
      BabelFile *active = a_babel_ui_file_type_selector_get(babel_selector);
      if (active == NULL) {
          a_dialog_error_msg ( VIK_GTK_WINDOW_FROM_LAYER(vtl), _("You did not select a valid file format.") );
      } else {
        gtk_widget_hide ( file_selector );
        vik_window_set_busy_cursor ( VIK_WINDOW(VIK_GTK_WINDOW_FROM_LAYER(vtl)) );
        gboolean tracks, routes, waypoints;
        a_babel_ui_modes_get( babel_modes, &tracks, &routes, &waypoints );
        failed = ! a_file_export_babel ( vtl, fn, active->name, tracks, routes, waypoints );
        vik_window_clear_busy_cursor ( VIK_WINDOW(VIK_GTK_WINDOW_FROM_LAYER(vtl)) );
        break;
      }
    }
  }
  //babel_ui_selector_destroy(babel_selector);
  gtk_widget_destroy ( file_selector );
  if ( failed )
    a_dialog_error_msg ( VIK_GTK_WINDOW_FROM_LAYER(vtl), _("The filename you requested could not be opened for writing.") );
}
