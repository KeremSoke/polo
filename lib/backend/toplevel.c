/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PoloBackendToplevel: one xdg_toplevel (via libweston-desktop) and the
 * weston_view that shows it. Requests become signals; the Vala
 * xdg adapter decides what to do with them. Popups are handled entirely by
 * libweston-desktop, which positions them relative to their parent's view.
 */

#include <libweston/libweston.h>
#include <libweston/desktop.h>

#include "polo-backend-private.h"

struct _PoloBackendToplevel {
	GObject parent_instance;

	PoloBackendServer *server;
	struct weston_desktop_surface *desktop_surface; /* NULL once gone */
	PoloBackendView *view;
	GPtrArray *subsurface_views;
	struct wl_listener metadata_listener;
	char *title;
	char *app_id;
	char *icon_name;
	GBytes *icon_pixels;
	int icon_size;
	gboolean server_decorated;
};

enum {
	PROP_0,
	PROP_TITLE,
	PROP_APP_ID,
	N_PROPS,
};

static GParamSpec *props[N_PROPS];

enum {
	SIGNAL_COMMITTED,
	SIGNAL_REQUEST_MOVE,
	SIGNAL_REQUEST_RESIZE,
	SIGNAL_REQUEST_MAXIMIZE,
	SIGNAL_REQUEST_FULLSCREEN,
	SIGNAL_REQUEST_MINIMIZE,
	SIGNAL_PARENT_CHANGED,
	SIGNAL_MODAL_HINT_CHANGED,
	SIGNAL_DECORATION_CHANGED,
	SIGNAL_REQUEST_WINDOW_MENU,
	SIGNAL_PING_TIMEOUT,
	SIGNAL_PONG,
	SIGNAL_ICON_CHANGED,
	SIGNAL_CLOSING,
	SIGNAL_DESTROYED,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(PoloBackendToplevel, polo_backend_toplevel, G_TYPE_OBJECT)

static PoloBackendToplevel *
from_desktop_surface(struct weston_desktop_surface *surface)
{
	return weston_desktop_surface_get_user_data(surface);
}

PoloBackendToplevel *
polo_backend_toplevel_from_surface(struct weston_surface *surface)
{
	struct weston_desktop_surface *ds;

	if (!surface || !weston_surface_is_desktop_surface(surface))
		return NULL;

	ds = weston_surface_get_desktop_surface(surface);
	return ds ? from_desktop_surface(ds) : NULL;
}

static PoloBackendSeat *
seat_handle(PoloBackendToplevel *self, struct weston_seat *seat)
{
	return polo_backend_server_lookup_seat(self->server, seat);
}

static gboolean
str_changed(char **cache, const char *value)
{
	if (g_strcmp0(*cache, value) == 0)
		return FALSE;

	g_free(*cache);
	*cache = g_strdup(value);
	return TRUE;
}

static void
metadata_changed(struct wl_listener *listener, void *data)
{
	PoloBackendToplevel *self =
		wl_container_of(listener, self, metadata_listener);

	if (str_changed(
		    &self->title,
		    weston_desktop_surface_get_title(self->desktop_surface)))
		g_object_notify_by_pspec(G_OBJECT(self), props[PROP_TITLE]);
	if (str_changed(
		    &self->app_id,
		    weston_desktop_surface_get_app_id(self->desktop_surface)))
		g_object_notify_by_pspec(G_OBJECT(self), props[PROP_APP_ID]);
}

static void
surface_added(struct weston_desktop_surface *desktop_surface, void *data)
{
	PoloBackendServer *server = data;
	PoloBackendToplevel *self;
	struct weston_view *view;

	view = weston_desktop_surface_create_view(desktop_surface);
	if (!view) {
		struct wl_client *client = weston_desktop_client_get_client(
			weston_desktop_surface_get_client(desktop_surface));

		if (client)
			wl_client_post_no_memory(client);
		return;
	}

	self = g_object_new(POLO_BACKEND_TYPE_TOPLEVEL, NULL);
	self->server = server;
	self->desktop_surface = desktop_surface;
	self->view = polo_backend_view_new(server, view, self);
	self->title =
		g_strdup(weston_desktop_surface_get_title(desktop_surface));
	self->app_id =
		g_strdup(weston_desktop_surface_get_app_id(desktop_surface));
	self->server_decorated = g_hash_table_remove(
		server->early_server_decorations,
		weston_desktop_surface_get_surface(desktop_surface));
	weston_desktop_surface_set_user_data(desktop_surface, self);

	self->metadata_listener.notify = metadata_changed;
	weston_desktop_surface_add_metadata_listener(desktop_surface,
						     &self->metadata_listener);

	polo_backend_server_emit_new_toplevel(server, self);
	polo_backend_xdg_foreign_surface_added(server);
	polo_backend_xdg_dialog_surface_added(server);
	polo_backend_gtk_shell_surface_added(server);
}

static void
surface_removed(struct weston_desktop_surface *desktop_surface, void *data)
{
	PoloBackendToplevel *self = from_desktop_surface(desktop_surface);
	struct weston_view *view;

	if (!self)
		return;

	g_signal_emit(self, signals[SIGNAL_CLOSING], 0);
	wl_list_remove(&self->metadata_listener.link);
	weston_desktop_surface_set_user_data(desktop_surface, NULL);

	view = polo_backend_view_get_weston_view(self->view);
	for (guint i = 0;
	     self->subsurface_views && i < self->subsurface_views->len; i++) {
		PoloBackendView *subsurface_view =
			g_ptr_array_index(self->subsurface_views, i);
		polo_backend_view_destroy(subsurface_view);
	}
	g_clear_pointer(&self->subsurface_views, g_ptr_array_unref);
	polo_backend_view_destroy(self->view);
	weston_desktop_surface_unlink_view(view);
	weston_view_destroy(view);

	self->desktop_surface = NULL;
	g_signal_emit(self, signals[SIGNAL_DESTROYED], 0);
	g_object_unref(self);
}

static void
surface_committed(struct weston_desktop_surface *desktop_surface,
		  struct weston_coord_surface buf_offset, void *data)
{
	PoloBackendToplevel *self = from_desktop_surface(desktop_surface);
	struct weston_view *view;

