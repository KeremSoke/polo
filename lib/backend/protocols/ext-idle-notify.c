/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_idle_notifier_v1, version 2: each notification is a
 * PoloBackendIdleWatch. get_idle_notification honours idle inhibitors;
 * get_input_idle_notification does not.
 */

#include "ext-idle-notify-v1-server-protocol.h"
#include "protocol-util.h"

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

static const struct ext_idle_notification_v1_interface notification_impl = {
	.destroy = polo_protocol_destroy,
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
create_notification(struct wl_resource *resource, uint32_t id, uint32_t timeout,
		    gboolean respect_inhibitors)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct notification *notification = g_new0(struct notification, 1);

	notification->watch = polo_backend_idle_watch_new(server, timeout,
							  respect_inhibitors);
	notification->resource = polo_protocol_child_new(
		resource, &ext_idle_notification_v1_interface, id,
		&notification_impl, notification,
		notification_resource_destroyed);
	if (!notification->resource) {
		g_object_unref(notification->watch);
		g_free(notification);
		return;
	}
	g_signal_connect(notification->watch, "idled", G_CALLBACK(on_idled),
			 notification);
	g_signal_connect(notification->watch, "resumed", G_CALLBACK(on_resumed),
			 notification);
}

static void
notifier_get_idle_notification(struct wl_client *client,
			       struct wl_resource *resource, uint32_t id,
			       uint32_t timeout, struct wl_resource *seat)
{
	create_notification(resource, id, timeout, TRUE);
}

static void
notifier_get_input_idle_notification(struct wl_client *client,
				     struct wl_resource *resource, uint32_t id,
				     uint32_t timeout, struct wl_resource *seat)
{
	create_notification(resource, id, timeout, FALSE);
}

static const struct ext_idle_notifier_v1_interface notifier_impl = {
	.destroy = polo_protocol_destroy,
	.get_idle_notification = notifier_get_idle_notification,
	.get_input_idle_notification = notifier_get_input_idle_notification,
};

POLO_PROTOCOL_BIND(notifier_bind, &ext_idle_notifier_v1_interface,
		   NOTIFIER_VERSION, &notifier_impl)

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
