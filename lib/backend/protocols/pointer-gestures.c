/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwp_pointer_gestures_v1: touchpad gestures go to the client under the
 * pointer. A gesture stays with that client until it ends, wherever the pointer
 * goes. Polo consumes none. One of Polo's own grabs has cleared the focus, so
 * none starts then.
 */

#include <libweston/libweston.h>

#include "pointer-gestures-unstable-v1-server-protocol.h"
#include "polo-backend-private.h"

struct polo_gestures {
	struct weston_pointer *pointer;
	struct wl_listener gesture;
	struct wl_list resources[WESTON_POINTER_GESTURE_HOLD + 1];
	struct wl_client *client[WESTON_POINTER_GESTURE_HOLD + 1];
	struct wl_listener client_destroy[WESTON_POINTER_GESTURE_HOLD + 1];
	struct wl_display *display;
};

static void
gesture_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_pointer_gesture_swipe_v1_interface swipe_impl = {
	.destroy = gesture_destroy,
};

static const struct zwp_pointer_gesture_pinch_v1_interface pinch_impl = {
	.destroy = gesture_destroy,
};

static const struct zwp_pointer_gesture_hold_v1_interface hold_impl = {
	.destroy = gesture_destroy,
};

static void
gesture_resource_destroyed(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
send_begin(struct wl_resource *r, enum weston_pointer_gesture_kind kind,
	   uint32_t serial, uint32_t time, struct wl_resource *surface,
	   uint32_t fingers)
{
	switch (kind) {
	case WESTON_POINTER_GESTURE_SWIPE:
		zwp_pointer_gesture_swipe_v1_send_begin(r, serial, time,
							surface, fingers);
		break;
	case WESTON_POINTER_GESTURE_PINCH:
		zwp_pointer_gesture_pinch_v1_send_begin(r, serial, time,
							surface, fingers);
		break;
	case WESTON_POINTER_GESTURE_HOLD:
		zwp_pointer_gesture_hold_v1_send_begin(r, serial, time, surface,
						       fingers);
		break;
	}
}

static void
send_update(struct wl_resource *r,
	    const struct weston_pointer_gesture_event *ev)
{
	switch (ev->kind) {
	case WESTON_POINTER_GESTURE_SWIPE:
		zwp_pointer_gesture_swipe_v1_send_update(
			r, ev->time_msec, wl_fixed_from_double(ev->dx),
			wl_fixed_from_double(ev->dy));
		break;
	case WESTON_POINTER_GESTURE_PINCH:
		zwp_pointer_gesture_pinch_v1_send_update(
			r, ev->time_msec, wl_fixed_from_double(ev->dx),
			wl_fixed_from_double(ev->dy),
			wl_fixed_from_double(ev->scale),
			wl_fixed_from_double(ev->rotation));
		break;
	case WESTON_POINTER_GESTURE_HOLD:
		break;
	}
}

static void
send_end(struct wl_resource *r, enum weston_pointer_gesture_kind kind,
	 uint32_t serial, uint32_t time, int32_t cancelled)
{
	switch (kind) {
	case WESTON_POINTER_GESTURE_SWIPE:
		zwp_pointer_gesture_swipe_v1_send_end(r, serial, time,
						      cancelled);
		break;
	case WESTON_POINTER_GESTURE_PINCH:
		zwp_pointer_gesture_pinch_v1_send_end(r, serial, time,
						      cancelled);
		break;
	case WESTON_POINTER_GESTURE_HOLD:
		zwp_pointer_gesture_hold_v1_send_end(r, serial, time,
						     cancelled);
		break;
	}
}

static void
stop_tracking(struct polo_gestures *gestures,
	      enum weston_pointer_gesture_kind kind)
{
	if (gestures->client[kind]) {
		wl_list_remove(&gestures->client_destroy[kind].link);
		gestures->client[kind] = NULL;
	}
}

static void
client_gone(struct polo_gestures *gestures,
	    enum weston_pointer_gesture_kind kind)
{
	stop_tracking(gestures, kind);
}

static void
swipe_client_gone(struct wl_listener *listener, void *data)
{
	struct polo_gestures *gestures =
		wl_container_of(listener, gestures,
				client_destroy[WESTON_POINTER_GESTURE_SWIPE]);

	client_gone(gestures, WESTON_POINTER_GESTURE_SWIPE);
}

static void
pinch_client_gone(struct wl_listener *listener, void *data)
{
	struct polo_gestures *gestures =
		wl_container_of(listener, gestures,
				client_destroy[WESTON_POINTER_GESTURE_PINCH]);

	client_gone(gestures, WESTON_POINTER_GESTURE_PINCH);
}

static void
hold_client_gone(struct wl_listener *listener, void *data)
{
	struct polo_gestures *gestures =
		wl_container_of(listener, gestures,
				client_destroy[WESTON_POINTER_GESTURE_HOLD]);

	client_gone(gestures, WESTON_POINTER_GESTURE_HOLD);
}

static void
gesture_event(struct wl_listener *listener, void *data)
{
	struct polo_gestures *gestures =
		wl_container_of(listener, gestures, gesture);
	const struct weston_pointer_gesture_event *ev = data;
	struct weston_pointer *pointer = gestures->pointer;
	struct wl_list *resources = &gestures->resources[ev->kind];
	struct wl_resource *r;

	if (ev->phase == WESTON_POINTER_GESTURE_BEGIN) {
		struct wl_resource *surface;

		stop_tracking(gestures, ev->kind);
		if (!pointer->focus || !pointer->focus->surface->resource)
			return;
		surface = pointer->focus->surface->resource;
		gestures->client[ev->kind] = wl_resource_get_client(surface);
		wl_client_add_destroy_listener(
			gestures->client[ev->kind],
			&gestures->client_destroy[ev->kind]);
		uint32_t serial = wl_display_next_serial(gestures->display);

		wl_resource_for_each(r, resources)
		{
			if (wl_resource_get_client(r) ==
			    gestures->client[ev->kind])
				send_begin(r, ev->kind, serial, ev->time_msec,
					   surface, ev->fingers);
		}
		return;
	}
	if (!gestures->client[ev->kind])
		return;
	if (ev->phase == WESTON_POINTER_GESTURE_UPDATE) {
		wl_resource_for_each(r, resources)
		{
			if (wl_resource_get_client(r) ==
			    gestures->client[ev->kind])
				send_update(r, ev);
		}
		return;
	}
	uint32_t serial = wl_display_next_serial(gestures->display);

	wl_resource_for_each(r, resources)
	{
		if (wl_resource_get_client(r) == gestures->client[ev->kind])
			send_end(r, ev->kind, serial, ev->time_msec,
				 ev->cancelled);
	}
	stop_tracking(gestures, ev->kind);
}

static void
get_gesture(struct wl_client *client, struct wl_resource *resource, uint32_t id,
	    struct wl_resource *pointer_resource,
	    enum weston_pointer_gesture_kind kind,
	    const struct wl_interface *interface, const void *impl)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_pointer *pointer =
		wl_resource_get_user_data(pointer_resource);
	PoloBackendSeat *seat = pointer
		? polo_backend_server_lookup_seat(server, pointer->seat)
		: NULL;
	struct polo_gestures *gestures =
		seat ? polo_backend_seat_get_gestures(seat) : NULL;
	struct wl_resource *gesture = wl_resource_create(
		client, interface, wl_resource_get_version(resource), id);

	if (!gesture) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(gesture, impl, NULL,
				       gesture_resource_destroyed);
	if (gestures)
		wl_list_insert(&gestures->resources[kind],
			       wl_resource_get_link(gesture));
	else
		wl_list_init(wl_resource_get_link(gesture));
}

