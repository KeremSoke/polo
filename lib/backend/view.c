/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PoloBackendView: a handle on a weston_view. The handle does not own the
 * view; its creator, such as a toplevel or layer surface, destroys it. A
 * mirror (another view of the same surface, for Expo and the cube) is the
 * exception: its handle owns it, and keeps the handles of the child views
 * libweston made for its subsurfaces.
 */

#include <math.h>

#include <libweston/libweston.h>

#include "polo-backend-private.h"

struct _PoloBackendView {
	GObject parent_instance;

	PoloBackendServer *server;
	struct weston_view *view;
	PoloBackendToplevel *toplevel; /* unowned */
	struct wl_listener destroy_listener;
	gboolean owned;
	gboolean scaled;
	gboolean deformed;
	gboolean effect_transformed;
	struct weston_transform scale;
	struct weston_transform effect;
	struct weston_surface *ghost_surface;
	GPtrArray *children;
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
	self->scaled = FALSE;
	self->deformed = FALSE;
	self->effect_transformed = FALSE;
	self->toplevel = NULL;
	g_signal_emit(self, signals[SIGNAL_DESTROYED], 0);
}

PoloBackendView *
polo_backend_view_mirror(PoloBackendView *source)
{
	PoloBackendView *mirror;

	g_return_val_if_fail(POLO_BACKEND_IS_VIEW(source), NULL);

	if (!source->view ||
	    weston_surface_get_main_surface(source->view->surface) !=
		    source->view->surface)
		return NULL;
	mirror = polo_backend_view_new(
		source->server, weston_view_create(source->view->surface),
		NULL);
	mirror->owned = TRUE;
	return mirror;
}

static void
collect_child_views(PoloBackendView *self, struct weston_view *view,
		    GPtrArray *views)
{
	struct weston_subsurface *sub;

	wl_list_for_each(sub, &view->surface->subsurface_list, parent_link)
	{
		struct weston_view *child;

		if (sub->surface == view->surface)
			continue;
		wl_list_for_each(child, &sub->surface->views, surface_link)
		{
			PoloBackendView *handle;

			if (child->parent_view != view)
				continue;
			handle = polo_backend_server_lookup_view(self->server,
								 child);
			if (!handle) {
				handle = polo_backend_view_new(self->server,
							       child, NULL);
				g_ptr_array_add(self->children, handle);
			}
			g_ptr_array_add(views, g_object_ref(handle));
			collect_child_views(self, child, views);
			break;
		}
	}
}

GPtrArray *
polo_backend_view_mirror_views(PoloBackendView *self)
{
	GPtrArray *views;

	g_return_val_if_fail(POLO_BACKEND_IS_VIEW(self), NULL);
	views = g_ptr_array_new_with_free_func(g_object_unref);
	if (!self->view)
		return views;
	if (!self->children)
		self->children = g_ptr_array_new_with_free_func(g_object_unref);
	g_ptr_array_add(views, g_object_ref(self));
	collect_child_views(self, self->view, views);
	return views;
}

static void
unscale(PoloBackendView *view)
{
	if (view->scaled) {
		weston_view_remove_transform(view->view, &view->scale);
		view->scaled = FALSE;
	}
}

void
polo_backend_view_place(PoloBackendView *view, double x, double y, double scale)
{
	struct weston_view *v;

	g_return_if_fail(POLO_BACKEND_IS_VIEW(view));
	v = view->view;
	if (!v)
		return;
	if (view->deformed) {
		weston_view_clear_deformation(v);
		view->deformed = FALSE;
	}
	unscale(view);
	weston_view_set_position(
		v, (struct weston_coord_global){ .c = weston_coord(x, y) });
	if (scale != 1.0) {
		weston_matrix_init(&view->scale.matrix);
		weston_matrix_scale(&view->scale.matrix, scale, scale, 1);
		weston_view_add_transform(v, &v->geometry.transformation_list,
					  &view->scale);
		view->scaled = TRUE;
	}
	weston_view_update_transform(v);
	weston_view_schedule_repaint(v);
}

PoloBackendView *
polo_backend_view_ghost(PoloBackendView *source)
{
	struct weston_view *view;
	struct weston_surface *surface;
	PoloBackendView *ghost;

	g_return_val_if_fail(POLO_BACKEND_IS_VIEW(source), NULL);
	if (!source->view || source->view->geometry.parent)
		return NULL;
	surface = source->view->surface;
	if (surface->ref_count <= 0)
		return NULL;
	view = weston_view_create(surface);
	if (!view)
		return NULL;
	weston_view_set_position(
		view, weston_view_get_pos_offset_global(source->view));
	weston_view_set_alpha(view, source->view->alpha);
	pixman_region32_clear(&surface->pending.input);
	pixman_region32_clear(&surface->input);
	if (!weston_surface_is_mapped(surface) &&
	    weston_surface_has_content(surface))
		weston_surface_map(surface);
	if (source->view->layer_link.layer)
		weston_view_move_to_layer(view, &source->view->layer_link);
	ghost = polo_backend_view_new(source->server, view, NULL);
	ghost->owned = TRUE;
	ghost->ghost_surface = weston_surface_ref(surface);
	weston_view_schedule_repaint(view);
	return ghost;
}

