/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwp_keyboard_shortcuts_inhibit_manager_v1. Granted on request; it works only
 * while its surface has keyboard focus. Ctrl+Alt+Shift+Escape ends it until
 * focus leaves the surface and comes back. VT switching is a libweston binding,
 * which runs before Polo's key filter and so is never inhibited.
 */

#include <libweston/libweston.h>

#include "keyboard-shortcuts-inhibit-unstable-v1-server-protocol.h"
#include "polo-backend-private.h"

struct polo_shortcuts {
	struct weston_keyboard *keyboard;
	struct wl_listener focus;
	struct wl_list inhibitors;
};

struct inhibitor {
	struct wl_resource *resource;
	struct polo_shortcuts *seat;
	struct weston_surface *surface;
	struct wl_listener surface_destroy;
	struct wl_list link;
	gboolean active;
	gboolean escaped;
};

static void
update_inhibitor(struct polo_shortcuts *seat, struct inhibitor *inhibitor)
{
	gboolean focused = seat->keyboard && inhibitor->surface &&
		seat->keyboard->focus == inhibitor->surface;
	gboolean active;

	if (!focused)
		inhibitor->escaped = FALSE;
	active = focused && !inhibitor->escaped;
	if (active == inhibitor->active)
		return;
	inhibitor->active = active;
	if (active)
		zwp_keyboard_shortcuts_inhibitor_v1_send_active(
			inhibitor->resource);
	else
		zwp_keyboard_shortcuts_inhibitor_v1_send_inactive(
			inhibitor->resource);
}

static void
focus_changed(struct wl_listener *listener, void *data)
{
	struct polo_shortcuts *seat = wl_container_of(listener, seat, focus);
	struct inhibitor *inhibitor;

	wl_list_for_each(inhibitor, &seat->inhibitors, link)
		update_inhibitor(seat, inhibitor);
}

static void
inhibitor_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_keyboard_shortcuts_inhibitor_v1_interface
	inhibitor_impl = {
		.destroy = inhibitor_destroy,
	};

static void
inhibitor_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct inhibitor *inhibitor =
		wl_container_of(listener, inhibitor, surface_destroy);

	wl_list_remove(&inhibitor->surface_destroy.link);
	wl_list_init(&inhibitor->surface_destroy.link);
	inhibitor->surface = NULL;
	inhibitor->active = FALSE;
}

static void
inhibitor_resource_destroyed(struct wl_resource *resource)
{
	struct inhibitor *inhibitor = wl_resource_get_user_data(resource);

	wl_list_remove(&inhibitor->surface_destroy.link);
	wl_list_remove(&inhibitor->link);
	g_free(inhibitor);
}

static void
manager_inhibit_shortcuts(struct wl_client *client,
			  struct wl_resource *resource, uint32_t id,
			  struct wl_resource *surface_resource,
			  struct wl_resource *seat_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct weston_seat *weston_seat =
		wl_resource_get_user_data(seat_resource);
	PoloBackendSeat *polo_seat = weston_seat
		? polo_backend_server_lookup_seat(server, weston_seat)
		: NULL;
	struct polo_shortcuts *seat =
		polo_seat ? polo_backend_seat_get_shortcuts(polo_seat) : NULL;
	struct inhibitor *inhibitor;

	if (seat) {
		wl_list_for_each(inhibitor, &seat->inhibitors, link)
		{
			if (inhibitor->surface == surface) {
				wl_resource_post_error(
					resource,
					ZWP_KEYBOARD_SHORTCUTS_INHIBIT_MANAGER_V1_ERROR_ALREADY_INHIBITED,
					"this surface already inhibits on this "
					"seat");
				return;
			}
		}
	}

	inhibitor = g_new0(struct inhibitor, 1);
	inhibitor->resource = wl_resource_create(
		client, &zwp_keyboard_shortcuts_inhibitor_v1_interface, 1, id);
	if (!inhibitor->resource) {
		g_free(inhibitor);
		wl_client_post_no_memory(client);
		return;
	}
	wl_list_init(&inhibitor->link);
	wl_list_init(&inhibitor->surface_destroy.link);
	wl_resource_set_implementation(inhibitor->resource, &inhibitor_impl,
				       inhibitor, inhibitor_resource_destroyed);
	if (!seat)
		return;
	inhibitor->seat = seat;
	inhibitor->surface = surface;
	inhibitor->surface_destroy.notify = inhibitor_surface_destroyed;
	wl_signal_add(&surface->destroy_signal, &inhibitor->surface_destroy);
	wl_list_insert(&seat->inhibitors, &inhibitor->link);
	update_inhibitor(seat, inhibitor);
}

static void
manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_keyboard_shortcuts_inhibit_manager_v1_interface
	manager_impl = {
		.destroy = manager_destroy,
		.inhibit_shortcuts = manager_inhibit_shortcuts,
	};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1,
		id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

gboolean
polo_shortcuts_takes_key(struct polo_shortcuts *seat, gboolean escape_hatch)
{
	struct inhibitor *inhibitor;

	wl_list_for_each(inhibitor, &seat->inhibitors, link)
	{
		if (!inhibitor->active)
			continue;
		if (!escape_hatch)
			return TRUE;
		inhibitor->escaped = TRUE;
		update_inhibitor(seat, inhibitor);
		return FALSE;
	}
	return FALSE;
}

struct polo_shortcuts *
polo_shortcuts_new(void)
{
	struct polo_shortcuts *seat = g_new0(struct polo_shortcuts, 1);

	wl_list_init(&seat->inhibitors);
	seat->focus.notify = focus_changed;
	return seat;
}

void
polo_shortcuts_attach_keyboard(struct polo_shortcuts *seat,
			       struct weston_keyboard *keyboard)
{
	if (seat->keyboard)
		wl_list_remove(&seat->focus.link);
	seat->keyboard = keyboard;
	if (keyboard)
		wl_signal_add(&keyboard->focus_signal, &seat->focus);
}

void
polo_shortcuts_free(struct polo_shortcuts *seat)
{
	struct inhibitor *inhibitor, *next;

	polo_shortcuts_attach_keyboard(seat, NULL);
	wl_list_for_each_safe(inhibitor, next, &seat->inhibitors, link)
	{
		inhibitor->seat = NULL;
		inhibitor->active = FALSE;
		wl_list_remove(&inhibitor->link);
		wl_list_init(&inhibitor->link);
	}
	g_free(seat);
}

gboolean
polo_backend_shortcuts_inhibit_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	server->shortcuts_inhibit_global = wl_global_create(
		server->display,
		&zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1, server,
		manager_bind);
	return TRUE;
}

void
polo_backend_shortcuts_inhibit_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->shortcuts_inhibit_global, wl_global_destroy);
}
