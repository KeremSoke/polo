/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * With no cursor image for the output, the pointer can still enter and leave
 * windows.
 */

#include <stdlib.h>

#include "harness.h"

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w;

	setenv("XCURSOR_PATH", "/nonexistent", 1);
	h = harness_start(argc, argv);
	w = harness_window_create(h, 200, 150, "under the pointer");
	harness_roundtrip(h);
	const struct harness_window_info *info = harness_window_info(w);
	for (int i = 0; i < 3; i++) {
		uint32_t before = h->pointer_enter_serial;

		harness_move_pointer(h, info->x + info->width + 40, info->y + info->height + 40);
		harness_roundtrip(h);
		harness_move_pointer(h, info->x + 20, info->y + 20);
		harness_roundtrip(h);
		CHECK(h->pointer_enter_serial != before, "the pointer did not enter the window");
	}
	harness_window_destroy(w);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
