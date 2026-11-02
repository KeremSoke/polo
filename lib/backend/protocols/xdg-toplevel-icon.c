/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * xdg_toplevel_icon_manager_v1. Only the largest ARGB8888 buffer is kept, and
 * it is copied, as the client may drop the icon once it is set. Other formats
 * and buffers over MAX_SIZE are ignored. set_icon applies with the toplevel's
 * commit.
 */

#include <string.h>

#include <libweston/desktop.h>
#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "xdg-toplevel-icon-v1-server-protocol.h"

#define MAX_SIZE 1024

struct icon_buffer {
	struct wl_list link;
	struct wl_resource *icon;
	struct wl_listener destroy;
};

struct icon {
	struct wl_resource *resource;
	char *name;
	GBytes *pixels;
	int size;
	gboolean immutable;
	struct wl_list buffers;
};

struct toplevel_state {
	struct wl_list link;
	struct wl_resource *toplevel;
	struct wl_listener toplevel_destroy;
	gboolean pending;
	char *name;
	GBytes *pixels;
	int size;
};

static void
buffer_destroyed(struct wl_listener *listener, void *data)
{
	struct icon_buffer *buffer = wl_container_of(listener, buffer, destroy);

	wl_resource_post_error(buffer->icon,
			       XDG_TOPLEVEL_ICON_V1_ERROR_NO_BUFFER,
			       "a buffer of this icon was destroyed first");
	wl_list_remove(&buffer->destroy.link);
	wl_list_remove(&buffer->link);
	g_free(buffer);
}

static void
icon_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
icon_set_name(struct wl_client *client, struct wl_resource *resource,
	      const char *name)
{
	struct icon *icon = wl_resource_get_user_data(resource);

	if (icon->immutable) {
		wl_resource_post_error(resource,
				       XDG_TOPLEVEL_ICON_V1_ERROR_IMMUTABLE,
				       "the icon was already assigned");
		return;
	}
	g_free(icon->name);
	icon->name = g_strdup(name);
}

static void
icon_add_buffer(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *buffer_resource, int32_t scale)
{
	struct icon *icon = wl_resource_get_user_data(resource);
	struct wl_shm_buffer *shm = wl_shm_buffer_get(buffer_resource);
	struct icon_buffer *buffer;
	int width, height;

	if (icon->immutable) {
		wl_resource_post_error(resource,
				       XDG_TOPLEVEL_ICON_V1_ERROR_IMMUTABLE,
				       "the icon was already assigned");
		return;
	}
	width = shm ? wl_shm_buffer_get_width(shm) : 0;
	height = shm ? wl_shm_buffer_get_height(shm) : 0;
	if (!shm || width != height || width <= 0) {
		wl_resource_post_error(
			resource, XDG_TOPLEVEL_ICON_V1_ERROR_INVALID_BUFFER,
			"an icon buffer must be square wl_shm");
		return;
	}

	buffer = g_new0(struct icon_buffer, 1);
	buffer->icon = resource;
	buffer->destroy.notify = buffer_destroyed;
	wl_resource_add_destroy_listener(buffer_resource, &buffer->destroy);
	wl_list_insert(&icon->buffers, &buffer->link);

	if (wl_shm_buffer_get_format(shm) != WL_SHM_FORMAT_ARGB8888 ||
	    width > MAX_SIZE || width < icon->size)
		return;

	int stride = wl_shm_buffer_get_stride(shm);
	size_t row = (size_t)width * 4;
	guint8 *copy = g_malloc(row * height);

	wl_shm_buffer_begin_access(shm);
	for (int y = 0; y < height; y++)
		memcpy(copy + y * row,
		       (const guint8 *)wl_shm_buffer_get_data(shm) +
			       (size_t)y * stride,
		       row);
	wl_shm_buffer_end_access(shm);
	g_clear_pointer(&icon->pixels, g_bytes_unref);
	icon->pixels = g_bytes_new_take(copy, row * height);
	icon->size = width;
}

static const struct xdg_toplevel_icon_v1_interface icon_impl = {
	.destroy = icon_destroy,
	.set_name = icon_set_name,
	.add_buffer = icon_add_buffer,
};

