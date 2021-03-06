/*
 * Copyright 2014 Javier S. Pedro <maemo@javispedro.com>
 *
 * This file is part of TopMenu.
 *
 * TopMenu is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TopMenu is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with TopMenu.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>

#include "../global.h"

#include "topmenu-widget.h"
#include "topmenu-server.h"

#ifdef HAVE_WNCK1
#define WNCK_I_KNOW_THIS_IS_UNSTABLE 1
#include <libwnck/libwnck.h>
#define HAVE_WNCK 1
#endif

#ifdef HAVE_WNCK3
#define WNCK_I_KNOW_THIS_IS_UNSTABLE 1
#include <libwnck/libwnck.h>
#define HAVE_WNCK 3
#endif

#ifdef HAVE_MATEWNCK
#include <libmatewnck/libmatewnck.h>
#endif

struct _TopMenuWidgetPrivate
{
	Atom atom_window;
	Atom atom_transient_for;
	GQueue followed_windows;
#ifdef HAVE_WNCK
	WnckScreen *wnck_screen;
#endif
#ifdef HAVE_MATEWNCK
	MatewnckScreen *matewnck_screen;
#endif
};

G_DEFINE_TYPE(TopMenuWidget, topmenu_widget, GTK_TYPE_BIN)

#define TOPMENU_WIDGET_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), TOPMENU_TYPE_WIDGET, TopMenuWidgetPrivate))

static Window read_window_property(Display *dpy, Window window, Atom property)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *prop_return;
	Status status;

	gdk_error_trap_push();
	status = XGetWindowProperty(dpy, window, property,
	                            0, sizeof(Window), False,
	                            XA_WINDOW, &actual_type, &actual_format, &nitems,
	                            &bytes_after, &prop_return);

	if (gdk_error_trap_pop() == 0 && status == Success) {
		if (prop_return && actual_type == XA_WINDOW) {
			return *(Window*)prop_return;
		}
	}

	return None;
}

static Display * topmenu_widget_get_display(TopMenuWidget *self)
{
	GdkWindow *gdk_win = gtk_widget_get_window(GTK_WIDGET(self));
	if (gdk_win) {
		return GDK_WINDOW_XDISPLAY(gdk_win);
	}
	return NULL;
}

static Window topmenu_widget_get_toplevel_xwindow(TopMenuWidget *self)
{
	GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(self));
	GdkWindow *window = gtk_widget_get_window(toplevel);
	if (window) {
		return GDK_WINDOW_XID(window);
	} else {
		return None;
	}
}

static Window topmenu_widget_get_current_active_window(TopMenuWidget *self)
{
#ifdef HAVE_WNCK
	WnckWindow *window = wnck_screen_get_active_window(self->priv->wnck_screen);
	if (window) {
		return wnck_window_get_xid(window);
	}
#endif
#ifdef HAVE_MATEWNCK
	MatewnckWindow *window = matewnck_screen_get_active_window(self->priv->matewnck_screen);
	if (window) {
		return matewnck_window_get_xid(window);
	}
#endif
	return None;
}

static Window topmenu_widget_get_session_leader(TopMenuWidget *self, Window window)
{
#ifdef HAVE_WNCK
	WnckWindow *w = wnck_window_get(window);
	if (w) {
		return wnck_window_get_group_leader(w);
	}
#endif
#ifdef HAVE_MATEWNCK
	MatewnckWindow *w = matewnck_window_get(window);
	if (w) {
		return matewnck_window_get_group_leader(w);
	}
#endif
	return None;
}

static Window topmenu_widget_get_window_transient(TopMenuWidget *self, Window window)
{
#ifdef HAVE_WNCK
	WnckWindow *w = wnck_window_get(window);
	if (w) {
		WnckWindow *t = wnck_window_get_transient(w);
		if (t) {
			return wnck_window_get_xid(t);
		}
	}
#endif
#ifdef HAVE_MATEWNCK
	MatewnckWindow *w = matewnck_window_get(window);
	if (w) {
		MatewnckWindow *t = matewnck_window_get_transient(w);
		if (t) {
			return matewnck_window_get_xid(t);
		}
	}
#endif
	return None;
}

static Window topmenu_widget_get_any_app_window_with_menu(TopMenuWidget *self, Window window)
{
#ifdef HAVE_WNCK
	Display *dpy = topmenu_widget_get_display(self);

	WnckWindow *w = wnck_window_get(window);
	if (!w) return None;

	WnckApplication *app = wnck_window_get_application(w);
	if (!app) return None;

	GList *i, *windows = wnck_screen_get_windows_stacked(self->priv->wnck_screen);
	if (!windows) return None;

	for (i = g_list_last(windows); i; i = g_list_previous(i)) {
		if (i->data != w && wnck_window_get_application(i->data) == app) {
			Window candidate = wnck_window_get_xid(i->data);
			Window menu_window = read_window_property(dpy, candidate, self->priv->atom_window);
			if (menu_window) {
				return candidate;
			}
		}
	}
#endif
#ifdef HAVE_MATEWNCK
	Display *dpy = topmenu_widget_get_display(self);

	MatewnckWindow *w = matewnck_window_get(window);
	if (!w) return None;

	MatewnckApplication *app = matewnck_window_get_application(w);
	if (!app) return None;

	GList *i, *windows = matewnck_screen_get_windows_stacked(self->priv->matewnck_screen);
	if (!windows) return None;

	for (i = g_list_last(windows); i; i = g_list_previous(i)) {
		if (i->data != w && matewnck_window_get_application(i->data) == app) {
			Window candidate = matewnck_window_get_xid(i->data);
			Window menu_window = read_window_property(dpy, candidate, self->priv->atom_window);
			if (menu_window) {
				return candidate;
			}
		}
	}
#endif
	return None;
}

static void topmenu_widget_embed_topmenu_window(TopMenuWidget *self, Window window)
{
	g_return_if_fail(self->socket);
	GdkWindow *cur = gtk_socket_get_plug_window(self->socket);

	if (cur) {
		if (GDK_WINDOW_XID(cur) == window) {
			// Trying to embed the same client again
			return; // Nothing to do
		}

		// Otherwise, disembed the current client
		g_debug("Disembedding window 0x%lx", GDK_WINDOW_XID(cur));
		gdk_error_trap_push();
		gdk_window_hide(cur);

		// Reparent back to root window to end embedding
		GdkScreen *screen = gdk_window_get_screen(cur);
		gdk_window_reparent(cur, gdk_screen_get_root_window(screen), 0, 0);

		gdk_flush();
		if (gdk_error_trap_pop()) {
			g_debug("error while disembedding window");
			// Assume it's destroyed, so continue.
		}
	}

#if GTK_VERSION == 2
	/* Seems that we might be adding the new plug before actually letting
	 * the socket receive the reparentnotify from above. */
	g_clear_object(&self->socket->plug_window);
	self->socket->current_width = self->socket->current_height = 0;