	if (!self)
		return;

	polo_backend_toplevel_icon_commit(self->server, desktop_surface);

	/* A buffer attached with an offset moves the surface origin; that
	 * is protocol mechanics, not policy (Weston kiosk-shell does the
	 * same). */
	view = polo_backend_view_get_weston_view(self->view);
	if (weston_surface_is_mapped(view->surface) &&
	    (buf_offset.c.x != 0 || buf_offset.c.y != 0)) {
		struct weston_coord_global pos =
			weston_view_get_pos_offset_global(view);

		weston_view_set_position_with_offset(view, pos, buf_offset);
		weston_view_update_transform(view);
	}

	g_signal_emit(self, signals[SIGNAL_COMMITTED], 0);
}

static void
surface_move(struct weston_desktop_surface *desktop_surface,
	     struct weston_seat *seat, uint32_t serial, void *data)
{
	PoloBackendToplevel *self = from_desktop_surface(desktop_surface);
	PoloBackendSeat *handle;

	if (!self || !(handle = seat_handle(self, seat)))
		return;
	g_signal_emit(self, signals[SIGNAL_REQUEST_MOVE], 0, handle, serial);
}

static void
surface_resize(struct weston_desktop_surface *desktop_surface,
	       struct weston_seat *seat, uint32_t serial,
	       enum weston_desktop_surface_edge edges, void *data)
{
	PoloBackendToplevel *self = from_desktop_surface(desktop_surface);
	PoloBackendSeat *handle;

