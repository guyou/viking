/*
 *    Viking - GPS data editor
 *    Copyright (C) 2007 Guilhem Bonnefille <guilhem.bonnefille@gmail.com>
 *    Based on:
 *    Copyright (C) 2003-2007 Leandro A. F. Pereira <leandro@linuxmag.com.br>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, version 2.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#ifdef WINDOWS
#include <windows.h>
#endif

#include <glib/gi18n.h>


#include "dialog.h"

static void
show_url (GtkWidget *parent,
          const char *url)
{
  GError *error = NULL;
  /*
  GdkScreen *screen;

  screen = gtk_widget_get_screen (parent);
  */

  gchar *command = getenv("BROWSER");
  command = g_strdup_printf("%s '%s'", command ? command : "gnome-open", url);
  /*
  if (!gnome_url_show_on_screen (url, screen, &error))
  */
  if (!g_spawn_command_line_async(command, &error))
  {
    GtkWidget *dialog;

    /* TODO I18N */
    dialog = gtk_message_dialog_new (GTK_WINDOW (parent),
             GTK_DIALOG_DESTROY_WITH_PARENT,
             GTK_MESSAGE_ERROR,
             GTK_BUTTONS_OK,
             "%s", "Could not open link");
    gtk_message_dialog_format_secondary_text
      (GTK_MESSAGE_DIALOG (dialog), "%s", error->message);
    g_error_free (error);

    g_signal_connect (dialog, "response",
          G_CALLBACK (gtk_widget_destroy), NULL);
    gtk_widget_show (dialog);
  }
  g_free(command);
}

static gboolean spawn_command_line_async(const gchar * cmd,
                                         const gchar * arg)
{
  gchar *cmdline = NULL;
  gboolean status;

  cmdline = g_strdup_printf("%s '%s'", cmd, arg);
  g_debug("Running: %s", cmdline);
    
  status = g_spawn_command_line_async(cmdline, NULL);

  g_free(cmdline);
 
  return status;
}

void open_url(GtkWindow *parent, const gchar * url)
{
  show_url (GTK_WIDGET (parent), url);
}

void new_email(GtkWindow *parent, const gchar *email)
{
  gchar *address;
  /* FIXME: escaping? */
  address = g_strdup_printf ("mailto:%s", email);
  show_url (GTK_WIDGET (parent), address);
  g_free (address);
}

gchar *uri_escape(gchar *str)
{
  gchar *esc_str = g_malloc(3*strlen(str));
  gchar *dst = esc_str;
  gchar *src;

  for (src = str; *src; src++) {
    if (*src == ' ')
     *dst++ = '+';
    else if (g_ascii_isalnum(*src))
     *dst++ = *src;
    else {
      g_sprintf(dst, "%%%02X", *src);
      dst += 3;
    }
  }
  *dst = '\0';

  return(esc_str);
}

