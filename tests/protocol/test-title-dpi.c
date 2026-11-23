/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The title bar follows org.mate.font-rendering dpi, as Marco's does: a
 * higher DPI makes it taller, and 0 brings back the 96 DPI height.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "harness.h"

static int
title_height(struct harness *h, struct harness_window *w, int not)
{
	const struct harness_window_info *info = harness_window_info(w);

	for (int i = 0; i < 100 && info->y == not; i++) {
		harness_roundtrip(h);
		usleep(10000);
		info = harness_window_info(w);
	}
	return info->y;
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w;
	int base, high, back;

	if (access("/usr/share/themes/Atlanta/metacity-1", F_OK) != 0) {
		printf("Atlanta is not installed\n");
		return 77;
	}
	setenv("POLO_TEST_PREFS", "general/theme='Atlanta'", 1);
	h = harness_start(argc, argv);

	w = harness_window_create_decorated(h, 200, 150, "title-dpi");
	xdg_toplevel_set_maximized(w->xdg_toplevel);
	base = title_height(h, w, 0);
	CHECK(base > 0, "a maximized framed window has no title bar");

	harness_set_setting(h, "org.mate.font-rendering", "dpi", "192.0");
	high = title_height(h, w, base);
	CHECK(high > base, "at 192 DPI the title bar is %d px, at 96 DPI %d px",
	      high, base);

	harness_set_setting(h, "org.mate.font-rendering", "dpi", "0.0");
	back = title_height(h, w, high);
	CHECK(back == base,
	      "with dpi 0 the title bar is %d px, not the 96 DPI %d px", back,
	      base);

	harness_window_destroy(w);
	harness_stop(h);
	return 0;
}