	if (!self || !(handle = seat_handle(self, seat)))
		return;
	g_signal_emit(self, signals[SIGNAL_REQUEST_RESIZE], 0, handle, serial,
		      (guint)edges);
}

static void
surface_maximized_requested(struct weston_desktop_surface *desktop_surface,
			    bool maximized, void *data)
{
	PoloBackendToplevel *self = from_desktop_surface(desktop_surface);

	if (self)
		g_signal_emit(self, signals[SIGNAL_REQUEST_MAXIMIZE], 0,
			      maximized);
}

static void
surface_fullscreen_requested(struct weston_desktop_surface *desktop_surface,
			     bool fullscreen, struct weston_output *output,
			     void *data)
{
	PoloBackendToplevel *self = from_desktop_surface(desktop_surface);

	if (self)
		g_signal_emit(self, signals[SIGNAL_REQUEST_FULLSCREEN], 0,
			      fullscreen);
}

static void
surface_minimized_requested(struct weston_desktop_surface *desktop_surface,
			    void *data)
{
	PoloBackendToplevel *self = from_desktop_surface(desktop_surface);

	if (self)
		g_signal_emit(self, signals[SIGNAL_REQUEST_MINIMIZE], 0);
}

/* xdg_toplevel.set_parent. libweston-desktop does not keep it (its
 * get_parent () is about popups), so the new parent travels in the
 * signal. */
static void
surface_set_parent(struct weston_desktop_surface *desktop_surface,
		   struct weston_desktop_surface *parent, void *data)
{
	PoloBackendToplevel *self = from_desktop_surface(desktop_surface);

	if (self)
		g_signal_emit(self, signals[SIGNAL_PARENT_CHANGED], 0,
			      parent ? from_desktop_surface(parent) : NULL);
}

void
polo_backend_toplevel_set_icon(struct weston_desktop_surface *surface,
			       const char *name, GBytes *pixels, int size)
{
	PoloBackendToplevel *self = from_desktop_surface(surface);

	if (!self)
		return;
	g_free(self->icon_name);
	self->icon_name = g_strdup(name);
	g_clear_pointer(&self->icon_pixels, g_bytes_unref);
	self->icon_pixels = pixels ? g_bytes_ref(pixels) : NULL;
	self->icon_size = size;
	g_signal_emit(self, signals[SIGNAL_ICON_CHANGED], 0);
}

const char *
polo_backend_toplevel_get_icon_name(PoloBackendToplevel *toplevel)
{
	return toplevel->icon_name;
}

GBytes *
polo_backend_toplevel_get_icon_pixels(PoloBackendToplevel *toplevel, int *size)
{
	*size = toplevel->icon_size;
	return toplevel->icon_pixels;
}

gboolean
polo_backend_toplevel_foreign_parent(struct weston_desktop_surface *child,
				     struct weston_desktop_surface *parent)
{
	PoloBackendToplevel *self = from_desktop_surface(child);
	PoloBackendToplevel *other =
		parent ? from_desktop_surface(parent) : NULL;

	if (!self || (parent && !other))
		return FALSE;
	g_signal_emit(self, signals[SIGNAL_PARENT_CHANGED], 0, other);
	return TRUE;
}

gboolean
polo_backend_toplevel_set_modal_hint(struct weston_desktop_surface *surface,
				     gboolean modal)
{
	PoloBackendToplevel *self = from_desktop_surface(surface);

	if (!self)
		return FALSE;
	g_signal_emit(self, signals[SIGNAL_MODAL_HINT_CHANGED], 0, modal);
	return TRUE;
}

static void
surface_show_window_menu(struct weston_desktop_surface *desktop_surface,
			 struct weston_seat *seat,
			 struct weston_coord_surface offset, void *data)
{
	PoloBackendToplevel *self = from_desktop_surface(desktop_surface);
	PoloBackendSeat *handle;
	struct weston_coord_global pos;

	if (!self || !(handle = seat_handle(self, seat)))
		return;

	pos = weston_coord_surface_to_global(
		polo_backend_view_get_weston_view(self->view), offset);
	g_signal_emit(self, signals[SIGNAL_REQUEST_WINDOW_MENU], 0, handle,
		      pos.c.x, pos.c.y);
}

static void
emit_for_surface(struct weston_desktop_surface *desktop_surface, void *data)
{
	PoloBackendToplevel *self = from_desktop_surface(desktop_surface);

	if (self)
		g_signal_emit(self, GPOINTER_TO_UINT(data), 0);
}

static void
client_ping_timeout(struct weston_desktop_client *client, void *data)
{
	weston_desktop_client_for_each_surface(
		client, emit_for_surface,
		GUINT_TO_POINTER(signals[SIGNAL_PING_TIMEOUT]));
}

static void
client_pong(struct weston_desktop_client *client, void *data)
{
	weston_desktop_client_for_each_surface(
		client, emit_for_surface,
		GUINT_TO_POINTER(signals[SIGNAL_PONG]));
}

static void
popup_constraint_area(struct weston_desktop_surface *popup,
		      struct weston_output *output,
		      struct weston_geometry *area, void *data)
{
	PoloBackendServer *server = data;
	struct weston_desktop_surface *root = popup, *parent;
	PoloBackendOutput *handle;

