/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tests frames on outputs of different scales: a frame drawn at scale 2 takes
 * clicks and drags like one at scale 1, and follows its window from one
 * output to the other.
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
	int x, y;

	if (access("/usr/share/themes/Atlanta/metacity-1", F_OK) != 0) {
		printf("Atlanta is not installed\n");
		return 77;
	}
	setenv("POLO_TEST_PREFS",
	       "general/theme='Atlanta';general/button-layout=':close'", 1);
	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	setenv("POLO_TEST_SCALE", "--scales=1,2", 1);
	h = harness_start(argc, argv);

	/* The second output, at scale 2, starts at x 1024. */
	harness_move_pointer(h, 1024 + 150, 150);
	w = harness_window_create_decorated(h, 200, 150, "scaled");
	settle(h);
	info = harness_window_info(w);
	x = info->x;
	y = info->y;
	CHECK(x >= 1024, "the window opened at %d,%d, not on the second output",
	      x, y);

	drag(h, x + 30, y - 8, 20, 30);
	info = harness_window_info(w);
	CHECK(info->x == x + 20 && info->y == y + 30,
	      "a titlebar drag at scale 2 moved the window to %d,%d, not %d,%d",
	      info->x, info->y, x + 20, y + 30);
	x = info->x;
	y = info->y;

	drag(h, x + 30, y - 8, -(x - 300), 0);
	info = harness_window_info(w);
	CHECK(info->x == 300 && info->y == y,
	      "the drag to the first output left the window at %d,%d",
	      info->x, info->y);
	x = info->x;

	drag(h, x + 30, y - 8, 1024 + 100 - x, 0);
	info = harness_window_info(w);
	CHECK(info->x == 1024 + 100,
	      "the drag back to the second output left the window at %d,%d",
	      info->x, info->y);
	x = info->x;
	y = info->y;

	CHECK(!w->close_requested, "the window was asked to close early");
	harness_click(h, x + info->width - 8, y - 8);
	settle(h);
	CHECK(w->close_requested,
	      "the close button of a frame at scale 2 did not close the window");

	harness_window_destroy(w);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
