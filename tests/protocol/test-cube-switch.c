/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Model switches immediately while the cube paints the old and new sides.
 * Repeated directional presses accumulate, wrap, and a carried window stays
 * flat.
 */
#include <linux/input.h>
#include <stdio.h>
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

#define TURN_MS 5000

static double
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int
first_change(struct harness *h)
{
	double deadline = now_ms() + TURN_MS;
	int pixels;

	do {
		pixels = window_pixels(h);
		if (pixels != 60000)
			break;
		harness_roundtrip(h);
	} while (now_ms() < deadline);
	return pixels;
}

static double turn_ms;
static double turn_over_at;

static void
set_turn_ms(struct harness *h, int ms)
{
	char value[16];

	snprintf(value, sizeof value, "%d", ms);
	harness_set_setting(h, "org.mate.polo.effects", "cube-duration-ms",
			    value);
	turn_ms = ms;
}

static void
turn_started(void)
{
	turn_over_at = now_ms() + turn_ms;
}

static int
landed(struct harness *h, int expected)
{
	double deadline = turn_over_at + 3000;
	int pixels;

	while (now_ms() < turn_over_at)
		pause_ms(h, 20);
	do {
		pixels = window_pixels(h);
		if (pixels == expected)
			break;
		pause_ms(h, 50);
	} while (now_ms() < deadline);
	return pixels;
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
	set_turn_ms(h, 1000);
	harness_move_pointer(h, 5, 5);
	struct harness_window *window =
		harness_window_create(h, 300, 200, "cube-switch");
	harness_wait_frame(h, window->surface);
	CHECK(window_pixels(h) == 60000, "initial window is incomplete");

	set_turn_ms(h, 3000);
	int click_x = harness_window_info(window)->x + 60;
	int click_y = harness_window_info(window)->y + 100;
	uint32_t serial = h->last_button_serial;

	hold(h, true, false);
	tap(h, KEY_RIGHT);
	turn_started();
	hold(h, false, false);
	pause_ms(h, 60);
	polo_test_v1_move_pointer(h->test, click_x, click_y);
	harness_roundtrip(h);
	polo_test_v1_button(h->test, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
	harness_roundtrip(h);
	polo_test_v1_button(h->test, BTN_LEFT,
			    WL_POINTER_BUTTON_STATE_RELEASED);
	harness_roundtrip(h);
	CHECK(h->last_button_serial == serial &&
		      h->pointer_focus != window->surface,
	      "a click during the turn reached the window on the old "
	      "workspace");
	polo_test_v1_move_pointer(h->test, 5, 5);
	harness_roundtrip(h);
	CHECK(landed(h, 0) == 0, "the clicked turn did not land");
	hold(h, true, false);
	tap(h, KEY_LEFT);
	turn_started();
	hold(h, false, false);
	CHECK(landed(h, 60000) == 60000,
	      "turning back did not show the window");
	set_turn_ms(h, 5000);

	hold(h, true, false);
	tap(h, KEY_RIGHT);
	turn_started();
	CHECK(!harness_window_info(window)->focused,
	      "model did not switch immediately");
	int turning = first_change(h);
	CHECK(turning > 1000 && turning < 60000,
	      "cube did not show the old side during switch: %d pixels",
	      turning);
	tap(h, KEY_RIGHT);
	turn_started();
	tap(h, KEY_RIGHT);
	turn_started();
	hold(h, false, false);
	CHECK(landed(h, 0) == 0,
	      "three presses did not leave the first workspace");

	hold(h, true, false);
	tap(h, KEY_RIGHT);
	turn_started();
	hold(h, false, false);
	int wrapped = landed(h, 60000);
	CHECK(wrapped == 60000 && harness_window_info(window)->focused,
	      "directional switching did not wrap to the first workspace: %d "
	      "pixels, focused %d",
	      wrapped, harness_window_info(window)->focused);

	set_turn_ms(h, 1000);
	hold(h, true, true);
	tap(h, KEY_RIGHT);
	turn_started();
	pause_ms(h, 200);
	CHECK(window_pixels(h) == 60000, "carried window was bent or hidden");
	hold(h, false, true);
	pause_ms(h, 1400);
	for (double deadline = now_ms() + 5000;
	     harness_window_info(window)->workspace != 1 &&
	     now_ms() < deadline;)
		pause_ms(h, 50);
	CHECK(harness_window_info(window)->workspace == 1 &&
		      harness_window_info(window)->focused &&
		      landed(h, 60000) == 60000,
	      "carried window did not land focused on workspace two");

	set_turn_ms(h, 5000);
	harness_key(h, KEY_LEFTCTRL, true);
	tap(h, KEY_F3);
	turn_started();
	harness_key(h, KEY_LEFTCTRL, false);
	CHECK(!harness_window_info(window)->focused,
	      "numbered binding did not change the model immediately");
	int numbered = first_change(h);
	CHECK(numbered > 1000 && numbered < 60000,
	      "numbered switch did not draw the old side: %d pixels, window "
	      "on workspace %d, focused %d",
	      numbered, harness_window_info(window)->workspace,
	      harness_window_info(window)->focused);
	CHECK(landed(h, 0) == 0, "numbered switch left the cube on screen");

	harness_window_destroy(window);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
