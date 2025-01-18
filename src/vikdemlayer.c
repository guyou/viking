/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2003-2005, Evan Battaglia <gtoevan@gmx.net>
 * Copyright (c) 2020, Rob Norris <rw_norris@hotmail.com>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include "background.h"
#include "viking.h"
#include "vikmapslayer.h"
#include "vikdemlayer.h"
#include "dem.h"
#include "dems.h"
#include "bbox.h"

#define DEM_FIXED_NAME "DEM"
#define MAPS_CACHE_DIR maps_layer_default_dir()
#define SRTM_CACHE_TEMPLATE "%ssrtm3-%s%s%c%02d%c%03d.hgt.zip"
// Legacy USGS location that stopped in 2021
//#define SRTM_HTTP_BASE_URL "https://dds.cr.usgs.gov/srtm/version2_1/SRTM3"

// Now no longer uses the continent scheme & uses extra stuff in the filename
// <url>/N12E034.SRTMGL1.hgt.zip
#define SRTM_HTTP_BASE_URL "https://e4ftl01.cr.usgs.gov/MEASURES/SRTMGL1.003/2000.02.11"
// And needs at least http username:password authentication and cookie handling to persist session across redirects
// https://wiki.earthdata.nasa.gov/display/EL/How+To+Access+Data+With+cURL+And+Wget
// At time of writing, cURL command in wiki is not complete as it also requires '--location-trusted'
//  (libcurl equivalent CURLOPT_UNRESTRICTED_AUTH)

// Potentially can use OAuth, but unclear how to register application

// A different alternative - which uses a different server layout - 'DEM_SCHEME_LATITUDE'
//#define SRTM_HTTP_BASE_URL "https://bailu.ch/dem3"

static gchar *base_url = NULL;
#define VIK_SETTINGS_SRTM_HTTP_BASE_URL "srtm_http_base_url"

#ifdef VIK_CONFIG_DEM24K
#define DEM24K_DOWNLOAD_SCRIPT "dem24k.pl"
#endif

#define UNUSED_LINE_THICKNESS 3

static VikDEMLayer *dem_layer_new ( VikViewport *vvp );
static void dem_layer_draw ( VikDEMLayer *vdl, VikViewport *vp );
static void dem_layer_free ( VikDEMLayer *vdl );
static VikDEMLayer *dem_layer_create ( VikViewport *vp );
static const gchar* dem_layer_tooltip( VikDEMLayer *vdl );
static void dem_layer_marshall( VikDEMLayer *vdl, guint8 **data, guint *len );
static VikDEMLayer *dem_layer_unmarshall( guint8 *data, guint len, VikViewport *vvp );
static gboolean dem_layer_set_param ( VikDEMLayer *vdl, VikLayerSetParam *vlsp );
static VikLayerParamData dem_layer_get_param ( VikDEMLayer *vdl, guint16 id, gboolean is_file_operation );
static void dem_layer_change_param ( GtkWidget *widget, ui_change_values values );
static void dem_layer_post_read ( VikDEMLayer *vdl, VikViewport *vp, gboolean from_file );
static void srtm_draw_existence ( VikViewport *vp );
static void dem_layer_apply_colors ( VikDEMLayer *vdl );

#ifdef VIK_CONFIG_DEM24K
static void dem24k_draw_existence ( VikViewport *vp );
#endif

/* Upped upper limit incase units are feet */
static VikLayerParamScale param_scales[] = {
  { -100, 30000, 10, 1 },
  { 0, 30000, 10, 1 },
  { 0, 255, 3, 0 }, // alpha
};

static gchar *params_source[] = {
	N_("SRTM Global 90m (3 arcsec)"),
#ifdef VIK_CONFIG_DEM24K
	"USA 10m (USGS 24k)",
#endif
	NULL
	};

static gchar *params_type[] = {
	N_("Absolute height"),
	N_("Height gradient"),
	NULL
};

enum { DEM_SOURCE_SRTM,
#ifdef VIK_CONFIG_DEM24K
       DEM_SOURCE_DEM24K,
#endif
     };

enum { DEM_TYPE_HEIGHT = 0,
       DEM_TYPE_GRADIENT,
       DEM_TYPE_NONE,
};

typedef enum {
  DEM_CS_DEFAULT = 0,
  DEM_CS_DELINEATE,
} dem_color_style_type;

typedef enum {
  DEM_SCHEME_NONE = 0,
  DEM_SCHEME_LATITUDE,
  DEM_SCHEME_CONTINENT,
} dem_dir_scheme_type;

// Potentially could try to make this more flexible/freeform
typedef enum {
  DEM_FILENAME_SRTMGL1 = 0,
  DEM_FILENAME_NORMAL,
} dem_filename_style_type;

static VikLayerParamData color_default ( void ) {
  VikLayerParamData data; gdk_color_parse ( "blue", &data.c ); return data;
}

// Height defaults
static VikLayerParamData color_min_default ( void ) {
  VikLayerParamData data; gdk_color_parse ( "#9b793c", &data.c ); return data;
}

static VikLayerParamData color_max_default ( void ) {
  VikLayerParamData data; gdk_color_parse ( "#ffffff", &data.c ); return data;
}

static VikLayerParamData source_default ( void ) { return VIK_LPD_UINT ( DEM_SOURCE_SRTM ); }
static VikLayerParamData type_default ( void ) { return VIK_LPD_UINT ( DEM_TYPE_HEIGHT ); }
static VikLayerParamData min_elev_default ( void ) { return VIK_LPD_DOUBLE ( 0.0 ); }
static VikLayerParamData max_elev_default ( void ) { return VIK_LPD_DOUBLE ( 1000.0 ); }
static VikLayerParamData color_scheme_default ( void ) { return VIK_LPD_UINT ( DEM_CS_DEFAULT ); }
static VikLayerParamData alpha_default ( void ) { return VIK_LPD_UINT ( 255 ); }

static VikLayerParamData dir_scheme_default ( void ) { return VIK_LPD_UINT ( DEM_SCHEME_NONE ); }
static VikLayerParamData filename_style_default ( void ) { return VIK_LPD_UINT ( DEM_FILENAME_SRTMGL1 ); }

static gchar *params_groups[] = { N_("Drawing"), N_("Download"), N_("Files") };
typedef enum { GROUP_DRAWING, GROUP_DOWNLOAD, GROUP_FILES } DEM_groups_t;

static VikLayerParamData url_default ( void )
{
  VikLayerParamData data; data.s = g_strdup ( base_url ); return data;
}

static VikLayerParamData no_files_default ( void )
{
  VikLayerParamData data; data.sl = NULL; return data;
}

static void reset_cb ( GtkWidget *widget, gpointer ptr )
{
  a_layer_defaults_reset_show ( DEM_FIXED_NAME, ptr, GROUP_FILES );
  a_layer_defaults_reset_show ( DEM_FIXED_NAME, ptr, GROUP_DRAWING );
  a_layer_defaults_reset_show ( DEM_FIXED_NAME, ptr, GROUP_DOWNLOAD );
}
static VikLayerParamData reset_default ( void ) { return VIK_LPD_PTR(reset_cb); }

static gchar *params_color_schemes[] = { N_("Default"), N_("Delineate"), NULL };
static gchar *params_dir_schemes[] = { N_("None"), N_("Latitude"), N_("Continent"), NULL }; // dem_dir_scheme_type
static gchar *params_filename_styles[] = { N_("SRTMGL1"), N_("Normal"), NULL }; // dem_filename_style_type

static VikLayerParam dem_layer_params[] = {
  { VIK_LAYER_DEM, "files", VIK_LAYER_PARAM_STRING_LIST, GROUP_FILES, N_("DEM Files:"), VIK_LAYER_WIDGET_FILELIST, NULL, NULL, NULL, no_files_default, NULL, NULL },
  { VIK_LAYER_DEM, "source", VIK_LAYER_PARAM_UINT, GROUP_DOWNLOAD, N_("Download Source:"), VIK_LAYER_WIDGET_RADIOGROUP_STATIC, params_source, NULL, NULL, source_default, NULL, NULL },
  { VIK_LAYER_DEM, "srtm_url_base", VIK_LAYER_PARAM_STRING, GROUP_DOWNLOAD, N_("Base URL:"), VIK_LAYER_WIDGET_ENTRY, NULL, NULL, NULL, url_default, NULL, NULL },
  { VIK_LAYER_DEM, "srtm_server_dir_scheme", VIK_LAYER_PARAM_UINT, GROUP_DOWNLOAD, N_("Layout:"), VIK_LAYER_WIDGET_COMBOBOX, params_dir_schemes, NULL, NULL, dir_scheme_default, NULL, NULL },
  { VIK_LAYER_DEM, "srtm_server_filename_style", VIK_LAYER_PARAM_UINT, GROUP_DOWNLOAD, N_("Filename Convention:"), VIK_LAYER_WIDGET_COMBOBOX, params_filename_styles, NULL, NULL, filename_style_default, NULL, NULL },
  { VIK_LAYER_DEM, "color", VIK_LAYER_PARAM_COLOR, GROUP_DRAWING, N_("Min Elev Color:"), VIK_LAYER_WIDGET_COLOR, NULL, NULL, NULL, color_default, NULL, NULL },
  { VIK_LAYER_DEM, "color_scheme", VIK_LAYER_PARAM_UINT, GROUP_DRAWING, N_("Color Scheme:"), VIK_LAYER_WIDGET_COMBOBOX, params_color_schemes, NULL, NULL, color_scheme_default, NULL, NULL },
  { VIK_LAYER_DEM, "color_min", VIK_LAYER_PARAM_COLOR, GROUP_DRAWING, N_("Start Color:"), VIK_LAYER_WIDGET_COLOR, NULL, NULL, NULL, color_min_default, NULL, NULL },
  { VIK_LAYER_DEM, "color_max", VIK_LAYER_PARAM_COLOR, GROUP_DRAWING, N_("End Color:"), VIK_LAYER_WIDGET_COLOR, NULL, NULL, NULL, color_max_default, NULL, NULL },
  { VIK_LAYER_DEM, "type", VIK_LAYER_PARAM_UINT, GROUP_DRAWING, N_("Type:"), VIK_LAYER_WIDGET_RADIOGROUP_STATIC, params_type, NULL, NULL, type_default, NULL, NULL },
  { VIK_LAYER_DEM, "min_elev", VIK_LAYER_PARAM_DOUBLE, GROUP_DRAWING, N_("Min Elev:"), VIK_LAYER_WIDGET_SPINBUTTON, param_scales + 0, NULL, NULL, min_elev_default, NULL, NULL },
  { VIK_LAYER_DEM, "max_elev", VIK_LAYER_PARAM_DOUBLE, GROUP_DRAWING, N_("Max Elev:"), VIK_LAYER_WIDGET_SPINBUTTON, param_scales + 0, NULL, NULL, max_elev_default, NULL, NULL },
  { VIK_LAYER_DEM, "alpha", VIK_LAYER_PARAM_UINT, GROUP_DRAWING, N_("Alpha:"), VIK_LAYER_WIDGET_HSCALE, param_scales+2, NULL,
    N_("Control the Alpha value for transparency effects"), alpha_default, NULL, NULL },
  { VIK_LAYER_DEM, "reset", VIK_LAYER_PARAM_PTR_DEFAULT, VIK_LAYER_GROUP_NONE, NULL,
    VIK_LAYER_WIDGET_BUTTON, N_("Reset to Defaults"), NULL, NULL, reset_default, NULL, NULL },
};

