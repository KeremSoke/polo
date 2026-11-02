/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdlib.h>
#include "harness.h"

int
main(int argc, char **argv)
{
	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	struct harness *h = harness_start(argc, argv);
	struct harness_window *window;
	const struct harness_window_info *info;
	harness_move_pointer(h, 1300, 300);
	window = harness_window_create(h, 300, 200, "output relocation");
	info = harness_window_info(window);
	CHECK(info->x >= 1024, "window should start on external output: %d",
	      info->x);
	int relative_x = info->x - 1024;
	int old_y = info->y;
	polo_test_v1_output_connected(h->test, 1, 0);
	harness_roundtrip(h);
	harness_wait_repaint(h);
	info = harness_window_info(window);
	CHECK(info->x == relative_x && info->y == old_y,
	      "relative position not preserved: %d,%d", info->x, info->y);
	polo_test_v1_output_connected(h->test, 1, 1);
	harness_roundtrip(h);
	harness_wait_repaint(h);
	polo_test_v1_configure_output(h->test, 0, 2, 1, -384, 0);
	harness_roundtrip(h);
	harness_wait_repaint(h);
	info = harness_window_info(window);
	CHECK(info->x >= -384 && info->x + info->width <= 0,
	      "rotation/scale clamp: %d", info->x);
	CHECK(info->y >= 0 && info->y + info->height <= 512, "height clamp: %d",
	      info->y);
	// Regression: re-enabling a disabled output with a new transform
	// crashed in weston_output_set_transform (seen on DRM at start-up).
	polo_test_v1_disable_output(h->test, 1);
	harness_roundtrip(h);
	harness_wait_repaint(h);
	polo_test_v1_configure_output(h->test, 1, 1, 1, 0, 0);
	harness_roundtrip(h);
	harness_wait_repaint(h);
	info = harness_window_info(window);
	CHECK(info->known, "window lost re-enabling a rotated output");
	// Last-output loss and reconnection retain the window and restore
	// visibility.
	polo_test_v1_output_connected(h->test, 1, 0);
	polo_test_v1_output_connected(h->test, 0, 0);
	harness_roundtrip(h);
	polo_test_v1_output_connected(h->test, 0, 1);
	harness_roundtrip(h);
	harness_wait_repaint(h);
	info = harness_window_info(window);
	CHECK(info->known, "window lost during all-output disconnect");
	harness_window_destroy(window);
	return harness_stop(h);
}