	while ((parent = weston_desktop_surface_get_parent(root)))
		root = parent;
	if (root == popup || !from_desktop_surface(root))
		return;
	handle = polo_backend_server_lookup_output(server, output);
	if (handle)
		polo_backend_output_get_work_area(handle, area);
}

static const struct weston_desktop_api desktop_api = {
	.struct_size = sizeof(struct weston_desktop_api),
	.ping_timeout = client_ping_timeout,
	.pong = client_pong,
	.surface_added = surface_added,
	.surface_removed = surface_removed,
	.committed = surface_committed,
	.show_window_menu = surface_show_window_menu,
	.set_parent = surface_set_parent,
	.get_popup_constraint_area = popup_constraint_area,
	.move = surface_move,
	.resize = surface_resize,
	.fullscreen_requested = surface_fullscreen_requested,
	.maximized_requested = surface_maximized_requested,
	.minimized_requested = surface_minimized_requested,
};

gboolean
polo_backend_desktop_init(PoloBackendServer *server, GError **error)
{
	server->desktop =
		weston_desktop_create(server->compositor, &desktop_api, server);
	if (!server->desktop) {
		g_set_error(error, POLO_BACKEND_ERROR,
			    POLO_BACKEND_ERROR_FAILED,
			    "cannot create libweston-desktop");
		return FALSE;
	}
	return TRUE;
}

void
polo_backend_desktop_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->desktop, weston_desktop_destroy);
}

#define RETURN_IF_GONE(self)                                                   \
	do {                                                                   \
		g_return_if_fail(POLO_BACKEND_IS_TOPLEVEL(self));              \
		if (!(self)->desktop_surface)                                  \
			return;                                                \
	} while (0)

#define RETURN_VAL_IF_GONE(self, val)                                          \
	do {                                                                   \
		g_return_val_if_fail(POLO_BACKEND_IS_TOPLEVEL(self), val);     \
		if (!(self)->desktop_surface)                                  \
			return val;                                            \
	} while (0)

const char *
polo_backend_toplevel_get_title(PoloBackendToplevel *toplevel)
{
	g_return_val_if_fail(POLO_BACKEND_IS_TOPLEVEL(toplevel), NULL);
	return toplevel->title;
}

const char *
polo_backend_toplevel_get_app_id(PoloBackendToplevel *toplevel)
{
	g_return_val_if_fail(POLO_BACKEND_IS_TOPLEVEL(toplevel), NULL);
	return toplevel->app_id;
}

int
polo_backend_toplevel_get_pid(PoloBackendToplevel *toplevel)
{
	RETURN_VAL_IF_GONE(toplevel, 0);
	return weston_desktop_surface_get_pid(toplevel->desktop_surface);
}

PoloBackendToplevel *
polo_backend_toplevel_get_parent(PoloBackendToplevel *toplevel)
{
	struct weston_desktop_surface *parent;

	RETURN_VAL_IF_GONE(toplevel, NULL);
	parent = weston_desktop_surface_get_parent(toplevel->desktop_surface);
	return parent ? from_desktop_surface(parent) : NULL;
}

void
polo_backend_toplevel_get_window_geometry(PoloBackendToplevel *toplevel, int *x,
					  int *y, int *width, int *height)
{
	struct weston_geometry geometry = { 0 };

	g_return_if_fail(POLO_BACKEND_IS_TOPLEVEL(toplevel));
	if (toplevel->desktop_surface)
		geometry = weston_desktop_surface_get_geometry(
			toplevel->desktop_surface);
	*x = geometry.x;
	*y = geometry.y;
	*width = geometry.width;
	*height = geometry.height;
}

void
polo_backend_toplevel_get_min_size(PoloBackendToplevel *toplevel, int *width,
				   int *height)
{
	struct weston_size size = { 0 };

	g_return_if_fail(POLO_BACKEND_IS_TOPLEVEL(toplevel));
	if (toplevel->desktop_surface)
		size = weston_desktop_surface_get_min_size(
			toplevel->desktop_surface);
	*width = size.width;
	*height = size.height;
}

