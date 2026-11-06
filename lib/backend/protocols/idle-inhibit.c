/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwp_idle_inhibit_manager_v1. An inhibitor counts only while its surface is on
 * an output, which is checked when asked, so minimizing or leaving the
 * workspace ends it with nothing to track. A window covered by another still
 * counts.
 */

#include "idle-inhibit-unstable-v1-server-protocol.h"
#include "protocol-util.h"

struct inhibitor {
	struct wl_list link;
	PoloBackendServer *server;
	struct polo_surface_ref surface;
};

static const struct zwp_idle_inhibitor_v1_interface inhibitor_impl = {
	.destroy = polo_protocol_destroy,
};

static void
inhibitor_resource_destroyed(struct wl_resource *resource)
{
	struct inhibitor *inhibitor = wl_resource_get_user_data(resource);

	polo_surface_ref_clear(&inhibitor->surface);
	wl_list_remove(&inhibitor->link);
	if (inhibitor->server)
		polo_backend_idle_inhibitors_changed(inhibitor->server, FALSE);
	g_free(inhibitor);
}

static void
manager_create_inhibitor(struct wl_client *client, struct wl_resource *resource,
			 uint32_t id, struct wl_resource *surface_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct inhibitor *inhibitor = g_new0(struct inhibitor, 1);

	if (!polo_protocol_child_new(resource, &zwp_idle_inhibitor_v1_interface,
				     id, &inhibitor_impl, inhibitor,
				     inhibitor_resource_destroyed)) {
		g_free(inhibitor);
		return;
	}
	polo_surface_ref_set(&inhibitor->surface,
			     wl_resource_get_user_data(surface_resource), NULL);
	inhibitor->server = server;
	wl_list_insert(&server->idle_inhibitors, &inhibitor->link);
	if (server->idle_ready)
		polo_backend_idle_inhibitors_changed(server, TRUE);
}

static const struct zwp_idle_inhibit_manager_v1_interface manager_impl = {
	.destroy = polo_protocol_destroy,
	.create_inhibitor = manager_create_inhibitor,
};

POLO_PROTOCOL_BIND(manager_bind, &zwp_idle_inhibit_manager_v1_interface, 1,
		   &manager_impl)

gboolean
polo_backend_server_get_idle_inhibited(PoloBackendServer *server)
{
	struct inhibitor *inhibitor;

	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), FALSE);
	wl_list_for_each(inhibitor, &server->idle_inhibitors, link)
	{
		if (inhibitor->surface.surface &&
		    inhibitor->surface.surface->output_mask != 0)
			return TRUE;
	}
	return FALSE;
}

gboolean
polo_backend_idle_inhibit_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	wl_list_init(&server->idle_inhibitors);
	server->idle_inhibit_global = wl_global_create(
		server->display, &zwp_idle_inhibit_manager_v1_interface, 1,
		server, manager_bind);
	return TRUE;
}

void
polo_backend_idle_inhibit_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->idle_inhibit_global, wl_global_destroy);
}
