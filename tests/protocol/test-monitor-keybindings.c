/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's window placement keys over two side-by-side outputs:
 * move-to-monitor only goes to an output sharing the edge and keeps the
 * relative geometry (meta_window_move_to_monitor ()), and move-to-corner
 * and move-to-side use the work area of all outputs together
 * (handle_move_to_corner_backend ()).
 */

#include <stdio.h>
#include <stdlib.h>

#include <linux/input.h>

#include "harness.h"

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTSHIFT, true);
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	harness_roundtrip(h);
	harness_wait_repaint(h);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *a;
	struct harness_window_info start, now;
	int status;

	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	setenv("POLO_TEST_PREFS",
	       "window-keybindings/move-to-monitor-e='<Control><Shift>e';"
	       "window-keybindings/move-to-monitor-w='<Control><Shift>w';"
	       "window-keybindings/move-to-corner-ne='<Control><Shift>o';"
	       "window-keybindings/move-to-side-w='<Control><Shift>h';"
	       "window-keybindings/move-to-center='<Control><Shift>c'",
	       1);
	h = harness_start(argc, argv);
	harness_move_pointer(h, 10, 10);
	a = harness_window_create(h, 300, 200, "monitors");
	start = *harness_window_info(a);
	CHECK(start.x + start.width <= HARNESS_OUTPUT_WIDTH,
	      "not on the first output");

	tap(h, KEY_E);
	now = *harness_window_info(a);
	CHECK(now.x == start.x + HARNESS_OUTPUT_WIDTH && now.y == start.y &&
		      now.width == start.width && now.height == start.height,
	      "move-to-monitor-e gave %d,%d %dx%d", now.x, now.y, now.width,
	      now.height);

	tap(h, KEY_E);
	CHECK(harness_window_info(a)->x == now.x,
	      "moved east of the last output");

	tap(h, KEY_W);
	now = *harness_window_info(a);
	CHECK(now.x == start.x && now.y == start.y,
	      "move-to-monitor-w did not come back");

	tap(h, KEY_O);
	now = *harness_window_info(a);
	CHECK(now.x == 2 * HARNESS_OUTPUT_WIDTH - now.width && now.y == 0,
	      "move-to-corner-ne gave %d,%d instead of the right edge of both "
	      "outputs",
	      now.x, now.y);

	tap(h, KEY_C);
	now = *harness_window_info(a);
	CHECK(now.x ==
			      HARNESS_OUTPUT_WIDTH +
				      (HARNESS_OUTPUT_WIDTH - now.width) / 2 &&
		      now.y == (HARNESS_OUTPUT_HEIGHT - now.height) / 2,
	      "move-to-center did not centre on the second output: %d,%d",
	      now.x, now.y);

	tap(h, KEY_H);
	now = *harness_window_info(a);
	CHECK(now.x == 0, "move-to-side-w gave x=%d", now.x);

	harness_window_destroy(a);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