void
polo_backend_toplevel_get_max_size(PoloBackendToplevel *toplevel, int *width,
				   int *height)
{
	struct weston_size size = { 0 };

	g_return_if_fail(POLO_BACKEND_IS_TOPLEVEL(toplevel));
	if (toplevel->desktop_surface)
		size = weston_desktop_surface_get_max_size(
			toplevel->desktop_surface);
	*width = size.width;
	*height = size.height;
}

struct weston_surface *
polo_backend_toplevel_get_weston_surface(PoloBackendToplevel *toplevel)
{
	RETURN_VAL_IF_GONE(toplevel, NULL);
	return weston_desktop_surface_get_surface(toplevel->desktop_surface);
}

void
polo_backend_toplevel_set_server_decorated(struct weston_surface *surface,
					   gboolean server_decorated)
{
	PoloBackendToplevel *self = polo_backend_toplevel_from_surface(surface);
	PoloBackendServer *server;

	if (!self) {
		server = surface
			? weston_compositor_get_user_data(surface->compositor)
			: NULL;
		if (!server)
			return;
		if (server_decorated)
			g_hash_table_add(server->early_server_decorations,
					 surface);
		else
			g_hash_table_remove(server->early_server_decorations,
					    surface);
		return;
	}
	if (self->server_decorated == server_decorated)
		return;
	self->server_decorated = server_decorated;
	g_signal_emit(self, signals[SIGNAL_DECORATION_CHANGED], 0);
}

gboolean
polo_backend_toplevel_get_server_decorated(PoloBackendToplevel *toplevel)
{
	g_return_val_if_fail(POLO_BACKEND_IS_TOPLEVEL(toplevel), FALSE);
	return toplevel->server_decorated;
}

gboolean
polo_backend_toplevel_has_content(PoloBackendToplevel *toplevel)
{
	RETURN_VAL_IF_GONE(toplevel, FALSE);
	return weston_desktop_surface_get_surface(toplevel->desktop_surface)
		       ->width > 0;
}

gboolean
polo_backend_toplevel_is_mapped(PoloBackendToplevel *toplevel)
{
	RETURN_VAL_IF_GONE(toplevel, FALSE);
	return weston_surface_is_mapped(
		weston_desktop_surface_get_surface(toplevel->desktop_surface));
}

void
polo_backend_toplevel_map(PoloBackendToplevel *toplevel)
{
	struct weston_surface *surface;

	RETURN_IF_GONE(toplevel);
	surface = weston_desktop_surface_get_surface(toplevel->desktop_surface);
	if (weston_surface_is_mapped(surface))
		return;

	weston_surface_map(surface);
	weston_view_update_transform(
		polo_backend_view_get_weston_view(toplevel->view));
	weston_compositor_schedule_repaint(surface->compositor);
}

PoloBackendView *
polo_backend_toplevel_get_view(PoloBackendToplevel *toplevel)
{
	g_return_val_if_fail(POLO_BACKEND_IS_TOPLEVEL(toplevel), NULL);
	return toplevel->view;
}

static void
collect_surface_tree_views(PoloBackendToplevel *toplevel,
			   struct weston_view *view, GPtrArray *views)
{
	PoloBackendView *handle =
		polo_backend_server_lookup_view(toplevel->server, view);
	struct weston_subsurface *subsurface;
	struct weston_subsurface *next;

	if (!handle) {
		handle =
			polo_backend_view_new(toplevel->server, view, toplevel);
		g_ptr_array_add(toplevel->subsurface_views, handle);
	}
	g_ptr_array_add(views, g_object_ref(handle));

	/* Only this view's own children: mirrors have views of the same
	 * subsurfaces. */
	wl_list_for_each_safe(subsurface, next, &view->surface->subsurface_list,
			      parent_link)
	{
		struct weston_view *child;

		if (subsurface->surface == view->surface)
			continue;
		wl_list_for_each(child, &subsurface->surface->views,
				 surface_link)
		{
			if (child->geometry.parent == view) {
				collect_surface_tree_views(toplevel, child,
							   views);
				break;
			}
		}
	}
}

