#ifndef __VIKING_MENU_XML_H
#define __VIKING_MENU_XML_H

static const char *menu_xml =
	"<ui>"
	"  <menubar name='MainMenu'>"
	"    <menu action='File'>"
	"      <menuitem action='New'/>"
	"      <menuitem action='Open'/>"
	"      <menuitem action='Append'/>"
	"      <menuitem action='Save'/>"
	"      <menuitem action='SaveAs'/>"
	"      <separator/>"
	"      <menu action='Acquire'>"
	"        <menuitem action='AcquireGPS'/>"
	"        <menuitem action='AcquireGoogle'/>"
#ifdef VIK_CONFIG_GEOCACHES
	"        <menuitem action='AcquireGC'/>"
#endif
	"      </menu>"
	"      <separator/>"
	"      <menuitem action='GenImg'/>"
	"      <menuitem action='GenImgDir'/>"
#if GTK_CHECK_VERSION(2,10,0)
	"      <menuitem action='Print'/>"
#endif
	"      <separator/>"
	"      <menuitem action='SaveExit'/>"
	"      <menuitem action='Exit'/>"
	"    </menu>"
	"    <menu action='Edit'>"
	"      <menuitem action='Cut'/>"
	"      <menuitem action='Copy'/>"
	"      <menuitem action='Paste'/>"
	"      <menuitem action='Delete'/>"
	"      <menuitem action='DeleteAll'/>"
	"      <separator/>"
	"      <menuitem action='Preferences'/>"
	"    </menu>"
	"    <menu action='View'>"
	"      <menuitem action='ModeUTM'/>"
#ifdef VIK_CONFIG_EXPEDIA
	"      <menuitem action='ModeExpedia'/>"
#endif
#ifdef VIK_CONFIG_OLD_GOOGLE
	"      <menuitem action='ModeGoogle'/>"
	"      <menuitem action='ModeKH'/>"
#endif
	"      <menuitem action='ModeMercator'/>"
	"      <separator/>"
	"      <menuitem action='GoogleMapsSearch'/>"
	"      <menuitem action='GotoLL'/>"
	"      <menuitem action='GotoUTM'/>"
	"      <separator/>"
	"      <menuitem action='ShowScale'/>"
	"      <menuitem action='ShowCenterMark'/>"
	"      <menuitem action='SetBGColor'/>"
	"      <menuitem action='FullScreen'/>"
	"      <separator/>"
	"      <menuitem action='ZoomIn'/>"
	"      <menuitem action='ZoomOut'/>"
	"      <menuitem action='ZoomTo'/>"
	"      <menu action='SetZoom'>"
	"        <menuitem action='Zoom0.25'/>"
	"        <menuitem action='Zoom0.5'/>"
	"        <menuitem action='Zoom1'/>"
	"        <menuitem action='Zoom2'/>"
	"        <menuitem action='Zoom4'/>"
	"        <menuitem action='Zoom8'/>"
	"        <menuitem action='Zoom16'/>"
	"        <menuitem action='Zoom32'/>"
	"        <menuitem action='Zoom64'/>"
	"        <menuitem action='Zoom128'/>"
	"      </menu>"
        "      <menu action='SetPan'>"
        "          <menuitem action='PanNorth'/>"
        "          <menuitem action='PanEast'/>"
        "          <menuitem action='PanWest'/>"
        "          <menuitem action='PanSouth'/>"
	"      </menu>"
	"      <separator/>"
	"      <menuitem action='BGJobs'/>"
	"    </menu>"
	"    <menu action='Layers'>"
	"      <menuitem action='Properties'/>"
	"      <separator/>"
	"    </menu>"
	"    <menu action='Tools'>"
	"      <menuitem action='Zoom'/>"
	"      <menuitem action='Ruler'/>"
	"    </menu>"
	"    <menu action='Help'>"
	"      <menuitem action='About'/>"
	"    </menu>"
	"  </menubar>"
	"  <toolbar name='MainToolbar'>"
	"    <placeholder name='FileToolItems'>"
	"      <toolitem name='New' action='New'/>"
	"      <toolitem name='Open' action='Open'/>"
	"      <toolitem name='Save' action='Save'/>"
#if GTK_CHECK_VERSION(2,10,0)
	"      <toolitem name='Print' action='Print'/>"
#endif
	"      <toolitem name='Exit' action='Exit'/>"
	"      <separator/>"
	"    </placeholder>"
	"    <placeholder name='EditToolItems'>"
	"      <toolitem name='Cut' action='Cut'/>"
	"      <toolitem name='Copy' action='Copy'/>"
	"      <toolitem name='Paste' action='Paste'/>"
	"      <toolitem name='Delete' action='Delete'/>"
	"      <separator/>"
	"    </placeholder>"
	"    <placeholder name='ViewToolItems'>"
	"      <toolitem action='GoogleMapsSearch'/>"
	"      <separator/>"
	"    </placeholder>"
	"    <placeholder name='ToolItems'>"
	"    <toolitem action='Zoom'/>"
	"    <toolitem action='Ruler'/>"
	"    </placeholder>"
	"  </toolbar>"
	"</ui>"
;

#endif
