/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwp_xwayland_keyboard_grab_manager_v1: Xwayland asks for this when an X
 * client holds an active keyboard grab. Trust offers it to Xwayland alone, and
 * the seat decides whether to grant it. There is no event for a refusal.
 */

#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "xwayland-keyboard-grab-unstable-v1-server-protocol.h"

static void
grab_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_xwayland_keyboard_grab_v1_interface grab_impl = {
	.destroy = grab_destroy,
};

static void
grab_resource_destroyed(struct wl_resource *resource)
{
	PoloBackendSeat *seat = wl_resource_get_user_data(resource);

	if (seat)
		polo_backend_seat_end_x_keyboard_grab(seat, resource);
}

static void
manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
manager_grab_keyboard(struct wl_client *client, struct wl_resource *resource,
		      uint32_t id, struct wl_resource *surface_resource,
		      struct wl_resource *seat_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct weston_seat *weston_seat =
		wl_resource_get_user_data(seat_resource);
	PoloBackendSeat *seat = weston_seat
		? polo_backend_server_lookup_seat(server, weston_seat)
		: NULL;
	struct wl_resource *grab = wl_resource_create(
		client, &zwp_xwayland_keyboard_grab_v1_interface,
		wl_resource_get_version(resource), id);

	if (!grab) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(grab, &grab_impl, NULL,
				       grab_resource_destroyed);
	if (seat &&
	    polo_backend_seat_begin_x_keyboard_grab(seat, grab, surface))
		wl_resource_set_user_data(grab, seat);
}

static const struct zwp_xwayland_keyboard_grab_manager_v1_interface
	manager_impl = {
		.destroy = manager_destroy,
		.grab_keyboard = manager_grab_keyboard,
	};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &zwp_xwayland_keyboard_grab_manager_v1_interface, 1,
		id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

gboolean
polo_backend_xwayland_keyboard_grab_init(PoloBackendServer *server,
					 GError **error)
{
	(void)error;
	server->xwayland_keyboard_grab_global = wl_global_create(
		server->display,
		&zwp_xwayland_keyboard_grab_manager_v1_interface, 1, server,
		manager_bind);
	return TRUE;
}

void
polo_backend_xwayland_keyboard_grab_fini(PoloBackendServer *server)
{
	if (server->xwayland_keyboard_grab_global) {
		wl_global_destroy(server->xwayland_keyboard_grab_global);
		server->xwayland_keyboard_grab_global = NULL;
	}
}
