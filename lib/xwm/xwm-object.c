/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The GObject facade over the vendored XWM. Starting Xwayland follows Weston's
 * frontend/xwayland.c spawn_xserver ().
 */

#include "polo-xwm.h"

#include <errno.h>
#include <gio/gio.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libweston/libweston.h>
#include <wayland-server.h>
#include <xcb/xcb.h>

#include "polo-backend-private.h"
#include "polo-xwm-private.h"
#include "props.h"

const char *
polo_xwm_vendored_from(void)
{
	return "Weston 16.0.1 xwayland/";
}

/* ---- PoloXWindow --------------------------------------------------------- */

struct _PoloXWindow {
	GObject parent_instance;
	struct weston_wm_window *window;
	struct polo_xprops props;
	gboolean props_dirty;
	gboolean intercept_configure;
};

enum {
	WINDOW_SIGNAL_DESTROYED,
	WINDOW_SIGNAL_PROPERTY_CHANGED,
	WINDOW_SIGNAL_GEOMETRY_CHANGED,
	WINDOW_SIGNAL_PONG,
	WINDOW_SIGNAL_UNMAPPED,
	WINDOW_SIGNAL_FOCUS_IN,
	WINDOW_SIGNAL_CLIENT_MESSAGE,
	WINDOW_SIGNAL_CONFIGURE_REQUESTED,
	N_WINDOW_SIGNALS
};

static guint window_signals[N_WINDOW_SIGNALS];

G_DEFINE_FINAL_TYPE(PoloXWindow, polo_xwindow, G_TYPE_OBJECT)

static void
polo_xwindow_finalize(GObject *object)
{
	polo_xprops_clear(&POLO_XWINDOW(object)->props);
	G_OBJECT_CLASS(polo_xwindow_parent_class)->finalize(object);
}

