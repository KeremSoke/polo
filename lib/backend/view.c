/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PoloBackendView: a handle on a weston_view. The handle does not own the
 * view; its creator, such as a toplevel or layer surface, destroys it.
 */

#include <libweston/libweston.h>

#include "polo-backend-private.h"

struct _PoloBackendView {
	GObject parent_instance;

	PoloBackendServer *server;
	struct weston_view *view;
	PoloBackendToplevel *toplevel; /* unowned */
	struct wl_listener destroy_listener;
};

enum {
	SIGNAL_DESTROYED,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(PoloBackendView, polo_backend_view, G_TYPE_OBJECT)

static void
weston_view_destroyed(struct wl_listener *listener, void *data)
{
	PoloBackendView *self =
		wl_container_of(listener, self, destroy_listener);

	polo_backend_view_destroy(self);
}

PoloBackendView *
polo_backend_view_new(PoloBackendServer *server, struct weston_view *view,
		      PoloBackendToplevel *toplevel)
{
	PoloBackendView *self = g_object_new(POLO_BACKEND_TYPE_VIEW, NULL);

	self->server = server;
	self->view = view;
	self->toplevel = toplevel;
	self->destroy_listener.notify = weston_view_destroyed;
	wl_signal_add(&view->destroy_signal, &self->destroy_listener);
	g_hash_table_insert(server->views, view, self);
	return self;
}

struct weston_view *
polo_backend_view_get_weston_view(PoloBackendView *view)
{
	return view->view;
}

void
polo_backend_view_destroy(PoloBackendView *self)
{
	if (!self->view)
		return;

	wl_list_remove(&self->destroy_listener.link);
	g_hash_table_remove(self->server->views, self->view);
	self->view = NULL;
	self->toplevel = NULL;
	g_signal_emit(self, signals[SIGNAL_DESTROYED], 0);
}

void
polo_backend_view_set_position(PoloBackendView *view, double x, double y)
{
	g_return_if_fail(POLO_BACKEND_IS_VIEW(view));
	if (!view->view)
		return;

	weston_view_set_position(
		view->view,
		(struct weston_coord_global){ .c = weston_coord(x, y) });
	weston_view_update_transform(view->view);
	weston_compositor_schedule_repaint(view->view->surface->compositor);
}

gboolean
polo_backend_view_set_deformation(PoloBackendView *view, const float *vertices,
				  guint cols, guint rows)
{
	g_return_val_if_fail(POLO_BACKEND_IS_VIEW(view), FALSE);
	G_STATIC_ASSERT(sizeof(struct weston_view_deform_vertex) ==
			4 * sizeof(float));
	if (!view->view)
		return FALSE;
	return weston_view_set_deformation(
		       view->view,
		       (const struct weston_view_deform_vertex *)vertices, cols,
		       rows) == 0;
}

void
polo_backend_view_clear_deformation(PoloBackendView *view)
{
	g_return_if_fail(POLO_BACKEND_IS_VIEW(view));
	if (view->view)
		weston_view_clear_deformation(view->view);
}

void
polo_backend_view_set_alpha(PoloBackendView *view, float alpha)
{
	g_return_if_fail(POLO_BACKEND_IS_VIEW(view));
	if (view->view && view->view->alpha != alpha) {
		weston_view_set_alpha(view->view, alpha);
		weston_view_schedule_repaint(view->view);
	}
}

void
polo_backend_view_get_position(PoloBackendView *view, double *x, double *y)
{
	struct weston_coord_global pos;

	g_return_if_fail(POLO_BACKEND_IS_VIEW(view));
	if (!view->view) {
		*x = *y = 0;
		return;
	}

	if (view->view->geometry.parent) {
		/* A subsurface's offset is relative to its parent. */
		weston_view_update_transform(view->view);
		pos = weston_coord_surface_to_global(
			view->view,
			weston_coord_surface(0, 0, view->view->surface));
	} else {
		pos = weston_view_get_pos_offset_global(view->view);
	}
	*x = pos.c.x;
	*y = pos.c.y;
}

void
polo_backend_view_get_size(PoloBackendView *view, int *width, int *height)
{
	g_return_if_fail(POLO_BACKEND_IS_VIEW(view));
	if (!view->view) {
		*width = *height = 0;
		return;
	}
	*width = view->view->surface->width;
	*height = view->view->surface->height;
}

void
polo_backend_view_move_to_layer(PoloBackendView *view, PoloBackendLayer *layer)
{
	g_return_if_fail(POLO_BACKEND_IS_VIEW(view));
	g_return_if_fail(!layer || layer->kind != POLO_BACKEND_LAYER_LOCK);
	if (!view->view)
		return;

	weston_view_move_to_layer(view->view,
				  layer ? &layer->layer.view_list : NULL);
	weston_compositor_schedule_repaint(view->view->surface->compositor);
}

PoloBackendToplevel *
polo_backend_view_get_toplevel(PoloBackendView *view)
{
	g_return_val_if_fail(POLO_BACKEND_IS_VIEW(view), NULL);
	return view->toplevel;
}

PoloBackendClient *
polo_backend_view_get_client(PoloBackendView *view)
{
	struct weston_surface *surface;

	g_return_val_if_fail(POLO_BACKEND_IS_VIEW(view), NULL);
	if (!view->view)
		return NULL;
	surface = weston_surface_get_main_surface(view->view->surface);
	if (!surface || !surface->resource)
		return NULL;
	return polo_backend_server_get_client(
		view->server, wl_resource_get_client(surface->resource));
}

static void
polo_backend_view_class_init(PoloBackendViewClass *klass)
{
	signals[SIGNAL_DESTROYED] = g_signal_new(
		"destroyed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_backend_view_init(PoloBackendView *self)
{
}