// ENUMERATION MUST BE IN THE SAME ORDER AS THE NAMED PARAMS ABOVE
enum {
      PARAM_FILES=0,
      // Download options
      PARAM_SOURCE,
      PARAM_SRTM_BASE_URL,
      PARAM_SVR_DIR_SCHEME,
      PARAM_SVR_FILENAME_STYLE,
      // Drawing options
      PARAM_COLOR,
      PARAM_COLOR_SCHEME,
      PARAM_COLOR_MIN,
      PARAM_COLOR_MAX,
      PARAM_TYPE,
      PARAM_MIN_ELEV,
      PARAM_MAX_ELEV,
      PARAM_ALPHA,
      PARAM_RESET,
      NUM_PARAMS
};

static gpointer dem_layer_download_create ( VikWindow *vw, VikViewport *vvp);
static VikLayerToolFuncStatus dem_layer_download_release ( VikDEMLayer *vdl, GdkEventButton *event, VikViewport *vvp );
static VikLayerToolFuncStatus dem_layer_download_click ( VikDEMLayer *vdl, GdkEventButton *event, VikViewport *vvp );

static VikToolInterface dem_tools[] = {
  { "demdl_18",
    { "DEMDownload", "demdl_18", N_("_DEM Download"), NULL, N_("DEM Download"), 0 },
    (VikToolConstructorFunc) dem_layer_download_create, NULL, NULL, NULL,
    (VikToolMouseFunc) dem_layer_download_click, NULL,  (VikToolMouseFunc) dem_layer_download_release,
    (VikToolKeyFunc) NULL,
    (VikToolKeyFunc) NULL,
    FALSE,
    GDK_CURSOR_IS_PIXMAP, "cursor_demdl", NULL },
};


/* HEIGHT COLORS (default profile)
   The shaded from brown to white are used to give an indication of height.
*/
static gchar *dem_height_colors[] = {
"#9b793c", "#9c7d40", "#9d8144", "#9e8549", "#9f894d", "#a08d51", "#a29156", "#a3955a", "#a4995e", "#a69d63",
"#a89f65", "#aaa267", "#ada569", "#afa76b", "#b1aa6d", "#b4ad6f", "#b6b071", "#b9b373", "#bcb676", "#beb978",
"#c0bc7a", "#c2c07d", "#c4c37f", "#c6c681", "#c8ca84", "#cacd86", "#ccd188", "#cfd58b", "#c2ce84", "#b5c87e",
"#a9c278", "#9cbb71", "#8fb56b", "#83af65", "#76a95e", "#6aa358", "#5e9d52", "#63a055", "#69a458", "#6fa85c",
"#74ac5f", "#7ab063", "#80b467", "#86b86a", "#8cbc6e", "#92c072", "#94c175", "#97c278", "#9ac47c", "#9cc57f",
"#9fc682", "#a2c886", "#a4c989", "#a7cb8d", "#aacd91", "#afce99", "#b5d0a1", "#bbd2aa", "#c0d3b2", "#c6d5ba",
"#ccd7c3", "#d1d9cb", "#d7dbd4", "#DDDDDD", "#e0e0e0", "#e4e4e4", "#e8e8e8", "#ebebeb", "#efefef", "#f3f3f3",
"#f7f7f7", "#fbfbfb", "#ffffff"
};

static const guint DEM_N_HEIGHT_COLORS = sizeof(dem_height_colors)/sizeof(dem_height_colors[0]);

/*
"#9b793c", "#9e8549", "#a29156", "#a69d63", "#ada569", "#b4ad6f", "#bcb676", "#c2c07d", "#c8ca84", "#cfd58b",
"#a9c278", "#83af65", "#5e9d52", "#6fa85c", "#80b467", "#92c072", "#9ac47c", "#a2c886", "#aacd91", "#bbd2aa",
"#ccd7c3", "#DDDDDD", "#e8e8e8", "#f3f3f3", "#FFFFFF"
};
*/

static gchar *dem_gradient_colors[] = {
"#AAAAAA",
"#000000", "#000011", "#000022", "#000033", "#000044", "#00004c", "#000055", "#00005d", "#000066", "#00006e",
"#000077", "#00007f", "#000088", "#000090", "#000099", "#0000a1", "#0000aa", "#0000b2", "#0000bb", "#0000c3",
"#0000cc", "#0000d4", "#0000dd", "#0000e5", "#0000ee", "#0000f6", "#0000ff", "#0008f7", "#0011ee", "#0019e6",
"#0022dd", "#002ad5", "#0033cc", "#003bc4", "#0044bb", "#004cb3", "#0055aa", "#005da2", "#006699", "#006e91",
"#007788", "#007f80", "#008877", "#00906f", "#009966", "#00a15e", "#00aa55", "#00b24d", "#00bb44", "#00c33c",
"#00cc33", "#00d42b", "#00dd22", "#00e51a", "#00ee11", "#00f609", "#00ff00", "#08f700", "#11ee00", "#19e600",
"#22dd00", "#2ad500", "#33cc00", "#3bc400", "#44bb00", "#4cb300", "#55aa00", "#5da200", "#669900", "#6e9100",
"#778800", "#7f8000", "#887700", "#906f00", "#996600", "#a15e00", "#aa5500", "#b24d00", "#bb4400", "#c33c00",
"#cc3300", "#d42b00", "#dd2200", "#e51a00", "#ee1100", "#f60900", "#ff0000",
"#FFFFFF"
};

static const guint DEM_N_GRADIENT_COLORS = sizeof(dem_gradient_colors)/sizeof(dem_gradient_colors[0]);


VikLayerInterface vik_dem_layer_interface = {
  DEM_FIXED_NAME,
  N_("DEM"),
  "<control><shift>D",
  "vikdemlayer", // Icon name

  dem_tools,
  G_N_ELEMENTS(dem_tools),

  dem_layer_params,
  NUM_PARAMS,
  params_groups,
  G_N_ELEMENTS(params_groups),

  VIK_MENU_ITEM_ALL,

  (VikLayerFuncCreate)                  dem_layer_create,
  (VikLayerFuncGetNewName)              NULL,
  (VikLayerFuncRealize)                 NULL,
  (VikLayerFuncPostRead)                dem_layer_post_read,
  (VikLayerFuncFree)                    dem_layer_free,

  (VikLayerFuncProperties)              NULL,
  (VikLayerFuncDraw)                    dem_layer_draw,
  (VikLayerFuncConfigure)               NULL,
  (VikLayerFuncChangeCoordMode)         NULL,

  (VikLayerFuncGetTimestamp)            NULL,

  (VikLayerFuncSetMenuItemsSelection)   NULL,
  (VikLayerFuncGetMenuItemsSelection)   NULL,

  (VikLayerFuncAddMenuItems)            NULL,
  (VikLayerFuncSublayerAddMenuItems)    NULL,

  (VikLayerFuncSublayerRenameRequest)   NULL,
  (VikLayerFuncSublayerToggleVisible)   NULL,
  (VikLayerFuncSublayerTooltip)         NULL,
  (VikLayerFuncLayerTooltip)            dem_layer_tooltip,
  (VikLayerFuncLayerSelected)           NULL,
  (VikLayerFuncLayerToggleVisible)      NULL,

  (VikLayerFuncMarshall)		dem_layer_marshall,
  (VikLayerFuncUnmarshall)		dem_layer_unmarshall,

  (VikLayerFuncSetParam)                dem_layer_set_param,
  (VikLayerFuncGetParam)                dem_layer_get_param,
  (VikLayerFuncChangeParam)             dem_layer_change_param,

  (VikLayerFuncReadFileData)            NULL,
  (VikLayerFuncWriteFileData)           NULL,

  (VikLayerFuncDeleteItem)              NULL,
  (VikLayerFuncCutItem)                 NULL,
  (VikLayerFuncCopyItem)                NULL,
  (VikLayerFuncPasteItem)               NULL,
  (VikLayerFuncFreeCopiedItem)          NULL,
  (VikLayerFuncDragDropRequest)		NULL,

  (VikLayerFuncSelectClick)             NULL,
  (VikLayerFuncSelectMove)              NULL,
  (VikLayerFuncSelectRelease)           NULL,
  (VikLayerFuncSelectedViewportMenu)    NULL,

  (VikLayerFuncRefresh)                 NULL,
};

