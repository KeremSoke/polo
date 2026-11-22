/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A VT switch away and back test.
 */

#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "harness.h"
#include "locker.h"

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec tick = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		nanosleep(&tick, NULL);
		harness_roundtrip(h);
	}
}

static void
away(struct harness *h)
{
	harness_set_session_active(h, false);
	pause_ms(h, 100);
	CHECK(!h->keyboard && !h->pointer,
	      "the seat kept its keyboard or pointer while away");
	h->last_key = 0;
	h->last_key_surface = NULL;
	harness_key(h, KEY_B, true);
	harness_key(h, KEY_B, false);
	harness_roundtrip(h);
	CHECK(h->last_key == 0, "a key went somewhere while away");
}

static void
back(struct harness *h)
{
	harness_set_session_active(h, true);
	pause_ms(h, 200);
	CHECK(h->keyboard && h->pointer,
	      "the seat did not give its keyboard and pointer back");
}

static bool
inside(const struct harness_window_info *info, int x, int y)
{
	return x >= info->x && y >= info->y && x < info->x + info->width &&
		y < info->y + info->height;
}

/* A point on a that b does not cover. */
static void
free_point(struct harness_window *a, struct harness_window *b, int *x, int *y)
{
	struct harness_window_info ia = *harness_window_info(a);
	struct harness_window_info ib = *harness_window_info(b);
	const int spots[][2] = {
		{ 1, 1 }, { 3, 1 }, { 1, 3 }, { 3, 3 }, { 2, 2 }
	};

	for (size_t i = 0; i < sizeof spots / sizeof spots[0]; i++) {
		*x = ia.x + ia.width * spots[i][0] / 4;
		*y = ia.y + ia.height * spots[i][1] / 4;
		if (!inside(&ib, *x, *y))
			return;
	}
	CHECK(false, "b covers all of a");
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *a, *b, *c;
	int x, y;

	setenv("POLO_TEST_PREFS", "general/center-new-windows=false", 1);
	h = harness_start(argc, argv);
	a = harness_window_create(h, 300, 200, "a");
	b = harness_window_create(h, 300, 200, "b");

	CHECK(h->keyboard_focus == b->surface, "b is not focused");
	CHECK(key_reaches(h, b->surface), "a key did not reach b");

	for (int round = 0; round < 3; round++) {
		away(h);
		back(h);
		CHECK(h->keyboard_focus == b->surface,
		      "b lost the keyboard after VT switch %d", round + 1);
		CHECK(harness_window_info(b)->focused,
		      "b is not the focused window after VT switch %d",
		      round + 1);
		CHECK(key_reaches(h, b->surface),
		      "a key did not reach b after VT switch %d", round + 1);
	}

	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_TAB, true);
	harness_key(h, KEY_TAB, false);
	pause_ms(h, 100);
	away(h);
	back(h);
	CHECK(h->keyboard_focus == a->surface && key_reaches(h, a->surface),
	      "leaving during Alt+Tab did not complete the switch to a");
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_TAB, true);
	harness_key(h, KEY_TAB, false);
	harness_key(h, KEY_LEFTALT, false);
	pause_ms(h, 200);
	CHECK(h->keyboard_focus == b->surface && key_reaches(h, b->surface),
	      "Alt+Tab did not work after leaving during Alt+Tab");

	free_point(a, b, &x, &y);
	harness_move_pointer(h, x, y);
	harness_roundtrip(h);
	CHECK(h->pointer_focus == a->surface,
	      "the pointer did not enter a after a VT switch");
	harness_click(h, x, y);
	CHECK(h->keyboard_focus == a->surface && key_reaches(h, a->surface),
	      "clicking a did not focus it after a VT switch");

	c = harness_window_create(h, 200, 150, "c");
	CHECK(h->keyboard_focus == c->surface, "c is not focused");
	away(h);
	harness_window_destroy(c);
	back(h);
	CHECK(h->keyboard_focus == a->surface && key_reaches(h, a->surface),
	      "the keyboard did not go to a after c closed while away");

	struct locker *l = locker_new(h);
	struct wl_surface *surface;
	struct ext_session_lock_surface_v1 *lock_surface;

	away(h);
	locker_lock(l);
	wl_display_roundtrip(l->display);
	surface = wl_compositor_create_surface(l->compositor);
	own(l, surface);
	lock_surface = ext_session_lock_v1_get_lock_surface(l->lock, surface,
							    l->output);
	own(l, lock_surface);
	ext_session_lock_surface_v1_add_listener(lock_surface,
						 &surface_listener, l);
	wl_display_roundtrip(l->display);
	CHECK(l->configured, "the lock surface was not configured");
	ext_session_lock_surface_v1_ack_configure(lock_surface, l->serial);
	struct wl_buffer *buffer =
		harness_buffer_create(l->shm, l->width, l->height);

	own(l, buffer);
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);
	wl_display_roundtrip(l->display);
	pump(h, l);
	back(h);
	CHECK(wait_locked(h, l), "no locked event after the return");
	CHECK(strcmp(harness_get_setting(h, "polo-test", "lock-has-keyboard"),
		     "true") == 0,
	      "the lock taken while away did not get the keyboard");
	CHECK(!key_reaches(h, a->surface), "a key reached a while locked");
	unlock(h, l);
	pause_ms(h, 200);
	CHECK(h->keyboard_focus == a->surface && key_reaches(h, a->surface),
	      "a did not get the keyboard back after the unlock");

	harness_window_destroy(b);
	harness_window_destroy(a);
	return harness_stop(h) == 0 ? 0 : 1;
}
