/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * libpolo-xwm: Polo's XWayland window manager, vendored from Weston's
 * xwayland/ into weston/. PoloXwm owns the X server, PoloXWindow is one X
 * window: its raw ICCCM and EWMH data, and the requests that change them.
 *
 * C side of vapi/polo-xwm.vapi. Vala sees the XWM only through these.
 */

#ifndef POLO_XWM_H
#define POLO_XWM_H

#include <glib-object.h>
#include "polo-backend.h"

G_BEGIN_DECLS

/* Upstream the XWM is vendored from. */
const char *
polo_xwm_vendored_from(void);

#define POLO_TYPE_XWINDOW (polo_xwindow_get_type())
G_DECLARE_FINAL_TYPE(PoloXWindow, polo_xwindow, POLO, XWINDOW, GObject)

#define POLO_TYPE_XWM (polo_xwm_get_type())
G_DECLARE_FINAL_TYPE(PoloXwm, polo_xwm, POLO, XWM, GObject)

PoloXwm *
polo_xwm_new(PoloBackendServer *server);

/* Start the X server with polo_xwm_start and keep it running, instead of
 * starting it for the first client and letting it exit with the last. */
void
polo_xwm_set_eager(PoloXwm *self, gboolean eager);

/* Listen on a free display, set DISPLAY and XAUTHORITY and emit ready. */
gboolean
polo_xwm_start(PoloXwm *self, GError **error);

const char *
polo_xwm_get_display_name(PoloXwm *self);

/* False until the X server runs. */
gboolean
polo_xwm_is_running(PoloXwm *self);

/* The EWMH root properties; they do nothing while the X server is not running. */
/* Any window's properties, for those without a PoloXWindow; empty if absent. */
guint32 *
polo_xwm_get_cardinals(PoloXwm *self, guint32 xid, const char *property, int *n);
/* Signals "focus-lost" when X focus fell to None. */
void
polo_xwm_clear_focus(PoloXwm *self);
void
polo_xwm_set_cardinals(PoloXwm *self, guint32 xid, const char *property,
		       const guint32 *values, int n);
/* Report PropertyNotify on a window the XWM does not know (_NET_WM_USER_TIME_WINDOW). */
void
polo_xwm_watch_window(PoloXwm *self, guint32 xid);
void
polo_xwm_set_supported(PoloXwm *self, const char *const *atoms, int n);
void
polo_xwm_set_gtk_workareas(PoloXwm *self, int desktop, const guint32 *areas, int n);
/* _NET_DESKTOP_LAYOUT as a pager left it; free with g_free (). */
guint32 *
polo_xwm_get_desktop_layout(PoloXwm *self, int *n);
void
polo_xwm_set_client_list(PoloXwm *self, const guint32 *windows, int n);
void
polo_xwm_set_client_list_stacking(PoloXwm *self, const guint32 *windows, int n);
void
polo_xwm_set_active_window(PoloXwm *self, guint32 window);
void
polo_xwm_set_number_of_desktops(PoloXwm *self, guint32 count);
void
polo_xwm_set_current_desktop(PoloXwm *self, guint32 desktop);
void
polo_xwm_set_desktop_names(PoloXwm *self, const char *const *names, int n);
void
polo_xwm_set_desktop_geometry(PoloXwm *self, guint32 width, guint32 height);
void
polo_xwm_set_desktop_viewport(PoloXwm *self, const guint32 *origins, int n);
void
polo_xwm_set_workarea(PoloXwm *self, const guint32 *areas, int n);
void
polo_xwm_set_showing_desktop(PoloXwm *self, gboolean showing);

/* The X window behind a desktop surface, or NULL for a native one. */
PoloXWindow *
polo_xwm_window_for_toplevel(PoloXwm *self, PoloBackendToplevel *toplevel);

