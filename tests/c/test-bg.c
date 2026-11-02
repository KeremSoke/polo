/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Exercises MateBG-compatible background drawing with generated images:
 * colours, gradients, placement, tiling and slideshows.
 */

#include <math.h>
#include <string.h>
#include <time.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "bg.h"

static char *tmpdir;

typedef struct {
	guint32 *pixels;
	int width, height;
} Frame;

static Frame
draw(PoloBg *bg, int width, int height, int scale, int ox, int oy, int sw,
     int sh)
{
	Frame f = { g_new0(guint32, width * height), width, height };

	polo_bg_draw_output(bg, (guint8 *)f.pixels, width, height, width * 4,
			    scale, ox, oy, sw, sh);
	return f;
}

static guint32
at(Frame f, int x, int y)
{
	g_assert_cmpint(x, >=, 0);
	g_assert_cmpint(x, <, f.width);
	g_assert_cmpint(y, >=, 0);
	g_assert_cmpint(y, <, f.height);
	return f.pixels[y * f.width + x];
}

static guint32
rgb(int r, int g, int b)
{
	return 0xff000000u | (r << 16) | (g << 8) | b;
}

/* Match MateBG's gradient interpolation. */
static int
gradient(int p, int s, int i, int n)
{
	double ratio = (i + 0.5) / n;

	return (int)((p / 255.0 * (1 - ratio) + s / 255.0 * ratio) * 0x100);
}

static char *
make_image(const char *name, int w, int h)
{
	GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, w, h);
	char *path = g_build_filename(tmpdir, name, NULL);
	int rowstride = gdk_pixbuf_get_rowstride(pb);
	guchar *px = gdk_pixbuf_get_pixels(pb);

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			guchar *p = px + y * rowstride + x * 3;

			p[0] = x < w / 2 ? 0 : 255;
			p[1] = 0;
			p[2] = x < w / 2 ? 255 : 0;
		}
	}
	g_assert_true(gdk_pixbuf_save(pb, path, "png", NULL, NULL));
	g_object_unref(pb);
	return path;
}

static char *
make_solid_image(const char *name, int r, int g, int b)
{
	GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 8, 8);
	char *path = g_build_filename(tmpdir, name, NULL);

	gdk_pixbuf_fill(pb, ((guint32)r << 24) | (g << 16) | (b << 8) | 0xff);
	g_assert_true(gdk_pixbuf_save(pb, path, "png", NULL, NULL));
	g_object_unref(pb);
	return path;
}

#define BLUE rgb(0, 0, 255)
#define RED rgb(255, 0, 0)

static void
test_solid(void)
{
	PoloBg *bg = polo_bg_new();
	Frame f;

	polo_bg_set_color(bg, POLO_BG_COLOR_SOLID, "#5891BC", "#3C8F25");
	f = draw(bg, 8, 4, 1, 0, 0, 8, 4);
	g_assert_cmphex(at(f, 0, 0), ==, rgb(0x58, 0x91, 0xbc));
	g_assert_cmphex(at(f, 7, 3), ==, rgb(0x58, 0x91, 0xbc));
	g_free(f.pixels);

	/* MATE stores colours in X11's 16-bit form; bad input becomes black. */
	polo_bg_set_color(bg, POLO_BG_COLOR_SOLID, "#ffff00000000", NULL);
	f = draw(bg, 2, 2, 1, 0, 0, 2, 2);
	g_assert_cmphex(at(f, 1, 1), ==, rgb(255, 0, 0));
	g_free(f.pixels);
	polo_bg_set_color(bg, POLO_BG_COLOR_SOLID, "not a colour", NULL);
	f = draw(bg, 2, 2, 1, 0, 0, 2, 2);
	g_assert_cmphex(at(f, 0, 0), ==, rgb(0, 0, 0));
	g_free(f.pixels);
	polo_bg_unref(bg);
}

static void
test_gradients(void)
{
	PoloBg *bg = polo_bg_new();
	Frame f, a, b, whole;

	polo_bg_set_color(bg, POLO_BG_COLOR_V_GRADIENT, "#000000", "#ffffff");
	f = draw(bg, 4, 10, 1, 0, 0, 4, 10);
	for (int y = 0; y < 10; y++) {
		int v = gradient(0, 255, y, 10);

		g_assert_cmphex(at(f, 2, y), ==, rgb(v, v, v));
	}
	g_free(f.pixels);

	/* Not spanned: each output has its own gradient (MateBG
	 * draw_color_each_monitor). */
	polo_bg_set_color(bg, POLO_BG_COLOR_H_GRADIENT, "#000000", "#ffffff");
	a = draw(bg, 10, 2, 1, 0, 0, 20, 2);
	b = draw(bg, 10, 2, 1, 10, 0, 20, 2);
	g_assert_cmphex(at(a, 0, 0), ==, at(b, 0, 0));
	g_assert_cmphex(at(a, 9, 1), ==, at(b, 9, 1));
	g_free(a.pixels);
	g_free(b.pixels);

	/* Spanned: one gradient over the whole screen. */
	polo_bg_set_placement(bg, POLO_BG_PLACEMENT_SPANNED);
	whole = draw(bg, 20, 2, 1, 0, 0, 20, 2);
	b = draw(bg, 10, 2, 1, 10, 0, 20, 2);
	for (int x = 0; x < 10; x++)
		g_assert_cmphex(at(b, x, 0), ==, at(whole, x + 10, 0));
	g_free(whole.pixels);
	g_free(b.pixels);
	polo_bg_unref(bg);
}

