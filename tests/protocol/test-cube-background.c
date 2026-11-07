/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The zoomed-out cube shows both halves of a skydome image behind it.
 */
#include <cairo.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "harness.h"

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };
	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_roundtrip(h);
}

static void
write_skydome(const char *path)
{
	cairo_surface_t *surface =
		cairo_image_surface_create(CAIRO_FORMAT_RGB24, 256, 128);
	cairo_t *cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 1, 0, 0);
	cairo_rectangle(cr, 0, 0, 256, 64);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0, 0, 1);
	cairo_rectangle(cr, 0, 64, 256, 64);
	cairo_fill(cr);
	cairo_destroy(cr);
	CHECK(cairo_surface_write_to_png(surface, path) == CAIRO_STATUS_SUCCESS,
	      "cannot write skydome image");
	cairo_surface_destroy(surface);
}

static void
write_cubemap(const char *path)
{
	cairo_surface_t *surface =
		cairo_image_surface_create(CAIRO_FORMAT_RGB24, 256, 192);
	cairo_t *cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 0, 0, 1);
	cairo_paint(cr);
	cairo_set_source_rgb(cr, 1, 0, 0);
	cairo_rectangle(cr, 192, 64, 64, 64); /* -Z (back) */
	cairo_fill(cr);
	cairo_destroy(cr);
	CHECK(cairo_surface_write_to_png(surface, path) == CAIRO_STATUS_SUCCESS,
	      "cannot write cubemap image");
	cairo_surface_destroy(surface);
}

static void
write_magenta_cubemap(const char *path)
{
	cairo_surface_t *surface =
		cairo_image_surface_create(CAIRO_FORMAT_RGB24, 256, 192);
	cairo_t *cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 1, 0, 1);
	cairo_paint(cr);
	cairo_destroy(cr);
	CHECK(cairo_surface_write_to_png(surface, path) == CAIRO_STATUS_SUCCESS,
	      "cannot write cubemap image");
	cairo_surface_destroy(surface);
}

static void
count_colours(struct harness *h, int *red, int *blue)
{
	int width, height;
	uint32_t *pixels = harness_capture(h, &width, &height);
	CHECK(pixels, "cannot capture cube background");
	*red = *blue = 0;
	for (int i = 0; i < width * height; i++) {
		uint32_t colour = pixels[i] & 0xffffff;
		*red += colour == 0xff0000;
		*blue += colour == 0x0000ff;
	}
	free(pixels);
}

int
main(int argc, char **argv)
{
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	struct harness *h = harness_start(argc, argv);
	char path[512], setting[540];
	snprintf(path, sizeof path, "%s/skydome.png", h->runtime_dir);
	write_skydome(path);
	snprintf(setting, sizeof setting, "'%s'", path);
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	harness_set_setting(h, "org.mate.polo.effects", "cube", "true");
	harness_set_setting(h, "org.mate.polo.effects", "cube-light", "false");
	harness_set_setting(h, "org.mate.polo.effects", "cube-background",
			    "'skydome'");
	harness_set_setting(h, "org.mate.polo.effects", "cube-skydome-image",
			    setting);
	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_C);
	harness_key(h, KEY_LEFTMETA, false);
	pause_ms(h, 700);
	int red, blue;
	count_colours(h, &red, &blue);
	CHECK(red > 1000 && blue > 1000,
	      "skydome colours missing behind cube: red=%d blue=%d", red, blue);
	tap(h, KEY_ESC);
	pause_ms(h, 700);

	snprintf(path, sizeof path, "%s/cubemap.png", h->runtime_dir);
	write_cubemap(path);
	snprintf(setting, sizeof setting, "'%s'", path);
	harness_set_setting(h, "org.mate.polo.effects", "cube-background",
			    "'cubemap'");
	harness_set_setting(h, "org.mate.polo.effects", "cube-cubemap-image",
			    setting);
	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_C);
	harness_key(h, KEY_LEFTMETA, false);
	pause_ms(h, 700);
	count_colours(h, &red, &blue);
	CHECK(red > 1000, "cubemap back face missing: red=%d", red);
	tap(h, KEY_RIGHT);
	pause_ms(h, 700);
	count_colours(h, &red, &blue);
	CHECK(blue > 1000, "cubemap did not turn to another face: blue=%d",
	      blue);
	tap(h, KEY_LEFT);
	tap(h, KEY_LEFT);
	pause_ms(h, 700);
	count_colours(h, &red, &blue);
	CHECK(red == 0 && blue > 1000,
	      "the cubemap smears its seam facing +X: red=%d blue=%d", red,
	      blue);
	tap(h, KEY_ESC);
	pause_ms(h, 700);

	write_magenta_cubemap(path);
	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_C);
	harness_key(h, KEY_LEFTMETA, false);
	pause_ms(h, 700);
	int width, height, magenta = 0;
	uint32_t *pixels = harness_capture(h, &width, &height);
	CHECK(pixels, "cannot capture the rewritten cubemap");
	red = 0;
	for (int i = 0; i < width * height; i++) {
		magenta += (pixels[i] & 0xffffff) == 0xff00ff;
		red += (pixels[i] & 0xffffff) == 0xff0000;
	}
	free(pixels);
	CHECK(magenta > 1000 && red == 0,
	      "the rewritten cubemap image was not reloaded: magenta=%d red=%d",
	      magenta, red);
	tap(h, KEY_ESC);
	pause_ms(h, 700);

	harness_set_setting(h, "org.mate.polo.effects", "cube-background-color",
			    "'#00ff00'");
	harness_set_setting(h, "org.mate.polo.effects", "cube-background",
			    "'skydome'");
	harness_set_setting(h, "org.mate.polo.effects", "cube-skydome-image",
			    "'/no/such/cube-image.png'");
	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_C);
	harness_key(h, KEY_LEFTMETA, false);
	pause_ms(h, 700);
	int green = 0;
	pixels = harness_capture(h, &width, &height);
	CHECK(pixels, "cannot capture missing-image fallback");
	for (int i = 0; i < width * height; i++)
		green += (pixels[i] & 0xffffff) == 0x00ff00;
	free(pixels);
	CHECK(green > 1000, "missing image did not use solid background");
	tap(h, KEY_ESC);
	pause_ms(h, 700);

	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