#endif

	if (window) {
		g_debug("Embedding window 0x%lx", window);
		gtk_socket_add_id(self->socket, window);
	}
}

static gboolean topmenu_widget_try_window(TopMenuWidget *self, Window window)
{
	Display *dpy = topmenu_widget_get_display(self);
	g_return_val_if_fail(dpy, FALSE);
	g_return_val_if_fail(window, FALSE);

	Window menu_window = read_window_property(dpy, window, self->priv->atom_window);
	if (menu_window) {
		topmenu_widget_embed_topmenu_window(self, menu_window);
		return TRUE;
	}

	return FALSE;
}

static gboolean topmenu_widget_follow_window(TopMenuWidget *self, Window window)
{
	Display *dpy = topmenu_widget_get_display(self);
	g_return_val_if_fail(dpy, FALSE);
	g_return_val_if_fail(window, FALSE);

	if (window == topmenu_widget_get_toplevel_xwindow(self)) {
		return FALSE; // Ignore the window this widget is on as a candidate
	}

	gdk_error_trap_push();
	XWindowAttributes win_attrs;
	if (XGetWindowAttributes(dpy, window, &win_attrs)) {
		long event_mask =  win_attrs.your_event_mask | StructureNotifyMask | PropertyChangeMask;
		if (event_mask != win_attrs.your_event_mask) {
			XSelectInput(dpy, window, event_mask);
		}
	}
	gdk_flush();
	if (gdk_error_trap_pop()) {
		g_debug("got error while trying to follow window 0x%lx", window);
		return FALSE; // Assume window has been destroyed.
	}

	// Add this window to the list of windows we are following
	g_queue_push_head(&self->priv->followed_windows, GSIZE_TO_POINTER(window));

	if (topmenu_widget_try_window(self, window)) {
		// Found a menu bar on this window
		return TRUE;
	} else {
		// This window had no menu bar, so let's check its transient_for windows.
		Window transient_for = topmenu_widget_get_window_transient(self, window);
		if (transient_for && transient_for != window) {
			if (topmenu_widget_follow_window(self, transient_for)) {
				return TRUE;
			}
		}

		// Also see if its client leader has a global menu bar....
		Window leader = topmenu_widget_get_session_leader(self, window);
		if (leader && leader != window) {
			if (topmenu_widget_follow_window(self, leader)) {
				return TRUE;
			}
		}

		// Otherwise, if this program has more than one window, then let's search
		// for any other window with a menu bar
		Window other = topmenu_widget_get_any_app_window_with_menu(self, window);
		if (other && other != window) {
			if (topmenu_widget_follow_window(self, other)) {
				return TRUE;
			}
		}
	}

	return FALSE;
}

