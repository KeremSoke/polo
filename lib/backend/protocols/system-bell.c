/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * xdg_system_bell_v1. A ring only says who rang.
 */

#include <libweston/desktop.h>
#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "xdg-system-bell-v1-server-protocol.h"

static void
bell_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
bell_ring(struct wl_client *client, struct wl_resource *resource,
	  struct wl_resource *surface_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	PoloBackendToplevel *toplevel = NULL;

	if (surface_resource) {
		struct weston_surface *surface =
			weston_surface_get_main_surface(
				wl_resource_get_user_data(surface_resource));

		if (weston_surface_is_desktop_surface(surface))
			toplevel = weston_desktop_surface_get_user_data(
				weston_surface_get_desktop_surface(surface));
	}
	polo_backend_server_emit_system_bell(server, toplevel);
}

static const struct xdg_system_bell_v1_interface bell_impl = {
	.destroy = bell_destroy,
	.ring = bell_ring,
};

static void
bell_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &xdg_system_bell_v1_interface, 1, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &bell_impl, data, NULL);
}

gboolean
polo_backend_system_bell_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	server->system_bell_global =
		wl_global_create(server->display, &xdg_system_bell_v1_interface,
				 1, server, bell_bind);
	return TRUE;
}

void
polo_backend_system_bell_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->system_bell_global, wl_global_destroy);
}
