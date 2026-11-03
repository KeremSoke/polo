/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tests moving, resizing, maximizing, and closing through server-side frame
 * pointer input.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "harness.h"

static void
settle(struct harness *h)
{
	for (int i = 0; i < 6; i++)
		harness_roundtrip(h);
}

static void
drag(struct harness *h, int x, int y, int dx, int dy)
{
	harness_move_pointer(h, x, y);
	harness_button(h, BTN_LEFT, true);
	for (int i = 1; i <= 4; i++)
		harness_move_pointer(h, x + dx * i / 4, y + dy * i / 4);
	harness_button(h, BTN_LEFT, false);
	settle(h);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w;
	const struct harness_window_info *info;
	int x, y, width, height;

	if (access("/usr/share/themes/Atlanta/metacity-1", F_OK) != 0) {
		printf("Atlanta is not installed\n");
		return 77;
	}
	setenv("POLO_TEST_PREFS",
	       "general/theme='Atlanta';general/button-layout=':close'", 1);
	h = harness_start(argc, argv);

	w = harness_window_create_decorated(h, 300, 200, "framed");
	settle(h);
	info = harness_window_info(w);
	x = info->x;
	y = info->y;
	CHECK(y > 10, "the client is at %d,%d, under no titlebar", x, y);

	drag(h, x + 40, y - 8, 120, 60);
	info = harness_window_info(w);
	CHECK(info->x == x + 120 && info->y == y + 60,
	      "a titlebar drag from %d,%d moved the window to %d,%d", x, y,
	      info->x, info->y);
	x = info->x;
	y = info->y;
	width = info->width;
	height = info->height;

	drag(h, x + width + 4, y + height + 4, 50, 30);
	info = harness_window_info(w);
	CHECK(info->x == x && info->y == y && w->width == width + 50 &&
		      w->height == height + 30,
	      "a corner drag made %d,%d %dx%d of %d,%d %dx%d", info->x,
	      info->y, w->width, w->height, x, y, width, height);

	harness_click(h, x + 40, y - 8);
	harness_click(h, x + 40, y - 8);
	settle(h);
	info = harness_window_info(w);
	CHECK(info->width > width + 50 && info->y < y,
	      "a titlebar double-click left the window at %d,%d %dx%d",
	      info->x, info->y, info->width, info->height);
	harness_click(h, 40, info->y - 8);
	harness_click(h, 40, info->y - 8);
	settle(h);
	info = harness_window_info(w);
	CHECK(info->x == x && info->y == y,
	      "a second double-click left the window at %d,%d, not %d,%d",
	      info->x, info->y, x, y);

	CHECK(!w->close_requested, "the window was asked to close early");
	harness_click(h, x + info->width - 8, y - 8);
	settle(h);
	CHECK(w->close_requested, "the close button did not close the window");

	harness_window_destroy(w);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
