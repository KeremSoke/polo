/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib.h>
#include <string.h>

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

static void
reference_line(guint8 *line, int count, int step, int radius)
{
	int window = 2 * radius + 1;
	guint8 *copy = g_new(guint8, (gsize)count * 4);

	for (int i = 0; i < count; i++)
		memcpy(copy + i * 4, line + (gsize)i * step, 4);
	for (int i = 0; i < count; i++)
		for (int c = 0; c < 4; c++) {
			guint32 sum = 0;

			for (int k = i - radius; k <= i + radius; k++)
				sum += copy[CLAMP(k, 0, count - 1) * 4 + c];
			line[(gsize)i * step + c] =
				(guint8)((sum + window / 2) / window);
		}
	g_free(copy);
}

static void
reference_blur(guint8 *pixels, int width, int height, int stride, int radius)
{
	for (int pass = 0; pass < 3; pass++) {
		for (int y = 0; y < height; y++)
			reference_line(pixels + (gsize)y * stride, width, 4,
				       radius);
		for (int x = 0; x < width; x++)
			reference_line(pixels + (gsize)x * 4, height, stride,
				       radius);
	}
}

static void
test_matches_reference(void)
{
	GRand *rand = g_rand_new_with_seed(7);

	for (int round = 0; round < 200; round++) {
		int width = g_rand_int_range(rand, 1, 40);
		int height = g_rand_int_range(rand, 1, 40);
		int stride = width * 4 + g_rand_int_range(rand, 0, 3) * 4;
		int radius = g_rand_int_range(rand, 1, 60);
		gsize size = (gsize)stride * height;
		guint8 *pixels = g_malloc(size), *expected = g_malloc(size);

		for (gsize i = 0; i < size; i++)
			pixels[i] = expected[i] = (guint8)g_rand_int(rand);
		polo_fx_blur(pixels, width, height, stride, radius);
		reference_blur(expected, width, height, stride, radius);
		g_assert_cmpmem(pixels, size, expected, size);
		g_free(pixels);
		g_free(expected);
	}
	g_rand_free(rand);
}

static void
test_downscale_matches_reference(void)
{
	GRand *rand = g_rand_new_with_seed(11);

	for (int round = 0; round < 200; round++) {
		int factor = g_rand_int_range(rand, 1, 6);
		int width = g_rand_int_range(rand, factor, 50);
		int height = g_rand_int_range(rand, factor, 50);
		int stride = width * 4 + g_rand_int_range(rand, 0, 3) * 4;
		int small_width = width / factor,
		    small_height = height / factor;
		int small_stride = small_width * 4 + 4;
		gsize small_size = (gsize)small_stride * small_height;
		guint8 *source = g_malloc((gsize)stride * height);
		guint8 *small = g_malloc0(small_size);
		guint8 *expected = g_malloc0(small_size);

		for (gsize i = 0; i < (gsize)stride * height; i++)
			source[i] = (guint8)g_rand_int(rand);
		for (int y = 0; y < small_height; y++)
			for (int x = 0; x < small_width; x++)
				for (int c = 0; c < 4; c++) {
					guint32 sum = 0, area = factor * factor;

					for (int j = 0; j < factor; j++)
						for (int i = 0; i < factor; i++)
							sum += source
								[(gsize)(y * factor +
									 j) *
									 stride +
								 (x * factor +
								  i) * 4 +
								 c];
					expected[(gsize)y * small_stride +
						 x * 4 + c] =
						(guint8)((sum + area / 2) /
							 area);
				}
		polo_fx_downscale(source, width, height, stride, factor, small,
				  small_stride);
		g_assert_cmpmem(small, small_size, expected, small_size);
		g_free(source);
		g_free(small);
		g_free(expected);
	}
	g_rand_free(rand);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/blur/flat", test_flat_stays_flat);
	g_test_add_func("/blur/spread", test_spreads_and_keeps_energy);
	g_test_add_func("/blur/dim", test_dim);
	g_test_add_func("/blur/downscale", test_downscale);
	g_test_add_func("/blur/reference", test_matches_reference);
	g_test_add_func("/blur/downscale-reference",
			test_downscale_matches_reference);
	return g_test_run();
}
