/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwp_virtual_keyboard_manager_v1, which input methods use to send back keys
 * they did not handle. Those keys already went through Polo's shortcuts, so
 * they go straight to the focused client. The client's keymap is required but
 * not used: key codes are taken as the seat's own.
 */

#include <time.h>
#include <unistd.h>

#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "virtual-keyboard-unstable-v1-server-protocol.h"

struct virtual_keyboard {
	struct weston_seat *seat;
	struct wl_listener seat_destroy;
	gboolean has_keymap;
};

static void
vk_keymap(struct wl_client *client, struct wl_resource *resource,
	  uint32_t format, int32_t fd, uint32_t size)
{
	struct virtual_keyboard *vk = wl_resource_get_user_data(resource);

	close(fd);
	vk->has_keymap = TRUE;
}

static void
vk_key(struct wl_client *client, struct wl_resource *resource, uint32_t time,
       uint32_t key, uint32_t state)
{
	struct virtual_keyboard *vk = wl_resource_get_user_data(resource);
	struct weston_keyboard *keyboard;
	struct timespec ts = { time / 1000, (time % 1000) * 1000000L };

	if (!vk->has_keymap) {
		wl_resource_post_error(resource,
				       ZWP_VIRTUAL_KEYBOARD_V1_ERROR_NO_KEYMAP,
				       "no keymap was set");
		return;
	}
	keyboard = vk->seat ? weston_seat_get_keyboard(vk->seat) : NULL;
	if (keyboard) {
		struct weston_key_event event;

		weston_key_event_init(&event, &ts, vk->seat, key, state,
				      STATE_UPDATE_NONE);
		weston_keyboard_send_key(keyboard, &event);
	}
}

static void
vk_modifiers(struct wl_client *client, struct wl_resource *resource,
	     uint32_t depressed, uint32_t latched, uint32_t locked,
	     uint32_t group)
{
	struct virtual_keyboard *vk = wl_resource_get_user_data(resource);
	struct weston_keyboard *keyboard;

	if (!vk->has_keymap) {
		wl_resource_post_error(resource,
				       ZWP_VIRTUAL_KEYBOARD_V1_ERROR_NO_KEYMAP,
				       "no keymap was set");
		return;
	}
	keyboard = vk->seat ? weston_seat_get_keyboard(vk->seat) : NULL;
	if (keyboard)
		weston_keyboard_send_modifiers(
			keyboard,
			wl_display_next_serial(
				vk->seat->compositor->wl_display),
			depressed, latched, locked, group);
}

static void
vk_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_virtual_keyboard_v1_interface vk_impl = {
	.keymap = vk_keymap,
	.key = vk_key,
	.modifiers = vk_modifiers,
	.destroy = vk_destroy,
};

static void
vk_seat_destroyed(struct wl_listener *listener, void *data)
{
	struct virtual_keyboard *vk =
		wl_container_of(listener, vk, seat_destroy);

	wl_list_remove(&vk->seat_destroy.link);
	wl_list_init(&vk->seat_destroy.link);
	vk->seat = NULL;
}

static void
vk_resource_destroyed(struct wl_resource *resource)
{
	struct virtual_keyboard *vk = wl_resource_get_user_data(resource);

	wl_list_remove(&vk->seat_destroy.link);
	g_free(vk);
}

static void
manager_create(struct wl_client *client, struct wl_resource *resource,
	       struct wl_resource *seat_resource, uint32_t id)
{
	struct virtual_keyboard *vk = g_new0(struct virtual_keyboard, 1);
	struct wl_resource *keyboard = wl_resource_create(
		client, &zwp_virtual_keyboard_v1_interface, 1, id);

	if (!keyboard) {
		g_free(vk);
		wl_client_post_no_memory(client);
		return;
	}
	vk->seat = wl_resource_get_user_data(seat_resource);
	wl_list_init(&vk->seat_destroy.link);
	if (vk->seat) {
		vk->seat_destroy.notify = vk_seat_destroyed;
		wl_signal_add(&vk->seat->destroy_signal, &vk->seat_destroy);
	}
	wl_resource_set_implementation(keyboard, &vk_impl, vk,
				       vk_resource_destroyed);
}

static const struct zwp_virtual_keyboard_manager_v1_interface manager_impl = {
	.create_virtual_keyboard = manager_create,
};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &zwp_virtual_keyboard_manager_v1_interface, 1, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

gboolean
polo_backend_virtual_keyboard_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	server->virtual_keyboard_global = wl_global_create(
		server->display, &zwp_virtual_keyboard_manager_v1_interface, 1,
		server, manager_bind);
	return TRUE;
}

void
polo_backend_virtual_keyboard_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->virtual_keyboard_global, wl_global_destroy);
}