struct _VikDEMLayer {
  VikLayer vl;
  GList *files;
  gdouble min_elev;
  gdouble max_elev;
  GdkColor color;
  GdkColor color_min;
  GdkColor color_max;
  guint color_scheme;
  guint source;
  guint type;
  guint alpha;

  gchar *srtm_base_url;
  // Server side only
  dem_dir_scheme_type dir_scheme;
  dem_filename_style_type filename_style;
  // ATM we always use the CONTINENT style for local disk storage

  GdkColor *height_colors;
  GdkColor *gradient_colors;

  guchar *pixels;

  // right click menu only stuff - similar to mapslayer
  GtkMenu *right_click_menu;
};

#define VIKING_DEM_PARAMS_GROUP_KEY "dem_srtm"
#define VIKING_DEM_PARAMS_NAMESPACE "dem_srtm."

#define DEM_USERNAME VIKING_DEM_PARAMS_NAMESPACE"username"
#define DEM_PASSWORD VIKING_DEM_PARAMS_NAMESPACE"password"

static VikLayerParam prefs[] = {
  { VIK_LAYER_NUM_TYPES, DEM_USERNAME, VIK_LAYER_PARAM_STRING, VIK_LAYER_GROUP_NONE, N_("Username:"), VIK_LAYER_WIDGET_ENTRY, NULL, NULL, N_("HTTP Basic Authorization"), NULL, NULL, NULL },
  { VIK_LAYER_NUM_TYPES, DEM_PASSWORD, VIK_LAYER_PARAM_STRING, VIK_LAYER_GROUP_NONE, N_("Password:"), VIK_LAYER_WIDGET_PASSWORD, NULL, NULL, NULL, NULL, NULL, NULL },
};

/**
 * Very early initialization, even before vik_dem_class_init()
 * Thus values available for the layer initialization
 */
void vik_dem_layer_init ()
{
  // Preferences
  a_preferences_register_group ( VIKING_DEM_PARAMS_GROUP_KEY, _("DEM Server") );
  guint ii = 0;
  VikLayerParamData tmp;
  tmp.s = NULL;
  a_preferences_register ( &prefs[ii++], tmp, VIKING_DEM_PARAMS_GROUP_KEY );
  a_preferences_register ( &prefs[ii++], tmp, VIKING_DEM_PARAMS_GROUP_KEY );

  // Note if suppling your own base URL - the site must still follow the Continent directory layout
  if ( ! a_settings_get_string ( VIK_SETTINGS_SRTM_HTTP_BASE_URL, &base_url ) ) {
    // Otherwise use the default
    base_url = g_strdup ( SRTM_HTTP_BASE_URL );
  }

}

void vik_dem_layer_uninit ()
{
  g_free ( base_url );
}

static GdkColor black_color;

// NB Only performed once per program run
static void vik_dem_class_init ( VikDEMLayerClass *klass )
{
  gdk_color_parse ( "#000000", &black_color );
}

#define VIK_SETTINGS_DEM_USERNAME "dem_basic_auth_username"
#define VIK_SETTINGS_DEM_PASSWORD "dem_basic_auth_password"

// Free after use
static gchar *dem_get_login ( VikDEMLayer *vdl )
{
  VikLayerParamData *pref_user = a_preferences_get ( DEM_USERNAME );
  VikLayerParamData *pref_password = a_preferences_get ( DEM_PASSWORD );

  gchar *up = NULL;
  if ( pref_user && pref_user->s && pref_user->s[0] != '\0') {
    if ( pref_password && pref_password->s ) {
      up = g_strdup_printf ( "%s:%s", pref_user->s, pref_password->s );
    }
  }
  return up;
}

GType vik_dem_layer_get_type ()
{
  static GType vdl_type = 0;

  if (!vdl_type)
  {
    static const GTypeInfo vdl_info =
    {
      sizeof (VikDEMLayerClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) vik_dem_class_init, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (VikDEMLayer),
      0,
      NULL /* instance init */
    };
    vdl_type = g_type_register_static ( VIK_LAYER_TYPE, "VikDEMLayer", &vdl_info, 0 );
  }

  return vdl_type;
}

static const gchar* dem_layer_tooltip( VikDEMLayer *vdl )
{
  static gchar tmp_buf[100];
  g_snprintf (tmp_buf, sizeof(tmp_buf), _("Number of files: %d"), g_list_length (vdl->files));
  return tmp_buf;
}

static void dem_layer_marshall( VikDEMLayer *vdl, guint8 **data, guint *len )
{
  vik_layer_marshall_params ( VIK_LAYER(vdl), data, len );
}

static VikDEMLayer *dem_layer_unmarshall( guint8 *data, guint len, VikViewport *vvp )
{
  VikDEMLayer *rv = dem_layer_new ( vvp );

  vik_layer_unmarshall_params ( VIK_LAYER(rv), data, len, vvp );

  dem_layer_apply_colors ( rv );

  return rv;
}

/* Structure for DEM data used in background thread */
typedef struct {
  VikDEMLayer *vdl;
} dem_load_thread_data;

/*
 * Function for starting the DEM file loading as a background thread
 */
static int dem_layer_load_list_thread ( dem_load_thread_data *dltd, gpointer threaddata )
{
  int result = 0; // Default to good
  // Actual Load
  if ( a_dems_load_list ( &(dltd->vdl->files), threaddata ) ) {
    // Thread cancelled
    result = -1;
  }

  // ATM as each file is processed the screen is not updated (no mechanism exposed to a_dems_load_list)
  // Thus force draw only at the end, as loading is complete/aborted
  // Test is helpful to prevent Gtk-CRITICAL warnings if the program is exitted whilst loading
  if ( IS_VIK_LAYER(dltd->vdl) )
    vik_layer_emit_update ( VIK_LAYER(dltd->vdl), FALSE ); // NB update requested from background thread

  return result;
}

static void dem_layer_thread_data_free ( dem_load_thread_data *data )
{
  // Simple release
  g_free ( data );
}

static void dem_layer_thread_cancel ( dem_load_thread_data *data )
{
  // Abort loading
  // Instead of freeing the list, leave it as partially processed
  // Thus we can see/use what was done
}

/**
 * Process the list of DEM files and convert each one to a relative path
 */
static GList *dem_layer_convert_to_relative_filenaming ( GList *files )
{
  gchar *cwd = g_get_current_dir();
  if ( !cwd )
    return files;

  GList *relfiles = NULL;

  while ( files ) {
    gchar *file = g_strdup ( file_GetRelativeFilename ( cwd, files->data ) );
    relfiles = g_list_prepend ( relfiles, file );
    files = files->next;
  }

  g_free ( cwd );

  if ( relfiles ) {
    // Replacing current list, so delete old values first.
    GList *iter = files;
    while ( iter ) {
      g_free ( iter->data );
      iter = iter->next;
    }
    g_list_free ( files );

    return relfiles;
  }

  return files;
}

