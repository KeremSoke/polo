/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Verifies server-side decorations: a Marco frame reduces the client area, so
 * maximized client content starts below the titlebar.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "harness.h"

static const struct harness_window_info *
maximize(struct harness *h, struct harness_window *w)
{
	xdg_toplevel_set_maximized(w->xdg_toplevel);
	for (int i = 0; i < 6; i++)
		harness_roundtrip(h);
	return harness_window_info(w);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *plain, *framed;
	const struct harness_window_info *info;

	if (access("/usr/share/themes/Atlanta/metacity-1", F_OK) != 0) {
		printf("Atlanta is not installed\n");
		return 77;
	}
	setenv("POLO_TEST_PREFS", "general/theme='Atlanta'", 1);
	h = harness_start(argc, argv);

	plain = harness_window_create(h, 200, 150, "client-side");
	info = maximize(h, plain);
	CHECK(info->x == 0 && info->y == 0 &&
		      info->width == HARNESS_OUTPUT_WIDTH &&
		      info->height == HARNESS_OUTPUT_HEIGHT,
	      "a maximized client-side window is at %d,%d %dx%d", info->x,
	      info->y, info->width, info->height);
	harness_window_destroy(plain);
	harness_roundtrip(h);

	framed = harness_window_create_decorated(h, 200, 150, "server-side");
	info = harness_window_info(framed);
	CHECK(info->width == 200 && info->height == 150,
	      "the client was resized to %dx%d", info->width, info->height);
	info = maximize(h, framed);
	CHECK(info->y > 0 &&
		      info->height < HARNESS_OUTPUT_HEIGHT - info->y + 1 &&
		      info->height < HARNESS_OUTPUT_HEIGHT &&
		      info->width <= HARNESS_OUTPUT_WIDTH,
	      "a maximized framed client is at %d,%d %dx%d", info->x, info->y,
	      info->width, info->height);
	CHECK(framed->width == info->width && framed->height == info->height,
	      "the client drew %dx%d for %dx%d", framed->width, framed->height,
	      info->width, info->height);

	harness_window_destroy(framed);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
