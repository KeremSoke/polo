/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Gestures go to the client under the pointer and stay with it until they end.
 * polo_test_v1.gesture enters where libweston's libinput code does.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/input.h>

#include "harness.h"
#include "pointer-gestures-unstable-v1-client-protocol.h"

/* The second connection dies on purpose with its proxies live. Polo is its own
 * process and is still checked. */
int
__lsan_is_turned_off(void);

int
__lsan_is_turned_off(void)
{
	return 1;
}

struct log {
	int begins, updates, ends;
	uint32_t fingers;
	struct wl_surface *surface;
	double dx, dy, scale, rotation;
	int cancelled;
};

static struct log swipe, pinch, hold, other_swipe;

static void
swipe_begin(void *data, struct zwp_pointer_gesture_swipe_v1 *g, uint32_t serial,
	    uint32_t time, struct wl_surface *surface, uint32_t fingers)
{
	struct log *log = data;

	log->begins++;
	log->fingers = fingers;
	log->surface = surface;
}

static void
swipe_update(void *data, struct zwp_pointer_gesture_swipe_v1 *g, uint32_t time,
	     wl_fixed_t dx, wl_fixed_t dy)
{
	struct log *log = data;

	log->updates++;
	log->dx = wl_fixed_to_double(dx);
	log->dy = wl_fixed_to_double(dy);
}

static void
swipe_end(void *data, struct zwp_pointer_gesture_swipe_v1 *g, uint32_t serial,
	  uint32_t time, int32_t cancelled)
{
	struct log *log = data;

	log->ends++;
	log->cancelled = cancelled;
}

static const struct zwp_pointer_gesture_swipe_v1_listener swipe_listener = {
	swipe_begin,
	swipe_update,
	swipe_end,
};

static void
pinch_begin(void *data, struct zwp_pointer_gesture_pinch_v1 *g, uint32_t serial,
	    uint32_t time, struct wl_surface *surface, uint32_t fingers)
{
	pinch.begins++;
	pinch.fingers = fingers;
}

static void
pinch_update(void *data, struct zwp_pointer_gesture_pinch_v1 *g, uint32_t time,
	     wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t scale, wl_fixed_t rotation)
{
	pinch.updates++;
	pinch.dx = wl_fixed_to_double(dx);
	pinch.scale = wl_fixed_to_double(scale);
	pinch.rotation = wl_fixed_to_double(rotation);
}

static void
pinch_end(void *data, struct zwp_pointer_gesture_pinch_v1 *g, uint32_t serial,
	  uint32_t time, int32_t cancelled)
{
	pinch.ends++;
	pinch.cancelled = cancelled;
}

static const struct zwp_pointer_gesture_pinch_v1_listener pinch_listener = {
	pinch_begin,
	pinch_update,
	pinch_end,
};

static void
hold_begin(void *data, struct zwp_pointer_gesture_hold_v1 *g, uint32_t serial,
	   uint32_t time, struct wl_surface *surface, uint32_t fingers)
{
	hold.begins++;
	hold.fingers = fingers;
}

static void
hold_end(void *data, struct zwp_pointer_gesture_hold_v1 *g, uint32_t serial,
	 uint32_t time, int32_t cancelled)
{
	hold.ends++;
	hold.cancelled = cancelled;
}

static const struct zwp_pointer_gesture_hold_v1_listener hold_listener = {
	hold_begin,
	hold_end,
};

struct binds {
	struct zwp_pointer_gestures_v1 *manager;
	struct wl_seat *seat;
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct binds *b = data;

