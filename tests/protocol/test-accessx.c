/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AccessX from org.mate.accessibility-keyboard, as the focused client sees
 * it: SlowKeys drops a short press and passes a held one; BounceKeys drops
 * a quick second press; StickyKeys latches, locks and unlocks Shift, and
 * two keys at once turn it off; five Shifts turn it on; MouseKeys moves and
 * clicks the pointer.
 */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <linux/input.h>

#include "harness.h"

#define SCHEMA "org.mate.accessibility-keyboard"
#define SHIFT 1u

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec ts = { 0, 10 * 1000000L };

	for (int waited = 0; waited < ms; waited += 10) {
		nanosleep(&ts, NULL);
		harness_roundtrip(h);
	}
}

static void
set(struct harness *h, const char *key, const char *value)
{
	harness_set_setting(h, SCHEMA, key, value);
}

static bool
setting_is(struct harness *h, const char *key, const char *value)
{
	return strcmp(harness_get_setting(h, SCHEMA, key), value) == 0;
}

static bool
typed(struct harness *h, uint32_t key)
{
	h->last_key = 0;
	harness_key(h, key, true);
	bool got = h->last_key == key && h->last_key_pressed;

	harness_key(h, key, false);
	return got;
}

static void
test_slow_keys(struct harness *h)
{
	set(h, "slowkeys-delay", "200");
	set(h, "slowkeys-enable", "true");

	h->last_key = 0;
	harness_key(h, KEY_A, true);
	pause_ms(h, 50);
	harness_key(h, KEY_A, false);
	pause_ms(h, 300);
	CHECK(h->last_key == 0, "SlowKeys passed a short press");

	harness_key(h, KEY_A, true);
	CHECK(h->last_key == 0, "SlowKeys passed a press at once");
	pause_ms(h, 300);
	CHECK(h->last_key == KEY_A && h->last_key_pressed,
	      "SlowKeys did not pass a held press");
	harness_key(h, KEY_A, false);
	CHECK(h->last_key == KEY_A && !h->last_key_pressed,
	      "the held key's release did not follow");
	set(h, "slowkeys-enable", "false");
}

static void
test_bounce_keys(struct harness *h)
{
	set(h, "bouncekeys-delay", "300");
	set(h, "bouncekeys-enable", "true");
	CHECK(typed(h, KEY_B), "BounceKeys dropped a first press");
	CHECK(!typed(h, KEY_B), "BounceKeys passed a bounce");
	pause_ms(h, 400);
	CHECK(typed(h, KEY_B), "BounceKeys dropped a press after the delay");
	set(h, "bouncekeys-enable", "false");
}

static void
test_sticky_keys(struct harness *h)
{
	set(h, "stickykeys-enable", "true");

	typed(h, KEY_LEFTSHIFT);
	pause_ms(h, 20);
	CHECK(h->mods_latched & SHIFT, "Shift did not latch");
	typed(h, KEY_A);
	CHECK(h->key_mods & SHIFT, "the latched Shift did not apply");
	pause_ms(h, 20);
	typed(h, KEY_A);
	CHECK(!(h->key_mods & SHIFT), "the latch outlived its key");

	typed(h, KEY_LEFTSHIFT);
	typed(h, KEY_LEFTSHIFT);
	pause_ms(h, 20);
	CHECK(h->mods_locked & SHIFT, "Shift twice did not lock");
	typed(h, KEY_A);
	pause_ms(h, 20);
	typed(h, KEY_A);
	CHECK(h->key_mods & SHIFT, "the lock did not hold");
	typed(h, KEY_LEFTSHIFT);
	pause_ms(h, 20);
	CHECK(!(h->mods_locked & SHIFT), "a third Shift did not unlock");

	set(h, "stickykeys-two-key-off", "true");
	harness_key(h, KEY_LEFTSHIFT, true);
	typed(h, KEY_A);
	harness_key(h, KEY_LEFTSHIFT, false);
	pause_ms(h, 20);
	CHECK(setting_is(h, "stickykeys-enable", "false"),
	      "two keys at once did not turn StickyKeys off");
}

static void
test_shift_toggle(struct harness *h)
{
	set(h, "enable", "true");
	for (int i = 0; i < 5; i++)
		typed(h, KEY_LEFTSHIFT);
	pause_ms(h, 20);
	CHECK(setting_is(h, "stickykeys-enable", "true"),
	      "Shift five times did not turn StickyKeys on");
	for (int i = 0; i < 5; i++)
		typed(h, KEY_LEFTSHIFT);
	pause_ms(h, 20);
	CHECK(setting_is(h, "stickykeys-enable", "false"),
	      "Shift five times did not turn StickyKeys off");
	set(h, "enable", "false");
}

static void
test_mouse_keys(struct harness *h, struct harness_window *w)
{
	const struct harness_window_info *info = harness_window_info(w);

	harness_move_pointer(h, info->x + 20, info->y + 20);
	set(h, "mousekeys-enable", "true");
	int motions = h->motions;

	CHECK(!typed(h, KEY_KP6), "a MouseKeys key reached the window");
	pause_ms(h, 20);
	CHECK(h->motions > motions, "KP_6 did not move the pointer");

	motions = h->motions;
	harness_key(h, KEY_KP6, true);
	pause_ms(h, 500);
	harness_key(h, KEY_KP6, false);
	CHECK(h->motions > motions + 2, "a held KP_6 did not keep moving");

	int presses = h->button_presses, releases = h->button_releases;

	typed(h, KEY_KP5);
	pause_ms(h, 20);
	CHECK(h->button_presses == presses + 1 &&
		      h->button_releases == releases + 1,
	      "KP_5 did not click");
	typed(h, KEY_KPPLUS);
	pause_ms(h, 20);
	CHECK(h->button_presses == presses + 3, "KP_Add did not double-click");
	set(h, "mousekeys-enable", "false");
	CHECK(typed(h, KEY_KP6), "KP_6 still moved with MouseKeys off");
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct harness_window *w = harness_window_create(h, 300, 200, "keys");

	CHECK(h->keyboard_focus == w->surface, "the window is not focused");
	CHECK(typed(h, KEY_A), "keys do not reach the window");
	test_slow_keys(h);
	test_bounce_keys(h);
	test_sticky_keys(h);
	test_shift_toggle(h);
	test_mouse_keys(h, w);

	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
