/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * xdg_system_bell_v1. A ring only says who rang.
 */

#include "protocol-util.h"
#include "xdg-system-bell-v1-server-protocol.h"

static void
bell_ring(struct wl_client *client, struct wl_resource *resource,
	  struct wl_resource *surface_resource)
{
	polo_backend_server_emit_system_bell(
		wl_resource_get_user_data(resource),
		polo_protocol_toplevel_from_surface(surface_resource));
}

static const struct xdg_system_bell_v1_interface bell_impl = {
	.destroy = polo_protocol_destroy,
	.ring = bell_ring,
};

POLO_PROTOCOL_BIND(bell_bind, &xdg_system_bell_v1_interface, 1, &bell_impl)

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