gboolean dem_layer_set_param ( VikDEMLayer *vdl, VikLayerSetParam *vlsp )
{
  gdouble oldd;
  gboolean changed = FALSE;

  switch ( vlsp->id )
  {
    case PARAM_COLOR:
      changed = vik_layer_param_change_color ( vlsp->data, &vdl->color );
      break;
    case PARAM_COLOR_SCHEME:
      if ( vlsp->data.u < G_N_ELEMENTS(params_color_schemes) )
        changed = vik_layer_param_change_uint ( vlsp->data, &vdl->color_scheme );
      else
        g_warning ( "%s: Unknown color scheme", __FUNCTION__ );
      break;
    case PARAM_COLOR_MIN:
      changed = vik_layer_param_change_color ( vlsp->data, &vdl->color_min );
      break;
    case PARAM_COLOR_MAX:
      changed = vik_layer_param_change_color ( vlsp->data, &vdl->color_max );
      break;
    case PARAM_SOURCE:
      changed = vik_layer_param_change_uint ( vlsp->data, &vdl->source );
      break;
    case PARAM_SRTM_BASE_URL:
      changed = vik_layer_param_change_string ( vlsp->data, &vdl->srtm_base_url );
      break;
    case PARAM_SVR_DIR_SCHEME:
      if ( vlsp->data.u < G_N_ELEMENTS(params_dir_schemes) )
        changed = vik_layer_param_change_uint ( vlsp->data, &vdl->dir_scheme );
      else
        g_warning ( "%s: Unknown dir scheme", __FUNCTION__ );
      break;
    case PARAM_SVR_FILENAME_STYLE:
      if ( vlsp->data.u < G_N_ELEMENTS(params_filename_styles) )
        changed = vik_layer_param_change_uint ( vlsp->data, &vdl->filename_style );
      else
        g_warning ( "%s: Unknown filename style", __FUNCTION__ );
      break;
    case PARAM_TYPE:
      changed = vik_layer_param_change_uint ( vlsp->data, &vdl->type );
      break;
    case PARAM_MIN_ELEV:
      oldd = vdl->min_elev;
      /* Convert to store internally
         NB file operation always in internal units (metres) */
      if (!vlsp->is_file_operation && a_vik_get_units_height () == VIK_UNITS_HEIGHT_FEET )
        vdl->min_elev = VIK_FEET_TO_METERS(vlsp->data.d);
      else
        vdl->min_elev = vlsp->data.d;
      changed = util_gdouble_different ( oldd, vdl->min_elev );
      break;
    case PARAM_MAX_ELEV:
      oldd = vdl->max_elev;
      /* Convert to store internally
         NB file operation always in internal units (metres) */
      if (!vlsp->is_file_operation && a_vik_get_units_height () == VIK_UNITS_HEIGHT_FEET )
        vdl->max_elev = VIK_FEET_TO_METERS(vlsp->data.d);
      else
        vdl->max_elev = vlsp->data.d;
      changed = util_gdouble_different ( oldd, vdl->max_elev );
      break;
    case PARAM_ALPHA:
      if ( vlsp->data.u <= 255 )
        changed = vik_layer_param_change_uint ( vlsp->data, &vdl->alpha );
      // Note since dem_layer_set_param() will be called for every parameter,
      //  only need to change colors once and this is the last associated colour parameter
      if ( !vlsp->is_file_operation )
        dem_layer_apply_colors ( vdl );
      break;
    case PARAM_FILES:
    {
      // If no change in the DEMs used, we can skip reloading them again
      if ( util_glist_of_strings_compare(vdl->files, vlsp->data.sl) )
        break;

      changed = TRUE;
      // Clear out old settings - if any commonalities with new settings they will have to be read again
      a_dems_list_free ( vdl->files );

      // Set file list so any other intermediate screen drawing updates will show currently loaded DEMs by the working thread
      vdl->files = vlsp->data.sl;
      // Ensure resolving of any relative path names
      util_make_absolute_filenames ( vdl->files, vlsp->dirpath );

      // No need for thread if no files
      if ( vdl->files ) {
        // Thread Load
        dem_load_thread_data *dltd = g_malloc ( sizeof(dem_load_thread_data) );
        dltd->vdl = vdl;

        a_background_thread ( BACKGROUND_POOL_LOCAL,
                              VIK_GTK_WINDOW_FROM_WIDGET(vlsp->vp),
                              _("DEM Loading"),
                              (vik_thr_func) dem_layer_load_list_thread,
                              dltd,
                              (vik_thr_free_func) dem_layer_thread_data_free,
                              (vik_thr_free_func) dem_layer_thread_cancel,
                              g_list_length ( vlsp->data.sl ) ); // Number of DEM files
      }
      break;
    }
    default: break;
  }
  if ( vik_debug && changed )
    g_debug ( "%s: Detected change on param %d", __FUNCTION__, vlsp->id );
  return changed;
}

static VikLayerParamData dem_layer_get_param ( VikDEMLayer *vdl, guint16 id, gboolean is_file_operation )
{
  VikLayerParamData rv;
  switch ( id )
  {
    case PARAM_FILES:
      rv.sl = vdl->files;
      if ( is_file_operation )
        // Save in relative format if necessary
        if ( a_vik_get_file_ref_format() == VIK_FILE_REF_FORMAT_RELATIVE )
          rv.sl = dem_layer_convert_to_relative_filenaming ( rv.sl );
      break;
    case PARAM_SOURCE: rv.u = vdl->source; break;
    case PARAM_SRTM_BASE_URL:
      rv.s = vdl->srtm_base_url;
      if ( !is_file_operation ) {
        if ( g_strcmp0(vdl->srtm_base_url, SRTM_HTTP_BASE_URL) == 0 ) {
          gchar *user_pass = dem_get_login ( vdl );
          if ( !user_pass ) {
            if ( a_dialog_yes_or_no_suppress ( NULL, //VIK_GTK_WINDOW_FROM_LAYER(vdl),
                                               _("To use the server %s,\n \
you must register with the service and provide those login details in the Viking's preferences for the DEM Server.\n \
Go to the registration website now?"), vdl->srtm_base_url) )
              open_url ( NULL, "https://urs.earthdata.nasa.gov" );
          } else
            g_free ( user_pass );
        }
      }
      break;
    case PARAM_SVR_DIR_SCHEME: rv.u = vdl->dir_scheme; break;
    case PARAM_SVR_FILENAME_STYLE: rv.u = vdl->filename_style; break;
    case PARAM_TYPE: rv.u = vdl->type; break;
    case PARAM_COLOR: rv.c = vdl->color; break;
    case PARAM_COLOR_SCHEME: rv.u = vdl->color_scheme; break;
    case PARAM_COLOR_MIN: rv.c = vdl->color_min; break;
    case PARAM_COLOR_MAX: rv.c = vdl->color_max; break;
    case PARAM_MIN_ELEV:
      /* Convert for display in desired units
         NB file operation always in internal units (metres) */
      if (!is_file_operation && a_vik_get_units_height () == VIK_UNITS_HEIGHT_FEET )
        rv.d = VIK_METERS_TO_FEET(vdl->min_elev);
      else
        rv.d = vdl->min_elev;
      break;
    case PARAM_MAX_ELEV:
      /* Convert for display in desired units
         NB file operation always in internal units (metres) */
      if (!is_file_operation && a_vik_get_units_height () == VIK_UNITS_HEIGHT_FEET )
        rv.d = VIK_METERS_TO_FEET(vdl->max_elev);
      else
        rv.d = vdl->max_elev;
      break;
    case PARAM_ALPHA: rv.u = vdl->alpha; break;
    case PARAM_RESET: rv.ptr = reset_cb; break;
    default: break;
  }
  return rv;
}

static void dem_layer_change_param ( GtkWidget *widget, ui_change_values values )
{
  switch ( GPOINTER_TO_INT(values[UI_CHG_PARAM_ID]) ) {
    // Alter sensitivity of color widgets according to the scheme setting
  case PARAM_COLOR_SCHEME: {
    // Get new value
    VikLayerParamData vlpd = a_uibuilder_widget_get_value ( widget, values[UI_CHG_PARAM] );

    gboolean sensitive = (vlpd.u == DEM_CS_DELINEATE);
    GtkWidget **ww1 = values[UI_CHG_WIDGETS];
    GtkWidget **ww2 = values[UI_CHG_LABELS];
    GtkWidget *w1 = ww1[PARAM_COLOR_MIN];
    GtkWidget *w2 = ww2[PARAM_COLOR_MIN];
    GtkWidget *w3 = ww1[PARAM_COLOR_MAX];
    GtkWidget *w4 = ww2[PARAM_COLOR_MAX];

    if ( w1 ) gtk_widget_set_sensitive ( w1, sensitive );
    if ( w2 ) gtk_widget_set_sensitive ( w2, sensitive );
    if ( w3 ) gtk_widget_set_sensitive ( w3, sensitive );
    if ( w4 ) gtk_widget_set_sensitive ( w4, sensitive );

    break;
  }

  default: break;
  }
}

static void dem_layer_apply_colors ( VikDEMLayer *vdl )
{
  GdkColor color;
  if ( vdl->color_scheme == DEM_CS_DEFAULT ) {

    for ( guint ii = 0; ii < DEM_N_HEIGHT_COLORS; ii++ ) {
      gdk_color_parse ( dem_height_colors[ii], &color );
      vdl->height_colors[ii] = color;
    }

    for ( guint ii = 0; ii < DEM_N_GRADIENT_COLORS; ii++ ) {
      gdk_color_parse ( dem_gradient_colors[ii], &color );
      vdl->gradient_colors[ii] = color;
    }
  }
  else {
    // Color Scheme == DEM_DELINEATE
    color = vdl->color_min;
    gdouble r_inc, g_inc, b_inc;
    r_inc = ((gdouble)vdl->color_max.red - (gdouble)vdl->color_min.red) / DEM_N_HEIGHT_COLORS;
    g_inc = ((gdouble)vdl->color_max.green - (gdouble)vdl->color_min.green) / DEM_N_HEIGHT_COLORS;
    b_inc = ((gdouble)vdl->color_max.blue - (gdouble)vdl->color_min.blue) / DEM_N_HEIGHT_COLORS;

    for ( guint ii = 0; ii < DEM_N_HEIGHT_COLORS; ii++ ) {
      color.red += (int)r_inc;
      color.green += (int)g_inc;
      color.blue += (int)b_inc;
      vdl->height_colors[ii] = color;
    }

    color = vdl->color_min;
    r_inc = ((gdouble)vdl->color_max.red - (gdouble)vdl->color_min.red) / DEM_N_GRADIENT_COLORS;
    g_inc = ((gdouble)vdl->color_max.green - (gdouble)vdl->color_min.green) / DEM_N_GRADIENT_COLORS;
    b_inc = ((gdouble)vdl->color_max.blue - (gdouble)vdl->color_min.blue) / DEM_N_GRADIENT_COLORS;

    for ( guint ii = 0; ii < DEM_N_GRADIENT_COLORS; ii++ ) {
      color.red += (int)r_inc;
      color.green += (int)g_inc;
      color.blue += (int)b_inc;
      vdl->gradient_colors[ii] = color;
    }
  }
}

static void dem_layer_post_read ( VikDEMLayer *vdl, VikViewport *vp, gboolean from_file )
{
  dem_layer_apply_colors ( vdl );
}