guint32
polo_xwindow_get_id(PoloXWindow *self);
guint32
polo_xwindow_get_frame_id(PoloXWindow *self);
/* _NET_WM_NAME, or NULL when absent or not UTF-8. */
const char *
polo_xwindow_get_net_wm_name(PoloXWindow *self);
/* WM_NAME in UTF-8. */
const char *
polo_xwindow_get_wm_name(PoloXWindow *self);
const char *
polo_xwindow_get_res_name(PoloXWindow *self);
const char *
polo_xwindow_get_res_class(PoloXWindow *self);
const char *
polo_xwindow_get_sm_client_id(PoloXWindow *self);
guint32
polo_xwindow_get_client_leader(PoloXWindow *self);
/* WM_HINTS; input is TRUE when the client does not say. */
gboolean
polo_xwindow_get_input(PoloXWindow *self);
gboolean
polo_xwindow_get_initially_iconic(PoloXWindow *self);
gboolean
polo_xwindow_get_urgent(PoloXWindow *self);
guint32
polo_xwindow_get_window_group(PoloXWindow *self);
/* _MOTIF_WM_HINTS; all zero when absent. */
void
polo_xwindow_get_motif_hints(PoloXWindow *self, guint32 *flags,
			     guint32 *functions, guint32 *decorations);
/* WM_TRANSIENT_FOR names the root window. */
gboolean
polo_xwindow_get_transient_for_root(PoloXWindow *self);
gboolean
polo_xwindow_get_supports_ping(PoloXWindow *self);
gboolean
polo_xwindow_get_supports_sync_request(PoloXWindow *self);
const char *
polo_xwindow_get_machine(PoloXWindow *self);
int
polo_xwindow_get_pid(PoloXWindow *self);
/* The _NET_WM_WINDOW_TYPE atom's name, or NULL. */
char *
polo_xwindow_get_window_type(PoloXWindow *self);
PoloXWindow *
polo_xwindow_get_transient_for(PoloXWindow *self);
gboolean
polo_xwindow_get_override_redirect(PoloXWindow *self);
gboolean
polo_xwindow_get_fullscreen(PoloXWindow *self);
gboolean
polo_xwindow_get_maximized_vert(PoloXWindow *self);
gboolean
polo_xwindow_get_maximized_horz(PoloXWindow *self);
gboolean
polo_xwindow_get_supports_delete(PoloXWindow *self);
gboolean
polo_xwindow_get_supports_take_focus(PoloXWindow *self);
int
polo_xwindow_get_decorations(PoloXWindow *self);
void
polo_xwindow_get_geometry(PoloXWindow *self, int *x, int *y, int *width,
			  int *height);
void
polo_xwindow_get_size_hints(PoloXWindow *self, guint32 *flags, int *min_width,
			    int *min_height, int *max_width, int *max_height,
			    int *width_inc, int *height_inc, int *base_width,
			    int *base_height, int *win_gravity);
void
polo_xwindow_get_aspect(PoloXWindow *self, int *min_x, int *min_y, int *max_x,
			int *max_y);

char *
polo_xwindow_name_of_atom(PoloXWindow *self, guint32 atom);
guint32 *
polo_xwindow_get_cardinals(PoloXWindow *self, const char *property, int *n);
char **
polo_xwindow_get_atoms(PoloXWindow *self, const char *property, int *n);
char *
polo_xwindow_get_text(PoloXWindow *self, const char *property);
/* The client draws these margins itself (_GTK_FRAME_EXTENTS): they leave the window geometry. */
void
polo_xwindow_set_frame_inset(PoloXWindow *self, int left, int right, int top, int bottom);
/* configure-requested reaches Polo instead of the XWM granting it. */
void
polo_xwindow_set_intercept_configure(PoloXWindow *self, gboolean intercept);
void
polo_xwindow_set_net_wm_desktop(PoloXWindow *self, guint32 desktop);

void
polo_xwindow_configure(PoloXWindow *self, int x, int y, int width, int height);
/* Signals "focus-in" when the client focused itself. */
void
polo_xwindow_set_input_focus(PoloXWindow *self, guint32 time);
void
polo_xwindow_send_take_focus(PoloXWindow *self, guint32 time);
void
polo_xwindow_send_delete(PoloXWindow *self, guint32 time);
void
polo_xwindow_send_ping(PoloXWindow *self, guint32 time);
void
polo_xwindow_set_visible_name(PoloXWindow *self, const char *name);
void
polo_xwindow_set_wm_state(PoloXWindow *self, int state);
void
polo_xwindow_set_net_wm_state(PoloXWindow *self, const char *const *atoms, int n);
void
polo_xwindow_set_frame_extents(PoloXWindow *self, guint32 left, guint32 right,
			       guint32 top, guint32 bottom);
void
polo_xwindow_set_allowed_actions(PoloXWindow *self, const char *const *atoms,
				 int n);
void
polo_xwindow_kill_client(PoloXWindow *self);

G_END_DECLS

#endif /* POLO_XWM_H */