static void
icon_resource_destroyed(struct wl_resource *resource)
{
	struct icon *icon = wl_resource_get_user_data(resource);
	struct icon_buffer *buffer, *next;

	wl_list_for_each_safe(buffer, next, &icon->buffers, link)
	{
		wl_list_remove(&buffer->destroy.link);
		wl_list_remove(&buffer->link);
		g_free(buffer);
	}
	g_free(icon->name);
	g_clear_pointer(&icon->pixels, g_bytes_unref);
	g_free(icon);
}

static void
toplevel_gone(struct wl_listener *listener, void *data)
{
	struct toplevel_state *state =
		wl_container_of(listener, state, toplevel_destroy);

	wl_list_remove(&state->toplevel_destroy.link);
	wl_list_remove(&state->link);
	g_free(state->name);
	g_clear_pointer(&state->pixels, g_bytes_unref);
	g_free(state);
}

static void
manager_create_icon(struct wl_client *client, struct wl_resource *resource,
		    uint32_t id)
{
	struct icon *icon = g_new0(struct icon, 1);

	icon->resource = wl_resource_create(
		client, &xdg_toplevel_icon_v1_interface, 1, id);
	if (!icon->resource) {
		g_free(icon);
		wl_client_post_no_memory(client);
		return;
	}
	wl_list_init(&icon->buffers);
	wl_resource_set_implementation(icon->resource, &icon_impl, icon,
				       icon_resource_destroyed);
}

static void
manager_set_icon(struct wl_client *client, struct wl_resource *resource,
		 struct wl_resource *toplevel,
		 struct wl_resource *icon_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct icon *icon =
		icon_resource ? wl_resource_get_user_data(icon_resource) : NULL;
	struct wl_listener *listener =
		wl_resource_get_destroy_listener(toplevel, toplevel_gone);
	struct toplevel_state *state;

	if (listener) {
		state = wl_container_of(listener, state, toplevel_destroy);
	} else {
		state = g_new0(struct toplevel_state, 1);
		state->toplevel = toplevel;
		state->toplevel_destroy.notify = toplevel_gone;
		wl_resource_add_destroy_listener(toplevel,
						 &state->toplevel_destroy);
		wl_list_insert(&server->toplevel_icons, &state->link);
	}
	g_clear_pointer(&state->name, g_free);
	g_clear_pointer(&state->pixels, g_bytes_unref);
	state->size = 0;
	state->pending = TRUE;
	if (icon) {
		icon->immutable = TRUE;
		state->name = g_strdup(icon->name);
		state->pixels = icon->pixels ? g_bytes_ref(icon->pixels) : NULL;
		state->size = icon->size;
	}
}

static void
manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct xdg_toplevel_icon_manager_v1_interface manager_impl = {
	.destroy = manager_destroy,
	.create_icon = manager_create_icon,
	.set_icon = manager_set_icon,
};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &xdg_toplevel_icon_manager_v1_interface, 1, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
	xdg_toplevel_icon_manager_v1_send_icon_size(resource, 48);
	xdg_toplevel_icon_manager_v1_send_done(resource);
}

void
polo_backend_toplevel_icon_commit(PoloBackendServer *server,
				  struct weston_desktop_surface *surface)
{
	struct toplevel_state *state;

	wl_list_for_each(state, &server->toplevel_icons, link)
	{
		if (state->pending &&
		    wl_resource_get_user_data(state->toplevel) == surface) {
			polo_backend_toplevel_set_icon(surface, state->name,
						       state->pixels,
						       state->size);
			state->pending = FALSE;
			return;
		}
	}
}

gboolean
polo_backend_toplevel_icon_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	wl_list_init(&server->toplevel_icons);
	server->toplevel_icon_global = wl_global_create(
		server->display, &xdg_toplevel_icon_manager_v1_interface, 1,
		server, manager_bind);
	return TRUE;
}

void
polo_backend_toplevel_icon_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->toplevel_icon_global, wl_global_destroy);
}
