/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * org_kde_kwin_server_decoration_manager for Qt 5 and GTK 3. Server is the
 * default, so GTK 3 windows without header bars use Polo's frame. None,
 * Client, and Server are granted as requested.
 */

#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "server-decoration-server-protocol.h"

struct kde_decoration {
	struct wl_resource *surface;
	struct wl_listener surface_destroy;
};

static void
decoration_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
set_server_decorated(struct kde_decoration *decoration, gboolean server)
{
	if (decoration->surface)
		polo_backend_toplevel_set_server_decorated(
			wl_resource_get_user_data(decoration->surface), server);
}

static void
decoration_request_mode(struct wl_client *client, struct wl_resource *resource,
			uint32_t mode)
{
	if (mode > ORG_KDE_KWIN_SERVER_DECORATION_MODE_SERVER)
		mode = ORG_KDE_KWIN_SERVER_DECORATION_MODE_CLIENT;
	org_kde_kwin_server_decoration_send_mode(resource, mode);
	set_server_decorated(
		wl_resource_get_user_data(resource),
		mode == ORG_KDE_KWIN_SERVER_DECORATION_MODE_SERVER);
}

static const struct org_kde_kwin_server_decoration_interface decoration_impl = {
	.release = decoration_release,
	.request_mode = decoration_request_mode,
};

static void
surface_destroyed(struct wl_listener *listener, void *data)
{
	struct kde_decoration *decoration =
		wl_container_of(listener, decoration, surface_destroy);

	set_server_decorated(decoration, FALSE);
	wl_list_remove(&decoration->surface_destroy.link);
	decoration->surface = NULL;
}

static void
decoration_destroyed(struct wl_resource *resource)
{
	struct kde_decoration *decoration = wl_resource_get_user_data(resource);

	set_server_decorated(decoration, FALSE);
	if (decoration->surface)
		wl_list_remove(&decoration->surface_destroy.link);
	g_free(decoration);
}

static void
manager_create(struct wl_client *client, struct wl_resource *resource,
	       uint32_t id, struct wl_resource *surface)
{
	struct kde_decoration *decoration;
	struct wl_resource *decoration_resource = wl_resource_create(
		client, &org_kde_kwin_server_decoration_interface, 1, id);

	if (!decoration_resource) {
		wl_client_post_no_memory(client);
		return;
	}
	decoration = g_new0(struct kde_decoration, 1);
	decoration->surface = surface;
	decoration->surface_destroy.notify = surface_destroyed;
	wl_resource_add_destroy_listener(surface, &decoration->surface_destroy);
	wl_resource_set_implementation(decoration_resource, &decoration_impl,
				       decoration, decoration_destroyed);
	org_kde_kwin_server_decoration_send_mode(
		decoration_resource,
		ORG_KDE_KWIN_SERVER_DECORATION_MODE_SERVER);
	set_server_decorated(decoration, TRUE);
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
		resource, ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_SERVER);
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
