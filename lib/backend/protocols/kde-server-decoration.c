/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * org_kde_kwin_server_decoration_manager for Qt 5 and GTK 3. Polo does not
 * draw server-side frames, so Client is the default, Server requests receive
 * Client, and None and Client are granted.
 */

#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "server-decoration-server-protocol.h"

static void
decoration_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
decoration_request_mode(struct wl_client *client, struct wl_resource *resource,
			uint32_t mode)
{
	org_kde_kwin_server_decoration_send_mode(
		resource,
		mode == ORG_KDE_KWIN_SERVER_DECORATION_MODE_NONE
			? ORG_KDE_KWIN_SERVER_DECORATION_MODE_NONE
			: ORG_KDE_KWIN_SERVER_DECORATION_MODE_CLIENT);
}

static const struct org_kde_kwin_server_decoration_interface decoration_impl = {
	.release = decoration_release,
	.request_mode = decoration_request_mode,
};

static void
manager_create(struct wl_client *client, struct wl_resource *resource,
	       uint32_t id, struct wl_resource *surface)
{
	struct wl_resource *decoration = wl_resource_create(
		client, &org_kde_kwin_server_decoration_interface, 1, id);

	if (!decoration) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(decoration, &decoration_impl, NULL,
				       NULL);
	org_kde_kwin_server_decoration_send_mode(
		decoration, ORG_KDE_KWIN_SERVER_DECORATION_MODE_CLIENT);
}

static const struct org_kde_kwin_server_decoration_manager_interface
	manager_impl = {
		.create = manager_create,
	};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &org_kde_kwin_server_decoration_manager_interface, 1,
		id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
	org_kde_kwin_server_decoration_manager_send_default_mode(
		resource, ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_CLIENT);
}

gboolean
polo_backend_kde_decoration_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	server->kde_decoration_global = wl_global_create(
		server->display,
		&org_kde_kwin_server_decoration_manager_interface, 1, server,
		manager_bind);
	return TRUE;
}

void
polo_backend_kde_decoration_fini(PoloBackendServer *server)
{
	if (server->kde_decoration_global) {
		wl_global_destroy(server->kde_decoration_global);
		server->kde_decoration_global = NULL;
	}
}
