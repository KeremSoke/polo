/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Model switches immediately while the cube paints the old and new sides.
 * Repeated directional presses accumulate, wrap, and a carried window stays
 * flat.
 */
#include <linux/input.h>
#include <stdlib.h>
#include <time.h>
#include "harness.h"

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };
	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static int
window_pixels(struct harness *h)
{
	int width, height, count = 0;
	uint32_t *pixels = harness_capture(h, &width, &height);
	CHECK(pixels, "cannot capture cube frame");
	for (int i = 0; i < width * height; i++)
		count += (pixels[i] & 0xffffff) == 0x3070c0;
	free(pixels);
	return count;
}

static void
hold(struct harness *h, bool pressed, bool shift)
{
	if (pressed) {
		harness_key(h, KEY_LEFTCTRL, true);
		harness_key(h, KEY_LEFTALT, true);
		if (shift)
			harness_key(h, KEY_LEFTSHIFT, true);
	} else {
		if (shift)
			harness_key(h, KEY_LEFTSHIFT, false);
		harness_key(h, KEY_LEFTALT, false);
		harness_key(h, KEY_LEFTCTRL, false);
	}
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_roundtrip(h);
}

int
main(int argc, char **argv)
{
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	setenv("POLO_TEST_PREFS",
	       "global-keybindings/switch-to-workspace-3='<Control>F3'", 1);
	struct harness *h = harness_start(argc, argv);
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	harness_set_setting(h, "org.mate.polo.effects", "cube", "true");
	harness_set_setting(h, "org.mate.polo.effects", "cube-light", "false");
	harness_set_setting(h, "org.mate.polo.effects", "cube-duration-ms",
			    "1000");
	harness_move_pointer(h, 5, 5);
	struct harness_window *window =
		harness_window_create(h, 300, 200, "cube-switch");
	harness_wait_frame(h, window->surface);
	CHECK(window_pixels(h) == 60000, "initial window is incomplete");

	hold(h, true, false);
	tap(h, KEY_RIGHT);
	CHECK(!harness_window_info(window)->focused,
	      "model did not switch immediately");
	pause_ms(h, 200);
	int turning = window_pixels(h);
	CHECK(turning > 1000 && turning < 60000,
	      "cube did not show the old side during switch: %d pixels",
	      turning);
	tap(h, KEY_RIGHT);
	tap(h, KEY_RIGHT);
	hold(h, false, false);
	pause_ms(h, 1400);
	CHECK(window_pixels(h) == 0,
	      "three presses did not leave the first workspace");

	hold(h, true, false);
	tap(h, KEY_RIGHT);
	hold(h, false, false);
	pause_ms(h, 1400);
	int wrapped = window_pixels(h);
	CHECK(wrapped == 60000 && harness_window_info(window)->focused,
	      "directional switching did not wrap to the first workspace: %d "
	      "pixels, focused %d",
	      wrapped, harness_window_info(window)->focused);

	hold(h, true, true);
	tap(h, KEY_RIGHT);
	pause_ms(h, 200);
	CHECK(window_pixels(h) == 60000, "carried window was bent or hidden");
	hold(h, false, true);
	pause_ms(h, 1400);
	CHECK(harness_window_info(window)->workspace == 1 &&
		      harness_window_info(window)->focused &&
		      window_pixels(h) == 60000,
	      "carried window did not land focused on workspace two");

	harness_key(h, KEY_LEFTCTRL, true);
	tap(h, KEY_F3);
	harness_key(h, KEY_LEFTCTRL, false);
	CHECK(!harness_window_info(window)->focused,
	      "numbered binding did not change the model immediately");
	pause_ms(h, 200);
	CHECK(window_pixels(h) > 1000,
	      "numbered switch did not draw the old side");
	pause_ms(h, 1200);
	CHECK(window_pixels(h) == 0, "numbered switch left the cube on screen");

	harness_window_destroy(window);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
