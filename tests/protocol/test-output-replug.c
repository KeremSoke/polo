/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tests an output whose monitor information changes, as a virtual machine's
 * display does when its window is resized: the output stays, and so do the
 * windows on it and the panels anchored to it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "harness.h"

struct panel {
	struct harness *h;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_buffer *buffer;
	bool mapped;
	bool closed;
};

static void
panel_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial,
		uint32_t width, uint32_t height)
{
	struct panel *p = data;

	zwlr_layer_surface_v1_ack_configure(ls, serial);
	if (!p->mapped) {
		p->buffer = harness_buffer_create(p->h->shm, (int)width, 24);
		wl_surface_attach(p->surface, p->buffer, 0, 0);
		p->mapped = true;
	}
	wl_surface_commit(p->surface);
}

static void
panel_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
	((struct panel *)data)->closed = true;
}

static const struct zwlr_layer_surface_v1_listener panel_listener = {
	panel_configure,
	panel_closed,
};

static void
settle(struct harness *h)
{
	for (int i = 0; i < 6; i++) {
		harness_roundtrip(h);
		usleep(10000);
	}
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct panel p = { .h = h };

	p.surface = wl_compositor_create_surface(h->compositor);
	p.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		h->layer_shell, p.surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		"panel");
	zwlr_layer_surface_v1_add_listener(p.layer_surface, &panel_listener,
					   &p);
	zwlr_layer_surface_v1_set_anchor(p.layer_surface,
					 ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
						 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
						 ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(p.layer_surface, 0, 24);
	zwlr_layer_surface_v1_set_exclusive_zone(p.layer_surface, 24);
	wl_surface_commit(p.surface);
	for (int i = 0; i < 20 && !harness_layer_info(h, p.surface)->mapped; i++)
		harness_roundtrip(h);
	CHECK(harness_layer_info(h, p.surface)->mapped, "the panel is not mapped");

	struct harness_window *w = harness_window_create(h, 300, 200, "window");

	settle(h);
	const struct harness_window_info *info = harness_window_info(w);
	int x = info->x, y = info->y, width = info->width, height = info->height;
	int removed = h->globals_removed;

	polo_test_v1_output_device_changed(h->test, h->outputs[0]);
	settle(h);

	CHECK(h->globals_removed == removed,
	      "the output's wl_output went away (%d globals removed)",
	      h->globals_removed - removed);
	CHECK(!p.closed, "the panel was closed");
	CHECK(harness_layer_info(h, p.surface)->mapped,
	      "the panel is no longer mapped");
	info = harness_window_info(w);
	CHECK(info->x == x && info->y == y && info->width == width &&
		      info->height == height,
	      "the window went from %d,%d %dx%d to %d,%d %dx%d", x, y, width,
	      height, info->x, info->y, info->width, info->height);

	harness_window_destroy(w);
	zwlr_layer_surface_v1_destroy(p.layer_surface);
	wl_surface_destroy(p.surface);
	wl_buffer_destroy(p.buffer);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
