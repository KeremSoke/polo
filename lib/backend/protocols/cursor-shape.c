/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wp_cursor_shape_manager_v1, with the same focus and serial rules as
 * wl_pointer.set_cursor. cursor.c holds the images. A tablet tool gets a
 * no-op device because Polo does not provide tablet cursors.
 */

#include "cursor-shape-v1-server-protocol.h"
#include "protocol-util.h"

struct shape_device {
	PoloBackendServer *server;
	struct polo_resource_ref pointer;
};

static void
device_set_shape(struct wl_client *client, struct wl_resource *resource,
		 uint32_t serial, uint32_t shape)
{
	struct shape_device *device = wl_resource_get_user_data(resource);
	struct weston_pointer *pointer = device->pointer.resource
		? wl_resource_get_user_data(device->pointer.resource)
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
	.destroy = polo_protocol_destroy,
	.set_shape = device_set_shape,
};

static void
device_resource_destroyed(struct wl_resource *resource)
{
	struct shape_device *device = wl_resource_get_user_data(resource);

	polo_resource_ref_clear(&device->pointer);
	g_free(device);
}

static void
create_device(struct wl_resource *resource, uint32_t id,
	      struct wl_resource *pointer)
{
	struct shape_device *device = g_new0(struct shape_device, 1);

	if (!polo_protocol_child_new(
		    resource, &wp_cursor_shape_device_v1_interface, id,
		    &device_impl, device, device_resource_destroyed)) {
		g_free(device);
		return;
	}
	device->server = wl_resource_get_user_data(resource);
	polo_resource_ref_set(&device->pointer, pointer, NULL);
}

static void
manager_get_pointer(struct wl_client *client, struct wl_resource *resource,
		    uint32_t id, struct wl_resource *pointer)
{
	create_device(resource, id, pointer);
}

static void
manager_get_tablet_tool(struct wl_client *client, struct wl_resource *resource,
			uint32_t id, struct wl_resource *tool)
{
	create_device(resource, id, NULL);
}

static const struct wp_cursor_shape_manager_v1_interface manager_impl = {
	.destroy = polo_protocol_destroy,
	.get_pointer = manager_get_pointer,
	.get_tablet_tool_v2 = manager_get_tablet_tool,
};

POLO_PROTOCOL_BIND(manager_bind, &wp_cursor_shape_manager_v1_interface, 2,
		   &manager_impl)

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