GPtrArray *
polo_backend_toplevel_get_surface_tree_views(PoloBackendToplevel *toplevel)
{
	GPtrArray *views;
	struct weston_view *view;

	g_return_val_if_fail(POLO_BACKEND_IS_TOPLEVEL(toplevel), NULL);
	if (!toplevel->desktop_surface || !toplevel->view)
		return g_ptr_array_new_with_free_func(g_object_unref);
	if (!toplevel->subsurface_views)
		toplevel->subsurface_views =
			g_ptr_array_new_with_free_func(g_object_unref);
	views = g_ptr_array_new_with_free_func(g_object_unref);
	view = polo_backend_view_get_weston_view(toplevel->view);
	if (view)
		collect_surface_tree_views(toplevel, view, views);
	return views;
}

PoloBackendClient *
polo_backend_toplevel_get_client(PoloBackendToplevel *toplevel)
{
	struct wl_client *client;

	RETURN_VAL_IF_GONE(toplevel, NULL);
	client = weston_desktop_client_get_client(
		weston_desktop_surface_get_client(toplevel->desktop_surface));
	return client ? polo_backend_server_get_client(toplevel->server, client)
		      : NULL;
}

void
polo_backend_toplevel_configure_size(PoloBackendToplevel *toplevel, int width,
				     int height)
{
	RETURN_IF_GONE(toplevel);
	weston_desktop_surface_set_size(toplevel->desktop_surface, width,
					height);
}

void
polo_backend_toplevel_set_activated(PoloBackendToplevel *toplevel,
				    gboolean activated)
{
	RETURN_IF_GONE(toplevel);
	weston_desktop_surface_set_activated(toplevel->desktop_surface,
					     activated);
}

void
polo_backend_toplevel_set_maximized(PoloBackendToplevel *toplevel,
				    gboolean maximized)
{
	RETURN_IF_GONE(toplevel);
	weston_desktop_surface_set_maximized(toplevel->desktop_surface,
					     maximized);
}

void
polo_backend_toplevel_set_fullscreen(PoloBackendToplevel *toplevel,
				     gboolean fullscreen)
{
	RETURN_IF_GONE(toplevel);
	weston_desktop_surface_set_fullscreen(toplevel->desktop_surface,
					      fullscreen);
}

void
polo_backend_toplevel_set_resizing(PoloBackendToplevel *toplevel,
				   gboolean resizing)
{
	RETURN_IF_GONE(toplevel);
	weston_desktop_surface_set_resizing(toplevel->desktop_surface,
					    resizing);
}

void
polo_backend_toplevel_set_tiled(PoloBackendToplevel *toplevel,
				PoloBackendEdges edges)
{
	enum weston_top_level_tiled_orientation orientation = 0;

	RETURN_IF_GONE(toplevel);
	if (edges & POLO_BACKEND_EDGE_LEFT)
		orientation |= WESTON_TOP_LEVEL_TILED_ORIENTATION_LEFT;
	if (edges & POLO_BACKEND_EDGE_RIGHT)
		orientation |= WESTON_TOP_LEVEL_TILED_ORIENTATION_RIGHT;
	if (edges & POLO_BACKEND_EDGE_TOP)
		orientation |= WESTON_TOP_LEVEL_TILED_ORIENTATION_TOP;
	if (edges & POLO_BACKEND_EDGE_BOTTOM)
		orientation |= WESTON_TOP_LEVEL_TILED_ORIENTATION_BOTTOM;
	weston_desktop_surface_set_orientation(toplevel->desktop_surface,
					       orientation);
}

void
polo_backend_toplevel_close(PoloBackendToplevel *toplevel)
{
	RETURN_IF_GONE(toplevel);
	weston_desktop_surface_close(toplevel->desktop_surface);
}

void
polo_backend_toplevel_ping(PoloBackendToplevel *toplevel)
{
	RETURN_IF_GONE(toplevel);
	weston_desktop_client_ping(
		weston_desktop_surface_get_client(toplevel->desktop_surface));
}

