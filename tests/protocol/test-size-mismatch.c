/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A maximized or fullscreen window whose frame is not the configured size
 * stays connected: a client drawing in the background commits its last
 * frame after acking a new size, and a window that can't be resized keeps
 * its own.
 */

#include <unistd.h>

#include "harness.h"

static void
commit_size(struct harness_window *w, int width, int height)
{
	struct wl_buffer *buffer =
		harness_buffer_create(w->harness->shm, width, height);

	wl_surface_attach(w->surface, buffer, 0, 0);
	wl_surface_damage_buffer(w->surface, 0, 0, width, height);
	wl_surface_commit(w->surface);
	if (w->buffer)
		wl_buffer_destroy(w->buffer);
	w->buffer = buffer;
	w->width = width;
	w->height = height;
	for (int i = 0; i < 5; i++)
		harness_roundtrip(w->harness);
}

static void
wait_for_size(struct harness_window *w, bool maximized)
{
	for (int i = 0; i < 100; i++) {
		harness_roundtrip(w->harness);
		if (w->xdg_maximized == maximized && w->pending_width > 0 &&
		    w->pending_height > 0)
			return;
		usleep(10000);
	}
	CHECK(false, "no %s configure with a size",
	      maximized ? "maximized" : "unmaximized");
}

static void
check_drawn(struct harness_window *w, int width, int height, const char *what)
{
	const struct harness_window_info *info = harness_window_info(w);

	CHECK(info->known, "%s: the window is gone", what);
	CHECK(info->width == width && info->height == height,
	      "%s: the window is %dx%d, not %dx%d", what, info->width,
	      info->height, width, height);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct harness_window *w =
		harness_window_create(h, 300, 200, "mismatch");
	int max_w, max_h;

	w->auto_configure = false;
	w->pending_width = w->pending_height = 0;
	xdg_toplevel_set_maximized(w->xdg_toplevel);
	wait_for_size(w, true);
	max_w = w->pending_width;
	max_h = w->pending_height;
	CHECK(max_w > 300 && max_h > 200, "maximized to %dx%d", max_w, max_h);

	commit_size(w, 300, 200);
	check_drawn(w, 300, 200, "a window that keeps its own size");

	commit_size(w, max_w, max_h);
	check_drawn(w, max_w, max_h, "the window catching up");

	commit_size(w, max_w, max_h - 1);
	check_drawn(w, max_w, max_h - 1, "a stale smaller frame");

	commit_size(w, max_w, max_h + 1);
	check_drawn(w, max_w, max_h + 1, "a stale larger frame");

	commit_size(w, max_w, max_h);
	check_drawn(w, max_w, max_h, "the window back at the maximized size");

	w->pending_width = w->pending_height = 0;
	xdg_toplevel_set_fullscreen(w->xdg_toplevel, NULL);
	for (int i = 0; i < 100 && w->pending_width != HARNESS_OUTPUT_WIDTH;
	     i++) {
		harness_roundtrip(h);
		usleep(10000);
	}
	CHECK(w->pending_width == HARNESS_OUTPUT_WIDTH &&
		      w->pending_height == HARNESS_OUTPUT_HEIGHT,
	      "fullscreen configured at %dx%d", w->pending_width,
	      w->pending_height);

	commit_size(w, HARNESS_OUTPUT_WIDTH + 10, HARNESS_OUTPUT_HEIGHT + 10);
	CHECK(harness_window_info(w)->known,
	      "a larger fullscreen frame: the window is gone");

	commit_size(w, HARNESS_OUTPUT_WIDTH, HARNESS_OUTPUT_HEIGHT);
	check_drawn(w, HARNESS_OUTPUT_WIDTH, HARNESS_OUTPUT_HEIGHT,
		    "the window at the fullscreen size");

	harness_window_destroy(w);
	harness_stop(h);
	return 0;
}