static void topmenu_widget_set_followed_window(TopMenuWidget *self, Window window)
{
	Display *dpy = topmenu_widget_get_display(self);
	g_return_if_fail(dpy);

	g_debug("Setting active window to 0x%lx", window);

	// Clear the list of currently followed windows.
	g_queue_clear(&self->priv->followed_windows);

	if (window) {
		// Initialize atoms now
		if (self->priv->atom_window == None) {
			self->priv->atom_window = XInternAtom(dpy, ATOM_TOPMENU_WINDOW, False);
		}
		if (self->priv->atom_transient_for) {
			self->priv->atom_transient_for = XInternAtom(dpy, "WM_TRANSIENT_FOR", False);
		}

		// Start by checking the active window
		// This will recursively check its transient_for windows.
		if (topmenu_widget_follow_window(self, window)) {
			g_debug("Also following %d windows",
			        g_queue_get_length(&self->priv->followed_windows));
			return;
		}

		// Otherwise fallback to "no menu bar".
		g_debug("Active window has no menu bar; following %d windows",
		        g_queue_get_length(&self->priv->followed_windows));
	}

	topmenu_widget_embed_topmenu_window(self, None);
}

static void handle_socket_realize(GtkSocket *socket, TopMenuWidget *self)
{
	// Workaround a "bug workaround" where GtkSocket will not select ButtonPress
	// events
	g_warn_if_fail(gtk_widget_get_realized(GTK_WIDGET(socket)));
	gtk_widget_add_events(GTK_WIDGET(socket),
	                      GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
}

static gboolean handle_socket_plug_removed(GtkSocket *socket, TopMenuWidget *self)
{
	g_debug("Plug has been removed");
	// No need to do anything
	return TRUE; // Do not destroy the socket
}

#ifdef HAVE_WNCK
static void handle_active_wnck_window_changed(WnckScreen *screen, WnckWindow *prev_window, TopMenuWidget *self)
{
	if (!gtk_widget_get_visible(GTK_WIDGET(self))) {
		return;
	}
	WnckWindow *window = wnck_screen_get_active_window(screen);
	if (window) {
		topmenu_widget_set_followed_window(self, wnck_window_get_xid(window));
	} else {
		// No active window?
	}
}
#endif

#ifdef HAVE_MATEWNCK
static void handle_active_matewnck_window_changed(MatewnckScreen *screen, MatewnckWindow *prev_window, TopMenuWidget *self)
{
	if (!gtk_widget_get_visible(GTK_WIDGET(self))) {
		return;
	}
	MatewnckWindow *window = matewnck_screen_get_active_window(screen);
	if (window) {
		topmenu_widget_set_followed_window(self, matewnck_window_get_xid(window));
	} else {
		// No active window?
	}
}
#endif

static GdkFilterReturn handle_gdk_event(GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
	TopMenuWidget *self = TOPMENU_WIDGET(data);
	XEvent *e = (XEvent*) xevent;

	if (e->type == PropertyNotify &&
	        (e->xproperty.atom == self->priv->atom_transient_for ||
	         e->xproperty.atom == self->priv->atom_window)) {
		// One of the properties we are interested in changed.
		// See if it's one of the windows we're following.
		if (g_queue_find(&self->priv->followed_windows,
		                 GSIZE_TO_POINTER(e->xproperty.window))) {
			// If so, try refollowing the currently followed window in order
			// to see if any window has suddenly grown a menu bar.
			g_debug("One of our followed windows changed");
			Window window = GPOINTER_TO_SIZE(g_queue_peek_tail(&self->priv->followed_windows));
			topmenu_widget_set_followed_window(self, window);
		}
	}

	return GDK_FILTER_CONTINUE;
}

static void topmenu_widget_map(GtkWidget *widget)
{
	TopMenuWidget *self = TOPMENU_WIDGET(widget);
	topmenu_server_register_server_widget(widget);
	topmenu_widget_set_followed_window(self,
	                                   topmenu_widget_get_current_active_window(self));
	GTK_WIDGET_CLASS(topmenu_widget_parent_class)->map(widget);
}

static void topmenu_widget_unmap(GtkWidget *widget)
{
	TopMenuWidget *self = TOPMENU_WIDGET(widget);
	topmenu_widget_set_followed_window(self, None);
	topmenu_server_unregister_server_widget(widget);
	GTK_WIDGET_CLASS(topmenu_widget_parent_class)->unmap(widget);
}

static void topmenu_widget_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	TopMenuWidget *self = TOPMENU_WIDGET(widget);
	GTK_WIDGET_CLASS(topmenu_widget_parent_class)->size_allocate(widget, allocation);
	if (self->socket) {
#if GTK_VERSION == 2
		/* Force a resize of the plug window */
		self->socket->current_width = self->socket->current_height = 0;
#endif
		gtk_widget_size_allocate(GTK_WIDGET(self->socket), allocation);
	}
}

