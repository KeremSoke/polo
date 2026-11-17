/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Outputs blanked for being idle come back on input after a monitor was
 * turned off, unplugged and plugged in again (and stayed off).
 */

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "harness.h"

static const int dell[][3] = { { 1920, 1080, 60000 } };

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
	struct harness *h;
	struct harness_window *window;

	setenv("POLO_TEST_MANAGED_OUTPUTS", "1", 1);
	h = harness_start(argc, argv);
	if (harness_get_setting(h, "org.mate.power-manager",
				"sleep-display-ac")[0] == '\0') {
		CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
		return 77;
	}
	harness_add_output(h, "HDMI-A-1", "DEL", 0x4321, 12345, "U2715H", false,
			   597, 336, dell, 1);
	pump(h, 200);
	polo_test_v1_disable_output(h->test, 1);
	pump(h, 200);
	harness_connect_output(h, "HDMI-A-1", false);
	pump(h, 200);
	harness_connect_output(h, "HDMI-A-1", true);
	pump(h, 200);

	harness_set_setting(h, "org.mate.power-manager", "sleep-display-ac",
			    "1");
	harness_set_setting(h, "org.mate.power-manager",
			    "sleep-display-battery", "1");
	window = harness_window_create(h, 300, 200, "frames");
	harness_wait_frame(h, window->surface);

	for (int cycle = 0; cycle < 3; cycle++) {
		pump(h, 2600);
		if (cycle % 2) {
			harness_key(h, KEY_LEFTSHIFT, true);
			harness_key(h, KEY_LEFTSHIFT, false);
		} else {
			harness_move_pointer(h, 100 + cycle, 100);
		}
		harness_roundtrip(h);
		harness_wait_frame(h, window->surface);
		for (int i = 0; i < 3; i++)
			harness_wait_frame(h, window->surface);
	}

	harness_window_destroy(window);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
