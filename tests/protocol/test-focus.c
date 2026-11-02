/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sloppy focus against headless Polo: the pointer moves focus between two
 * clients without raising either, the background keeps focus, and a click
 * still raises.
 */

#include <stdio.h>
#include <stdlib.h>

#include "harness.h"

static bool
inside(const struct harness_window_info *info, int x, int y)
{
	return x >= info->x && x < info->x + info->width && y >= info->y &&
		y < info->y + info->height;
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *a, *b;
	struct harness_window_info ia, ib;
	int x, y, status;

	setenv("POLO_TEST_PREFS",
	       "general/focus-mode='sloppy';general/center-new-windows=false",
	       1);
	h = harness_start(argc, argv);

	a = harness_window_create(h, 300, 200, "a");
	b = harness_window_create(h, 300, 200, "b");
	ia = *harness_window_info(a);
	ib = *harness_window_info(b);
	CHECK(h->keyboard_focus == b->surface, "b not focused after mapping");
	CHECK(ib.stack_index == 1 && ia.stack_index == 0, "b not on top");
	CHECK(!inside(&ia, ib.x + ib.width / 2, ib.y + ib.height / 2) &&
		      !inside(&ib, ia.x + ia.width / 2, ia.y + ia.height / 2),
	      "a at %d,%d and b at %d,%d overlap", ia.x, ia.y, ib.x, ib.y);

	harness_move_pointer(h, ia.x + ia.width / 2, ia.y + ia.height / 2);
	CHECK(h->keyboard_focus == a->surface, "pointer did not focus a");
	CHECK(harness_window_info(a)->focused, "model: a not focused");
	CHECK(harness_window_info(a)->stack_index == 0, "entering a raised it");

	harness_move_pointer(h, ib.x + ib.width / 2, ib.y + ib.height / 2);
	CHECK(h->keyboard_focus == b->surface, "pointer did not focus b");
	CHECK(harness_window_info(b)->stack_index == 1 &&
		      harness_window_info(a)->stack_index == 0,
	      "entering b restacked");

	x = HARNESS_OUTPUT_WIDTH - 2;
	y = HARNESS_OUTPUT_HEIGHT - 2;
	CHECK(!inside(&ia, x, y) && !inside(&ib, x, y), "no background");
	harness_move_pointer(h, x, y);
	CHECK(h->keyboard_focus == b->surface, "background took focus");
	CHECK(harness_window_info(b)->focused, "model: b lost focus");

	harness_click(h, ia.x + ia.width / 2, ia.y + ia.height / 2);
	CHECK(h->keyboard_focus == a->surface, "click did not focus a");
	CHECK(harness_window_info(a)->stack_index == 1,
	      "click did not raise a");

	harness_window_destroy(a);
	harness_window_destroy(b);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