static VikDEMLayer *dem_layer_new ( VikViewport *vvp )
{
  VikDEMLayer *vdl = VIK_DEM_LAYER ( g_object_new ( VIK_DEM_LAYER_TYPE, NULL ) );

  vik_layer_set_type ( VIK_LAYER(vdl), VIK_LAYER_DEM );

  vdl->files = NULL;

  vdl->height_colors = g_malloc0 ( sizeof(GdkColor) * DEM_N_HEIGHT_COLORS );
  vdl->gradient_colors = g_malloc0 ( sizeof(GdkColor) * DEM_N_GRADIENT_COLORS );

  vik_layer_set_defaults ( VIK_LAYER(vdl), vvp );

  return vdl;
}


static inline guint16 get_height_difference(gint16 elev, gint16 new_elev)
{
  if(new_elev == VIK_DEM_INVALID_ELEVATION)
    return 0;
  else
    return abs(new_elev - elev);
}

// Only for RGBA layout (i.e. 4 bytes)
// A few manual calculation optimizations are applied
// No bounds checking here in writing to the pixels array,
//  it must be performed by the caller.
static inline void pixels_set_area ( guchar* pixels, GdkColor gcolor, guint alpha, guint width, guint box_x, guint box_y, guint box_width, guint box_height )
{
  guint width4 = width * 4;
  guint index = (width4 * box_y) + (box_x * 4);
  guchar rr = gcolor.red / 256;
  guchar gg = gcolor.green / 256;
  guchar bb = gcolor.blue / 256;

  for ( guint yy = 0; yy < box_height; yy++ ) {
    guint yindex = index + (yy * width4);
    for ( guint xx = 0; xx < box_width; xx++ ) {
      guint tindex = yindex + (xx * 4);
      pixels[tindex+0] = rr;
      pixels[tindex+1] = gg;
      pixels[tindex+2] = bb;
      pixels[tindex+3] = alpha;
    }
  }
}

static void vik_dem_layer_draw_dem ( VikDEMLayer *vdl, VikViewport *vp, VikDEM *dem )
{
  VikDEMColumn *column, *prevcolumn, *nextcolumn;

  LatLonBBox vp_bbox = vik_viewport_get_bbox ( vp );
  LatLonBBox dem_bbox = vik_dem_get_bbox ( dem );

  /**** Check if viewport and DEM data overlap ****/
  if ( ! BBOX_INTERSECT(dem_bbox, vp_bbox) ) {
    return;
  }

  const guint width = vik_viewport_get_width ( vp );
  const guint height = vik_viewport_get_height ( vp );

  /* boxes to show where we have DEM instead of actually drawing the DEM.
   * useful if we want to see what areas we have coverage for (if we want
   * to get elevation data for a track) but don't want to cover the map.
   */

  #if 0
  /* draw a box if a DEM is loaded. in future I'd like to add an option for this
   * this is useful if we want to see what areas we have dem for but don't want to
   * cover the map (or maybe we just need translucent DEM?) */
  {
    VikCoord demne, demsw;
    gint x1, y1, x2, y2;
    vik_coord_load_from_latlon(&demne, vik_viewport_get_coord_mode(vp), &dem_northeast);
    vik_coord_load_from_latlon(&demsw, vik_viewport_get_coord_mode(vp), &dem_southwest);

    vik_viewport_coord_to_screen ( vp, &demne, &x1, &y1 );
    vik_viewport_coord_to_screen ( vp, &demsw, &x2, &y2 );

    if ( x1 > vik_viewport_get_width(vp) ) x1=vik_viewport_get_width(vp);
    if ( y2 > vik_viewport_get_height(vp) ) y2=vik_viewport_get_height(vp);
    if ( x2 < 0 ) x2 = 0;
    if ( y1 < 0 ) y1 = 0;
    vik_viewport_draw_rectangle ( vp, vik_viewport_get_black_gc(vp), FALSE, x2, y1, x1-x2, y2-y1 );
    return;
  }
  #endif

  if ( dem->horiz_units == VIK_DEM_HORIZ_LL_ARCSECONDS ) {
    VikCoord tmp; /* TODO: don't use coord_load_from_latlon, especially if in latlon drawing mode */

    gdouble max_lat_as, max_lon_as, min_lat_as, min_lon_as;
    gdouble start_lat_as, end_lat_as, start_lon_as, end_lon_as;

    gdouble start_lat, end_lat, start_lon, end_lon;

    struct LatLon counter;

    guint x, y, start_x, start_y;

    gint16 elev;

    guint skip_factor = ceil ( vik_viewport_get_xmpp(vp) / 80 ); /* todo: smarter calculation. */

    gdouble nscale_deg = dem->north_scale / ((gdouble) 3600);
    gdouble escale_deg = dem->east_scale / ((gdouble) 3600);

    max_lat_as = vp_bbox.north * 3600;
    min_lat_as = vp_bbox.south * 3600;
    max_lon_as = vp_bbox.east * 3600;
    min_lon_as = vp_bbox.west * 3600;

    start_lat_as = MAX(min_lat_as, dem->min_north);
    end_lat_as   = MIN(max_lat_as, dem->max_north);
    start_lon_as = MAX(min_lon_as, dem->min_east);
    end_lon_as   = MIN(max_lon_as, dem->max_east);

    start_lat = floor(start_lat_as / dem->north_scale) * nscale_deg;
    end_lat   = ceil (end_lat_as / dem->north_scale) * nscale_deg;
    start_lon = floor(start_lon_as / dem->east_scale) * escale_deg;
    end_lon   = ceil (end_lon_as / dem->east_scale) * escale_deg;

    vik_dem_east_north_to_xy ( dem, start_lon_as, start_lat_as, &start_x, &start_y );
    guint gradient_skip_factor = 1;
    if(vdl->type == DEM_TYPE_GRADIENT)
	    gradient_skip_factor = skip_factor;

    /* verify sane elev interval */
    if ( vdl->max_elev <= vdl->min_elev )
      vdl->max_elev = vdl->min_elev + 1;

    for ( x=start_x, counter.lon = start_lon; counter.lon <= end_lon+escale_deg*skip_factor; counter.lon += escale_deg * skip_factor, x += skip_factor ) {
      // NOTE: ( counter.lon <= end_lon + ESCALE_DEG*SKIP_FACTOR ) is neccessary so in high zoom modes,
      // the leftmost column does also get drawn, if the center point is out of viewport.
      if ( x < dem->n_columns ) {
        column = g_ptr_array_index ( dem->columns, x );
        // get previous and next column. catch out-of-bound.
	gint32 new_x = x;
	new_x -= gradient_skip_factor;
        if(new_x < 0)
          prevcolumn = g_ptr_array_index ( dem->columns, 0);
        else
          prevcolumn = g_ptr_array_index ( dem->columns, new_x);
	new_x = x;
	new_x += gradient_skip_factor;
        if(new_x >= dem->n_columns)
          nextcolumn = g_ptr_array_index ( dem->columns, dem->n_columns-1);
        else
          nextcolumn = g_ptr_array_index ( dem->columns, new_x);

        for ( y=start_y, counter.lat = start_lat; counter.lat <= end_lat; counter.lat += nscale_deg * skip_factor, y += skip_factor ) {
          if ( y > column->n_points )
            break;

          elev = column->points[y];

	  // calculate bounding box for drawing
	  gint box_x, box_y, box_width, box_height;
	  struct LatLon box_c;
	  box_c = counter;
	  box_c.lat += (nscale_deg * skip_factor)/2;
          box_c.lon -= (escale_deg * skip_factor)/2;
	  vik_coord_load_from_latlon(&tmp, vik_viewport_get_coord_mode(vp), &box_c);
	  vik_viewport_coord_to_screen(vp, &tmp, &box_x, &box_y);
	  // catch box at borders
	  if(box_x < 0)
            box_x = 0;
	  if(box_y < 0)
            box_y = 0;
          box_c.lat -= nscale_deg * skip_factor;
	  box_c.lon += escale_deg * skip_factor;
	  vik_coord_load_from_latlon(&tmp, vik_viewport_get_coord_mode(vp), &box_c);
	  vik_viewport_coord_to_screen(vp, &tmp, &box_width, &box_height);
	  box_width -= box_x;
	  box_height -= box_y;
          // catch box at borders
	  if(box_width <= 0 || box_height <= 0)
	    // skip this as is out of the viewport (e.g. zoomed in so this point is way off screen)
	    continue;

          // Also too far out
          if ( (box_x > width) || (box_y > height) )
            continue;

          gboolean minimum_level = FALSE;
          if(vdl->type == DEM_TYPE_HEIGHT) {
            if ( elev != VIK_DEM_INVALID_ELEVATION && elev <= vdl->min_elev ) {
              // Prevent 'elev - vdl->min_elev' from being negative so can safely use as array index
              elev = ceil ( vdl->min_elev );
              minimum_level = TRUE;
	    }
            if ( elev != VIK_DEM_INVALID_ELEVATION && elev > vdl->max_elev )
              elev = vdl->max_elev;
          }

          {
            if(vdl->type == DEM_TYPE_GRADIENT) {
              if( elev == VIK_DEM_INVALID_ELEVATION ) {
                /* don't draw it */
              } else {
                // calculate and sum gradient in all directions
                gint16 change = 0;
		gint32 new_y;

		// calculate gradient from height points all around the current one
                // Note this code suffers from edge effects, as currently prev & next columns
                //  (and for rows when y < 0 && y > n_points) should really attempt to read values
                //  from a different DEM file to get a change value across this particular DEM's boundary.
                // However it's probably not worth trying to do this as accessing the right DEM is not
                //  straight-forward and the other DEMs could have differing numbers of columns/points
                //  and so would need to reconsider scale factors as well...
		new_y = y - gradient_skip_factor;
		if(new_y < 0)
                  new_y = 0;
		change += get_height_difference(elev, prevcolumn->points[new_y]);
		change += get_height_difference(elev, column->points[new_y]);
		change += get_height_difference(elev, nextcolumn->points[new_y]);

		change += get_height_difference(elev, prevcolumn->points[y]);
		change += get_height_difference(elev, nextcolumn->points[y]);

		new_y = y + gradient_skip_factor;
		if(new_y >= column->n_points)
			new_y = y;
		change += get_height_difference(elev, prevcolumn->points[new_y]);
		change += get_height_difference(elev, column->points[new_y]);
		change += get_height_difference(elev, nextcolumn->points[new_y]);

		change = change / ((skip_factor > 1) ? log(skip_factor) : 0.55); // FIXME: better calc.

                if(change < vdl->min_elev)
                  // Prevent 'change - vdl->min_elev' from being negative so can safely use as array index
                  change = ceil ( vdl->min_elev );

                if(change > vdl->max_elev)
                  change = vdl->max_elev;

                if ( ((box_x + box_width) > width) )
                  box_width = width - box_x;

                guint index = (gint)floor(((change - vdl->min_elev)/(vdl->max_elev - vdl->min_elev))*(DEM_N_GRADIENT_COLORS-2))+1;
                GdkColor gcolor = vdl->gradient_colors[index];
                pixels_set_area ( vdl->pixels, gcolor, vdl->alpha, width, box_x, box_y, box_width, box_height );
              }
            } else {
              if(vdl->type == DEM_TYPE_HEIGHT) {
                if ( elev == VIK_DEM_INVALID_ELEVATION )
                  continue; /* don't draw it */
                // Draw to edge
                if ( ((box_x + box_width) > width) )
                  box_width = width - box_x;
                GdkColor gcolor;
                /* If 'sea' colour or below the defined mininum draw in the configurable colour */
                if ( minimum_level )
                  gcolor = vdl->color;
                else {
                  guint index = (gint)floor(((elev - vdl->min_elev)/(vdl->max_elev - vdl->min_elev))*(DEM_N_HEIGHT_COLORS-2))+1;
                  gcolor = vdl->height_colors[index];
                }
                pixels_set_area ( vdl->pixels, gcolor, vdl->alpha, width, box_x, box_y, box_width, box_height );
              }
            }
          }
        } /* for y= */
      }
    } /* for x= */
  } else if ( dem->horiz_units == VIK_DEM_HORIZ_UTM_METERS ) {
    gdouble max_nor, max_eas, min_nor, min_eas;
    gdouble start_nor, start_eas, end_nor, end_eas;

    gint16 elev;

    guint x, y, start_x, start_y;

    VikCoord tmp; /* TODO: don't use coord_load_from_latlon, especially if in latlon drawing mode */
    struct UTM counter;

    guint skip_factor = ceil ( vik_viewport_get_xmpp(vp) / 10 ); /* todo: smarter calculation. */

    VikCoord tleft, tright, bleft, bright;

    vik_viewport_screen_to_coord ( vp, 0, 0, &tleft );
    vik_viewport_screen_to_coord ( vp, vik_viewport_get_width(vp), 0, &tright );
    vik_viewport_screen_to_coord ( vp, 0, vik_viewport_get_height(vp), &bleft );
    vik_viewport_screen_to_coord ( vp, vik_viewport_get_width(vp), vik_viewport_get_height(vp), &bright );


    vik_coord_convert(&tleft, VIK_COORD_UTM);
    vik_coord_convert(&tright, VIK_COORD_UTM);
    vik_coord_convert(&bleft, VIK_COORD_UTM);
    vik_coord_convert(&bright, VIK_COORD_UTM);

    max_nor = MAX(tleft.north_south, tright.north_south);
    min_nor = MIN(bleft.north_south, bright.north_south);
    max_eas = MAX(bright.east_west, tright.east_west);
    min_eas = MIN(bleft.east_west, tleft.east_west);

    start_nor = MAX(min_nor, dem->min_north);
    end_nor   = MIN(max_nor, dem->max_north);
    if ( tleft.utm_zone == dem->utm_zone && bleft.utm_zone == dem->utm_zone
         && (tleft.utm_letter >= 'N') == (dem->utm_letter >= 'N')
         && (bleft.utm_letter >= 'N') == (dem->utm_letter >= 'N') ) /* if the utm zones/hemispheres are different, min_eas will be bogus */
      start_eas = MAX(min_eas, dem->min_east);
    else
      start_eas = dem->min_east;
    if ( tright.utm_zone == dem->utm_zone && bright.utm_zone == dem->utm_zone
         && (tright.utm_letter >= 'N') == (dem->utm_letter >= 'N')
         && (bright.utm_letter >= 'N') == (dem->utm_letter >= 'N') ) /* if the utm zones/hemispheres are different, min_eas will be bogus */
      end_eas = MIN(max_eas, dem->max_east);
    else
      end_eas = dem->max_east;

    start_nor = floor(start_nor / dem->north_scale) * dem->north_scale;
    end_nor   = ceil (end_nor / dem->north_scale) * dem->north_scale;
    start_eas = floor(start_eas / dem->east_scale) * dem->east_scale;
    end_eas   = ceil (end_eas / dem->east_scale) * dem->east_scale;

    vik_dem_east_north_to_xy ( dem, start_eas, start_nor, &start_x, &start_y );

    /* TODO: why start_x and start_y are -1 -- rounding error from above? */

    counter.zone = dem->utm_zone;
    counter.letter = dem->utm_letter;

    for ( x=start_x, counter.easting = start_eas; counter.easting <= end_eas; counter.easting += dem->east_scale * skip_factor, x += skip_factor ) {
      if ( x >= 0 && x < dem->n_columns ) {
        column = g_ptr_array_index ( dem->columns, x );
        for ( y=start_y, counter.northing = start_nor; counter.northing <= end_nor; counter.northing += dem->north_scale * skip_factor, y += skip_factor ) {
          if ( y > column->n_points )
            continue;
          elev = column->points[y];
          if ( elev != VIK_DEM_INVALID_ELEVATION && elev < vdl->min_elev )
            elev=vdl->min_elev;
          if ( elev != VIK_DEM_INVALID_ELEVATION && elev > vdl->max_elev )
            elev=vdl->max_elev;

          {
            gint a, b;
            vik_coord_load_from_utm(&tmp, vik_viewport_get_coord_mode(vp), &counter);
            vik_viewport_coord_to_screen(vp, &tmp, &a, &b);
            // Check a & b are in bounds:
            if ( a < 0 || b < 0 || (a > width) || (b > height) )
              continue;
            if ( elev == VIK_DEM_INVALID_ELEVATION )
              ; /* don't draw it */
            else if ( elev <= 0 ) {
              pixels_set_area ( vdl->pixels, vdl->color, vdl->alpha, width, a-1, b-1, 2, 2 );
            }
            else {
              guint index = (gint)floor((elev - vdl->min_elev)/(vdl->max_elev - vdl->min_elev)*(DEM_N_HEIGHT_COLORS-2))+1;
              GdkColor gcolor = vdl->height_colors[index];
              pixels_set_area ( vdl->pixels, gcolor, vdl->alpha, width, a-1, b-1, 2, 2 );
            }
          }
        } /* for y= */
      }
    } /* for x= */
  }
}

