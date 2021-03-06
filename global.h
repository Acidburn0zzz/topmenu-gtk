#ifndef _TOPMENU_GLOBAL_H_
#define _TOPMENU_GLOBAL_H_

/* Private definitions that are common to entire project. */

#include "config.h"

/** The window ID of this top level's window attached menu window. */
#define ATOM_TOPMENU_WINDOW "_TOPMENU_WINDOW"

/** The X11 selection that is used to indicate the current server widget. */
#define ATOM_TOPMENU_SERVER_SELECTION "_TOPMENU_SERVER"

/* Gobject data keys */
/** For a GtkWindow, stores its associated TopMenu GtkPlug */
#define OBJECT_DATA_KEY_PLUG "topmenu-plug"

/** For a server widget, stores the associated stub window.
    This is the window that might own ATOM_TOPMENU_SERVER_SELECTION. */
#define OBJECT_DATA_KEY_SERVER_STUB "topmenu-server-stub"

#endif