static void
drop_effect_transform(PoloBackendView *view)
{
	if (view->effect_transformed) {
		weston_view_remove_transform(view->view, &view->effect);
		view->effect_transformed = FALSE;
	}
}

void
polo_backend_view_set_effect_transform(PoloBackendView *view, double cx,
				       double cy, double scale_x,
				       double scale_y, double angle, double dx,
				       double dy)
{
	struct weston_view *v;
	struct weston_coord_global pos;
	double c = cos(angle), s = sin(angle);
	double a11 = c * scale_x, a12 = -s * scale_y;
	double a21 = s * scale_x, a22 = c * scale_y;
	double px, py;

	g_return_if_fail(POLO_BACKEND_IS_VIEW(view));
	v = view->view;
	if (!v || v->geometry.parent)
		return;
	pos = weston_view_get_pos_offset_global(v);
	px = pos.c.x - cx;
	py = pos.c.y - cy;
	drop_effect_transform(view);
	weston_matrix_init(&view->effect.matrix);
	weston_matrix_scale(&view->effect.matrix, scale_x, scale_y, 1);
	weston_matrix_rotate_xy(&view->effect.matrix, c, s);
	weston_matrix_translate(&view->effect.matrix,
				cx + dx + a11 * px + a12 * py - pos.c.x,
				cy + dy + a21 * px + a22 * py - pos.c.y, 0);
	weston_view_add_transform(v, &v->geometry.transformation_list,
				  &view->effect);
	view->effect_transformed = TRUE;
	weston_view_update_transform(v);
	weston_view_schedule_repaint(v);
}

void
polo_backend_view_clear_effect_transform(PoloBackendView *view)
{
	g_return_if_fail(POLO_BACKEND_IS_VIEW(view));
	if (!view->view || !view->effect_transformed)
		return;
	drop_effect_transform(view);
	weston_view_update_transform(view->view);
	weston_view_schedule_repaint(view->view);
}

void
polo_backend_view_set_position(PoloBackendView *view, double x, double y)
{
	g_return_if_fail(POLO_BACKEND_IS_VIEW(view));
	if (!view->view)
		return;

	if (view->view->geometry.pos_offset.x == x &&
	    view->view->geometry.pos_offset.y == y &&
	    !view->view->geometry.parent)
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
	unscale(view);
	view->deformed =
		weston_view_set_deformation(
			view->view,
			(const struct weston_view_deform_vertex *)vertices,
			cols, rows) == 0;
	return view->deformed;
}

void
polo_backend_view_clear_deformation(PoloBackendView *view)
{
	g_return_if_fail(POLO_BACKEND_IS_VIEW(view));
	if (view->view)
		weston_view_clear_deformation(view->view);
	view->deformed = FALSE;
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

	/* Already on top of that layer: moving it there again only damages
	 * all of it (a full-screen wallpaper on every panel commit). */
	if (layer && view->view->layer_link.layer == &layer->layer &&
	    layer->layer.view_list.link.next == &view->view->layer_link.link)
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
polo_backend_view_dispose(GObject *object)
{
	PoloBackendView *self = POLO_BACKEND_VIEW(object);
	struct weston_view *view = self->view;

	if (self->owned && view) {
		if (self->scaled)
			weston_view_remove_transform(view, &self->scale);
		if (self->effect_transformed)
			weston_view_remove_transform(view, &self->effect);
		polo_backend_view_destroy(self);
		weston_view_destroy(view);
	} else if (view && self->effect_transformed) {
		drop_effect_transform(self);
		weston_view_update_transform(view);
	}
	if (self->ghost_surface) {
		weston_surface_unref(self->ghost_surface);
		self->ghost_surface = NULL;
	}
	g_clear_pointer(&self->children, g_ptr_array_unref);
	G_OBJECT_CLASS(polo_backend_view_parent_class)->dispose(object);
}

static void
polo_backend_view_class_init(PoloBackendViewClass *klass)
{
	G_OBJECT_CLASS(klass)->dispose = polo_backend_view_dispose;
	signals[SIGNAL_DESTROYED] = g_signal_new(
		"destroyed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_backend_view_init(PoloBackendView *self)
{
	wl_list_init(&self->scale.link);
	wl_list_init(&self->effect.link);
}