/* return the continent for the specified lat, lon */
/* TODO */
static const gchar *srtm_continent_dir ( gint lat, gint lon )
{
  extern const char *_srtm_continent_data[];
  static GHashTable *srtm_continent = NULL;
  const gchar *continent;
  gchar name[16];

  if (!srtm_continent) {
    const gchar **s;

    srtm_continent = g_hash_table_new(g_str_hash, g_str_equal);
    s = _srtm_continent_data;
    while (*s != (gchar *)-1) {
      continent = *s++;
      while (*s) {
        g_hash_table_insert(srtm_continent, (gpointer) *s, (gpointer) continent);
        s++;
      }
      s++;
    }
  }
  g_snprintf(name, sizeof(name), "%c%02d%c%03d",
                  (lat >= 0) ? 'N' : 'S', ABS(lat),
		  (lon >= 0) ? 'E' : 'W', ABS(lon));

  return(g_hash_table_lookup(srtm_continent, name));
}

static void dem_layer_draw ( VikDEMLayer *vdl, VikViewport *vp )
{
  GList *dems_iter = vdl->files;
  VikDEM *dem;


  /* search for SRTM3 90m */

  if ( vdl->source == DEM_SOURCE_SRTM )
    srtm_draw_existence ( vp );
#ifdef VIK_CONFIG_DEM24K
  else if ( vdl->source == DEM_SOURCE_DEM24K )
    dem24k_draw_existence ( vp );
#endif

  const guint width = vik_viewport_get_width ( vp );
  const guint height = vik_viewport_get_height ( vp );

  // RGBA, natural alignment of rows on 4 byte boundary
  vdl->pixels = g_malloc0 ( sizeof(guchar*) * width * height * 4 );

  while ( dems_iter ) {
    dem = a_dems_get ( (const char *) (dems_iter->data) );
    if ( dem )
      vik_dem_layer_draw_dem ( vdl, vp, dem );
    dems_iter = dems_iter->next;
  }

  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data ( vdl->pixels, GDK_COLORSPACE_RGB, TRUE, 8, width, height, width*4, NULL, NULL );
  vik_viewport_draw_pixbuf ( vp, pixbuf, 0, 0, 0, 0, width, height );
  g_object_unref ( pixbuf );
  g_free ( vdl->pixels );
}