static PoloBg *
image_bg(const char *path, PoloBgPlacement placement)
{
	PoloBg *bg = polo_bg_new();

	polo_bg_set_color(bg, POLO_BG_COLOR_SOLID, "#00ff00", "#00ff00");
	polo_bg_set_placement(bg, placement);
	polo_bg_set_filename(bg, path);
	return bg;
}

#define GREEN rgb(0, 255, 0)

static void
test_placements(void)
{
	g_autofree char *path = make_image("half.png", 40, 20);
	PoloBg *bg;
	Frame f;

	/* zoom: scaled 4× to 160 × 80 and cropped to the middle 80 × 80. */
	bg = image_bg(path, POLO_BG_PLACEMENT_ZOOMED);
	f = draw(bg, 80, 80, 1, 0, 0, 80, 80);
	g_assert_cmphex(at(f, 2, 40), ==, BLUE);
	g_assert_cmphex(at(f, 77, 40), ==, RED);
	g_assert_cmphex(at(f, 40, 0), !=, GREEN);
	g_free(f.pixels);
	polo_bg_unref(bg);

	/* scaled: 2× to 80 × 40, letterboxed at y = 20. */
	bg = image_bg(path, POLO_BG_PLACEMENT_SCALED);
	f = draw(bg, 80, 80, 1, 0, 0, 80, 80);
	g_assert_cmphex(at(f, 40, 10), ==, GREEN);
	g_assert_cmphex(at(f, 10, 40), ==, BLUE);
	g_assert_cmphex(at(f, 70, 40), ==, RED);
	g_assert_cmphex(at(f, 40, 70), ==, GREEN);
	g_free(f.pixels);
	polo_bg_unref(bg);

	/* centered: unscaled at (20, 30). */
	bg = image_bg(path, POLO_BG_PLACEMENT_CENTERED);
	f = draw(bg, 80, 80, 1, 0, 0, 80, 80);
	g_assert_cmphex(at(f, 10, 40), ==, GREEN);
	g_assert_cmphex(at(f, 25, 40), ==, BLUE);
	g_assert_cmphex(at(f, 55, 40), ==, RED);
	g_assert_cmphex(at(f, 40, 25), ==, GREEN);
	g_free(f.pixels);
	polo_bg_unref(bg);

	/* stretched (fill screen): the whole output. */
	bg = image_bg(path, POLO_BG_PLACEMENT_FILL_SCREEN);
	f = draw(bg, 80, 80, 1, 0, 0, 80, 80);
	g_assert_cmphex(at(f, 2, 2), ==, BLUE);
	g_assert_cmphex(at(f, 77, 77), ==, RED);
	g_free(f.pixels);
	polo_bg_unref(bg);

	/* wallpaper (tiled): tiles start at the screen's origin, so the
	 * second output continues the first one's pattern. */
	bg = image_bg(path, POLO_BG_PLACEMENT_TILED);
	f = draw(bg, 50, 20, 1, 50, 0, 100, 20);
	g_assert_cmphex(at(f, 5, 5), ==, BLUE);  /* screen x 55 → 15 */
	g_assert_cmphex(at(f, 15, 5), ==, RED);  /* screen x 65 → 25 */
	g_assert_cmphex(at(f, 35, 5), ==, BLUE); /* screen x 85 → 5 */
	g_free(f.pixels);
	polo_bg_unref(bg);

	/* spanned: fitted to the whole 80 × 40 screen once. */
	bg = image_bg(path, POLO_BG_PLACEMENT_SPANNED);
	f = draw(bg, 40, 40, 1, 0, 0, 80, 40);
	g_assert_cmphex(at(f, 5, 20), ==, BLUE);
	g_assert_cmphex(at(f, 35, 20), ==, BLUE);
	g_free(f.pixels);
	f = draw(bg, 40, 40, 1, 40, 0, 80, 40);
	g_assert_cmphex(at(f, 5, 20), ==, RED);
	g_free(f.pixels);
	polo_bg_unref(bg);

	/* Scale 2: the same picture at twice the pixels. */
	bg = image_bg(path, POLO_BG_PLACEMENT_SCALED);
	f = draw(bg, 160, 160, 2, 0, 0, 80, 80);
	g_assert_cmphex(at(f, 80, 20), ==, GREEN);
	g_assert_cmphex(at(f, 20, 80), ==, BLUE);
	g_free(f.pixels);
	polo_bg_unref(bg);

	/* A missing file: colours only. */
	bg = image_bg("/nonexistent/picture.png", POLO_BG_PLACEMENT_ZOOMED);
	f = draw(bg, 8, 8, 1, 0, 0, 8, 8);
	g_assert_cmphex(at(f, 4, 4), ==, GREEN);
	g_assert_cmpuint(polo_bg_get_timeout(bg), ==, G_MAXUINT);
	g_free(f.pixels);
	polo_bg_unref(bg);
}

