/*
 *    Viking - GPS data editor
 *    Copyright (C) 2007, Guilhem Bonnefille <guilhem.bonnefille@gmail.com>
 *    Copyright (C) 2014, Rob Norris <rw_norris@hotmail.com>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
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
 /*
  * Dependencies must be just on Glib or other basic system types (math, string, etc...)
  * see ui_utils for thing that depend on Gtk
  * see vikutils for things that further depend on other Viking types
  */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <gio/gio.h>
#include <math.h>
#include <ctype.h> // For isalpha() etc...

#include "util.h"
#include "globals.h"
#include "fileutils.h"

guint util_get_number_of_cpus ()
{
  return g_get_num_processors();
}

/**
 * split_string_from_file_on_equals:
 *
 * @buf: the input string
 * @key: newly allocated string that is before the '='
 * @val: newly allocated string after the '='
 *
 * Designed for file line processing, so it ignores strings beginning with special
 *  characters, such as '#'; returns false in such situations.
 * Also returns false if no equals character is found
 *
 * e.g. if buf = "GPS.parameter=42"
 *   key = "GPS.parameter"
 *   val = "42"
 */
gboolean split_string_from_file_on_equals ( const gchar *buf, gchar **key, gchar **val )
{
  // comments, special characters in viking file format
  if ( buf == NULL || buf[0] == '\0' || buf[0] == '~' || buf[0] == '=' || buf[0] == '#' )
    return FALSE;

  if ( ! strchr ( buf, '=' ) )
    return FALSE;

  gchar **strv = g_strsplit ( buf, "=", 2 );

  gint gi = 0;
  gchar *str = strv[gi];
  while ( str ) {
	if ( gi == 0 )
	  *key = g_strdup ( str );
	else
	  *val = g_strdup ( str );
    gi++;
    str = strv[gi];
  }

  g_strfreev ( strv );

  // Remove newline from val and also any other whitespace
  *key = g_strstrip ( *key );
  *val = g_strstrip ( *val );
  return TRUE;
}

static GSList* deletion_list = NULL;

/**
 * util_add_to_deletion_list:
 *
 * Add a name of a file into the list that is to be deleted on program exit
 * Normally this is for files that get used asynchronously,
 *  so we don't know when it's time to delete them - other than at this program's end
 */
void util_add_to_deletion_list ( const gchar* filename )
{
	deletion_list = g_slist_append ( deletion_list, g_strdup (filename) );
}

/**
 * util_remove_all_in_deletion_list:
 *
 * Delete all the files in the deletion list
 * This should only be called on program exit
 */
void util_remove_all_in_deletion_list ( void )
{
	while ( deletion_list )
	{
		if ( g_remove ( (const char*)deletion_list->data ) )
			g_warning ( "%s: Failed to remove %s", __FUNCTION__, (char*)deletion_list->data );
		g_free ( deletion_list->data );
		deletion_list = g_slist_delete_link ( deletion_list, deletion_list );
	}
}

/**
 *  Removes characters from a string, in place.
 *
 *  @param string String to search.
 *  @param chars Characters to remove.
 *
 *  @return @a string - return value is only useful when nesting function calls, e.g.:
 *  @code str = utils_str_remove_chars(g_strdup("f_o_o"), "_"); @endcode
 *
 *  @see @c g_strdelimit.
 **/
gchar *util_str_remove_chars(gchar *string, const gchar *chars)
{
	const gchar *r;
	gchar *w = string;

	g_return_val_if_fail(string, NULL);
	if (G_UNLIKELY(EMPTY(chars)))
		return string;

	foreach_str(r, string)
	{
		if (!strchr(chars, *r))
			*w++ = *r;
	}
	*w = 0x0;
	return string;
}

/**
 * In 'extreme' debug mode don't remove temporary files
 *  thus the contents can be inspected if things go wrong
 *  with the trade off the user may need to delete tmp files manually
 * Only use this for 'occasional' downloaded temporary files that need interpretation,
 *  rather than large volume items such as Bing attributions.
 */
int util_remove ( const gchar *filename )
{
	if ( vik_debug && vik_verbose ) {
		g_warning ( "Not removing file: %s", filename );
		return 0;
	}
	else
		return g_remove ( filename );
}

/**
 * Stream write buffer to a temporary file (in one go)
 *
 * @param buffer The buffer to write
 * @param count Size of the buffer to write
 *
 * @return the filename of the buffer that was written
 */
gchar* util_write_tmp_file_from_bytes ( const void *buffer, gsize count )
{
	GFileIOStream *gios;
	GError *error = NULL;
	GFile *gf = g_file_new_tmp ( "vik-tmp.XXXXXX", &gios, &error );
	gchar *tmpname = g_file_get_path ( gf );
	GFile *file = g_file_new_for_path ( tmpname );
	gios = g_file_open_readwrite ( file, NULL, &error );
	g_object_unref ( file );
	if ( error ) {
		g_warning ( "%s", error->message );
		g_error_free ( error );
		return NULL;
	}

	GOutputStream *gos = g_io_stream_get_output_stream ( G_IO_STREAM(gios) );
	if ( g_output_stream_write ( gos, buffer, count, NULL, &error ) < 0 ) {
		g_critical ( "Couldn't write tmp %s file due to %s", tmpname, error->message );
		g_free (tmpname);
		tmpname = NULL;
	}

	g_output_stream_close ( gos, NULL, &error );
	g_object_unref ( gios );

	return tmpname;
}

/**
 * util_formatd:
 *
 * Convert a double to a string WITHOUT LOCALE in the specified format
 *
 * The returned value must be freed by g_free.
 */