static void dem_layer_free ( VikDEMLayer *vdl )
{
  a_dems_list_free ( vdl->files );

  g_free ( vdl->srtm_base_url );
  g_free ( vdl->height_colors );
  g_free ( vdl->gradient_colors );
}

VikDEMLayer *dem_layer_create ( VikViewport *vp )
{
  VikDEMLayer *vdl = dem_layer_new ( vp );
  return vdl;
}
/**************************************************************
 **** SOURCES & DOWNLOADING
 **************************************************************/
typedef struct {
  gchar *dest;
  gdouble lat, lon;

  GMutex *mutex;
  VikDEMLayer *vdl; /* NULL if not alive */

  guint source;
} DEMDownloadParams;

/**************************************************
 *  SOURCE: SRTM                                  *
 **************************************************/

// Free returned string after use
static gchar *srtm_server_url ( gchar *b_url, dem_dir_scheme_type scheme, dem_filename_style_type style, gdouble lat, gdouble lon )
{
  gint intlat, intlon;
  intlat = (int)floor(lat);
  intlon = (int)floor(lon);
  gchar *src_url = NULL;

  // Check to see if we get a valid directory, even if we don't use it in the URL anymore
  //  since it will us if the request has any chance of being furfilled
  const gchar *continent_dir = srtm_continent_dir (intlat, intlon);
  if ( continent_dir ) {
    switch ( scheme ) {
    case DEM_SCHEME_CONTINENT:
      // USGS up until late 2020
      // eg. <url>/srtm3-Eurasia/N12E034.hgt.zip
      src_url = g_strdup_printf("%s/%s/%c%02d%c%03d.hgt.zip",
                                b_url,
                                continent_dir,
                                (intlat >= 0) ? 'N' : 'S',
                                ABS(intlat),
                                (intlon >= 0) ? 'E' : 'W',
                                ABS(intlon) );
      break;
    case DEM_SCHEME_LATITUDE:
      // eg. <url>/N12/N12E034.hgt.zip
      src_url = g_strdup_printf ( "%s/%c%02d/%c%02d%c%03d.hgt.zip",
                                b_url,
                                (intlat >= 0) ? 'N' : 'S',
                                ABS(intlat),
                                (intlat >= 0) ? 'N' : 'S',
                                ABS(intlat),
                                (intlon >= 0) ? 'E' : 'W',
                                ABS(intlon) );
      break;
    default:
      // NB also file name can be different too
      if ( style == DEM_FILENAME_SRTMGL1 )
        // Location as of 2021
        // https://e4ftl01.cr.usgs.gov/MEASURES/SRTMGL1.003/2000.02.11/N47E008.SRTMGL1.hgt.zip
        src_url = g_strdup_printf ( "%s/%c%02d%c%03d.SRTMGL1.hgt.zip",
                                    b_url,
                                    (intlat >= 0) ? 'N' : 'S',
                                    ABS(intlat),
                                    (intlon >= 0) ? 'E' : 'W',
                                    ABS(intlon) );
      else
        src_url = g_strdup_printf ( "%s/%c%02d%c%03d.hgt.zip",
                                    b_url,
                                    (intlat >= 0) ? 'N' : 'S',
                                    ABS(intlat),
                                    (intlon >= 0) ? 'E' : 'W',
                                    ABS(intlon) );
    }
  }
  return src_url;
}

static void srtm_dem_download_thread ( DEMDownloadParams *p, gpointer threaddata )
{
  if ( !p->vdl )
    return;

  gchar *src_url = srtm_server_url ( p->vdl->srtm_base_url, p->vdl->dir_scheme, p->vdl->filename_style, p->lat, p->lon );

  // TODO: Might be better practice that the valid location request check is made before creating this thread
  if ( !src_url ) {
    gchar *msg = g_strdup_printf ( _("No SRTM data available for %f, %f"), p->lat, p->lon );
    vik_window_statusbar_update ( (VikWindow*)VIK_GTK_WINDOW_FROM_LAYER(p->vdl), msg, VIK_STATUSBAR_INFO );
    g_free ( msg );
    return;
  }

  gchar *user_pass = dem_get_login ( p->vdl );
  // For USGS DEM Server usage, we need to make credentials follow redirects + use cookies                      --vvvv--vvvv
  DownloadFileOptions options = { FALSE, FALSE, NULL, 10, NULL, NULL, ONE_WEEK_SECS, a_check_map_file, user_pass, TRUE, TRUE, NULL };
  DownloadResult_t result = a_http_download_get_url ( src_url, NULL, p->dest, &options, NULL );
  switch ( result ) {
    case DOWNLOAD_PARAMETERS_ERROR:
    case DOWNLOAD_CONTENT_ERROR:
    case DOWNLOAD_HTTP_ERROR: {
      gchar *msg = g_strdup_printf ( _("DEM download failure for %f, %f"), p->lat, p->lon );
      vik_window_statusbar_update ( (VikWindow*)VIK_GTK_WINDOW_FROM_LAYER(p->vdl), msg, VIK_STATUSBAR_INFO );
      g_free ( msg );
      break;
    }
    case DOWNLOAD_FILE_WRITE_ERROR: {
      gchar *msg = g_strdup_printf ( _("DEM write failure for %s"), p->dest );
      vik_window_statusbar_update ( (VikWindow*)VIK_GTK_WINDOW_FROM_LAYER(p->vdl), msg, VIK_STATUSBAR_INFO );
      g_free ( msg );
      break;
    }
    case DOWNLOAD_SUCCESS:
    case DOWNLOAD_NOT_REQUIRED:
    case DOWNLOAD_USER_ABORTED:
    default:
      break;
  }
  g_free ( user_pass );
  g_free ( src_url );
}

static gchar *srtm_lat_lon_to_dest_fn ( gdouble lat, gdouble lon )
{
  gint intlat, intlon;
  const gchar *continent_dir;

  intlat = (int)floor(lat);
  intlon = (int)floor(lon);
  continent_dir = srtm_continent_dir(intlat, intlon);

  if (!continent_dir)
    continent_dir = "nowhere";

  return g_strdup_printf("srtm3-%s%s%c%02d%c%03d.hgt.zip",
                continent_dir,
                G_DIR_SEPARATOR_S,
		(intlat >= 0) ? 'N' : 'S',
		ABS(intlat),
		(intlon >= 0) ? 'E' : 'W',
		ABS(intlon) );

}

/* TODO: generalize */
static void srtm_draw_existence ( VikViewport *vp )
{
  gdouble max_lat, max_lon, min_lat, min_lon;
  gchar buf[strlen(MAPS_CACHE_DIR)+strlen(SRTM_CACHE_TEMPLATE)+30];
  gint i, j;

  vik_viewport_get_min_max_lat_lon ( vp, &min_lat, &max_lat, &min_lon, &max_lon );

  for (i = floor(min_lat); i <= floor(max_lat); i++) {
    for (j = floor(min_lon); j <= floor(max_lon); j++) {
      const gchar *continent_dir;
      if ((continent_dir = srtm_continent_dir(i, j)) == NULL)
        continue;
      g_snprintf(buf, sizeof(buf), SRTM_CACHE_TEMPLATE,
                MAPS_CACHE_DIR,
                continent_dir,
                G_DIR_SEPARATOR_S,
		(i >= 0) ? 'N' : 'S',
		ABS(i),
		(j >= 0) ? 'E' : 'W',
		ABS(j) );
      if ( g_file_test(buf, G_FILE_TEST_EXISTS ) == TRUE ) {
        VikCoord ne, sw;
        gint x1, y1, x2, y2;
        sw.north_south = i;
        sw.east_west = j;
        sw.mode = VIK_COORD_LATLON;
        ne.north_south = i+1;
        ne.east_west = j+1;
        ne.mode = VIK_COORD_LATLON;
        vik_viewport_coord_to_screen ( vp, &sw, &x1, &y1 );
        vik_viewport_coord_to_screen ( vp, &ne, &x2, &y2 );
        if ( x1 < 0 ) x1 = 0;
        if ( y2 < 0 ) y2 = 0;
        vik_viewport_draw_rectangle ( vp, vik_viewport_get_black_gc(vp), FALSE, x1, y2, x2-x1, y1-y2, &black_color );
      }
    }
  }
}


/**************************************************
 *  SOURCE: USGS 24K                              *
 **************************************************/

#ifdef VIK_CONFIG_DEM24K

static void dem24k_dem_download_thread ( DEMDownloadParams *p, gpointer threaddata )
{
  /* TODO: dest dir */
  gchar *cmdline = g_strdup_printf("%s %.03f %.03f",
	DEM24K_DOWNLOAD_SCRIPT,
	floor(p->lat*8)/8,
	ceil(p->lon*8)/8 );
  /* FIX: don't use system, use execv or something. check for existence */
  system(cmdline);
  g_free ( cmdline );
}

static gchar *dem24k_lat_lon_to_dest_fn ( gdouble lat, gdouble lon )
{
  return g_strdup_printf("dem24k/%d/%d/%.03f,%.03f.dem",
	(gint) lat,
	(gint) lon,
	floor(lat*8)/8,
	ceil(lon*8)/8);
}

