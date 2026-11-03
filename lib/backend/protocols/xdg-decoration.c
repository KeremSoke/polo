/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zxdg_decoration_manager_v1. Server-side is the default and is used when a
 * toplevel has no preference; client-side is granted when requested. A
 * toplevel
 * destroyed before its decoration leaves the decoration inert rather than an
 * orphaned error, so a client that tears down in the wrong order keeps running.
 */

#include <libweston/desktop.h>
#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "xdg-decoration-unstable-v1-server-protocol.h"

struct decoration {
	struct wl_resource *resource;
	struct wl_resource *toplevel;
	struct wl_listener toplevel_destroy;
	uint32_t mode;
};

static void
send_mode(struct decoration *decoration)
{
	gboolean server = decoration->mode ==
		ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;

	zxdg_toplevel_decoration_v1_send_configure(
		decoration->resource,
		server ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
		       : ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
	if (decoration->toplevel) {
		struct weston_desktop_surface *surface =
			wl_resource_get_user_data(decoration->toplevel);

		if (surface)
			polo_backend_toplevel_set_server_decorated(
				weston_desktop_surface_get_surface(surface),
				server);
		weston_desktop_surface_force_configure(surface);
	}
}

static void
decoration_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
decoration_set_mode(struct wl_client *client, struct wl_resource *resource,
		    uint32_t mode)
{
	struct decoration *decoration = wl_resource_get_user_data(resource);

	decoration->mode = mode;
	send_mode(decoration);
}

static void
decoration_unset_mode(struct wl_client *client, struct wl_resource *resource)
{
	struct decoration *decoration = wl_resource_get_user_data(resource);

	decoration->mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
	send_mode(decoration);
}

static const struct zxdg_toplevel_decoration_v1_interface decoration_impl = {
	.destroy = decoration_destroy,
	.set_mode = decoration_set_mode,
	.unset_mode = decoration_unset_mode,
};

static void
decoration_resource_destroyed(struct wl_resource *resource)
{
	struct decoration *decoration = wl_resource_get_user_data(resource);

	if (decoration->toplevel) {
		struct weston_desktop_surface *surface =
			wl_resource_get_user_data(decoration->toplevel);

		wl_list_remove(&decoration->toplevel_destroy.link);
		if (surface)
			polo_backend_toplevel_set_server_decorated(
				weston_desktop_surface_get_surface(surface),
				FALSE);
	}
	g_free(decoration);
}

static void
toplevel_destroyed(struct wl_listener *listener, void *data)
{
	struct decoration *decoration =
		wl_container_of(listener, decoration, toplevel_destroy);
	struct weston_desktop_surface *surface =
		wl_resource_get_user_data(decoration->toplevel);

	if (surface)
		polo_backend_toplevel_set_server_decorated(
			weston_desktop_surface_get_surface(surface), FALSE);
	wl_list_remove(&decoration->toplevel_destroy.link);
	decoration->toplevel = NULL;
}

static void
manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
manager_get_toplevel_decoration(struct wl_client *client,
				struct wl_resource *resource, uint32_t id,
				struct wl_resource *toplevel)
{
	struct weston_desktop_surface *surface =
		wl_resource_get_user_data(toplevel);

	if (wl_resource_get_destroy_listener(toplevel, toplevel_destroyed)) {
		wl_resource_post_error(
			resource,
			ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ALREADY_CONSTRUCTED,
			"this toplevel already has a decoration");
		return;
	}
	if (surface &&
	    weston_surface_has_content(
		    weston_desktop_surface_get_surface(surface))) {
		wl_resource_post_error(
			resource,
			ZXDG_TOPLEVEL_DECORATION_V1_ERROR_UNCONFIGURED_BUFFER,
			"this toplevel already has a buffer");
		return;
	}

	struct decoration *decoration = g_new0(struct decoration, 1);

	decoration->resource = wl_resource_create(
		client, &zxdg_toplevel_decoration_v1_interface,
		wl_resource_get_version(resource), id);
	if (!decoration->resource) {
		g_free(decoration);
		wl_client_post_no_memory(client);
		return;
	}
	decoration->toplevel = toplevel;
	decoration->mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
	decoration->toplevel_destroy.notify = toplevel_destroyed;
	wl_resource_add_destroy_listener(toplevel,
					 &decoration->toplevel_destroy);
	wl_resource_set_implementation(decoration->resource, &decoration_impl,
				       decoration,
				       decoration_resource_destroyed);
	send_mode(decoration);
}

static const struct zxdg_decoration_manager_v1_interface manager_impl = {
	.destroy = manager_destroy,
	.get_toplevel_decoration = manager_get_toplevel_decoration,
};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &zxdg_decoration_manager_v1_interface, version, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

gboolean
polo_backend_xdg_decoration_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	server->xdg_decoration_global = wl_global_create(
		server->display, &zxdg_decoration_manager_v1_interface, 1,
		server, manager_bind);
	return TRUE;
}

void
polo_backend_xdg_decoration_fini(PoloBackendServer *server)
{
	if (server->xdg_decoration_global) {
		wl_global_destroy(server->xdg_decoration_global);
		server->xdg_decoration_global = NULL;
	}
}
