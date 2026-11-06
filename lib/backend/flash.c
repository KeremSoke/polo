/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's fullscreen visual bell (bell.c bell_flash_screen ()): a white
 * window mapped over the screen and unmapped at once. Here a white solid
 * view on each output, in the overlay layer, until the frame after the one
 * that showed it, as the lock curtain counts.
 */

#include <libweston/libweston.h>

#include "polo-backend-private.h"

struct flash {
	struct wl_list link;
	struct weston_output *output;
	struct weston_surface *surface;
	struct weston_view *view;
	struct weston_buffer_reference *buffer;
	struct wl_listener frame;
	struct wl_listener output_destroy;
	int frames;
};

static void
flash_free(struct flash *flash)
{
	wl_list_remove(&flash->frame.link);
	wl_list_remove(&flash->output_destroy.link);
	wl_list_remove(&flash->link);
	weston_view_destroy(flash->view);
	weston_surface_unref(flash->surface);
	weston_buffer_destroy_solid(flash->buffer);
	g_free(flash);
}

static void
flash_frame(struct wl_listener *listener, void *data)
{
	struct flash *flash = wl_container_of(listener, flash, frame);
	struct weston_output *output = flash->output;

	flash->frames++;
	if (flash->frames < 2) {
		weston_output_schedule_repaint(output);
		return;
	}
	flash_free(flash);
	weston_output_schedule_repaint(output);
}

static void
flash_output_destroyed(struct wl_listener *listener, void *data)
{
	struct flash *flash = wl_container_of(listener, flash, output_destroy);

	flash_free(flash);
}

static gboolean
flashing(PoloBackendServer *server, struct weston_output *output)
{
	struct flash *flash;

	wl_list_for_each(flash, &server->flashes, link)
	{
		if (flash->output == output)
			return TRUE;
	}
	return FALSE;
}

static void
flash_output(PoloBackendServer *server, struct weston_output *output)
{
	struct flash *flash;

	if (!output->enabled || flashing(server, output))
		return;
	flash = g_new0(struct flash, 1);
	flash->output = output;
	flash->surface = weston_surface_create(output->compositor, NULL);
	weston_surface_set_label(
		flash->surface,
		g_strdup_printf("polo visual bell %s", output->name));
	flash->buffer = weston_buffer_create_solid_rgba(output->compositor, 1.0,
							1.0, 1.0, 1.0);
	weston_surface_attach_solid(flash->surface, flash->buffer,
				    output->width, output->height);
	flash->view = weston_view_create(flash->surface);
	weston_view_set_position(flash->view, output->pos);
	weston_view_move_to_layer(flash->view,
				  &polo_backend_server_get_layer(
					   server, POLO_BACKEND_LAYER_OVERLAY)
					   ->layer.view_list);
	weston_surface_map(flash->surface);
	flash->frame.notify = flash_frame;
	wl_signal_add(&output->frame_signal, &flash->frame);
	flash->output_destroy.notify = flash_output_destroyed;
	wl_signal_add(&output->destroy_signal, &flash->output_destroy);
	wl_list_insert(&server->flashes, &flash->link);
	weston_output_schedule_repaint(output);
}

void
polo_backend_server_flash_outputs(PoloBackendServer *server)
{
	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	if (!server->compositor)
		return;
	for (guint i = 0; i < server->outputs->len; i++) {
		struct weston_output *output =
			polo_backend_output_get_weston_output(
				server->outputs->pdata[i]);

		if (output)
			flash_output(server, output);
	}
}

void
polo_backend_flash_init(PoloBackendServer *server)
{
	wl_list_init(&server->flashes);
}

void
polo_backend_flash_fini(PoloBackendServer *server)
{
	struct flash *flash, *tmp;

	if (!server->flashes.next)
		return;
	wl_list_for_each_safe(flash, tmp, &server->flashes, link)
		flash_free(flash);
}
