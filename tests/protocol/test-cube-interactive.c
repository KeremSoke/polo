/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The interactive cube opens zoomed out, turns with keys and pointer motion,
 * zooms with the wheel, and snaps or returns when the grab ends.
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
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_roundtrip(h);
}

static void
open_cube(struct harness *h)
{
	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_C);
	harness_key(h, KEY_LEFTMETA, false);
	pause_ms(h, 650);
}

static void
switch_left(struct harness *h)
{
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	tap(h, KEY_LEFT);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	pause_ms(h, 700);
}

int
main(int argc, char **argv)
{
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	struct harness *h = harness_start(argc, argv);
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	harness_set_setting(h, "org.mate.polo.effects", "cube", "true");
	harness_set_setting(h, "org.mate.polo.effects", "cube-light", "false");
	harness_set_setting(h, "org.mate.polo.effects", "cube-duration-ms",
			    "500");
	harness_move_pointer(h, 512, 384);
	struct harness_window *window =
		harness_window_create(h, 300, 200, "interactive-cube");
	harness_wait_frame(h, window->surface);
	CHECK(window_pixels(h) == 60000, "initial window is incomplete");

	open_cube(h);
	int zoomed = window_pixels(h);
	CHECK(zoomed > 1000 && zoomed < 60000,
	      "cube key did not zoom out: %d window pixels", zoomed);
	harness_scroll(h, 10);
	pause_ms(h, 200);
	int scrolled = window_pixels(h);
	CHECK(scrolled > 0 && scrolled < zoomed,
	      "wheel did not zoom the front side: %d versus %d", scrolled,
	      zoomed);
	tap(h, KEY_ESC);
	pause_ms(h, 700);
	CHECK(window_pixels(h) == 60000,
	      "Escape did not return to the start and clear the cube");

	open_cube(h);
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	pause_ms(h, 700);
	CHECK(window_pixels(h) == 60000, "click did not take the front side");

	open_cube(h);
	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_C);
	harness_key(h, KEY_LEFTMETA, false);
	pause_ms(h, 700);
	CHECK(window_pixels(h) == 60000, "cube key did not close the cube");

	open_cube(h);
	tap(h, KEY_RIGHT);
	pause_ms(h, 600);
	tap(h, KEY_ENTER);
	pause_ms(h, 700);
	CHECK(window_pixels(h) == 0,
	      "Right and Enter did not take the next workspace");
	switch_left(h);
	CHECK(window_pixels(h) == 60000, "failed to return to the first side");

	open_cube(h);
	int before_x, before_y, after_x, after_y;
	harness_pointer_position(h, &before_x, &before_y);
	harness_move_pointer(h, before_x - 80, before_y);
	harness_pointer_position(h, &after_x, &after_y);
	CHECK(after_x == before_x && after_y == before_y,
	      "relative motion moved the pointer: %d,%d to %d,%d", before_x,
	      before_y, after_x, after_y);
	pause_ms(h, 600);
	tap(h, KEY_SPACE);
	pause_ms(h, 700);
	CHECK(window_pixels(h) == 0,
	      "relative motion and Space did not select the next side");
	switch_left(h);
	CHECK(window_pixels(h) == 60000,
	      "relative turn did not land one side to the right");

	open_cube(h);
	harness_pointer_position(h, &before_x, &before_y);
	harness_move_pointer_absolute(h, before_x + 90, before_y);
	harness_pointer_position(h, &after_x, &after_y);
	CHECK(after_x == before_x + 90 && after_y == before_y,
	      "absolute motion did not move to %d,%d: got %d,%d", before_x + 90,
	      before_y, after_x, after_y);
	harness_move_pointer_absolute(h, 5000, before_y);
	harness_pointer_position(h, &after_x, &after_y);
	CHECK(after_x < HARNESS_OUTPUT_WIDTH && after_y == before_y,
	      "absolute pointer escaped the output at %d,%d", after_x, after_y);
	tap(h, KEY_ESC);
	pause_ms(h, 700);
	int after_absolute = window_pixels(h);
	CHECK(after_absolute == 60000,
	      "absolute motion left the cube visible: %d pixels",
	      after_absolute);

	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	harness_button(h, BTN_LEFT, true);
	pause_ms(h, 600);
	CHECK(window_pixels(h) < 60000, "cube-drag did not open the cube");
	harness_move_pointer(h, after_x - 80, after_y);
	pause_ms(h, 600);
	harness_button(h, BTN_LEFT, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	pause_ms(h, 700);
	CHECK(window_pixels(h) == 0, "cube-drag release did not land");
	switch_left(h);
	CHECK(window_pixels(h) == 60000, "cube-drag turned more than one side");

	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	harness_button(h, BTN_LEFT, true);
	harness_move_pointer(h, after_x - 80, after_y);
	pause_ms(h, 600);
	tap(h, KEY_ESC);
	pause_ms(h, 600);
	harness_button(h, BTN_LEFT, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	pause_ms(h, 100);
	CHECK(window_pixels(h) == 60000,
	      "Escape during cube-drag did not return to the first side");

	harness_window_destroy(window);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