/* TODO: generalize */
static void dem24k_draw_existence ( VikViewport *vp )
{
  gdouble max_lat, max_lon, min_lat, min_lon;
  gchar buf[strlen(MAPS_CACHE_DIR)+40];
  gdouble i, j;

  vik_viewport_get_min_max_lat_lon ( vp, &min_lat, &max_lat, &min_lon, &max_lon );

  for (i = floor(min_lat*8)/8; i <= floor(max_lat*8)/8; i+=0.125) {
    /* check lat dir first -- faster */
    g_snprintf(buf, sizeof(buf), "%sdem24k/%d/",
        MAPS_CACHE_DIR,
	(gint) i );
    if ( g_file_test(buf, G_FILE_TEST_EXISTS) == FALSE )
      continue;
    for (j = floor(min_lon*8)/8; j <= floor(max_lon*8)/8; j+=0.125) {
      /* check lon dir first -- faster */
      g_snprintf(buf, sizeof(buf), "%sdem24k/%d/%d/",
        MAPS_CACHE_DIR,
	(gint) i,
        (gint) j );
      if ( g_file_test(buf, G_FILE_TEST_EXISTS) == FALSE )
        continue;
      g_snprintf(buf, sizeof(buf), "%sdem24k/%d/%d/%.03f,%.03f.dem",
	        MAPS_CACHE_DIR,
		(gint) i,
		(gint) j,
		floor(i*8)/8,
		floor(j*8)/8 );
      if ( g_file_test(buf, G_FILE_TEST_EXISTS ) == TRUE ) {
        VikCoord ne, sw;
        gint x1, y1, x2, y2;
        sw.north_south = i;
        sw.east_west = j-0.125;
        sw.mode = VIK_COORD_LATLON;
        ne.north_south = i+0.125;
        ne.east_west = j;
        ne.mode = VIK_COORD_LATLON;
        vik_viewport_coord_to_screen ( vp, &sw, &x1, &y1 );
        vik_viewport_coord_to_screen ( vp, &ne, &x2, &y2 );
        if ( x1 < 0 ) x1 = 0;
        if ( y2 < 0 ) y2 = 0;
        vik_viewport_draw_rectangle ( vp, vik_viewport_get_black_gc(vp), FALSE, x1, y2, x2-x1, y1-y2, &black_color );
      }
    }
  }
}
#endif

/**************************************************
 *   SOURCES -- DOWNLOADING & IMPORTING TOOL      *
 **************************************************
 */

static void weak_ref_cb ( gpointer ptr, GObject * dead_vdl )
{
  DEMDownloadParams *p = ptr;
  g_mutex_lock ( p->mutex );
  p->vdl = NULL;
  g_mutex_unlock ( p->mutex );
}

/* Try to add file full_path.
 * filename will be copied.
 * returns FALSE if file does not exists, TRUE otherwise.
 */
static gboolean dem_layer_add_file ( VikDEMLayer *vdl, const gchar *filename )
{
  if ( g_file_test(filename, G_FILE_TEST_EXISTS) == TRUE ) {
    /* only load if file size is not 0 (not in progress) */
    GStatBuf sb;
    (void)g_stat ( filename, &sb );
    if ( sb.st_size ) {
      gchar *duped_path = g_strdup(filename);
      vdl->files = g_list_prepend ( vdl->files, duped_path );
      a_dems_load ( duped_path );
      g_debug("%s: %s", __FUNCTION__, duped_path);
    }
    return TRUE;
  } else
    return FALSE;
}

static void dem_download_thread ( DEMDownloadParams *p, gpointer threaddata )
{
  if ( p->source == DEM_SOURCE_SRTM )
    srtm_dem_download_thread ( p, threaddata );
#ifdef VIK_CONFIG_DEM24K
  else if ( p->source == DEM_SOURCE_DEM24K )
    dem24k_dem_download_thread ( p, threaddata );
#endif
  else
    return;

  g_mutex_lock ( p->mutex );
  if ( p->vdl ) {
    g_object_weak_unref ( G_OBJECT(p->vdl), weak_ref_cb, p );

    if ( dem_layer_add_file ( p->vdl, p->dest ) ) {
      vik_layer_emit_update ( VIK_LAYER(p->vdl), TRUE ); // NB update requested from background thread
    }
  }
  g_mutex_unlock ( p->mutex );
}


static void free_dem_download_params ( DEMDownloadParams *p )
{
  vik_mutex_free ( p->mutex );
  g_free ( p->dest );
  g_free ( p );
}

static gpointer dem_layer_download_create ( VikWindow *vw, VikViewport *vvp)
{
  return vvp;
}


typedef enum { MA_LL = 0, MA_VDL, MA_LAST } menu_array_index;
typedef gpointer menu_array_data[MA_LAST];

/**
 * Display a simple dialog with information about the DEM file at this location
 */
static void dem_layer_file_info ( menu_array_data values )
{
  struct LatLon *ll = values[MA_LL];
  VikDEMLayer *vdl = VIK_DEM_LAYER(values[MA_VDL]);
  gchar *source = srtm_server_url ( vdl->srtm_base_url, vdl->dir_scheme, vdl->filename_style, ll->lat, ll->lon );
  if ( !source )
    // Probably not over any land...
    source = g_strdup ( _("No DEM File Available") );

  gchar *filename = NULL;
  gchar *dem_file = NULL;
#ifdef VIK_CONFIG_DEM24K
  dem_file = dem24k_lat_lon_to_dest_fn ( ll->lat, ll->lon );
#else
  dem_file = srtm_lat_lon_to_dest_fn ( ll->lat, ll->lon );
#endif
  gchar *message = NULL;

  filename = g_strdup_printf ( "%s%s", MAPS_CACHE_DIR, dem_file );

  if ( g_file_test ( filename, G_FILE_TEST_EXISTS ) ) {
    // Get some timestamp information of the file
    GStatBuf stat_buf;
    if ( g_stat ( filename, &stat_buf ) == 0 ) {
      gchar time_buf[64];
      strftime ( time_buf, sizeof(time_buf), "%c", gmtime((const time_t *)&stat_buf.st_mtime) );
      message = g_strdup_printf ( _("\nSource: %s\n\nDEM File: %s\nDEM File Timestamp: %s"), source, filename, time_buf );
    }
  }
  else
    message = g_strdup_printf ( _("Source: %s\n\nNo DEM File!"), source );

  // Show the info
  a_dialog_info_msg ( VIK_GTK_WINDOW_FROM_LAYER(vdl), message );

  g_free ( message );
  g_free ( source );
  g_free ( dem_file );
  g_free ( filename );
}

static VikLayerToolFuncStatus dem_layer_download_release ( VikDEMLayer *vdl, GdkEventButton *event, VikViewport *vvp )
{
  VikCoord coord;
  // Data made available for the right click menu
  static struct LatLon ll;
  static menu_array_data data;
  data[MA_LL] = &ll;
  data[MA_VDL] = vdl;

  gchar *full_path;
  gchar *dem_file = NULL;

  vik_viewport_screen_to_coord ( vvp, event->x, event->y, &coord );
  vik_coord_to_latlon ( &coord, &ll );

  if ( vdl->source == DEM_SOURCE_SRTM )
    dem_file = srtm_lat_lon_to_dest_fn ( ll.lat, ll.lon );
#ifdef VIK_CONFIG_DEM24K
  else if ( vdl->source == DEM_SOURCE_DEM24K )
    dem_file = dem24k_lat_lon_to_dest_fn ( ll.lat, ll.lon );
#endif

  if ( ! dem_file )
    return VIK_LAYER_TOOL_IGNORED;

  full_path = g_strdup_printf("%s%s", MAPS_CACHE_DIR, dem_file );

  g_debug("%s: %s", __FUNCTION__, full_path);

  if ( event->button == 1 ) {
    // TODO: check if already in filelist
    if ( ! dem_layer_add_file(vdl, full_path) ) {
      gchar *tmp = g_strdup_printf ( _("Downloading DEM %s"), dem_file );
      DEMDownloadParams *p = g_malloc(sizeof(DEMDownloadParams));
      p->dest = g_strdup(full_path);
      p->lat = ll.lat;
      p->lon = ll.lon;
      p->vdl = vdl;
      p->mutex = vik_mutex_new();
      p->source = vdl->source;
      g_object_weak_ref(G_OBJECT(p->vdl), weak_ref_cb, p );

      a_background_thread ( BACKGROUND_POOL_REMOTE,
                            VIK_GTK_WINDOW_FROM_LAYER(vdl), tmp,
                            (vik_thr_func) dem_download_thread, p,
                            (vik_thr_free_func) free_dem_download_params, NULL, 1 );

      g_free ( tmp );
    }
    else
      vik_layer_emit_update ( VIK_LAYER(vdl), FALSE );
  }
  else {
    if ( !vdl->right_click_menu ) {
      vdl->right_click_menu = GTK_MENU ( gtk_menu_new () );
      vu_menu_add_item ( vdl->right_click_menu, _("_Show DEM File Information"), GTK_STOCK_INFO, G_CALLBACK(dem_layer_file_info), data );
    }

    gtk_menu_popup ( vdl->right_click_menu, NULL, NULL, NULL, NULL, event->button, event->time );
    gtk_widget_show_all ( GTK_WIDGET(vdl->right_click_menu) );
  }

  g_free ( dem_file );
  g_free ( full_path );

  return VIK_LAYER_TOOL_ACK;
}

static VikLayerToolFuncStatus dem_layer_download_click ( VikDEMLayer *vdl, GdkEventButton *event, VikViewport *vvp )
{
/* choose & keep track of cache dir
 * download in background thread
 * download over area */
  return VIK_LAYER_TOOL_ACK;
}


