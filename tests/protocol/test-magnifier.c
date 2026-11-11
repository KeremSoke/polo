/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The screen magnifier (Weston's output zoom) on two headless outputs: with
 * it off an output pixel maps to the same global point; on, each output
 * shows the 1/scale part around its centre, kept inside the output, so a
 * touchscreen-style position lands where it is drawn. The output under the
 * pointer follows it and the other keeps its own part. A window is drawn
 * scale times larger, the key bindings change the factor and toggle it, and
 * the focused window is brought into view.
 *
 *   test-magnifier POLO [pixman|gl|hidpi]
 */

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"

#define WINDOW_COLOUR 0x3070c0u

static const int monitor_modes[][3] = { { 1920, 1080, 60000 } };

static struct harness *h;
static struct harness_output_state outs[2];
static double centre_x[2], centre_y[2];
static int n_outs;
static double zoom = 1;

static void
read_outputs(void)
{
	n_outs = harness_output_count(h);
	CHECK(n_outs == 2, "expected two outputs, got %d", n_outs);
	outs[0] = h->output_states[0];
	outs[1] = h->output_states[1];
	if (outs[1].x < outs[0].x) {
		struct harness_output_state t = outs[0];

		outs[0] = outs[1];
		outs[1] = t;
	}
	for (int i = 0; i < 2; i++) {
		centre_x[i] = outs[i].x + outs[i].width / 2.0;
		centre_y[i] = outs[i].y + outs[i].height / 2.0;
	}
}

static int
output_of(double x, double y)
{
	for (int i = 0; i < n_outs; i++)
		if (x >= outs[i].x && x < outs[i].x + outs[i].width &&
		    y >= outs[i].y && y < outs[i].y + outs[i].height)
			return i;
	return -1;
}

static void
origin(int o, double *x, double *y)
{
	double w = outs[o].width / zoom, hgt = outs[o].height / zoom;

	*x = fmin(fmax(centre_x[o] - w / 2, outs[o].x),
		  outs[o].x + outs[o].width - w);
	*y = fmin(fmax(centre_y[o] - hgt / 2, outs[o].y),
		  outs[o].y + outs[o].height - hgt);
}

static void
followed(double x, double y)
{
	int o = output_of(x, y);

	if (o >= 0 && zoom > 1) {
		centre_x[o] = x;
		centre_y[o] = y;
	}
}

static void
move_global(double x, double y)
{
	harness_move_pointer(h, (int)x, (int)y);
	followed((int)x, (int)y);
}

static void
check_pixel(int o, double px, double py, const char *what)
{
	double ox, oy, gx, gy;
	int x, y;

	origin(o, &ox, &oy);
	gx = ox + px / (outs[o].scale * zoom);
	gy = oy + py / (outs[o].scale * zoom);
	harness_move_pointer_output(h, outs[o].name, px, py);
	harness_pointer_position(h, &x, &y);
	CHECK(fabs(x - gx) <= 1 && fabs(y - gy) <= 1,
	      "%s: %s pixel (%.0f, %.0f) at %.0f× went to (%d, %d), not "
	      "(%.1f, %.1f)",
	      what, outs[o].name, px, py, zoom, x, y, gx, gy);
	followed(x, y);
}

static void
set_zoom(const char *factor, const char *enabled)
{
	harness_set_setting(h, "org.gnome.desktop.a11y.magnifier", "mag-factor",
			    factor);
	harness_set_setting(h, "org.gnome.desktop.a11y.applications",
			    "screen-magnifier-enabled", enabled);
	harness_wait_repaint(h);
	zoom = strcmp(enabled, "true") == 0 ? atof(factor) : 1;
}

static void
chord(uint32_t key)
{
	harness_key(h, KEY_LEFTMETA, true);
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTMETA, false);
	harness_wait_repaint(h);
	harness_wait_repaint(h);
}

static int
run(const uint32_t *pixels, int width, int height, int x, int y, int dx, int dy)
{
	int n = 0;

	while (x >= 0 && y >= 0 && x < width && y < height &&
	       (pixels[y * width + x] & 0xffffff) == WINDOW_COLOUR) {
		n++;
		x += dx;
		y += dy;
	}
	return n;
}

static void
window_extent(double px, double py, int *left, int *up)
{
	double ox, oy;
	int width, height, sx, sy;
	uint32_t *pixels = harness_capture_output(h, 0, &width, &height);

	CHECK(pixels, "cannot capture %s", outs[0].name);
	origin(0, &ox, &oy);
	sx = (int)lround((px - ox) * outs[0].scale * zoom) - 2;
	sy = (int)lround((py - oy) * outs[0].scale * zoom) - 2;
	*left = run(pixels, width, height, sx, sy, -1, 0) + 2;
	*up = run(pixels, width, height, sx, sy, 0, -1) + 2;
	free(pixels);
}

static void
check_drawn(const struct harness_window_info *info, int left0, int up0)
{
	double px = info->x + info->width / 2.0,
	       py = info->y + info->height / 2.0;
	int left, up;

	window_extent(px, py, &left, &up);
	CHECK(abs(left - (int)lround(left0 * zoom)) <= 4 &&
		      abs(up - (int)lround(up0 * zoom)) <= 4,
	      "at %.0f× the window reaches %d px left and %d up of the "
	      "pointer, not %.0f and %.0f",
	      zoom, left, up, left0 * zoom, up0 * zoom);
}