static void
polo_xwindow_class_init(PoloXWindowClass *klass)
{
	G_OBJECT_CLASS(klass)->finalize = polo_xwindow_finalize;
	window_signals[WINDOW_SIGNAL_FOCUS_IN] = g_signal_new(
		"focus-in", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
	window_signals[WINDOW_SIGNAL_CLIENT_MESSAGE] = g_signal_new(
		"client-message", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 6, G_TYPE_STRING, G_TYPE_UINT,
		G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
	window_signals[WINDOW_SIGNAL_CONFIGURE_REQUESTED] = g_signal_new(
		"configure-requested", G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 5,
		G_TYPE_UINT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);
	window_signals[WINDOW_SIGNAL_UNMAPPED] = g_signal_new(
		"unmapped", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
	window_signals[WINDOW_SIGNAL_PONG] = g_signal_new(
		"pong", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 0);
	window_signals[WINDOW_SIGNAL_DESTROYED] = g_signal_new(
		"destroyed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
	window_signals[WINDOW_SIGNAL_PROPERTY_CHANGED] = g_signal_new(
		"property-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
	window_signals[WINDOW_SIGNAL_GEOMETRY_CHANGED] = g_signal_new(
		"geometry-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_xwindow_init(PoloXWindow *self)
{
	self->props_dirty = TRUE;
}

static gboolean
window_info(PoloXWindow *self, struct polo_wm_window_info *info)
{
	memset(info, 0, sizeof *info);
	g_return_val_if_fail(POLO_IS_XWINDOW(self), FALSE);
	if (!self->window)
		return FALSE;
	polo_wm_window_get_info(self->window, info);
	return TRUE;
}

#define INFO_GETTER(type, name, field)                                         \
	type polo_xwindow_get_##name(PoloXWindow *self)                        \
	{                                                                      \
		struct polo_wm_window_info info;                               \
		window_info(self, &info);                                      \
		return info.field;                                             \
	}

INFO_GETTER(guint32, id, id)
INFO_GETTER(guint32, frame_id, frame_id)
INFO_GETTER(const char *, machine, machine)
INFO_GETTER(int, pid, pid)
INFO_GETTER(PoloXWindow *, transient_for, transient_for)
INFO_GETTER(gboolean, override_redirect, override_redirect)
INFO_GETTER(gboolean, fullscreen, fullscreen)
INFO_GETTER(gboolean, maximized_vert, maximized_vert)
INFO_GETTER(gboolean, maximized_horz, maximized_horz)
INFO_GETTER(int, decorations, decorations)

static struct polo_xprop_atoms prop_atoms;
static xcb_connection_t *prop_atoms_conn;

static const struct polo_xprops *
window_props(PoloXWindow *self)
{
	g_return_val_if_fail(POLO_IS_XWINDOW(self), NULL);
	if (self->window && self->props_dirty) {
		xcb_connection_t *conn = polo_wm_window_get_conn(self->window);
		struct polo_wm_window_info info;

		if (prop_atoms_conn != conn) {
			polo_xprop_atoms_init(&prop_atoms, conn);
			prop_atoms_conn = conn;
		}
		polo_wm_window_get_info(self->window, &info);
		polo_xprops_load(&self->props, conn, &prop_atoms, info.id);
		self->props_dirty = FALSE;
	}
	return &self->props;
}

#define PROPS_GETTER(type, name)                                               \
	type polo_xwindow_get_##name(PoloXWindow *self)                        \
	{                                                                      \
		return window_props(self)->name;                               \
	}

PROPS_GETTER(const char *, net_wm_name)
PROPS_GETTER(const char *, wm_name)
PROPS_GETTER(const char *, res_name)
PROPS_GETTER(const char *, res_class)
PROPS_GETTER(const char *, sm_client_id)
PROPS_GETTER(guint32, client_leader)
PROPS_GETTER(gboolean, input)
PROPS_GETTER(gboolean, initially_iconic)
PROPS_GETTER(gboolean, urgent)
PROPS_GETTER(guint32, window_group)
PROPS_GETTER(gboolean, supports_delete)
PROPS_GETTER(gboolean, supports_take_focus)
PROPS_GETTER(gboolean, supports_ping)
PROPS_GETTER(gboolean, supports_sync_request)

void
polo_xwindow_get_motif_hints(PoloXWindow *self, guint32 *flags,
			     guint32 *functions, guint32 *decorations)
{
	const struct polo_xprops *props = window_props(self);

	*flags = props->mwm_flags;
	*functions = props->mwm_functions;
	*decorations = props->mwm_decorations;
}

gboolean
polo_xwindow_get_transient_for_root(PoloXWindow *self)
{
	const struct polo_xprops *props = window_props(self);

	return self->window && props->transient_for != 0 &&
		polo_wm_window_is_root(self->window, props->transient_for);
}

char *
polo_xwindow_get_window_type(PoloXWindow *self)
{
	struct polo_wm_window_info info;

	if (!window_info(self, &info))
		return NULL;
	return polo_wm_atom_name(self->window, info.window_type);
}

void
polo_xwindow_get_geometry(PoloXWindow *self, int *x, int *y, int *width,
			  int *height)
{
	struct polo_wm_window_info info;

	window_info(self, &info);
	*x = info.x;
	*y = info.y;
	*width = info.width;
	*height = info.height;
}

void
polo_xwindow_get_size_hints(PoloXWindow *self, guint32 *flags, int *min_width,
			    int *min_height, int *max_width, int *max_height,
			    int *width_inc, int *height_inc, int *base_width,
			    int *base_height, int *win_gravity)
{
	struct polo_wm_window_info info;

	window_info(self, &info);
	*flags = info.hints_flags;
	*min_width = info.min_width;
	*min_height = info.min_height;
	*max_width = info.max_width;
	*max_height = info.max_height;
	*width_inc = info.width_inc;
	*height_inc = info.height_inc;
	*base_width = info.base_width;
	*base_height = info.base_height;
	*win_gravity = info.win_gravity;
}

void
polo_xwindow_get_aspect(PoloXWindow *self, int *min_x, int *min_y, int *max_x,
			int *max_y)
{
	struct polo_wm_window_info info;

	window_info(self, &info);
	*min_x = info.min_aspect_x;
	*min_y = info.min_aspect_y;
	*max_x = info.max_aspect_x;
	*max_y = info.max_aspect_y;
}

#define RETURN_IF_GONE(self)                                                   \
	do {                                                                   \
		g_return_if_fail(POLO_IS_XWINDOW(self));                       \
		if (!(self)->window)                                           \
			return;                                                \
	} while (0)

void
polo_xwindow_configure(PoloXWindow *self, int x, int y, int width, int height)
{
	RETURN_IF_GONE(self);
	polo_wm_window_configure(self->window, x, y, width, height);
}

void
polo_xwindow_set_input_focus(PoloXWindow *self, guint32 time)
{
	RETURN_IF_GONE(self);
	polo_wm_window_set_input_focus(self->window, time);
}

void
polo_xwindow_send_take_focus(PoloXWindow *self, guint32 time)
{
	RETURN_IF_GONE(self);
	polo_wm_window_send_take_focus(self->window, time);
}

void
polo_xwindow_send_delete(PoloXWindow *self, guint32 time)
{
	RETURN_IF_GONE(self);
	polo_wm_window_send_delete(self->window, time);
}

void
polo_xwindow_send_ping(PoloXWindow *self, guint32 time)
{
	RETURN_IF_GONE(self);
	polo_wm_window_send_ping(self->window, time);
}

void
polo_xwindow_set_visible_name(PoloXWindow *self, const char *name)
{
	RETURN_IF_GONE(self);
	polo_wm_window_set_visible_name(self->window, name);
}

void
polo_xwindow_set_wm_state(PoloXWindow *self, int state)
{
	RETURN_IF_GONE(self);
	polo_wm_window_set_wm_state(self->window, state);
}

void
polo_xwindow_set_net_wm_state(PoloXWindow *self, const char *const *atoms,
			      int n)
{
	RETURN_IF_GONE(self);
	polo_wm_window_set_atoms(self->window, "_NET_WM_STATE", atoms, n);
}

void
polo_xwindow_set_frame_extents(PoloXWindow *self, guint32 left, guint32 right,
			       guint32 top, guint32 bottom)
{
	RETURN_IF_GONE(self);
	polo_wm_window_set_frame_extents(self->window, left, right, top,
					 bottom);
}

void
polo_xwindow_set_allowed_actions(PoloXWindow *self, const char *const *atoms,
				 int n)
{
	RETURN_IF_GONE(self);
	polo_wm_window_set_atoms(self->window, "_NET_WM_ALLOWED_ACTIONS", atoms,
				 n);
}

char *
polo_xwindow_name_of_atom(PoloXWindow *self, guint32 atom)
{
	g_return_val_if_fail(POLO_IS_XWINDOW(self), NULL);
	if (!self->window)
		return NULL;
	return g_strdup(polo_wm_atom_name_cached(
		polo_wm_window_get_wm(self->window), atom));
}

guint32 *
polo_xwindow_get_cardinals(PoloXWindow *self, const char *property, int *n)
{
	struct polo_wm_window_info info;

	*n = 0;
	if (!window_info(self, &info))
		return NULL;
	return polo_wm_get_cardinals(polo_wm_window_get_wm(self->window),
				     info.id, property, n);
}

char **
polo_xwindow_get_atoms(PoloXWindow *self, const char *property, int *n)
{
	struct polo_wm_window_info info;

	*n = 0;
	if (!window_info(self, &info))
		return g_new0(char *, 1);
	return polo_wm_get_atom_names(polo_wm_window_get_wm(self->window),
				      info.id, property, n);
}

char *
polo_xwindow_get_text(PoloXWindow *self, const char *property)
{
	struct polo_wm_window_info info;

	if (!window_info(self, &info))
		return NULL;
	return polo_wm_get_text(polo_wm_window_get_wm(self->window), info.id,
				property);
}

void
polo_xwindow_set_frame_inset(PoloXWindow *self, int left, int right, int top,
			     int bottom)
{
	RETURN_IF_GONE(self);
	polo_wm_window_set_frame_inset(self->window, left, right, top, bottom);
}

void
polo_xwindow_set_intercept_configure(PoloXWindow *self, gboolean intercept)
{
	g_return_if_fail(POLO_IS_XWINDOW(self));
	self->intercept_configure = intercept;
}

void
polo_xwindow_set_net_wm_desktop(PoloXWindow *self, guint32 desktop)
{
	struct polo_wm_window_info info;

	if (!window_info(self, &info))
		return;
	polo_wm_set_window_cardinals(polo_wm_window_get_wm(self->window),
				     info.id, "_NET_WM_DESKTOP", &desktop, 1);
}

void
polo_xwindow_kill_client(PoloXWindow *self)
{
	RETURN_IF_GONE(self);
	polo_wm_window_kill_client(self->window);
}

/* ---- PoloXwm ------------------------------------------------------------- */

#define MAX_CRASHES 5
#define CRASH_WINDOW_US (60 * G_USEC_PER_SEC)

struct _PoloXwm {
	GObject parent_instance;

	PoloBackendServer *server;
	struct weston_xserver *wxs;
	char *display_name;

	gboolean eager;
	char *xauth_path;
	int64_t crashes[MAX_CRASHES];
	int n_crashes;

	GPid pid;
	guint child_watch;
	int wm_fd;
	int display_fd;
	struct wl_event_source *display_fd_source;
	GSource *x_queue_source;
};

enum {
	SIGNAL_WINDOW_CREATED,
	SIGNAL_READY,
	SIGNAL_STARTED,
	SIGNAL_EXITED,
	SIGNAL_ROOT_PROPERTY_CHANGED,
	SIGNAL_ROOT_MESSAGE,
	SIGNAL_FOCUS_LOST,
	SIGNAL_WATCHED_PROPERTY_CHANGED,
	SIGNAL_BELL,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* The vendored XWM has no place for a user pointer on its windows' creation
 * path, and a compositor has one X server. */
static PoloXwm *current;

G_DEFINE_FINAL_TYPE(PoloXwm, polo_xwm, G_TYPE_OBJECT)

static void
child_watch_cancel(PoloXwm *self)
{
	if (self->child_watch) {
		g_source_remove(self->child_watch);
		self->child_watch = 0;
	}
	if (self->pid) {
		g_spawn_close_pid(self->pid);
		self->pid = 0;
	}
}

static void
cancel_pending_start(PoloXwm *self)
{
	if (self->display_fd_source) {
		wl_event_source_remove(self->display_fd_source);
		self->display_fd_source = NULL;
		close(self->display_fd);
	}
	if (self->wm_fd >= 0) {
		close(self->wm_fd);
		self->wm_fd = -1;
	}
}

static void
remove_xauth(PoloXwm *self)
{
	if (self->xauth_path) {
		unlink(self->xauth_path);
		g_clear_pointer(&self->xauth_path, g_free);
	}
}

static void
polo_xwm_finalize(GObject *object)
{
	PoloXwm *self = POLO_XWM(object);

	child_watch_cancel(self);
	cancel_pending_start(self);
	remove_xauth(self);
	if (self->x_queue_source) {
		g_source_destroy(self->x_queue_source);
		g_source_unref(self->x_queue_source);
	}
	if (current == self)
		current = NULL;
	g_clear_object(&self->server);
	g_free(self->display_name);
	G_OBJECT_CLASS(polo_xwm_parent_class)->finalize(object);
}

static void
polo_xwm_class_init(PoloXwmClass *klass)
{
	G_OBJECT_CLASS(klass)->finalize = polo_xwm_finalize;
	signals[SIGNAL_WINDOW_CREATED] = g_signal_new(
		"window-created", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 1, POLO_TYPE_XWINDOW);
	signals[SIGNAL_READY] = g_signal_new(
		"ready", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
	signals[SIGNAL_STARTED] = g_signal_new(
		"started", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_FOCUS_LOST] = g_signal_new(
		"focus-lost", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_ROOT_MESSAGE] = g_signal_new(
		"root-message", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);
	signals[SIGNAL_WATCHED_PROPERTY_CHANGED] = g_signal_new(
		"watched-property-changed", G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
		G_TYPE_UINT, G_TYPE_STRING);
	signals[SIGNAL_BELL] = g_signal_new(
		"bell", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 1, POLO_TYPE_XWINDOW);
	signals[SIGNAL_ROOT_PROPERTY_CHANGED] =
		g_signal_new("root-property-changed", G_TYPE_FROM_CLASS(klass),
			     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			     G_TYPE_NONE, 1, G_TYPE_STRING);
	signals[SIGNAL_EXITED] = g_signal_new(
		"exited", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
polo_xwm_init(PoloXwm *self)
{
	self->wm_fd = -1;
	self->display_fd = -1;
}

PoloXwm *
polo_xwm_new(PoloBackendServer *server)
{
	PoloXwm *self = g_object_new(POLO_TYPE_XWM, NULL);

	self->server = g_object_ref(server);
	return self;
}

struct _PoloXWindow *
polo_xwindow_created(struct weston_wm_window *window)
{
	PoloXWindow *self = g_object_new(POLO_TYPE_XWINDOW, NULL);

	self->window = window;
	if (current)
		g_signal_emit(current, signals[SIGNAL_WINDOW_CREATED], 0, self);
	return self;
}

void
polo_xwindow_destroyed(struct _PoloXWindow *self)
{
	self->window = NULL;
	g_signal_emit(self, window_signals[WINDOW_SIGNAL_DESTROYED], 0);
	g_object_unref(self);
}

void
polo_xwindow_property_changed(struct _PoloXWindow *self, const char *atom)
{
	self->props_dirty = TRUE;
	g_signal_emit(self, window_signals[WINDOW_SIGNAL_PROPERTY_CHANGED], 0,
		      atom);
}

void
polo_xwindow_focus_in(struct _PoloXWindow *self)
{
	g_signal_emit(self, window_signals[WINDOW_SIGNAL_FOCUS_IN], 0);
}

void
polo_xwindow_client_message(struct _PoloXWindow *self, const char *type,
			    const uint32_t *data)
{
	g_signal_emit(self, window_signals[WINDOW_SIGNAL_CLIENT_MESSAGE], 0,
		      type, data[0], data[1], data[2], data[3], data[4]);
}

bool
polo_xwindow_configure_requested(struct _PoloXWindow *self, uint32_t mask,
				 int x, int y, int width, int height)
{
	if (!self->intercept_configure)
		return false;
	g_signal_emit(self, window_signals[WINDOW_SIGNAL_CONFIGURE_REQUESTED],
		      0, mask, x, y, width, height);
	return true;
}

void
polo_xwindow_unmapped(struct _PoloXWindow *self)
{
	g_signal_emit(self, window_signals[WINDOW_SIGNAL_UNMAPPED], 0);
}

void
polo_xwindow_pong(struct _PoloXWindow *self)
{
	g_signal_emit(self, window_signals[WINDOW_SIGNAL_PONG], 0);
}

void
polo_xwindow_geometry_changed(struct _PoloXWindow *self)
{
	g_signal_emit(self, window_signals[WINDOW_SIGNAL_GEOMETRY_CHANGED], 0);
}

void
polo_xserver_destroyed(struct weston_xserver *wxs)
{
	if (current && current->wxs == wxs) {
		current->wxs = NULL;
		remove_xauth(current);
	}
}

/*
 * A synchronous X call moves pending events from the socket into libxcb's
 * queue, where no fd wakes anyone for them. Weston relies on
 * wl_event_source_check (); under GLib the wl_event_loop only runs when its fd
 * is ready, so the queue is looked at before every poll.
 */
static gboolean
x_queue_prepare(GSource *source, gint *timeout)
{
	struct weston_wm *wm = current && current->wxs
		? polo_xserver_get_wm(current->wxs)
		: NULL;

	*timeout = -1;
	return wm && polo_wm_has_queued_event(wm);
}

static gboolean
x_queue_dispatch(GSource *source, GSourceFunc callback, gpointer data)
{
	struct weston_wm *wm = current && current->wxs
		? polo_xserver_get_wm(current->wxs)
		: NULL;

	if (wm)
		polo_wm_process_queued_events(wm);
	return G_SOURCE_CONTINUE;
}

static GSourceFuncs x_queue_funcs = {
	.prepare = x_queue_prepare,
	.dispatch = x_queue_dispatch,
};

static gboolean
too_many_crashes(PoloXwm *self)
{
	int64_t now = g_get_monotonic_time();
	int kept = 0;

	for (int i = 0; i < self->n_crashes; i++)
		if (now - self->crashes[i] < CRASH_WINDOW_US)
			self->crashes[kept++] = self->crashes[i];
	self->n_crashes = kept;
	if (self->n_crashes >= MAX_CRASHES)
		return TRUE;
	self->crashes[self->n_crashes++] = now;
	return FALSE;
}

static void
xserver_child_exited(GPid pid, gint status, gpointer data)
{
	PoloXwm *self = data;
	gboolean missing = WIFEXITED(status) && WEXITSTATUS(status) == 127;
	gboolean crashed =
		!missing && (!WIFEXITED(status) || WEXITSTATUS(status) != 0);

	self->child_watch = 0;
	g_spawn_close_pid(pid);
	self->pid = 0;
	cancel_pending_start(self);
	if (!self->wxs)
		return;

	polo_xserver_exited(self->wxs);
	prop_atoms_conn = NULL;
	if (crashed)
		weston_log("polo-xwm: Xwayland crashed (wait status %d); its X "
			   "clients are gone\n",
			   status);
	g_signal_emit(self, signals[SIGNAL_EXITED], 0, crashed);

	if (missing) {
		weston_log("polo-xwm: Xwayland is not installed; X11 apps "
			   "will not run\n");
		polo_xserver_give_up(self->wxs);
		remove_xauth(self);
		return;
	}
	if (crashed && too_many_crashes(self)) {
		weston_log("polo-xwm: Xwayland crashed %d times within a "
			   "minute; not restarting it\n",
			   MAX_CRASHES + 1);
		polo_xserver_give_up(self->wxs);
		remove_xauth(self);
		return;
	}
	if (self->eager)
		polo_xserver_spawn(self->wxs);
}

static int
handle_display_fd(int fd, uint32_t mask, void *data)
{
	PoloXwm *self = data;
	char buf[64];
	ssize_t n;

	if (mask & WL_EVENT_READABLE) {
		/* Xwayland writes twice and dies if the pipe closes in between.
		 */
		n = read(fd, buf, sizeof buf);
		if (n < 0 && errno != EAGAIN)
			weston_log("read from Xwayland display_fd failed: %s\n",
				   strerror(errno));
		else if (n <= 0 || buf[n - 1] != '\n')
			return 1;
		else if (self->wxs && self->wm_fd >= 0) {
			polo_xserver_loaded(self->wxs, self->wm_fd);
			self->wm_fd = -1;
			g_signal_emit(self, signals[SIGNAL_STARTED], 0);
		}
	}
	wl_event_source_remove(self->display_fd_source);
	self->display_fd_source = NULL;
	close(fd);
	return 0;
}

static void
keep_across_exec(int fd)
{
	fcntl(fd, F_SETFD, 0);
}

static struct wl_client *
spawn_xserver(void *user_data, const char *display, int abstract_fd,
	      int unix_fd)
{
	PoloXwm *self = user_data;
	struct wl_display *wl_display = self->server->display;
	struct wl_client *client;
	int wl_fds[2], wm_fds[2], display_pipe[2];
	char wl_str[16], wm_str[16], display_str[16], abstract_str[16],
		unix_str[16];
	sigset_t all;
	pid_t pid;

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wl_fds) < 0) {
		weston_log("wl connection socketpair failed\n");
		return NULL;
	}
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wm_fds) < 0) {
		weston_log("X wm connection socketpair failed\n");
		close(wl_fds[0]);
		close(wl_fds[1]);
		return NULL;
	}
	if (pipe2(display_pipe, O_CLOEXEC) < 0) {
		weston_log("pipe creation for displayfd failed\n");
		close(wl_fds[0]);
		close(wl_fds[1]);
		close(wm_fds[0]);
		close(wm_fds[1]);
		return NULL;
	}
	snprintf(wl_str, sizeof wl_str, "%d", wl_fds[1]);
	snprintf(wm_str, sizeof wm_str, "%d", wm_fds[1]);
	snprintf(display_str, sizeof display_str, "%d", display_pipe[1]);
	snprintf(abstract_str, sizeof abstract_str, "%d", abstract_fd);
	snprintf(unix_str, sizeof unix_str, "%d", unix_fd);

	pid = fork();
	if (pid < 0) {
		weston_log("Couldn't start Xwayland: %s\n", strerror(errno));
		close(wl_fds[0]);
		close(wl_fds[1]);
		close(wm_fds[0]);
		close(wm_fds[1]);
		close(display_pipe[0]);
		close(display_pipe[1]);
		return NULL;
	}
	if (pid == 0) {
		sigemptyset(&all);
		sigprocmask(SIG_SETMASK, &all, NULL);
		keep_across_exec(wl_fds[1]);
		keep_across_exec(wm_fds[1]);
		keep_across_exec(display_pipe[1]);
		keep_across_exec(abstract_fd);
		keep_across_exec(unix_fd);
		setenv("WAYLAND_SOCKET", wl_str, 1);
		unsetenv("WAYLAND_DISPLAY");
		execlp("Xwayland", "Xwayland", display, "-rootless", "-auth",
		       self->xauth_path, "-listenfd", abstract_str, "-listenfd",
		       unix_str, "-displayfd", display_str, "-wm", wm_str,
		       self->eager ? NULL : "-terminate", NULL);
		_exit(127);
	}

	client = wl_client_create(wl_display, wl_fds[0]);
	if (client) {
		PoloBackendClient *backend_client =
			g_hash_table_lookup(self->server->clients, client);

		if (backend_client)
			polo_backend_client_set_xserver(backend_client);
	}
	close(wl_fds[1]);
	close(wm_fds[1]);
	close(display_pipe[1]);
	if (!client) {
		weston_log("Couldn't create client for Xwayland\n");
		close(wm_fds[0]);
		close(display_pipe[0]);
		kill(pid, SIGTERM);
		return NULL;
	}

	self->wm_fd = wm_fds[0];
	self->pid = pid;
	self->child_watch = g_child_watch_add(pid, xserver_child_exited, self);
	self->display_fd = display_pipe[0];
	self->display_fd_source = wl_event_loop_add_fd(
		wl_display_get_event_loop(wl_display), display_pipe[0],
		WL_EVENT_READABLE, handle_display_fd, self);
	return client;
}

static void
write_xauth_field(GString *out, const void *data, guint16 length)
{
	guint16 be = GUINT16_TO_BE(length);

	g_string_append_len(out, (const char *)&be, 2);
	g_string_append_len(out, data, length);
}

/* One MIT-MAGIC-COOKIE-1 entry for any host and this display, in the format
 * of libXau's XauWriteAuth (). */
static gboolean
create_xauth(PoloXwm *self, int display, GError **error)
{
	const char *dir = g_get_user_runtime_dir();
	guint8 cookie[16];
	char number[16];
	guint16 family = GUINT16_TO_BE(0xffff);
	g_autofree char *path =
		g_build_filename(dir, "polo-xauth-XXXXXX", NULL);
	g_autoptr(GString) entry = g_string_new(NULL);
	int fd;
	gboolean written;

	if (getrandom(cookie, sizeof cookie, 0) != sizeof cookie) {
		g_set_error_literal(error, G_IO_ERROR,
				    g_io_error_from_errno(errno),
				    "no random data for the X cookie");
		return FALSE;
	}
	snprintf(number, sizeof number, "%d", display);
	g_string_append_len(entry, (const char *)&family, 2);
	write_xauth_field(entry, "", 0);
	write_xauth_field(entry, number, strlen(number));
	write_xauth_field(entry, "MIT-MAGIC-COOKIE-1", 18);
	write_xauth_field(entry, cookie, sizeof cookie);

	fd = g_mkstemp_full(path, O_WRONLY | O_CLOEXEC, 0600);
	if (fd < 0) {
		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
			    "cannot create %s: %s", path, strerror(errno));
		return FALSE;
	}
	written = write(fd, entry->str, entry->len) == (ssize_t)entry->len;
	close(fd);
	if (!written) {
		unlink(path);
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			    "cannot write %s", path);
		return FALSE;
	}
	self->xauth_path = g_steal_pointer(&path);
	setenv("XAUTHORITY", self->xauth_path, 1);
	return TRUE;
}

void
polo_xwm_set_eager(PoloXwm *self, gboolean eager)
{
	g_return_if_fail(POLO_IS_XWM(self));
	self->eager = eager;
}

gboolean
polo_xwm_start(PoloXwm *self, GError **error)
{
	struct weston_compositor *compositor;

	g_return_val_if_fail(POLO_IS_XWM(self), FALSE);
	g_return_val_if_fail(current == NULL, FALSE);

	compositor = self->server->compositor;
	if (!compositor || !compositor->xwayland_interface) {
		g_set_error_literal(error, G_IO_ERROR,
				    G_IO_ERROR_NOT_INITIALIZED,
				    "the server has not started");
		return FALSE;
	}
	compositor->no_xwm_decorations = true;

	self->wxs = polo_xserver_create(compositor);
	if (!self->wxs) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				    "cannot create the XWM");
		return FALSE;
	}
	current = self;
	self->x_queue_source = g_source_new(&x_queue_funcs, sizeof(GSource));
	g_source_attach(self->x_queue_source, NULL);
	if (polo_xserver_listen(self->wxs, self, spawn_xserver) < 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				    "cannot listen on an X display");
		return FALSE;
	}
	if (!create_xauth(self, polo_xserver_get_display(self->wxs), error)) {
		polo_xserver_give_up(self->wxs);
		return FALSE;
	}
	self->display_name =
		g_strdup_printf(":%d", polo_xserver_get_display(self->wxs));
	g_signal_emit(self, signals[SIGNAL_READY], 0, self->display_name);
	if (self->eager)
		polo_xserver_spawn(self->wxs);
	return TRUE;
}

const char *
polo_xwm_get_display_name(PoloXwm *self)
{
	g_return_val_if_fail(POLO_IS_XWM(self), NULL);
	return self->display_name;
}

PoloXWindow *
polo_xwm_window_for_toplevel(PoloXwm *self, PoloBackendToplevel *toplevel)
{
	struct weston_surface *surface;
	struct weston_wm_window *window;

	g_return_val_if_fail(POLO_IS_XWM(self), NULL);
	surface = polo_backend_toplevel_get_weston_surface(toplevel);
	if (!surface || !self->wxs)
		return NULL;
	window = polo_wm_window_for_surface(surface);
	return window ? polo_wm_window_get_polo(window) : NULL;
}

static struct weston_wm *
running_wm(PoloXwm *self)
{
	g_return_val_if_fail(POLO_IS_XWM(self), NULL);
	return self->wxs ? polo_xserver_get_wm(self->wxs) : NULL;
}

gboolean
polo_xwm_is_running(PoloXwm *self)
{
	return running_wm(self) != NULL;
}

static void
set_cardinals(PoloXwm *self, const char *property, uint32_t type,
	      const guint32 *values, int n)
{
	struct weston_wm *wm = running_wm(self);

	if (wm)
		polo_wm_set_root_cardinals(wm, property, type, values, n);
}

void
polo_xwm_set_client_list(PoloXwm *self, const guint32 *windows, int n)
{
	set_cardinals(self, "_NET_CLIENT_LIST", XCB_ATOM_WINDOW, windows, n);
}

void
polo_xwm_set_client_list_stacking(PoloXwm *self, const guint32 *windows, int n)
{
	set_cardinals(self, "_NET_CLIENT_LIST_STACKING", XCB_ATOM_WINDOW,
		      windows, n);
}

void
polo_xwm_set_active_window(PoloXwm *self, guint32 window)
{
	set_cardinals(self, "_NET_ACTIVE_WINDOW", XCB_ATOM_WINDOW, &window, 1);
}

void
polo_xwm_set_number_of_desktops(PoloXwm *self, guint32 count)
{
	set_cardinals(self, "_NET_NUMBER_OF_DESKTOPS", XCB_ATOM_CARDINAL,
		      &count, 1);
}

void
polo_xwm_set_current_desktop(PoloXwm *self, guint32 desktop)
{
	set_cardinals(self, "_NET_CURRENT_DESKTOP", XCB_ATOM_CARDINAL, &desktop,
		      1);
}

void
polo_xwm_set_desktop_names(PoloXwm *self, const char *const *names, int n)
{
	struct weston_wm *wm = running_wm(self);
	GString *value;

	if (!wm)
		return;
	value = g_string_new(NULL);
	for (int i = 0; i < n; i++)
		g_string_append_len(value, names[i], strlen(names[i]) + 1);
	polo_wm_set_root_utf8(wm, "_NET_DESKTOP_NAMES", value->str, value->len);
	g_string_free(value, TRUE);
}

void
polo_xwm_set_desktop_geometry(PoloXwm *self, guint32 width, guint32 height)
{
	guint32 values[2] = { width, height };

	set_cardinals(self, "_NET_DESKTOP_GEOMETRY", XCB_ATOM_CARDINAL, values,
		      2);
}

void
polo_xwm_set_desktop_viewport(PoloXwm *self, const guint32 *origins, int n)
{
	set_cardinals(self, "_NET_DESKTOP_VIEWPORT", XCB_ATOM_CARDINAL, origins,
		      n);
}

void
polo_xwm_set_workarea(PoloXwm *self, const guint32 *areas, int n)
{
	set_cardinals(self, "_NET_WORKAREA", XCB_ATOM_CARDINAL, areas, n);
}

void
polo_xwm_set_showing_desktop(PoloXwm *self, gboolean showing)
{
	guint32 value = showing ? 1 : 0;

	set_cardinals(self, "_NET_SHOWING_DESKTOP", XCB_ATOM_CARDINAL, &value,
		      1);
}

void
polo_xwm_set_supported(PoloXwm *self, const char *const *atoms, int n)
{
	struct weston_wm *wm = running_wm(self);

	if (wm)
		polo_wm_set_root_atoms(wm, "_NET_SUPPORTED", atoms, n);
}

void
polo_xwm_set_gtk_workareas(PoloXwm *self, int desktop, const guint32 *areas,
			   int n)
{
	g_autofree char *name = g_strdup_printf("_GTK_WORKAREAS_D%d", desktop);

	set_cardinals(self, name, XCB_ATOM_CARDINAL, areas, n);
}

guint32 *
polo_xwm_get_desktop_layout(PoloXwm *self, int *n)
{
	struct weston_wm *wm = running_wm(self);
	uint32_t *values;
	guint32 *copy;

	*n = 0;
	if (!wm)
		return NULL;
	values = polo_wm_get_root_cardinals(wm, "_NET_DESKTOP_LAYOUT", n);
	copy = g_memdup2(values, *n * sizeof *values);
	free(values);
	return copy;
}

void
polo_xwm_root_property_changed(const char *atom)
{
	if (current)
		g_signal_emit(current, signals[SIGNAL_ROOT_PROPERTY_CHANGED], 0,
			      atom);
}

void
polo_xwm_bell(struct _PoloXWindow *window)
{
	if (current)
		g_signal_emit(current, signals[SIGNAL_BELL], 0, window);
}

void
polo_xwm_focus_lost(void)
{
	if (current)
		g_signal_emit(current, signals[SIGNAL_FOCUS_LOST], 0);
}

void
polo_xwm_clear_focus(PoloXwm *self)
{
	struct weston_wm *wm = running_wm(self);

	if (wm)
		polo_wm_clear_focus(wm);
}

void
polo_xwm_root_message(const char *type, const uint32_t *data)
{
	if (current)
		g_signal_emit(current, signals[SIGNAL_ROOT_MESSAGE], 0, type,
			      data[0]);
}

void
polo_xwm_watched_property_changed(uint32_t xid, const char *atom)
{
	if (current)
		g_signal_emit(current, signals[SIGNAL_WATCHED_PROPERTY_CHANGED],
			      0, xid, atom);
}

guint32 *
polo_xwm_get_cardinals(PoloXwm *self, guint32 xid, const char *property, int *n)
{
	struct weston_wm *wm = running_wm(self);

	*n = 0;
	return wm ? polo_wm_get_cardinals(wm, xid, property, n) : NULL;
}

void
polo_xwm_set_cardinals(PoloXwm *self, guint32 xid, const char *property,
		       const guint32 *values, int n)
{
	struct weston_wm *wm = running_wm(self);

	if (wm)
		polo_wm_set_window_cardinals(wm, xid, property, values, n);
}

void
polo_xwm_watch_window(PoloXwm *self, guint32 xid)
{
	struct weston_wm *wm = running_wm(self);

	if (wm)
		polo_wm_watch_window(wm, xid);
}
