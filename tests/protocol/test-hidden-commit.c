/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Under the GL renderer, a window that commits a smaller buffer just before
 * it leaves an output, by a workspace switch or a move to the next monitor
 * and back, shows that buffer when it comes back, rather than having the
 * renderer read it with the old buffer's pitch.
 */

#include <stdlib.h>
#include <unistd.h>

#include <linux/input.h>

#include "harness.h"

#define OLD_COLOUR 0xff3070c0u
#define NEW_COLOUR 0xffc07030u

static void
switch_keys(struct harness *h, uint32_t arrow)
{
	polo_test_v1_key(h->test, KEY_LEFTCTRL, WL_KEYBOARD_KEY_STATE_PRESSED);
	polo_test_v1_key(h->test, KEY_LEFTALT, WL_KEYBOARD_KEY_STATE_PRESSED);
	polo_test_v1_key(h->test, arrow, WL_KEYBOARD_KEY_STATE_PRESSED);
	polo_test_v1_key(h->test, arrow, WL_KEYBOARD_KEY_STATE_RELEASED);
	polo_test_v1_key(h->test, KEY_LEFTALT, WL_KEYBOARD_KEY_STATE_RELEASED);
	polo_test_v1_key(h->test, KEY_LEFTCTRL, WL_KEYBOARD_KEY_STATE_RELEASED);
}

static void
monitor_keys(struct harness *h, uint32_t key)
{
	polo_test_v1_key(h->test, KEY_LEFTCTRL, WL_KEYBOARD_KEY_STATE_PRESSED);
	polo_test_v1_key(h->test, KEY_LEFTSHIFT, WL_KEYBOARD_KEY_STATE_PRESSED);
	polo_test_v1_key(h->test, key, WL_KEYBOARD_KEY_STATE_PRESSED);
	polo_test_v1_key(h->test, key, WL_KEYBOARD_KEY_STATE_RELEASED);
	polo_test_v1_key(h->test, KEY_LEFTSHIFT,
			 WL_KEYBOARD_KEY_STATE_RELEASED);
	polo_test_v1_key(h->test, KEY_LEFTCTRL, WL_KEYBOARD_KEY_STATE_RELEASED);
}

static void
replace_buffer(struct harness_window *w, int width, int height, uint32_t colour)
{
	if (w->buffer)
		wl_buffer_destroy(w->buffer);
	w->buffer = harness_buffer_create_colour(w->harness->shm, width, height,
						 colour);
	wl_surface_attach(w->surface, w->buffer, 0, 0);
	wl_surface_damage_buffer(w->surface, 0, 0, width, height);
	wl_surface_commit(w->surface);
}

static int
count_colour_on(struct harness *h, int output, uint32_t colour)
{
	int width, height, n = 0;
	uint32_t *pixels = harness_capture_output(h, output, &width, &height);

	CHECK(pixels, "no capture");
	for (int i = 0; i < width * height; i++)
		if ((pixels[i] & 0xffffff) == (colour & 0xffffff))
			n++;
	free(pixels);
	return n;
}

static int
count_colour(struct harness *h, uint32_t colour)
{
	return count_colour_on(h, 0, colour);
}

/* On whichever output the window ended up. */
static int
count_window_colour(struct harness *h, struct harness_window *w,
		    uint32_t colour)
{
	return count_colour_on(
		h, harness_window_info(w)->x >= HARNESS_OUTPUT_WIDTH ? 1 : 0,
		colour);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w;

	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	setenv("POLO_TEST_PREFS",
	       "window-keybindings/move-to-monitor-e='<Control><Shift>e';"
	       "window-keybindings/move-to-monitor-w='<Control><Shift>w'",
	       1);
	h = harness_start(argc, argv);
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "2");
	w = harness_window_create(h, 435, 235, "resized");
	harness_wait_frame(h, w->surface);

	for (int round = 0; round < 4; round++) {
		int width = 426 - round, height = 240 - round;

		replace_buffer(w, width, height, NEW_COLOUR);
		switch_keys(h, KEY_RIGHT);
		harness_roundtrip(h);
		usleep(100000);
		harness_roundtrip(h);
		switch_keys(h, KEY_LEFT);
		harness_roundtrip(h);
		harness_wait_frame(h, w->surface);
		CHECK(count_colour(h, NEW_COLOUR) == width * height,
		      "switch %d: the window shows %d pixels of its new %dx%d "
		      "buffer",
		      round, count_colour(h, NEW_COLOUR), width, height);
		replace_buffer(w, 435, 235, OLD_COLOUR);
		harness_wait_frame(h, w->surface);
	}

	for (int round = 0; round < 15; round++) {
		int width = 300 + round % 7, height = 200 + round % 5;

		replace_buffer(w, width, height, NEW_COLOUR);
		monitor_keys(h, KEY_E);
		wl_display_flush(h->display);
		usleep(1000 * round);
		monitor_keys(h, KEY_W);
		harness_roundtrip(h);
		harness_wait_frame(h, w->surface);
		harness_wait_frame(h, w->surface);
		CHECK(count_window_colour(h, w, NEW_COLOUR) == width * height,
		      "move %d: the window shows %d pixels of its new %dx%d "
		      "buffer",
		      round, count_window_colour(h, w, NEW_COLOUR), width,
		      height);
		replace_buffer(w, 435 - round % 3, 235, OLD_COLOUR);
		harness_wait_frame(h, w->surface);
	}

	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
