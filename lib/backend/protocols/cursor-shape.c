/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wp_cursor_shape_manager_v1, with the same focus and serial rules as
 * wl_pointer.set_cursor. cursor.c holds the images. A tablet tool gets a
 * no-op device because Polo does not provide tablet cursors.
 */

#include <libweston/libweston.h>

#include "cursor-shape-v1-server-protocol.h"
#include "polo-backend-private.h"

struct shape_device {
	PoloBackendServer *server;
	struct wl_resource *pointer;
	struct wl_listener pointer_destroy;
};

static void
device_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
device_set_shape(struct wl_client *client, struct wl_resource *resource,
		 uint32_t serial, uint32_t shape)
{
	struct shape_device *device = wl_resource_get_user_data(resource);
	struct weston_pointer *pointer = device->pointer
		? wl_resource_get_user_data(device->pointer)
		: NULL;

	if (shape == 0 || (int)shape >= polo_backend_cursor_n_shapes() ||
	    (wl_resource_get_version(resource) < 2 &&
	     shape > WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_OUT)) {
		wl_resource_post_error(
			resource, WP_CURSOR_SHAPE_DEVICE_V1_ERROR_INVALID_SHAPE,
			"no cursor shape %u", shape);
		return;
	}
	if (!pointer || !pointer->focus || !pointer->focus->surface->resource ||
	    wl_resource_get_client(pointer->focus->surface->resource) !=
		    client ||
	    pointer->focus_serial - serial > UINT32_MAX / 2)
		return;
	polo_backend_cursor_set_shape(device->server, pointer, shape);
}

static const struct wp_cursor_shape_device_v1_interface device_impl = {
	.destroy = device_destroy,
	.set_shape = device_set_shape,
};

static void
device_resource_destroyed(struct wl_resource *resource)
{
	struct shape_device *device = wl_resource_get_user_data(resource);

	if (device->pointer)
		wl_list_remove(&device->pointer_destroy.link);
	g_free(device);
}

static void
device_pointer_destroyed(struct wl_listener *listener, void *data)
{
	struct shape_device *device =
		wl_container_of(listener, device, pointer_destroy);

	wl_list_remove(&device->pointer_destroy.link);
	device->pointer = NULL;
}

static void
create_device(struct wl_client *client, struct wl_resource *resource,
	      uint32_t id, struct wl_resource *pointer)
{
	struct shape_device *device = g_new0(struct shape_device, 1);
	struct wl_resource *object =
		wl_resource_create(client, &wp_cursor_shape_device_v1_interface,
				   wl_resource_get_version(resource), id);

	if (!object) {
		g_free(device);
		wl_client_post_no_memory(client);
		return;
	}
	device->server = wl_resource_get_user_data(resource);
	device->pointer = pointer;
	if (pointer) {
		device->pointer_destroy.notify = device_pointer_destroyed;
		wl_resource_add_destroy_listener(pointer,
						 &device->pointer_destroy);
	}
	wl_resource_set_implementation(object, &device_impl, device,
				       device_resource_destroyed);
}

static void
manager_get_pointer(struct wl_client *client, struct wl_resource *resource,
		    uint32_t id, struct wl_resource *pointer)
{
	create_device(client, resource, id, pointer);
}

static void
manager_get_tablet_tool(struct wl_client *client, struct wl_resource *resource,
			uint32_t id, struct wl_resource *tool)
{
	create_device(client, resource, id, NULL);
}

static void
manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct wp_cursor_shape_manager_v1_interface manager_impl = {
	.destroy = manager_destroy,
	.get_pointer = manager_get_pointer,
	.get_tablet_tool_v2 = manager_get_tablet_tool,
};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &wp_cursor_shape_manager_v1_interface, version, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

gboolean
polo_backend_cursor_shape_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	server->cursor_shape_global = wl_global_create(
		server->display, &wp_cursor_shape_manager_v1_interface, 2,
		server, manager_bind);
	return TRUE;
}

void
polo_backend_cursor_shape_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->cursor_shape_global, wl_global_destroy);
}