gchar* util_formatd ( const gchar *format, gdouble dd )
{
  gchar *buffer = g_malloc(G_ASCII_DTOSTR_BUF_SIZE*sizeof(gchar));
  g_ascii_formatd (buffer, G_ASCII_DTOSTR_BUF_SIZE, format, dd);
  return buffer;
}

/**
 * util_make_absolute_filename:
 *
 * Returns a newly allocated string of the absolute filename or
 *   NULL if name is already absolute (or dirpath is unusable)
 */
gchar* util_make_absolute_filename ( const gchar *filename, const gchar *dirpath )
{
	if ( !dirpath ) return NULL;

	// Is it ready absolute?
	if ( g_path_is_absolute ( filename ) ) {
		return NULL;
	}
	else {
		// Otherwise create the absolute filename from the given directory and filename
		gchar *full = g_strconcat ( dirpath, G_DIR_SEPARATOR_S, filename, NULL );
		gchar *absolute = file_realpath_dup ( full ); // resolved into the canonical name
		g_free ( full );
		return absolute;
	}
}

/**
 * util_make_absolute_filenames:
 *
 * Ensures the supplied list of filenames are all absolute
 */
void util_make_absolute_filenames ( GList *filenames, const gchar *dirpath )
{
	if ( !filenames ) return;
	if ( !dirpath ) return;

	GList *gl;
	foreach_list ( gl, filenames ) {
		gchar *fn = util_make_absolute_filename ( gl->data, dirpath );
		if ( fn ) {
			g_free ( gl->data );
			gl->data = fn;
		}
	}
}

/**
 * Some systems don't have timegm()
 */
time_t util_timegm (struct tm *tm)
{
#ifdef _DEFAULT_SOURCE
	return timegm ( tm );
#else
	// Assumed tm is mostly valid
	// array access is constrained by use of '%'
	// Not bothered with leapseconds
	static const gint yeardays[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

	gint year = 1900 + tm->tm_year + tm->tm_mon / 12;
	gint leapdays = ((year - 1968) / 4) - ((year - 1900) / 100) + ((year - 1600) / 400);
	// First in days
	// Check if passed in year is a leapyear but not gone past the leapday
	if ( (year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0) && (tm->tm_mon % 12) < 2 )
		leapdays--;
	// Sum up days, hours, minutes & seconds
	time_t result = ((((year - 1970) * 365) + yeardays[tm->tm_mon % 12] + tm->tm_mday-1 + leapdays) * 24); // days
	result = ((result + tm->tm_hour ) * 60 ) + tm->tm_min; // minutes
	result = (result * 60) + tm->tm_sec; // seconds

	if ( tm->tm_isdst == 1 )
	  result -= 3600;

	return result;
#endif
}

/**
 * util_time_decompose:
 *
 * Returns for a given time period in seconds, the hours, minutes and seconds components
 */
void util_time_decompose ( gdouble total_seconds, guint *hours, guint *minutes, guint *seconds )
{
	*hours = total_seconds / 3600; // Note automatic truncation
	gdouble mins = (total_seconds - *hours*3600)/60.0;
	*minutes = (guint)trunc(mins);
	*seconds = (guint)round(total_seconds - *hours*3600 - *minutes*60);

	// Check for modular arithmetic
	if ( *seconds == 60 ) {
		*seconds = 0;
		(*minutes)++;
	}

	// Check for modular arithmetic
	if ( *minutes == 60 ) {
		*minutes = 0;
		(*hours)++;
	}
}

/**
 * util_is_url:
 *
 * See if a string URI starts with 'http:' or similar
 *
 */
gboolean util_is_url ( const gchar *str )
{
	if ( g_ascii_strncasecmp(str, "http://", 7) == 0 ||
	     g_ascii_strncasecmp(str, "https://", 8) == 0 ||
	     g_ascii_strncasecmp(str, "ftp://", 6) == 0 ) {
		return TRUE;
	}
	return FALSE;
}

// 'Hide' a string
gchar* util_frob ( gchar *str, guint ii )
{
	gchar *ptr = str;
	while ( ptr && (ii-- > 0) ) {
		if ( isalpha(*ptr) ) {
			if ( tolower(*ptr) < 'n' )
				*ptr += 13;
			else
				*ptr -= 13;
		}
		else if ( isdigit(*ptr) ) {
			if ( *ptr < '5' )
				*ptr += 5;
			else
				*ptr -= 5;
		}
		ptr++;
	}
	return str;
}

/**
 * util_glist_of_strings_compare:
 *
 * Returns: TRUE if they have the same list of string contents
 */
gboolean util_glist_of_strings_compare ( GList *aa, GList *bb )
{
  gboolean ans = TRUE;
  if ( g_list_length(aa) == g_list_length(bb) ) {
    while ( aa ) {
      if ( g_strcmp0((gchar*)aa->data, (gchar*)bb->data) != 0 ) {
        ans = FALSE;
        break;
      }
      aa = aa->next;
      bb = bb->next;
    }
  }
  else
    // Must be different if different lengths
    ans = FALSE;

  return ans;
}

// NB directly comparing "NaN != NaN" results in true!!
//  so need to check isnan() first
//  and further directly comparing floating point numbers is not wise
// Not forgetting that any one value being NAN, but the other isn't
//  should be marked as different
gboolean util_gdouble_different ( gdouble aa, gdouble bb )
{
  static const gdouble TOL = 0.000005; // Tolerance
  return ( (isnan(aa) && !isnan(bb)) || (!isnan(aa) && isnan(bb)) ) ||
         ( ( !isnan(aa) || !isnan(bb) ) &&
             ( (aa < (bb - TOL)) || (aa > (bb + TOL)) ) );
}
