/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Per-monitor wallpapers (org.mate.background.monitor under the monitor's
 * id): a monitor with its own settings shows them and is left out of the
 * spanned picture's box; with use-shared it joins the box again; under a
 * transparent caja desktop the wallpaper shows only when draw-background
 * is false.
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
#define GREEN 0x00ff00u

#define OWN                                                                    \
	"org.mate.background.monitor:/org/mate/desktop/background/"            \
	"monitors/PNP_QQB_-Right/"

static const int same_size[][3] = { { HARNESS_OUTPUT_WIDTH,
				      HARNESS_OUTPUT_HEIGHT, 60000 } };
static const int smaller[][3] = { { 800, 600, 60000 } };

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
pixel_at(struct harness *h, int index, int x, int y)
{
	int width, height;
	uint32_t *pixels = harness_capture_output(h, index, &width, &height);
	uint32_t pixel;

	CHECK(pixels, "cannot capture output %d", index);
	CHECK(x < width && y < height, "output %d is %dx%d", index, width,
	      height);
	pixel = pixels[y * width + x] & 0xffffff;
	free(pixels);
	return pixel;
}

static void
expect(struct harness *h, int index, int x, int y, uint32_t colour,
       const char *what)
{
	uint32_t seen = 0;

	for (int i = 0; i < 100; i++) {
		seen = pixel_at(h, index, x, y);
		if (seen == colour)
			return;
		harness_roundtrip(h);
		usleep(20000);
	}
	CHECK(false, "%s: output %d at %d,%d shows 0x%06x, not 0x%06x", what,
	      index, x, y, seen, colour);
}

static void
expect_not(struct harness *h, int index, int x, int y, uint32_t colour,
	   const char *what)
{
	uint32_t seen = colour;

	for (int i = 0; i < 100; i++) {
		seen = pixel_at(h, index, x, y);
		if (seen != colour)
			return;
		harness_roundtrip(h);
		usleep(20000);
	}
	CHECK(false, "%s: output %d at %d,%d still shows 0x%06x", what, index,
	      x, y, colour);
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
	char path[512], value[600];

	setenv("POLO_TEST_MANAGED_OUTPUTS", "1", 1);
	struct harness *h = harness_start(argc, argv);

	CHECK(harness_output_count(h) == 1, "not one output at start");
	harness_add_output(h, "DP-1", "QQA", 0x0101, 11, "Left", false, 340,
			   270, same_size, 1);
	harness_add_output(h, "DP-2", "QQB", 0x0202, 22, "Right", false, 300,
			   220, smaller, 1);
	for (int i = 0; i < 100 && h->n_outputs < 3; i++)
		harness_roundtrip(h);
	CHECK(h->n_outputs == 3, "%d wl_outputs", h->n_outputs);

	snprintf(path, sizeof path, "%s/span.png", h->runtime_dir);
	write_picture(path);
	snprintf(value, sizeof value, "'%s'", path);
	harness_set_setting(h, "org.mate.background", "picture-options",
			    "'spanned'");
	harness_set_setting(h, "org.mate.background", "picture-filename",
			    value);

	harness_set_setting(h, OWN, "color-shading-type", "'solid'");
	harness_set_setting(h, OWN, "primary-color", "'#00ff00'");
	harness_set_setting(h, OWN, "picture-filename", "''");
	harness_set_setting(h, OWN, "use-shared", "false");
	expect(h, 2, 400, 300, GREEN, "own settings");
	expect(h, 0, 256, 384, RED, "first shared output");
	expect(h, 1, 256, 384, BLUE,
	       "second shared output, box without the own monitor");

	harness_set_setting(h, OWN, "use-shared", "true");
	expect(h, 2, 100, 300, BLUE, "use-shared again");
	expect(h, 1, 256, 384, RED, "box with all three");

	struct wl_surface *surface =
		wl_compositor_create_surface(h->compositor);
	struct zwlr_layer_surface_v1 *desktop =
		zwlr_layer_shell_v1_get_layer_surface(
			h->layer_shell, surface, h->outputs[0],
			ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "desktop");

	zwlr_layer_surface_v1_add_listener(desktop, &desktop_listener, h);
	zwlr_layer_surface_v1_set_anchor(
		desktop,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(desktop, -1);
	wl_surface_commit(surface);
	harness_roundtrip(h);

	struct wl_buffer *clear = harness_buffer_create_colour(
		h->shm, HARNESS_OUTPUT_WIDTH, HARNESS_OUTPUT_HEIGHT, 0);

	wl_surface_attach(surface, clear, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, HARNESS_OUTPUT_WIDTH,
				 HARNESS_OUTPUT_HEIGHT);
	harness_wait_frame(h, surface);
	expect_not(h, 0, 700, 384, RED,
		   "transparent desktop, caja draws the background");
	expect(h, 1, 256, 384, RED, "the other output keeps its wallpaper");

	harness_set_setting(h, "org.mate.background", "draw-background",
			    "false");
	expect(h, 0, 700, 384, RED, "draw-background off, Polo draws under it");

	harness_set_setting(h, "org.mate.background", "draw-background",
			    "true");
	expect_not(h, 0, 700, 384, RED, "draw-background on again");

	zwlr_layer_surface_v1_destroy(desktop);
	wl_surface_destroy(surface);
	wl_buffer_destroy(clear);
	harness_roundtrip(h);
	expect(h, 0, 700, 384, RED, "after caja's desktop went");

	unlink(path);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
