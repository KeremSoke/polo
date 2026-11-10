/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Outputs blanked for being idle come back on input, and clients get frame
 * callbacks again afterwards, however many times it happens.
 */

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <time.h>

#include "harness.h"

static void
pump(struct harness *h, int msec)
{
	struct timespec tick = { 0, 20 * 1000000L };

	for (int i = 0; i < msec / 20; i++) {
		nanosleep(&tick, NULL);
		harness_roundtrip(h);
	}
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct harness_window *window;

	if (harness_get_setting(h, "org.mate.power-manager",
				"sleep-display-ac")[0] == '\0') {
		CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
		return 77;
	}
	harness_set_setting(h, "org.mate.power-manager", "sleep-display-ac",
			    "2");
	harness_set_setting(h, "org.mate.power-manager",
			    "sleep-display-battery", "2");
	window = harness_window_create(h, 300, 200, "frames");
	harness_wait_frame(h, window->surface);

	for (int cycle = 0; cycle < 4; cycle++) {
		pump(h, 3500);
		fprintf(stderr, "cycle %d: blanked, waking by %s\n", cycle,
			cycle % 2 ? "key" : "pointer");
		if (cycle % 2) {
			harness_key(h, KEY_LEFTSHIFT, true);
			harness_key(h, KEY_LEFTSHIFT, false);
		} else {
			harness_move_pointer(h, 100 + cycle, 100);
		}
		harness_roundtrip(h);
		harness_wait_frame(h, window->surface);
		for (int i = 0; i < 20; i++)
			harness_wait_frame(h, window->surface);
	}

	harness_window_destroy(window);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