#if GTK_MAJOR_VERSION == 3
static void topmenu_widget_get_preferred_width(GtkWidget *widget, gint *minimal_width, gint *natural_width)
{
	TopMenuWidget *self = TOPMENU_WIDGET(widget);
	if (self->socket) {
		gtk_widget_get_preferred_width(GTK_WIDGET(self->socket), minimal_width, natural_width);
	}
}

static void topmenu_widget_get_preferred_height(GtkWidget *widget, gint *minimal_height, gint *natural_height)
{
	TopMenuWidget *self = TOPMENU_WIDGET(widget);
	if (self->socket) {
		gtk_widget_get_preferred_height(GTK_WIDGET(self->socket), minimal_height, natural_height);
	}
}
#elif GTK_MAJOR_VERSION == 2
static void topmenu_widget_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
	TopMenuWidget *self = TOPMENU_WIDGET(widget);
	if (self->socket) {
		gtk_widget_size_request(GTK_WIDGET(self->socket), requisition);
	}
}
#endif

static void topmenu_widget_dispose(GObject *obj)
{
	TopMenuWidget *self = TOPMENU_WIDGET(obj);
	gdk_window_remove_filter(NULL, handle_gdk_event, self);
	if (self->socket) {
		g_signal_handlers_disconnect_by_data(self->socket, self);
		self->socket = NULL;
	}
	g_queue_clear(&self->priv->followed_windows);
#ifdef HAVE_WNCK
	if (self->priv->wnck_screen) {
		g_signal_handlers_disconnect_by_data(self->priv->wnck_screen, self);
		self->priv->wnck_screen = NULL;
	}
#endif
#ifdef HAVE_MATEWNCK
	if (self->priv->matewnck_screen) {
		g_signal_handlers_disconnect_by_data(self->priv->matewnck_screen, self);
		self->priv->matewnck_screen = NULL;
	}
#endif
	G_OBJECT_CLASS(topmenu_widget_parent_class)->dispose(obj);
}

static void topmenu_widget_class_init(TopMenuWidgetClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->map = topmenu_widget_map;
	widget_class->unmap = topmenu_widget_unmap;
	widget_class->size_allocate = topmenu_widget_size_allocate;
#if GTK_MAJOR_VERSION == 3
	widget_class->get_preferred_width = topmenu_widget_get_preferred_width;
	widget_class->get_preferred_height = topmenu_widget_get_preferred_height;
#elif GTK_MAJOR_VERSION == 2
	widget_class->size_request = topmenu_widget_size_request;
#endif

	GObjectClass *obj_class = G_OBJECT_CLASS(klass);
	obj_class->dispose = topmenu_widget_dispose;

	g_type_class_add_private(klass, sizeof(TopMenuWidgetPrivate));
}

static void topmenu_widget_init(TopMenuWidget *self)
{
	self->priv = TOPMENU_WIDGET_GET_PRIVATE(self);
	self->socket = GTK_SOCKET(gtk_socket_new());
	g_signal_connect_after(self->socket, "realize",
	                       G_CALLBACK(handle_socket_realize), self);
	g_signal_connect(self->socket, "plug-removed",
	                 G_CALLBACK(handle_socket_plug_removed), self);
	self->priv->atom_window = None;
	self->priv->atom_transient_for = None;
	g_queue_init(&self->priv->followed_windows);
#ifdef HAVE_WNCK
	self->priv->wnck_screen = wnck_screen_get_default();
	g_signal_connect(self->priv->wnck_screen, "active-window-changed",
	                 G_CALLBACK(handle_active_wnck_window_changed), self);
#endif
#ifdef HAVE_MATEWNCK
	self->priv->matewnck_screen = matewnck_screen_get_default();
	g_signal_connect(self->priv->matewnck_screen, "active-window-changed",
	                 G_CALLBACK(handle_active_matewnck_window_changed), self);
#endif
	gdk_window_add_filter(NULL, handle_gdk_event, self);
	gtk_container_add(GTK_CONTAINER(self), GTK_WIDGET(self->socket));
}

GtkWidget *topmenu_widget_new(void)
{
	return GTK_WIDGET(g_object_new(TOPMENU_TYPE_WIDGET, NULL));
}
