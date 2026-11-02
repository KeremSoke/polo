/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tile bindings and drag-to-edge as a client sees them: the geometry, the
 * xdg_toplevel tiled states, shaking loose, Escape and Shift.
 */

#include <stdlib.h>

#include <linux/input.h>

#include "harness.h"

#define LEFT 1u
#define RIGHT 2u
#define TOP 4u
#define BOTTOM 8u

static void
settle(struct harness *h, struct harness_window *w)
{
	for (int i = 0; i < 6; i++) {
		harness_roundtrip(h);
		harness_wait_frame(h, w->surface);
	}
}

static void
press_super(struct harness *h, struct harness_window *w, uint32_t key)
{
	harness_key(h, KEY_LEFTMETA, true);
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_key(h, KEY_LEFTMETA, false);
	settle(h, w);
}

static void
check_geometry(struct harness_window *w, int x, int y, int width, int height)
{
	const struct harness_window_info *info = harness_window_info(w);

	CHECK(info->x == x && info->y == y && info->width == width &&
		      info->height == height,
	      "at %d,%d %dx%d, wanted %d,%d %dx%d", info->x, info->y,
	      info->width, info->height, x, y, width, height);
}

static void
begin_drag(struct harness *h, struct harness_window *w)
{
	const struct harness_window_info *info = harness_window_info(w);

	harness_move_pointer(h, info->x + 60, info->y + 60);
	harness_key(h, KEY_LEFTALT, true);
	harness_button(h, BTN_LEFT, true);
}

static void
end_drag(struct harness *h, struct harness_window *w)
{
	harness_button(h, BTN_LEFT, false);
	harness_key(h, KEY_LEFTALT, false);
	settle(h, w);
}

static void
park(struct harness *h, struct harness_window *w)
{
	begin_drag(h, w);
	harness_move_pointer(h, 500, 300);
	end_drag(h, w);
}

static void
test_bindings(struct harness *h, struct harness_window *w)
{
	press_super(h, w, KEY_LEFT);
	check_geometry(w, 0, 0, 512, 768);
	CHECK(w->tiled == (LEFT | TOP | BOTTOM), "tiled states %#x", w->tiled);
	CHECK(!w->xdg_maximized,
	      "a side-tiled window was told it is maximized");

	press_super(h, w, KEY_LEFT);
	check_geometry(w, 0, 0, 341, 768);

	press_super(h, w, KEY_RIGHT);
	check_geometry(w, 512, 0, 512, 768);
	CHECK(w->tiled == (RIGHT | TOP | BOTTOM), "tiled states %#x", w->tiled);

	press_super(h, w, KEY_UP);
	check_geometry(w, 0, 0, 512, 384);
	CHECK(w->tiled == (LEFT | TOP), "tiled states %#x", w->tiled);

	harness_key(h, KEY_LEFTMETA, true);
	harness_key(h, KEY_DOWN, true);
	harness_key(h, KEY_DOWN, false);
	harness_key(h, KEY_LEFTMETA, false);
	settle(h, w);
	check_geometry(w, 512, 384, 512, 384);
	CHECK(w->tiled == (RIGHT | BOTTOM), "tiled states %#x", w->tiled);
}

static void
test_drag_side(struct harness *h, struct harness_window *w)
{
	park(h, w);
	begin_drag(h, w);
	harness_move_pointer(h, 1, 300);
	end_drag(h, w);
	check_geometry(w, 0, 0, 512, 768);
	CHECK(w->tiled == (LEFT | TOP | BOTTOM), "tiled states %#x", w->tiled);

	begin_drag(h, w);
	harness_move_pointer(h, 300, 300);
	harness_roundtrip(h);
	CHECK(w->tiled == 0 || harness_window_info(w)->x != 0,
	      "the tiled window did not come loose");
	harness_move_pointer(h, 1022, 300);
	end_drag(h, w);
	check_geometry(w, 512, 0, 512, 768);
	CHECK(w->tiled == (RIGHT | TOP | BOTTOM), "tiled states %#x", w->tiled);

	begin_drag(h, w);
	harness_move_pointer(h, 300, 300);
	end_drag(h, w);
	CHECK(w->tiled == 0, "a free window keeps tiled states %#x", w->tiled);
	CHECK(harness_window_info(w)->width != 512, "kept the tiled width");
}

static void
test_drag_corner(struct harness *h, struct harness_window *w)
{
	park(h, w);
	begin_drag(h, w);
	harness_move_pointer(h, 1022, 766);
	end_drag(h, w);
	check_geometry(w, 512, 384, 512, 384);
	begin_drag(h, w);
	harness_move_pointer(h, 300, 300);
	end_drag(h, w);
	CHECK(w->tiled == 0, "tiled states %#x", w->tiled);
}