	if (strcmp(interface, zwp_pointer_gestures_v1_interface.name) == 0)
		b->manager = wl_registry_bind(
			registry, name, &zwp_pointer_gestures_v1_interface, 3);
	else if (strcmp(interface, wl_seat_interface.name) == 0 && !b->seat)
		b->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

#define F(x) wl_fixed_from_double(x)

static void
gesture(struct harness *h, uint32_t kind, uint32_t phase, uint32_t fingers,
	double dx, double dy, double scale, double rotation, int cancelled)
{
	polo_test_v1_gesture(h->test, kind, phase, fingers, F(dx), F(dy), F(scale),
			     F(rotation), cancelled);
	harness_roundtrip(h);
}

enum { SWIPE, PINCH, HOLD };
enum { BEGIN, UPDATE, END };

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct binds b = { 0 };
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, &b);
	harness_roundtrip(h);
	CHECK(b.manager, "no zwp_pointer_gestures_v1");
	struct zwp_pointer_gesture_swipe_v1 *sw =
		zwp_pointer_gestures_v1_get_swipe_gesture(b.manager, h->pointer);
	struct zwp_pointer_gesture_pinch_v1 *pi =
		zwp_pointer_gestures_v1_get_pinch_gesture(b.manager, h->pointer);
	struct zwp_pointer_gesture_hold_v1 *ho =
		zwp_pointer_gestures_v1_get_hold_gesture(b.manager, h->pointer);

	zwp_pointer_gesture_swipe_v1_add_listener(sw, &swipe_listener, &swipe);
	zwp_pointer_gesture_pinch_v1_add_listener(pi, &pinch_listener, NULL);
	zwp_pointer_gesture_hold_v1_add_listener(ho, &hold_listener, NULL);
	harness_roundtrip(h);

	/* Nothing under the pointer: nothing goes anywhere. */
	gesture(h, SWIPE, BEGIN, 3, 0, 0, 1, 0, 0);
	gesture(h, SWIPE, UPDATE, 3, 4, 5, 1, 0, 0);
	gesture(h, SWIPE, END, 3, 0, 0, 1, 0, 0);
	CHECK(swipe.begins == 0 && swipe.updates == 0 && swipe.ends == 0,
	      "a gesture went somewhere with nothing under the pointer");

	struct harness_window *w = harness_window_create(h, 400, 300, "a");
	const struct harness_window_info *info = harness_window_info(w);
	int ax = info->x + 12, ay = info->y + 12;

	harness_move_pointer(h, ax, ay);
	harness_roundtrip(h);
	CHECK(h->pointer_focus == w->surface, "the pointer is not on the window");

	gesture(h, SWIPE, BEGIN, 3, 0, 0, 1, 0, 0);
	CHECK(swipe.begins == 1 && swipe.fingers == 3 && swipe.surface == w->surface,
	      "begin %d fingers %u", swipe.begins, swipe.fingers);
	gesture(h, SWIPE, UPDATE, 3, 4.5, -5.25, 1, 0, 0);
	CHECK(swipe.updates == 1 && swipe.dx == 4.5 && swipe.dy == -5.25,
	      "update %d %g,%g", swipe.updates, swipe.dx, swipe.dy);
	gesture(h, SWIPE, END, 3, 0, 0, 1, 0, 1);
	CHECK(swipe.ends == 1 && swipe.cancelled == 1, "end %d cancelled %d",
	      swipe.ends, swipe.cancelled);

	gesture(h, PINCH, BEGIN, 2, 0, 0, 1, 0, 0);
	gesture(h, PINCH, UPDATE, 2, 1.5, 0, 1.25, 30, 0);
	gesture(h, PINCH, END, 2, 0, 0, 1, 0, 0);
	CHECK(pinch.begins == 1 && pinch.fingers == 2 && pinch.updates == 1 &&
		      pinch.scale == 1.25 && pinch.rotation == 30 &&
		      pinch.dx == 1.5 && pinch.ends == 1 && pinch.cancelled == 0,
	      "pinch %d %g %g", pinch.updates, pinch.scale, pinch.rotation);
	CHECK(swipe.updates == 1, "a pinch reached the swipe object");

	gesture(h, HOLD, BEGIN, 1, 0, 0, 1, 0, 0);
	gesture(h, HOLD, END, 1, 0, 0, 1, 0, 0);
	CHECK(hold.begins == 1 && hold.fingers == 1 && hold.ends == 1,
	      "hold %d %d", hold.begins, hold.ends);

	/* Another client, listening, under no pointer, hears nothing. */
	struct binds ob = { 0 };
	struct wl_display *second = wl_display_connect(h->socket);

	CHECK(second != NULL, "second connection");
	wl_registry_add_listener(wl_display_get_registry(second), &registry_listener,
				 &ob);
	wl_display_roundtrip(second);
	struct wl_pointer *other_pointer = wl_seat_get_pointer(ob.seat);
	struct zwp_pointer_gesture_swipe_v1 *other_sw =
		zwp_pointer_gestures_v1_get_swipe_gesture(ob.manager, other_pointer);

	zwp_pointer_gesture_swipe_v1_add_listener(other_sw, &swipe_listener,
						  &other_swipe);
	wl_display_roundtrip(second);
	gesture(h, SWIPE, BEGIN, 4, 0, 0, 1, 0, 0);
	gesture(h, SWIPE, UPDATE, 4, 2, 0, 1, 0, 0);
	wl_display_roundtrip(second);
	CHECK(other_swipe.begins == 0 && other_swipe.updates == 0,
	      "another client heard the gesture");
	CHECK(swipe.begins == 2 && swipe.updates == 2,
	      "the client under the pointer did not");

	/* It stays with its client though the pointer leaves. */
	harness_move_pointer(h, info->x > 60 ? info->x - 40 : info->x + 440, ay);
	harness_roundtrip(h);
	CHECK(h->pointer_focus != w->surface, "the pointer did not leave");
	gesture(h, SWIPE, UPDATE, 4, 7, 0, 1, 0, 0);
	CHECK(swipe.updates == 3 && swipe.dx == 7, "it did not stay with its client");
	gesture(h, SWIPE, END, 4, 0, 0, 1, 0, 0);
	CHECK(swipe.ends == 2, "no end for the client that began it");
	wl_display_disconnect(second);
	harness_roundtrip(h);

	/* An update or end with no begin goes nowhere. */
	gesture(h, SWIPE, UPDATE, 3, 1, 1, 1, 0, 0);
	gesture(h, SWIPE, END, 3, 0, 0, 1, 0, 0);
	CHECK(swipe.updates == 3 && swipe.ends == 2, "a stray event was forwarded");

	/* None starts during a window move. */
	harness_move_pointer(h, ax, ay);
	harness_roundtrip(h);
	harness_key(h, KEY_LEFTALT, true);
	harness_button(h, BTN_LEFT, true);
	harness_move_pointer(h, ax + 20, ay + 20);
	harness_roundtrip(h);
	int before = swipe.begins;

	gesture(h, SWIPE, BEGIN, 3, 0, 0, 1, 0, 0);
	CHECK(swipe.begins == before, "a gesture started during a window move");
	gesture(h, SWIPE, END, 3, 0, 0, 1, 0, 0);
	harness_button(h, BTN_LEFT, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_roundtrip(h);

	/* A client that goes mid-gesture leaves nothing behind. */
	gesture(h, PINCH, BEGIN, 2, 0, 0, 1, 0, 0);
	harness_window_destroy(w);
	gesture(h, PINCH, UPDATE, 2, 0, 0, 1, 0, 0);
	gesture(h, PINCH, END, 2, 0, 0, 1, 0, 0);

	zwp_pointer_gesture_swipe_v1_destroy(sw);
	zwp_pointer_gesture_pinch_v1_destroy(pi);
	zwp_pointer_gesture_hold_v1_destroy(ho);
	zwp_pointer_gestures_v1_release(b.manager);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
