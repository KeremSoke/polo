/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib.h>

#include "polo-fx.h"

#define SIZE 32

static guint8 *
filled(guint32 colour)
{
	guint32 *pixels = g_new(guint32, SIZE * SIZE);

	for (int i = 0; i < SIZE * SIZE; i++)
		pixels[i] = colour;
	return (guint8 *)pixels;
}

static guint32
pixel(const guint8 *pixels, int x, int y)
{
	return ((const guint32 *)pixels)[y * SIZE + x];
}

static void
test_flat_stays_flat(void)
{
	guint8 *pixels = filled(0xff336699);

	polo_fx_blur(pixels, SIZE, SIZE, SIZE * 4, 5);
	for (int y = 0; y < SIZE; y++)
		for (int x = 0; x < SIZE; x++)
			g_assert_cmphex(pixel(pixels, x, y), ==, 0xff336699);
	g_free(pixels);
}

static void
test_spreads_and_keeps_energy(void)
{
	guint8 *pixels = filled(0xff000000);
	guint32 *words = (guint32 *)pixels;
	guint64 before = 0, after = 0;

	for (int y = 12; y < 20; y++)
		for (int x = 12; x < 20; x++)
			words[y * SIZE + x] = 0xffffffff;
	for (int i = 0; i < SIZE * SIZE; i++)
		before += words[i] & 0xff;
	polo_fx_blur(pixels, SIZE, SIZE, SIZE * 4, 3);
	for (int i = 0; i < SIZE * SIZE; i++)
		after += words[i] & 0xff;
	g_assert_cmpuint(pixel(pixels, 15, 15) & 0xff, <, 0xff);
	g_assert_cmpuint(pixel(pixels, 15, 15) & 0xff, >, 0x80);
	g_assert_cmpuint(pixel(pixels, 8, 15) & 0xff, >, 0);
	g_assert_cmpuint(pixel(pixels, 0, 0) & 0xff, ==, 0);
	g_assert_cmpuint(pixel(pixels, 8, 15) >> 24, ==, 0xff);
	g_assert_cmpfloat(ABS((double)after - before) / before, <, 0.05);
	g_free(pixels);
}

static void
test_dim(void)
{
	guint8 *pixels = filled(0xff80c040);

	polo_fx_dim(pixels, SIZE, SIZE, SIZE * 4, 0.5);
	g_assert_cmphex(pixel(pixels, 3, 7), ==, 0xff406020);
	polo_fx_dim(pixels, SIZE, SIZE, SIZE * 4, 0.0);
	g_assert_cmphex(pixel(pixels, 3, 7), ==, 0xff406020);
	polo_fx_dim(pixels, SIZE, SIZE, SIZE * 4, 1.0);
	g_assert_cmphex(pixel(pixels, 3, 7), ==, 0xff000000);
	g_free(pixels);
}

static void
test_downscale(void)
{
	guint8 *pixels = filled(0xff000000);
	guint32 *words = (guint32 *)pixels;
	guint32 small[(SIZE / 4) * (SIZE / 4)];

	for (int y = 0; y < SIZE; y++)
		for (int x = 0; x < SIZE; x += 2)
			words[y * SIZE + x] = 0xffffffff;
	polo_fx_downscale(pixels, SIZE, SIZE, SIZE * 4, 4, (guint8 *)small,
			  SIZE);
	g_assert_cmphex(small[0], ==, 0xff808080);
	g_assert_cmphex(small[(SIZE / 4) * (SIZE / 4) - 1], ==, 0xff808080);
	g_free(pixels);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/blur/flat", test_flat_stays_flat);
	g_test_add_func("/blur/spread", test_spreads_and_keeps_energy);
	g_test_add_func("/blur/dim", test_dim);
	g_test_add_func("/blur/downscale", test_downscale);
	return g_test_run();
}
