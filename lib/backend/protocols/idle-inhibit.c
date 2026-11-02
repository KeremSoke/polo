/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwp_idle_inhibit_manager_v1. An inhibitor counts only while its surface is on
 * an output, which is checked when asked, so minimizing or leaving the
 * workspace ends it with nothing to track. A window covered by another still
 * counts.
 */

#include <libweston/libweston.h>

#include "idle-inhibit-unstable-v1-server-protocol.h"
#include "polo-backend-private.h"

struct inhibitor {
	struct wl_list link;
	struct weston_surface *surface;
	struct wl_listener surface_destroy;
};

static void
inhibitor_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct inhibitor *inhibitor =
		wl_container_of(listener, inhibitor, surface_destroy);

	wl_list_remove(&inhibitor->surface_destroy.link);
	inhibitor->surface = NULL;
}

static void
inhibitor_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_idle_inhibitor_v1_interface inhibitor_impl = {
	.destroy = inhibitor_destroy,
};

static void
inhibitor_resource_destroyed(struct wl_resource *resource)
{
	struct inhibitor *inhibitor = wl_resource_get_user_data(resource);

	if (inhibitor->surface)
		wl_list_remove(&inhibitor->surface_destroy.link);
	wl_list_remove(&inhibitor->link);
	g_free(inhibitor);
}

static void
manager_create_inhibitor(struct wl_client *client, struct wl_resource *resource,
			 uint32_t id, struct wl_resource *surface_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct inhibitor *inhibitor = g_new0(struct inhibitor, 1);
	struct wl_resource *object = wl_resource_create(
		client, &zwp_idle_inhibitor_v1_interface, 1, id);

	if (!object) {
		g_free(inhibitor);
		wl_client_post_no_memory(client);
		return;
	}
	inhibitor->surface = wl_resource_get_user_data(surface_resource);
	inhibitor->surface_destroy.notify = inhibitor_surface_destroyed;
	wl_signal_add(&inhibitor->surface->destroy_signal,
		      &inhibitor->surface_destroy);
	wl_list_insert(&server->idle_inhibitors, &inhibitor->link);
	wl_resource_set_implementation(object, &inhibitor_impl, inhibitor,
				       inhibitor_resource_destroyed);
}

static void
manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_idle_inhibit_manager_v1_interface manager_impl = {
	.destroy = manager_destroy,
	.create_inhibitor = manager_create_inhibitor,
};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &zwp_idle_inhibit_manager_v1_interface, 1, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

gboolean
polo_backend_server_get_idle_inhibited(PoloBackendServer *server)
{
	struct inhibitor *inhibitor;

	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), FALSE);
	wl_list_for_each(inhibitor, &server->idle_inhibitors, link)
	{
		if (inhibitor->surface && inhibitor->surface->output_mask != 0)
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
