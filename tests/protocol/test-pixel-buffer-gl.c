/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Under the GL renderer, which lets go of a shm buffer once it is uploaded,
 * Polo-drawn surfaces keep redrawing from their last frame: the wallpaper is
 * redrawn for each colour.
 */

#include <stdio.h>
#include <stdlib.h>

#include "harness.h"

int
main(int argc, char **argv)
{
	static const char *colours[] = { "'#102030'", "'#405060'", "'#708090'",
					 "'#a0b0c0'", "'#d0e0f0'" };
	struct harness *h;

	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	h = harness_start(argc, argv);
	harness_set_setting(h, "org.mate.background", "picture-filename", "''");
	for (int round = 0; round < 4; round++) {
		for (unsigned i = 0; i < sizeof colours / sizeof colours[0];
		     i++) {
			harness_set_setting(h, "org.mate.background",
					    "primary-color", colours[i]);
			harness_wait_repaint(h);
		}
	}
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
