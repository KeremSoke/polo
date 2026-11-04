/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_idle_notifier_v1, version 2: each notification is a
 * PoloBackendIdleWatch. get_idle_notification honours idle inhibitors;
 * get_input_idle_notification does not.
 */

#include <libweston/libweston.h>

#include "ext-idle-notify-v1-server-protocol.h"
#include "polo-backend-private.h"

#define NOTIFIER_VERSION 2

struct notification {
	struct wl_resource *resource;
	PoloBackendIdleWatch *watch;
};

static void
on_idled(PoloBackendIdleWatch *watch, gpointer data)
{
	struct notification *notification = data;

	ext_idle_notification_v1_send_idled(notification->resource);
}

static void
on_resumed(PoloBackendIdleWatch *watch, gpointer data)
{
	struct notification *notification = data;

	ext_idle_notification_v1_send_resumed(notification->resource);
}

static void
notification_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct ext_idle_notification_v1_interface notification_impl = {
	.destroy = notification_destroy,
};

static void
notification_resource_destroyed(struct wl_resource *resource)
{
	struct notification *notification = wl_resource_get_user_data(resource);

	g_signal_handlers_disconnect_by_data(notification->watch, notification);
	g_object_unref(notification->watch);
	g_free(notification);
}

static void
create_notification(struct wl_client *client, struct wl_resource *resource,
		    uint32_t id, uint32_t timeout, gboolean respect_inhibitors)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct notification *notification = g_new0(struct notification, 1);

	notification->resource =
		wl_resource_create(client, &ext_idle_notification_v1_interface,
				   wl_resource_get_version(resource), id);
	if (!notification->resource) {
		g_free(notification);
		wl_client_post_no_memory(client);
		return;
	}
	notification->watch = polo_backend_idle_watch_new(server, timeout,
							  respect_inhibitors);
	g_signal_connect(notification->watch, "idled", G_CALLBACK(on_idled),
			 notification);
	g_signal_connect(notification->watch, "resumed", G_CALLBACK(on_resumed),
			 notification);
	wl_resource_set_implementation(notification->resource,
				       &notification_impl, notification,
				       notification_resource_destroyed);
}

static void
notifier_get_idle_notification(struct wl_client *client,
			       struct wl_resource *resource, uint32_t id,
			       uint32_t timeout, struct wl_resource *seat)
{
	create_notification(client, resource, id, timeout, TRUE);
}

static void
notifier_get_input_idle_notification(struct wl_client *client,
				     struct wl_resource *resource, uint32_t id,
				     uint32_t timeout, struct wl_resource *seat)
{
	create_notification(client, resource, id, timeout, FALSE);
}

static void
notifier_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct ext_idle_notifier_v1_interface notifier_impl = {
	.destroy = notifier_destroy,
	.get_idle_notification = notifier_get_idle_notification,
	.get_input_idle_notification = notifier_get_input_idle_notification,
};

static void
notifier_bind(struct wl_client *client, void *data, uint32_t version,
	      uint32_t id)
{
	struct wl_resource *resource =
		wl_resource_create(client, &ext_idle_notifier_v1_interface,
				   MIN(version, NOTIFIER_VERSION), id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &notifier_impl, data, NULL);
}

gboolean
polo_backend_idle_notify_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	server->idle_notifier_global = wl_global_create(
		server->display, &ext_idle_notifier_v1_interface,
		NOTIFIER_VERSION, server, notifier_bind);
	return TRUE;
}

void
polo_backend_idle_notify_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->idle_notifier_global, wl_global_destroy);
}