static void
polo_backend_toplevel_get_property(GObject *object, guint prop_id,
				   GValue *value, GParamSpec *pspec)
{
	PoloBackendToplevel *self = POLO_BACKEND_TOPLEVEL(object);

	switch (prop_id) {
	case PROP_TITLE:
		g_value_set_string(value, self->title);
		break;
	case PROP_APP_ID:
		g_value_set_string(value, self->app_id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void
polo_backend_toplevel_finalize(GObject *object)
{
	PoloBackendToplevel *self = POLO_BACKEND_TOPLEVEL(object);
	if (self->subsurface_views) {
		for (guint i = 0; i < self->subsurface_views->len; i++) {
			polo_backend_view_destroy(
				g_ptr_array_index(self->subsurface_views, i));
		}
		g_clear_pointer(&self->subsurface_views, g_ptr_array_unref);
	}

	g_clear_object(&self->view);
	g_free(self->icon_name);
	g_clear_pointer(&self->icon_pixels, g_bytes_unref);
	g_free(self->title);
	g_free(self->app_id);
	G_OBJECT_CLASS(polo_backend_toplevel_parent_class)->finalize(object);
}

static void
polo_backend_toplevel_class_init(PoloBackendToplevelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GType type = G_TYPE_FROM_CLASS(klass);

	object_class->get_property = polo_backend_toplevel_get_property;
	object_class->finalize = polo_backend_toplevel_finalize;

	props[PROP_TITLE] =
		g_param_spec_string("title", NULL, NULL, NULL,
				    G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY |
					    G_PARAM_STATIC_STRINGS);
	props[PROP_APP_ID] =
		g_param_spec_string("app-id", NULL, NULL, NULL,
				    G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY |
					    G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties(object_class, N_PROPS, props);

	signals[SIGNAL_COMMITTED] =
		g_signal_new("committed", type, G_SIGNAL_RUN_LAST, 0, NULL,
			     NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_REQUEST_MOVE] = g_signal_new(
		"request-move", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		G_TYPE_NONE, 2, POLO_BACKEND_TYPE_SEAT, G_TYPE_UINT);
	signals[SIGNAL_REQUEST_RESIZE] =
		g_signal_new("request-resize", type, G_SIGNAL_RUN_LAST, 0, NULL,
			     NULL, NULL, G_TYPE_NONE, 3, POLO_BACKEND_TYPE_SEAT,
			     G_TYPE_UINT, G_TYPE_UINT);
	signals[SIGNAL_REQUEST_MAXIMIZE] =
		g_signal_new("request-maximize", type, G_SIGNAL_RUN_LAST, 0,
			     NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals[SIGNAL_REQUEST_FULLSCREEN] =
		g_signal_new("request-fullscreen", type, G_SIGNAL_RUN_LAST, 0,
			     NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals[SIGNAL_REQUEST_MINIMIZE] =
		g_signal_new("request-minimize", type, G_SIGNAL_RUN_LAST, 0,
			     NULL, NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_PARENT_CHANGED] = g_signal_new(
		"parent-changed", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		G_TYPE_NONE, 1, POLO_BACKEND_TYPE_TOPLEVEL);
	signals[SIGNAL_MODAL_HINT_CHANGED] =
		g_signal_new("modal-hint-changed", type, G_SIGNAL_RUN_LAST, 0,
			     NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals[SIGNAL_DECORATION_CHANGED] =
		g_signal_new("decoration-changed", type, G_SIGNAL_RUN_LAST, 0,
			     NULL, NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_REQUEST_WINDOW_MENU] = g_signal_new(
		"request-window-menu", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
		NULL, G_TYPE_NONE, 3, POLO_BACKEND_TYPE_SEAT, G_TYPE_DOUBLE,
		G_TYPE_DOUBLE);
	signals[SIGNAL_PING_TIMEOUT] =
		g_signal_new("ping-timeout", type, G_SIGNAL_RUN_LAST, 0, NULL,
			     NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_PONG] = g_signal_new("pong", type, G_SIGNAL_RUN_LAST, 0,
					    NULL, NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_ICON_CHANGED] =
		g_signal_new("icon-changed", type, G_SIGNAL_RUN_LAST, 0, NULL,
			     NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_CLOSING] =
		g_signal_new("closing", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
			     NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_DESTROYED] =
		g_signal_new("destroyed", type, G_SIGNAL_RUN_LAST, 0, NULL,
			     NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_backend_toplevel_init(PoloBackendToplevel *self)
{
}