int
main(int argc, char **argv)
{
	struct harness_window *w, *other;
	const struct harness_window_info *info;
	int left0, up0;
	double px, py;

	CHECK(argc >= 2, "usage: %s POLO [pixman|gl|hidpi]", argv[0]);
	if (argc > 2 && strcmp(argv[2], "gl") == 0) {
		setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
		setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
	}
	if (argc > 2 && strcmp(argv[2], "hidpi") == 0)
		setenv("POLO_TEST_SCALE", "--scale=2", 1);
	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	h = harness_start(argc, argv);
	read_outputs();

	check_pixel(0, 100 * outs[0].scale, 50 * outs[0].scale, "off");
	check_pixel(1, 30 * outs[1].scale, 70 * outs[1].scale, "off");

	move_global(outs[0].x + 200, outs[0].y + 200);
	w = harness_window_create(h, 240, 160, "magnified");
	harness_wait_frame(h, w->surface);
	info = harness_window_info(w);
	CHECK(info->known && output_of(info->x, info->y) == 0,
	      "the window is not on the first output (known %d at %d,%d "
	      "%dx%d; outputs %s %d,%d %dx%d and %s %d,%d %dx%d)",
	      info->known, info->x, info->y, info->width, info->height,
	      outs[0].name, outs[0].x, outs[0].y, outs[0].width, outs[0].height,
	      outs[1].name, outs[1].x, outs[1].y, outs[1].width,
	      outs[1].height);
	px = info->x + info->width / 2.0;
	py = info->y + info->height / 2.0;
	move_global(px, py);
	window_extent(px, py, &left0, &up0);
	CHECK(left0 >= 100 && up0 >= 60, "the window shows %d by %d px", left0,
	      up0);

	set_zoom("2.0", "true");
	followed(px, py);
	check_drawn(info, left0, up0);

	move_global(outs[0].x + outs[0].width / 2.0,
		    outs[0].y + outs[0].height / 2.0);
	check_pixel(0, 0, 0, "top-left pixel");
	check_pixel(0, outs[0].width * outs[0].scale / 2.0,
		    outs[0].height * outs[0].scale / 2.0, "middle pixel");
	move_global(outs[0].x + outs[0].width - 1,
		    outs[0].y + outs[0].height - 1);
	check_pixel(0, 10, 10, "near the bottom-right corner");

	{
		const struct harness_output_state *plugged;
		double gx, gy;
		int x, y;

		harness_add_output(h, "HDMI-A-1", "DEL", 0x4321, 7, "Monitor",
				   false, 527, 296, monitor_modes, 1);
		harness_wait_repaint(h);
		plugged = harness_output_state(h, "HDMI-A-1");
		CHECK(plugged && plugged->enabled, "the plugged output is off");
		gx = plugged->x + plugged->width / 4.0;
		gy = plugged->y + plugged->height / 4.0;
		harness_move_pointer_output(h, "HDMI-A-1", 0, 0);
		harness_pointer_position(h, &x, &y);
		CHECK(fabs(x - gx) <= 1 && fabs(y - gy) <= 1,
		      "an output plugged in while magnified shows (%d, %d) at "
		      "its corner, not (%.1f, %.1f)",
		      x, y, gx, gy);
		move_global(centre_x[0], centre_y[0]);
		harness_remove_output(h, "HDMI-A-1");
		harness_wait_repaint(h);
		CHECK(harness_output_count(h) == 2,
		      "unplugging left %d outputs", harness_output_count(h));
	}
	check_pixel(1, 0, 0, "the other output, untouched");
	check_pixel(0, 40, 30, "back on the first output");
	check_pixel(1, outs[1].width * outs[1].scale - 1,
		    outs[1].height * outs[1].scale - 1,
		    "the other output's last pixel");

	chord(KEY_EQUAL);
	CHECK(strcmp(harness_get_setting(h, "org.gnome.desktop.a11y.magnifier",
					 "mag-factor"),
		     "3.0") == 0,
	      "zoom in left mag-factor at %s",
	      harness_get_setting(h, "org.gnome.desktop.a11y.magnifier",
				  "mag-factor"));
	zoom = 3;
	check_pixel(0, 200, 100, "after zooming in");
	move_global(px, py);
	check_drawn(info, left0, up0);

	other = harness_window_create(h, 120, 90, "focused");
	harness_wait_frame(h, other->surface);
	harness_wait_repaint(h);
	{
		const struct harness_window_info *o =
			harness_window_info(other);
		int out = output_of(o->x + o->width / 2.0,
				    o->y + o->height / 2.0);

		CHECK(out >= 0, "the second window is off screen");
		centre_x[out] = o->x + o->width / 2.0;
		centre_y[out] = o->y + o->height / 2.0;
		check_pixel(out, 50, 50,
			    "the focused window brought into view");
	}

	chord(KEY_MINUS);
	chord(KEY_MINUS);
	CHECK(strcmp(harness_get_setting(h, "org.gnome.desktop.a11y.magnifier",
					 "mag-factor"),
		     "1.0") == 0,
	      "zooming out twice left mag-factor at %s",
	      harness_get_setting(h, "org.gnome.desktop.a11y.magnifier",
				  "mag-factor"));
	zoom = 1;
	check_pixel(0, 300, 200, "at a factor of one");

	chord(KEY_EQUAL);
	chord(KEY_8);
	CHECK(strcmp(harness_get_setting(h,
					 "org.gnome.desktop.a11y.applications",
					 "screen-magnifier-enabled"),
		     "false") == 0,
	      "the toggle key did not turn the magnifier off");
	zoom = 1;
	check_pixel(0, 123, 45, "turned off again");
	check_pixel(1, 67, 89, "turned off again");
	move_global(px, py);
	window_extent(px, py, &left0, &up0);
	CHECK(abs(left0 - (int)(info->width / 2.0 * outs[0].scale)) <= 4,
	      "turned off, the window reaches %d px left of its middle", left0);

	harness_window_destroy(other);
	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
