/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * msd-locate-pointer: with locate-pointer on, Control pressed and released
 * alone draws circles around the pointer and they go; the key still reaches
 * the focused window. Another key or a button in between, or the setting
 * off, draws nothing. Super may be held, as msd's Mod4 grab allows.
 */
#include <stdlib.h>
#include <time.h>

#include <linux/input.h>

#include "harness.h"

#define X 100
#define Y 100
#define HALF 50

static void
pause_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static uint32_t *
around(struct harness *h)
{
	int width, height;
	uint32_t *pixels = harness_capture(h, &width, &height);
	uint32_t *square = calloc((2 * HALF) * (2 * HALF), sizeof *square);

	for (int y = 0; pixels && y < 2 * HALF; y++)
		for (int x = 0; x < 2 * HALF; x++)
			square[y * 2 * HALF + x] =
				pixels[(Y - HALF + y) * width + X - HALF + x] &
				0xffffff;
	free(pixels);
	return square;
}

static int
changed(struct harness *h, const uint32_t *before)
{
	uint32_t *now = around(h);
	int count = 0;

	for (int i = 0; i < (2 * HALF) * (2 * HALF); i++)
		count += now[i] != before[i];
	free(now);
	return count;
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct harness_window *w = harness_window_create(h, 200, 150, "keys");
	const struct harness_window_info *info = harness_window_info(w);

	CHECK(info->x > X + HALF || info->y > Y + HALF,
	      "the window covers the pointer's square");
	harness_move_pointer(h, X, Y);
	pause_ms(100);
	uint32_t *before = around(h);

	CHECK(changed(h, before) == 0, "the square is not still");

	tap(h, KEY_LEFTCTRL);
	pause_ms(150);
	CHECK(changed(h, before) == 0, "circles with locate-pointer off");

	harness_set_setting(h, "org.mate.peripherals-mouse", "locate-pointer",
			    "true");
	h->last_key_surface = NULL;
	tap(h, KEY_LEFTCTRL);
	CHECK(h->last_key_surface == w->surface,
	      "Control did not reach the focused window");
	pause_ms(150);
	CHECK(changed(h, before) > 100, "no circles around the pointer");
	pause_ms(900);
	CHECK(changed(h, before) == 0, "the circles stayed");

	harness_key(h, KEY_RIGHTCTRL, true);
	tap(h, KEY_A);
	harness_key(h, KEY_RIGHTCTRL, false);
	pause_ms(150);
	CHECK(changed(h, before) == 0, "circles after Control+A");

	harness_key(h, KEY_LEFTCTRL, true);
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	pause_ms(150);
	CHECK(changed(h, before) == 0, "circles after a click");

	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_LEFTCTRL);
	harness_key(h, KEY_LEFTMETA, false);
	pause_ms(150);
	CHECK(changed(h, before) > 100, "no circles with Super held");
	pause_ms(900);

	harness_set_setting(h, "org.mate.peripherals-mouse", "locate-pointer",
			    "false");
	free(before);
	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