static void
manager_get_swipe(struct wl_client *client, struct wl_resource *resource,
		  uint32_t id, struct wl_resource *pointer)
{
	get_gesture(client, resource, id, pointer, WESTON_POINTER_GESTURE_SWIPE,
		    &zwp_pointer_gesture_swipe_v1_interface, &swipe_impl);
}

static void
manager_get_pinch(struct wl_client *client, struct wl_resource *resource,
		  uint32_t id, struct wl_resource *pointer)
{
	get_gesture(client, resource, id, pointer, WESTON_POINTER_GESTURE_PINCH,
		    &zwp_pointer_gesture_pinch_v1_interface, &pinch_impl);
}

static void
manager_get_hold(struct wl_client *client, struct wl_resource *resource,
		 uint32_t id, struct wl_resource *pointer)
{
	get_gesture(client, resource, id, pointer, WESTON_POINTER_GESTURE_HOLD,
		    &zwp_pointer_gesture_hold_v1_interface, &hold_impl);
}

static void
manager_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_pointer_gestures_v1_interface manager_impl = {
	.get_swipe_gesture = manager_get_swipe,
	.get_pinch_gesture = manager_get_pinch,
	.release = manager_release,
	.get_hold_gesture = manager_get_hold,
};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &zwp_pointer_gestures_v1_interface, version, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

struct polo_gestures *
polo_gestures_new(PoloBackendServer *server)
{
	struct polo_gestures *gestures = g_new0(struct polo_gestures, 1);

	gestures->display = server->display;
	gestures->gesture.notify = gesture_event;
	gestures->client_destroy[WESTON_POINTER_GESTURE_SWIPE].notify =
		swipe_client_gone;
	gestures->client_destroy[WESTON_POINTER_GESTURE_PINCH].notify =
		pinch_client_gone;
	gestures->client_destroy[WESTON_POINTER_GESTURE_HOLD].notify =
		hold_client_gone;
	for (int i = 0; i <= WESTON_POINTER_GESTURE_HOLD; i++)
		wl_list_init(&gestures->resources[i]);
	return gestures;
}

void
polo_gestures_attach_pointer(struct polo_gestures *gestures,
			     struct weston_pointer *pointer)
{
	if (gestures->pointer)
		wl_list_remove(&gestures->gesture.link);
	gestures->pointer = pointer;
	if (pointer)
		wl_signal_add(&pointer->gesture_signal, &gestures->gesture);
}

void
polo_gestures_free(struct polo_gestures *gestures)
{
	polo_gestures_attach_pointer(gestures, NULL);
	for (int i = 0; i <= WESTON_POINTER_GESTURE_HOLD; i++) {
		struct wl_resource *r, *next;

		stop_tracking(gestures, i);
		wl_resource_for_each_safe(r, next, &gestures->resources[i])
		{
			wl_list_remove(wl_resource_get_link(r));
			wl_list_init(wl_resource_get_link(r));
		}
	}
	g_free(gestures);
}

gboolean
polo_backend_pointer_gestures_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	server->pointer_gestures_global = wl_global_create(
		server->display, &zwp_pointer_gestures_v1_interface, 3, server,
		manager_bind);
	return TRUE;
}

void
polo_backend_pointer_gestures_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->pointer_gestures_global, wl_global_destroy);
}
