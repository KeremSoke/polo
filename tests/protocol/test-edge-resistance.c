/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's edge resistance (edge-resistance.c) on modifier moves and
 * resizes: an edge dragged less than 32 px past a screen edge, or 16 px
 * past another window's edge, stays on it; further, it goes past.
 */

#include <stdio.h>
#include <stdlib.h>

#include <linux/input.h>

#include "harness.h"

static void
binding(struct harness *h, uint32_t key)
{
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTSHIFT, true);
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	harness_roundtrip(h);
}

static void
press(struct harness *h, uint32_t button, int x, int y)
{
	harness_move_pointer(h, x, y);
	harness_key(h, KEY_LEFTALT, true);
	harness_button(h, button, true);
}

static void
release(struct harness *h, uint32_t button)
{
	harness_button(h, button, false);
	harness_key(h, KEY_LEFTALT, false);
}

static struct harness_window_info
info(struct harness_window *w)
{
	return *harness_window_info(w);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *a, *b;
	struct harness_window_info start;
	int status, i;

	setenv("POLO_TEST_PREFS",
	       "general/resize-with-right-button=false;"
	       "window-keybindings/move-to-corner-nw='<Control><Shift>y';"
	       "window-keybindings/move-to-corner-ne='<Control><Shift>u'",
	       1);
	h = harness_start(argc, argv);
	a = harness_window_create(h, 300, 200, "a");

	binding(h, KEY_Y);
	start = info(a);
	CHECK(start.x == 0 && start.y == 0, "move-to-corner-nw gave %d,%d",
	      start.x, start.y);
	press(h, BTN_LEFT, 150, 100);
	harness_move_pointer(h, 250, 100);
	CHECK(info(a).x == 100, "moving off the screen edge gave x=%d",
	      info(a).x);
	harness_move_pointer(h, 130, 100);
	CHECK(info(a).x == 0, "20 px past the screen edge gave x=%d",
	      info(a).x);
	harness_move_pointer(h, 110, 100);
	CHECK(info(a).x == -40, "40 px past the screen edge gave x=%d",
	      info(a).x);
	release(h, BTN_LEFT);
	binding(h, KEY_U);

	b = harness_window_create(h, 300, 200, "b");
	CHECK(h->keyboard_focus == b->surface, "b not focused");
	binding(h, KEY_Y);
	CHECK(info(b).x == 0 && info(b).y == 0, "b not in the corner");
	start = info(a);
	harness_move_pointer(h, start.x + start.width / 2,
			     start.y + start.height / 2);
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	CHECK(h->keyboard_focus == a->surface, "a not focused");
	binding(h, KEY_U);
	start = info(a);
	CHECK(start.x + start.width == HARNESS_OUTPUT_WIDTH && start.y == 0,
	      "a not in the north-east corner: %d,%d", start.x, start.y);

	press(h, BTN_LEFT, start.x + 100, start.y + 100);
	harness_move_pointer(h, 290 + 100, 50 + 100);
	CHECK(info(a).x == 300 && info(a).y == 50,
	      "10 px past b's right edge gave %d,%d", info(a).x, info(a).y);
	harness_move_pointer(h, 270 + 100, 50 + 100);
	CHECK(info(a).x == 270, "30 px past b's right edge gave x=%d",
	      info(a).x);
	release(h, BTN_LEFT);

	binding(h, KEY_U);
	start = info(a);
	press(h, BTN_MIDDLE, start.x + 5, start.y + start.height / 2);
	harness_move_pointer(h, 290 + 5, start.y + start.height / 2);
	for (i = 0; i < 4; i++)
		harness_roundtrip(h);
	CHECK(info(a).x == 300 && info(a).width == HARNESS_OUTPUT_WIDTH - 300,
	      "resizing 10 px past b's right edge gave x=%d width=%d",
	      info(a).x, info(a).width);
	harness_move_pointer(h, 270 + 5, start.y + start.height / 2);
	for (i = 0; i < 4; i++)
		harness_roundtrip(h);
	CHECK(info(a).x == 270 && info(a).width == HARNESS_OUTPUT_WIDTH - 270,
	      "resizing 30 px past b's right edge gave x=%d width=%d",
	      info(a).x, info(a).width);
	release(h, BTN_MIDDLE);
	for (i = 0; i < 4; i++)
		harness_roundtrip(h);

	harness_window_destroy(b);
	harness_window_destroy(a);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
