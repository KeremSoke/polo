/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Interactive move and resize through xdg_toplevel.move/resize, as CSD
 * titlebars and borders request them, plus maximize.
 */

#include <stdio.h>
#include <time.h>

#include <linux/input.h>

#include "harness.h"

#define STATE_MAXIMIZED ((1u << 2) | (1u << 3))

static void
test_move(struct harness *h, struct harness_window *w)
{
	struct harness_window_info start = *harness_window_info(w);

	/* Press on the "titlebar", then the client asks to move. */
	harness_move_pointer(h, start.x + 50, start.y + 10);
	harness_button(h, BTN_LEFT, true);
	xdg_toplevel_move(w->xdg_toplevel, h->seat, h->last_button_serial);
	harness_roundtrip(h);

	harness_move_pointer(h, start.x + 50 + 70, start.y + 10 + 40);
	CHECK(harness_window_info(w)->x == start.x + 70 &&
		      harness_window_info(w)->y == start.y + 40,
	      "moved to %d,%d", w->info.x, w->info.y);

	harness_button(h, BTN_LEFT, false);
	harness_move_pointer(h, 5, 5);
	CHECK(harness_window_info(w)->x == start.x + 70,
	      "window followed the pointer after release");
}

static void
test_move_with_stale_serial(struct harness *h, struct harness_window *w)
{
	struct harness_window_info start = *harness_window_info(w);
	uint32_t stale = h->last_button_serial;

	/* No button held: Polo must refuse the grab. */
	xdg_toplevel_move(w->xdg_toplevel, h->seat, stale);
	harness_roundtrip(h);
	harness_move_pointer(h, 300, 300);
	CHECK(harness_window_info(w)->x == start.x &&
		      harness_window_info(w)->y == start.y,
	      "moved without a held button");
}

static void
test_resize_bottom_right(struct harness *h, struct harness_window *w, int dx,
			 int dy)
{
	struct harness_window_info start = *harness_window_info(w);
	int i;

	harness_move_pointer(h, start.x + start.width - 2,
			     start.y + start.height - 2);
	harness_button(h, BTN_LEFT, true);
	xdg_toplevel_resize(w->xdg_toplevel, h->seat, h->last_button_serial,
			    XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT);
	harness_roundtrip(h);

	harness_move_pointer(h, start.x + start.width - 2 + dx,
			     start.y + start.height - 2 + dy);
	for (i = 0; i < 4; i++)
		harness_roundtrip(h);
	harness_button(h, BTN_LEFT, false);

	harness_window_info(w);
	CHECK(w->info.width == start.width + dx &&
		      w->info.height == start.height + dy,
	      "resized to %dx%d", w->info.width, w->info.height);
	CHECK(w->info.x == start.x && w->info.y == start.y,
	      "origin moved to %d,%d", w->info.x, w->info.y);
}

static void
test_resize_top_left(struct harness *h, struct harness_window *w)
{
	struct harness_window_info start = *harness_window_info(w);
	int i;

	harness_move_pointer(h, start.x + 2, start.y + 2);
	harness_button(h, BTN_LEFT, true);
	xdg_toplevel_resize(w->xdg_toplevel, h->seat, h->last_button_serial,
			    XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT);
	harness_roundtrip(h);
	harness_move_pointer(h, start.x + 2 + 30, start.y + 2 + 20);
	for (i = 0; i < 4; i++)
		harness_roundtrip(h);
	harness_button(h, BTN_LEFT, false);

	harness_window_info(w);
	CHECK(w->info.width == start.width - 30 &&
		      w->info.height == start.height - 20,
	      "resized to %dx%d", w->info.width, w->info.height);
	CHECK(w->info.x + w->info.width == start.x + start.width &&
		      w->info.y + w->info.height == start.y + start.height,
	      "bottom-right corner moved");
}