/* Durations divide 3600, so MateBG's parser and DST offsets cancel out over
 * complete cycles. */
static char *
make_slideshow(const char *name, time_t start, const char *a, const char *b)
{
	char *path = g_build_filename(tmpdir, name, NULL);
	struct tm tm;
	char *xml;

	localtime_r(&start, &tm);
	xml = g_strdup_printf(
		"<background><starttime><year>%d</year><month>%d</month>"
		"<day>%d</day><hour>%d</hour><minute>%d</minute>"
		"<second>%d</second></starttime>"
		"<static><duration>10.0</duration><file>%s</file></static>"
		"<transition type=\"overlay\"><duration>10.0</duration>"
		"<from>%s</from><to>%s</to></transition></background>",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
		tm.tm_min, tm.tm_sec, a, a, b);
	g_assert_true(g_file_set_contents(path, xml, -1, NULL));
	g_free(xml);
	return path;
}

static void
test_slideshow(void)
{
	g_autofree char *red = make_solid_image("red.png", 255, 0, 0);
	g_autofree char *blue = make_solid_image("blue.png", 0, 0, 255);
	time_t now = time(NULL);
	PoloBg *bg;
	Frame f;
	guint32 p;

	/* 5 s into the static slide: the first file, redrawn when it ends
	 * (MateBG times a static slide by its duration). */
	{
		g_autofree char *path =
			make_slideshow("static.xml", now - 5, red, blue);

		bg = image_bg(path, POLO_BG_PLACEMENT_FILL_SCREEN);
		f = draw(bg, 4, 4, 1, 0, 0, 4, 4);
		g_assert_cmphex(at(f, 1, 1), ==, RED);
		g_assert_cmpuint(polo_bg_get_timeout(bg), ==, 10);
		g_free(f.pixels);
		polo_bg_unref(bg);
	}

	/* 15 s in: halfway through the transition, a blend; 64 steps. */
	{
		g_autofree char *path =
			make_slideshow("transition.xml", now - 15, red, blue);

		bg = image_bg(path, POLO_BG_PLACEMENT_FILL_SCREEN);
		f = draw(bg, 4, 4, 1, 0, 0, 4, 4);
		p = at(f, 1, 1);
		g_assert_cmpint((p >> 16) & 0xff, >, 60); /* red fading */
		g_assert_cmpint((p >> 16) & 0xff, <, 195);
		g_assert_cmpint(p & 0xff, >, 60); /* blue appearing */
		g_assert_cmpint(p & 0xff, <, 195);
		g_assert_cmpuint(polo_bg_get_timeout(bg), ==, 0); /* 10/64 s */
		g_free(f.pixels);
		polo_bg_unref(bg);
	}

	{
		g_autofree char *path =
			g_build_filename(tmpdir, "junk.xml", NULL);

		g_assert_true(g_file_set_contents(path, "junk", -1, NULL));
		bg = image_bg(path, POLO_BG_PLACEMENT_FILL_SCREEN);
		f = draw(bg, 2, 2, 1, 0, 0, 2, 2);
		g_assert_cmphex(at(f, 0, 0), ==, GREEN);
		g_free(f.pixels);
		polo_bg_unref(bg);
	}
}

int
main(int argc, char **argv)
{
	int status;

	g_test_init(&argc, &argv, NULL);
	tmpdir = g_dir_make_tmp("polo-bg-XXXXXX", NULL);
	g_assert_nonnull(tmpdir);

	g_test_add_func("/bg/solid", test_solid);
	g_test_add_func("/bg/gradients", test_gradients);
	g_test_add_func("/bg/placements", test_placements);
	g_test_add_func("/bg/slideshow", test_slideshow);
	status = g_test_run();

	{
		GDir *dir = g_dir_open(tmpdir, 0, NULL);
		const char *name;

		while (dir && (name = g_dir_read_name(dir))) {
			g_autofree char *path =
				g_build_filename(tmpdir, name, NULL);

			g_unlink(path);
		}
		if (dir)
			g_dir_close(dir);
		g_rmdir(tmpdir);
		g_free(tmpdir);
	}
	return status;
}
