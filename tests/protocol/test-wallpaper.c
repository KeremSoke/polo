/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The wallpaper fallback with picture-options "spanned" over two outputs:
 * each shows its part of the picture, also after an output moves left of
 * the other (to negative coordinates); caja's desktop hides the fallback
 * on its output, and it comes back when that desktop goes, as when
 * show-desktop-icons is turned off.
 */
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define RED 0xff0000u
#define BLUE 0x0000ffu
#define DESKTOP 0x3070c0u

static void
write_picture(const char *path)
{
	cairo_surface_t *surface = cairo_image_surface_create(
		CAIRO_FORMAT_RGB24, 2 * HARNESS_OUTPUT_WIDTH,
		HARNESS_OUTPUT_HEIGHT);
	cairo_t *cr = cairo_create(surface);

	cairo_set_source_rgb(cr, 1, 0, 0);
	cairo_rectangle(cr, 0, 0, HARNESS_OUTPUT_WIDTH, HARNESS_OUTPUT_HEIGHT);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0, 0, 1);
	cairo_rectangle(cr, HARNESS_OUTPUT_WIDTH, 0, HARNESS_OUTPUT_WIDTH,
			HARNESS_OUTPUT_HEIGHT);
	cairo_fill(cr);
	cairo_destroy(cr);
	CHECK(cairo_surface_write_to_png(surface, path) == CAIRO_STATUS_SUCCESS,
	      "cannot write %s", path);
	cairo_surface_destroy(surface);
}

static uint32_t
centre(struct harness *h, int index)
{
	int width, height;
	uint32_t *pixels = harness_capture_output(h, index, &width, &height);
	uint32_t pixel;

	CHECK(pixels, "cannot capture output %d", index);
	pixel = pixels[(height / 2) * width + width / 2] & 0xffffff;
	free(pixels);
	return pixel;
}

static void
expect(struct harness *h, int index, uint32_t colour, const char *what)
{
	uint32_t seen = 0;

	for (int i = 0; i < 100; i++) {
		seen = centre(h, index);
		if (seen == colour)
			return;
		harness_roundtrip(h);
		usleep(20000);
	}
	CHECK(false, "%s: output %d shows 0x%06x, not 0x%06x", what, index,
	      seen, colour);
}

static void
desktop_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
		  uint32_t serial, uint32_t width, uint32_t height)
{
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
}

static void
desktop_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
}

static const struct zwlr_layer_surface_v1_listener desktop_listener = {
	desktop_configure,
	desktop_closed,
};

int
main(int argc, char **argv)
{
	char path[512];

	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	struct harness *h = harness_start(argc, argv);

	CHECK(h->n_outputs == 2, "%d outputs", h->n_outputs);
	snprintf(path, sizeof path, "%s/span.png", h->runtime_dir);
	write_picture(path);

	char value[600];

	snprintf(value, sizeof value, "'%s'", path);
	harness_set_setting(h, "org.mate.background", "picture-options",
			    "'spanned'");
	harness_set_setting(h, "org.mate.background", "picture-filename",
			    value);
	expect(h, 0, RED, "spanned, left output");
	expect(h, 1, BLUE, "spanned, right output");

	polo_test_v1_configure_output(h->test, 1, 1, 0,
				      -HARNESS_OUTPUT_WIDTH, 0);
	harness_roundtrip(h);
	expect(h, 1, RED, "spanned after moving output 1 left");
	expect(h, 0, BLUE, "spanned after moving output 1 left");

	struct wl_surface *surface = wl_compositor_create_surface(h->compositor);
	struct zwlr_layer_surface_v1 *desktop = zwlr_layer_shell_v1_get_layer_surface(
		h->layer_shell, surface, h->outputs[0],
		ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "desktop");

	zwlr_layer_surface_v1_add_listener(desktop, &desktop_listener, h);
	zwlr_layer_surface_v1_set_anchor(desktop,
					 ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
						 ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
						 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
						 ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(desktop, -1);
	wl_surface_commit(surface);
	harness_roundtrip(h);

	struct wl_buffer *buffer = harness_buffer_create(
		h->shm, HARNESS_OUTPUT_WIDTH, HARNESS_OUTPUT_HEIGHT);

	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, HARNESS_OUTPUT_WIDTH,
				 HARNESS_OUTPUT_HEIGHT);
	harness_wait_frame(h, surface);
	expect(h, 0, DESKTOP, "caja's desktop");
	expect(h, 1, RED, "the other output with caja's desktop");

	zwlr_layer_surface_v1_destroy(desktop);
	wl_surface_destroy(surface);
	wl_buffer_destroy(buffer);
	harness_roundtrip(h);
	expect(h, 0, BLUE, "after caja's desktop went");

	unlink(path);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