static void
test_resize_configure_throttle(struct harness *h, struct harness_window *w)
{
	struct harness_window_info start = *harness_window_info(w);
	int configures;
	int i;

	harness_move_pointer(h, start.x + start.width - 2,
			     start.y + start.height - 2);
	harness_button(h, BTN_LEFT, true);
	xdg_toplevel_resize(w->xdg_toplevel, h->seat, h->last_button_serial,
			    XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT);
	harness_roundtrip(h);
	w->auto_configure = false;
	configures = w->configure_count;
	struct timespec moved, now;

	clock_gettime(CLOCK_MONOTONIC, &moved);
	harness_move_pointer(h, start.x + start.width + 8,
			     start.y + start.height + 8);
	CHECK(w->configure_count == configures + 1,
	      "first resize did not send one configure");
	polo_test_v1_move_pointer(h->test, start.x + start.width + 18,
				  start.y + start.height + 18);
	polo_test_v1_move_pointer(h->test, start.x + start.width + 38,
				  start.y + start.height + 28);
	harness_roundtrip(h);
	clock_gettime(CLOCK_MONOTONIC, &now);
	/* Polo gives up waiting after 50 ms and may rightly send another. */
	if ((now.tv_sec - moved.tv_sec) * 1000 +
		    (now.tv_nsec - moved.tv_nsec) / 1000000 <
	    50)
		CHECK(w->configure_count == configures + 1,
		      "sent %d configures before the first buffer was drawn",
		      w->configure_count - configures);
	w->auto_configure = true;
	harness_window_draw_pending(w);
	for (i = 0; i < 4; i++)
		harness_roundtrip(h);
	CHECK(harness_window_info(w)->width == start.width + 40 &&
		      harness_window_info(w)->height == start.height + 30,
	      "queued resize did not reach final pointer position");
	CHECK(w->configure_count == configures + 2,
	      "queued resize did not coalesce to one later configure");
	harness_button(h, BTN_LEFT, false);
}

static void
test_maximize(struct harness *h, struct harness_window *w)
{
	struct harness_window_info start = *harness_window_info(w);
	int i;

	xdg_toplevel_set_maximized(w->xdg_toplevel);
	for (i = 0; i < 4; i++)
		harness_roundtrip(h);
	harness_window_info(w);
	CHECK((w->info.state & STATE_MAXIMIZED) == STATE_MAXIMIZED,
	      "not maximized: 0x%x", w->info.state);
	CHECK(w->info.x == 0 && w->info.y == 0 &&
		      w->info.width == HARNESS_OUTPUT_WIDTH &&
		      w->info.height == HARNESS_OUTPUT_HEIGHT,
	      "maximized to %d,%d %dx%d", w->info.x, w->info.y, w->info.width,
	      w->info.height);

	xdg_toplevel_unset_maximized(w->xdg_toplevel);
	for (i = 0; i < 4; i++)
		harness_roundtrip(h);
	harness_window_info(w);
	CHECK(w->info.x == start.x && w->info.y == start.y &&
		      w->info.width == start.width &&
		      w->info.height == start.height,
	      "restored to %d,%d %dx%d", w->info.x, w->info.y, w->info.width,
	      w->info.height);
}

static void
test_drag_from_maximized(struct harness *h, struct harness_window *w)
{
	struct harness_window_info restored = *harness_window_info(w);
	int i;

	xdg_toplevel_set_maximized(w->xdg_toplevel);
	for (i = 0; i < 4; i++)
		harness_roundtrip(h);
	CHECK((harness_window_info(w)->state & STATE_MAXIMIZED) ==
		      STATE_MAXIMIZED,
	      "window was not maximized before drag");
	harness_move_pointer(h, 800, 100);
	harness_button(h, BTN_LEFT, true);
	xdg_toplevel_move(w->xdg_toplevel, h->seat, h->last_button_serial);
	harness_roundtrip(h);
	harness_move_pointer(h, 840, 120);
	for (i = 0; i < 4; i++)
		harness_roundtrip(h);
	harness_window_info(w);
	CHECK(!(w->info.state & STATE_MAXIMIZED),
	      "maximized window did not restore when dragged");
	CHECK(w->info.width == restored.width &&
		      w->info.height == restored.height,
	      "drag restore size became %dx%d", w->info.width, w->info.height);
	CHECK(w->info.x > 500 && w->info.x < 700 && w->info.y > 40 &&
		      w->info.y < 160,
	      "drag did not preserve relative pointer position: %d,%d",
	      w->info.x, w->info.y);
	harness_button(h, BTN_LEFT, false);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct harness_window *w;
	int status;

	w = harness_window_create(h, 300, 200, "interactive");
	test_move(h, w);
	test_move_with_stale_serial(h, w);
	test_resize_bottom_right(h, w, 60, 40);
	test_resize_top_left(h, w);
	test_resize_configure_throttle(h, w);
	test_maximize(h, w);
	test_drag_from_maximized(h, w);

	harness_window_destroy(w);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
