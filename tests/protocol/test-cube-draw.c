/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Drawing the desktop cube on GL, turned by its rotate keys: partway
 * through a turn the window's side is narrower and further left, with the
 * cube's background around the turning sides; the turn lands on the next
 * workspace with nothing of the cube left; lighting darkens a side seen at
 * an angle; with the cube off the keys do nothing.
 */

#include <stdlib.h>
#include <time.h>

#include <linux/input.h>

#include "harness.h"

#define WINDOW_COLOUR 0x3070c0u
#define BACKGROUND 0x204060u

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

struct sample {
	int window, background, bluish;
	double mean_x;
};

static struct sample
sample(struct harness *h)
{
	int width, height;
	uint32_t *pixels = harness_capture(h, &width, &height);
	struct sample s = { 0 };
	double sum_x = 0;

	CHECK(pixels, "cannot capture");
	for (int i = 0; i < width * height; i++) {
		uint32_t c = pixels[i] & 0xffffff;
		int r = c >> 16 & 0xff, g = c >> 8 & 0xff, b = c & 0xff;

		if (c == WINDOW_COLOUR) {
			s.window++;
			sum_x += i % width;
		} else if (c == BACKGROUND) {
			s.background++;
		} else if (b > 0x40 && b > 2 * r && b > g && c != BACKGROUND) {
			s.bluish++;
		}
	}
	s.mean_x = s.window ? sum_x / s.window : 0;
	free(pixels);
	return s;
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, KEY_LEFTMETA, true);
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_key(h, KEY_LEFTMETA, false);
	harness_roundtrip(h);
}

int
main(int argc, char **argv)
{
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	struct harness *h = harness_start(argc, argv);

	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	harness_set_setting(h, "org.mate.polo.keybindings", "cube-rotate-left",
			    "'<Mod4>bracketleft'");
	harness_set_setting(h, "org.mate.polo.keybindings", "cube-rotate-right",
			    "'<Mod4>bracketright'");
	harness_set_setting(h, "org.mate.polo.effects", "cube-background-color",
			    "'#204060'");
	harness_set_setting(h, "org.mate.polo.effects", "cube-duration-ms",
			    "2000");
	harness_set_setting(h, "org.mate.polo.effects", "cube-light", "false");
	harness_move_pointer(h, 5, 5);
	struct harness_window *window =
		harness_window_create(h, 300, 200, "cube");
	harness_wait_frame(h, window->surface);
	struct sample rest = sample(h);

	CHECK(rest.window == 300 * 200 && rest.background == 0,
	      "before: %d window and %d background pixels", rest.window,
	      rest.background);

	/* Off: the keys do nothing. */
	tap(h, KEY_RIGHTBRACE);
	pause_ms(h, 300);
	CHECK(sample(h).window == rest.window,
	      "with the cube off the rotate key changed the screen");

	harness_set_setting(h, "org.mate.polo.effects", "cube", "true");
	tap(h, KEY_RIGHTBRACE);
	pause_ms(h, 200);
	struct sample turning = sample(h);

	CHECK(turning.window > 1000 && turning.window < rest.window,
	      "partway through the window shows %d of %d pixels",
	      turning.window, rest.window);
	CHECK(turning.mean_x < rest.mean_x - 20,
	      "the window's side did not turn left: %.0f from %.0f",
	      turning.mean_x, rest.mean_x);
	CHECK(turning.background > 1000,
	      "no cube background around the turning sides (%d pixels)",
	      turning.background);

	pause_ms(h, 2200);
	struct sample landed = sample(h);

	CHECK(landed.window == 0 && landed.background == 0,
	      "after the turn: %d window and %d background pixels",
	      landed.window, landed.background);
	CHECK(harness_window_info(window)->workspace == 0,
	      "the window moved to workspace %d",
	      harness_window_info(window)->workspace);

	tap(h, KEY_LEFTBRACE);
	pause_ms(h, 2400);
	struct sample back = sample(h);

	CHECK(back.window == rest.window && back.background == 0 &&
		      back.mean_x == rest.mean_x,
	      "turning back: %d window pixels at %.0f, %d background",
	      back.window, back.mean_x, back.background);

	/* Two presses during one turn add up: two workspaces on. */
	tap(h, KEY_RIGHTBRACE);
	pause_ms(h, 100);
	tap(h, KEY_RIGHTBRACE);
	pause_ms(h, 2600);
	CHECK(sample(h).window == 0, "two presses showed the window");
	tap(h, KEY_LEFTBRACE);
	pause_ms(h, 2400);
	CHECK(sample(h).window == 0,
	      "one turn back from two on showed the window");
	tap(h, KEY_LEFTBRACE);
	pause_ms(h, 2400);
	CHECK(sample(h).window == rest.window,
	      "two turns back did not reach the window's workspace");

	/* Lighting: the side seen at an angle is darker. */
	harness_set_setting(h, "org.mate.polo.effects", "cube-light", "true");
	tap(h, KEY_RIGHTBRACE);
	pause_ms(h, 400);
	struct sample lit = sample(h);

	CHECK(lit.window == 0 && lit.bluish > 1000,
	      "lit: %d exact window pixels, %d shaded ones", lit.window,
	      lit.bluish);
	pause_ms(h, 2000);
	tap(h, KEY_LEFTBRACE);
	pause_ms(h, 2400);
	CHECK(sample(h).window == rest.window, "the lit turn left a shade");

	harness_window_destroy(window);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