static void
test_drag_top(struct harness *h, struct harness_window *w)
{
	park(h, w);
	begin_drag(h, w);
	harness_move_pointer(h, 500, 0);
	end_drag(h, w);
	check_geometry(w, 0, 0, 1024, 768);
	CHECK(w->xdg_maximized, "the top edge did not maximize");
	CHECK(w->tiled == 0, "tiled states %#x", w->tiled);
	begin_drag(h, w);
	harness_move_pointer(h, 500, 300);
	end_drag(h, w);
	CHECK(!w->xdg_maximized, "still maximized");
}

static void
test_escape(struct harness *h, struct harness_window *w)
{
	park(h, w);
	struct harness_window_info start = *harness_window_info(w);

	begin_drag(h, w);
	harness_move_pointer(h, 1, 300);
	harness_key(h, KEY_ESC, true);
	harness_key(h, KEY_ESC, false);
	end_drag(h, w);
	check_geometry(w, start.x, start.y, start.width, start.height);
	CHECK(w->tiled == 0, "Escape tiled the window: %#x", w->tiled);
}

static void
test_escape_retiles(struct harness *h, struct harness_window *w)
{
	park(h, w);
	begin_drag(h, w);
	harness_move_pointer(h, 1, 300);
	end_drag(h, w);
	check_geometry(w, 0, 0, 512, 768);
	begin_drag(h, w);
	harness_move_pointer(h, 400, 300);
	settle(h, w);
	CHECK(harness_window_info(w)->width != 512, "did not come loose");
	harness_key(h, KEY_ESC, true);
	harness_key(h, KEY_ESC, false);
	end_drag(h, w);
	check_geometry(w, 0, 0, 512, 768);
	CHECK(w->tiled == (LEFT | TOP | BOTTOM), "tiled states %#x", w->tiled);
	begin_drag(h, w);
	harness_move_pointer(h, 300, 300);
	end_drag(h, w);
}

static void
test_shift_does_not_tile(struct harness *h, struct harness_window *w)
{
	park(h, w);
	begin_drag(h, w);
	harness_key(h, KEY_LEFTSHIFT, true);
	harness_move_pointer(h, 1, 300);
	harness_button(h, BTN_LEFT, false);
	harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, KEY_LEFTALT, false);
	settle(h, w);
	CHECK(w->tiled == 0, "a snap move tiled the window: %#x", w->tiled);
}

static void
test_tiling_off(int argc, char **argv)
{
	setenv("POLO_TEST_PREFS", "general/allow-tiling=false", 1);
	struct harness *h = harness_start(argc, argv);
	struct harness_window *w = harness_window_create(h, 300, 200, "off");

	settle(h, w);
	park(h, w);
	begin_drag(h, w);
	harness_move_pointer(h, 1, 300);
	end_drag(h, w);
	CHECK(w->tiled == 0 && harness_window_info(w)->width == 300,
	      "tiled with allow-tiling off");
	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

static void
test_move_to_monitor(int argc, char **argv)
{
	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	setenv("POLO_TEST_PREFS",
	       "window-keybindings/tile-to-side-w='<Super>Left';"
	       "window-keybindings/move-to-monitor-e='<Super>Right'",
	       1);
	struct harness *h = harness_start(argc, argv);
	struct harness_window *w =
		harness_window_create(h, 300, 200, "monitor");

	settle(h, w);
	press_super(h, w, KEY_LEFT);
	check_geometry(w, 0, 0, 512, 768);
	press_super(h, w, KEY_RIGHT);
	const struct harness_window_info *info = harness_window_info(w);

	CHECK(info->x >= 1024, "stayed on the first output at %d", info->x);
	CHECK(w->tiled == (LEFT | TOP | BOTTOM), "tiled states %#x", w->tiled);
	CHECK(info->height != 200, "lost the tiled height");
	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	unsetenv("POLO_TEST_TWO_OUTPUTS");
}

int
main(int argc, char **argv)
{
	setenv("POLO_TEST_PREFS",
	       "general/allow-tiling=true;general/allow-top-tiling=true;"
	       "general/allow-tile-cycling=true;"
	       "window-keybindings/tile-to-side-w='<Super>Left';"
	       "window-keybindings/tile-to-side-e='<Super>Right';"
	       "window-keybindings/tile-to-corner-nw='<Super>Up';"
	       "window-keybindings/tile-to-corner-se='<Super>Down'",
	       1);
	struct harness *h = harness_start(argc, argv);
	struct harness_window *w = harness_window_create(h, 300, 200, "tiling");

	settle(h, w);
	test_bindings(h, w);
	test_drag_side(h, w);
	test_drag_corner(h, w);
	test_drag_top(h, w);
	test_escape(h, w);
	test_escape_retiles(h, w);
	test_shift_does_not_tile(h, w);
	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");

	test_tiling_off(argc, argv);
	test_move_to_monitor(argc, argv);
	return 0;
}
