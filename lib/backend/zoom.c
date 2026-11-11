/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The screen magnifier: every output shows its area magnified by one scale
 * (weston_output_set_zoom). The output under a pointer follows it; the others
 * keep showing what they showed last, at first their middle.
 */

#include <math.h>

#include "polo-backend-private.h"

struct zoom_pointer {
	struct polo_zoom *zoom;
	struct weston_pointer *pointer;
	struct wl_listener motion;
	struct wl_listener destroy;
	struct wl_list link;
};

struct polo_zoom {
	PoloBackendServer *server;
	double scale;
	struct wl_listener output_created;
	struct wl_list pointers;
};

static struct weston_output *
output_at(struct weston_compositor *compositor, struct weston_coord_global pos)
{
	struct weston_output *output;

	wl_list_for_each(
		output, &compositor->output_list,
		link) if (weston_output_contains_coord(output,
						       pos)) return output;
	return NULL;
}

static struct weston_coord_global
middle(struct weston_output *output)
{
	return (struct weston_coord_global){
		.c = weston_coord(output->pos.c.x + output->width / 2.0,
				  output->pos.c.y + output->height / 2.0)
	};
}

static void
pointer_moved(struct wl_listener *listener, void *data)
{
	struct zoom_pointer *zp = wl_container_of(listener, zp, motion);
	struct polo_zoom *zoom = zp->zoom;
	struct weston_output *output;

	if (zoom->scale <= 1)
		return;
	output = output_at(zoom->server->compositor, zp->pointer->pos);
	if (output)
		weston_output_set_zoom(output, zoom->scale, zp->pointer->pos);
}

static void
pointer_destroyed(struct wl_listener *listener, void *data)
{
	struct zoom_pointer *zp = wl_container_of(listener, zp, destroy);

	wl_list_remove(&zp->motion.link);
	wl_list_remove(&zp->destroy.link);
	wl_list_remove(&zp->link);
	g_free(zp);
}

static void
output_created(struct wl_listener *listener, void *data)
{
	struct polo_zoom *zoom =
		wl_container_of(listener, zoom, output_created);
	struct weston_output *output = data;

	weston_output_set_zoom(output, zoom->scale, middle(output));
}

void
polo_backend_zoom_init(PoloBackendServer *server)
{
	struct polo_zoom *zoom = g_new0(struct polo_zoom, 1);
	struct weston_seat *seat;

	zoom->server = server;
	zoom->scale = 1;
	wl_list_init(&zoom->pointers);
	zoom->output_created.notify = output_created;
	wl_signal_add(&server->compositor->output_created_signal,
		      &zoom->output_created);
	server->zoom = zoom;
	wl_list_for_each(seat, &server->compositor->seat_list, link)
		polo_backend_zoom_add_pointer(server,
					      weston_seat_get_pointer(seat));
}

void
polo_backend_zoom_fini(PoloBackendServer *server)
{
	struct polo_zoom *zoom = server->zoom;
	struct zoom_pointer *zp, *next;

	if (!zoom)
		return;
	wl_list_for_each_safe(zp, next, &zoom->pointers, link)
		pointer_destroyed(&zp->destroy, NULL);
	wl_list_remove(&zoom->output_created.link);
	g_free(zoom);
	server->zoom = NULL;
}

void
polo_backend_zoom_add_pointer(PoloBackendServer *server,
			      struct weston_pointer *pointer)
{
	struct polo_zoom *zoom = server->zoom;
	struct zoom_pointer *zp;

	if (!zoom || !pointer)
		return;
	wl_list_for_each(zp, &zoom->pointers,
			 link) if (zp->pointer == pointer) return;
	zp = g_new0(struct zoom_pointer, 1);
	zp->zoom = zoom;
	zp->pointer = pointer;
	zp->motion.notify = pointer_moved;
	zp->destroy.notify = pointer_destroyed;
	wl_signal_add(&pointer->motion_signal, &zp->motion);
	wl_signal_add(&pointer->destroy_signal, &zp->destroy);
	wl_list_insert(&zoom->pointers, &zp->link);
}

void
polo_backend_server_set_zoom(PoloBackendServer *server, double scale)
{
	struct polo_zoom *zoom = server->zoom;
	struct weston_output *output;
	struct zoom_pointer *zp;

	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	if (!zoom)
		return;
	if (!(scale >= 1))
		scale = 1;
	scale = MIN(scale, 32);
	if (scale == zoom->scale)
		return;
	zoom->scale = scale;
	wl_list_for_each(output, &server->compositor->output_list, link)
		weston_output_set_zoom(output, scale,
				       weston_output_zoom_active(output)
					       ? output->zoom.center
					       : middle(output));
	wl_list_for_each(zp, &zoom->pointers, link)
		pointer_moved(&zp->motion, NULL);
	polo_log_scope_info(polo_backend_log_scope("polo"),
			    scale > 1 ? "magnifier: %.2f×" : "magnifier: off",
			    scale);
}

double
polo_backend_server_get_zoom(PoloBackendServer *server)
{
	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), 1);
	return server->zoom ? server->zoom->scale : 1;
}

void
polo_backend_server_zoom_to(PoloBackendServer *server, double x, double y)
{
	struct weston_coord_global pos = { .c = weston_coord(x, y) };
	struct weston_output *output;

	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	if (!server->zoom || server->zoom->scale <= 1)
		return;
	output = output_at(server->compositor, pos);
	if (output)
		weston_output_set_zoom(output, server->zoom->scale, pos);
}

gboolean
polo_backend_output_get_shown_area(PoloBackendOutput *self, double *x,
				   double *y, double *width, double *height)
{
	struct weston_output *output;
	struct weston_coord_global a, b;

	g_return_val_if_fail(POLO_BACKEND_IS_OUTPUT(self), FALSE);
	output = polo_backend_output_get_weston_output(self);
	if (!output || !output->enabled)
		return FALSE;
	a.c = weston_matrix_transform_coord(&output->inverse_matrix,
					    weston_coord(0, 0));
	b.c = weston_matrix_transform_coord(
		&output->inverse_matrix,
		weston_coord(output->current_mode->width,
			     output->current_mode->height));
	*x = MIN(a.c.x, b.c.x);
	*y = MIN(a.c.y, b.c.y);
	*width = fabs(b.c.x - a.c.x);
	*height = fabs(b.c.y - a.c.y);
	return TRUE;
}
