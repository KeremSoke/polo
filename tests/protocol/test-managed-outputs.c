/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DRM's output start-up path, headless: outputs are created disabled and
 * the Vala layout enables them through polo_backend_output_apply (). The
 * first DRM run crashed here (weston_output_set_transform on a
 * never-enabled output that already had a transform).
 */

#include <stdlib.h>

#include "harness.h"

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *window;
	const struct harness_window_info *info;
	int status;

	setenv("POLO_TEST_MANAGED_OUTPUTS", "1", 1);
	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	h = harness_start(argc, argv);

	harness_move_pointer(h, 300, 300);
	window = harness_window_create(h, 300, 200, "managed outputs");
	info = harness_window_info(window);
	CHECK(info->known, "no window on layout-enabled outputs");
	CHECK(info->x >= 0 && info->x + info->width <= HARNESS_OUTPUT_WIDTH,
	      "window not on the first output: %d", info->x);

	/* Switching an enabled output's transform keeps working too. */
	polo_test_v1_configure_output(h->test, 0, 1, 1, 0, 0);
	harness_roundtrip(h);
	harness_wait_repaint(h);
	CHECK(harness_window_info(window)->known, "window lost after rotation");

	harness_window_destroy(window);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d", status);
	return 0;
}
