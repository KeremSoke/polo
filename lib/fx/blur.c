/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "polo-fx.h"

static void
box_line(guint8 *line, int count, int step, int radius, guint32 *scratch)
{
	int window = 2 * radius + 1;

	for (int c = 0; c < 4; c++) {
		guint32 sum = 0;

		for (int i = -radius; i <= radius; i++)
			sum += line[CLAMP(i, 0, count - 1) * step + c];
		for (int i = 0; i < count; i++) {
			scratch[i] = (sum + window / 2) / window;
			sum += line[MIN(i + radius + 1, count - 1) * step + c];
			sum -= line[MAX(i - radius, 0) * step + c];
		}
		for (int i = 0; i < count; i++)
			line[i * step + c] = (guint8)scratch[i];
	}
}

void
polo_fx_blur(guint8 *pixels, int width, int height, int stride, int radius)
{
	guint32 *scratch;

	g_return_if_fail(pixels != NULL);
	if (radius <= 0 || width <= 0 || height <= 0)
		return;
	scratch = g_new(guint32, MAX(width, height));
	for (int pass = 0; pass < 3; pass++) {
		for (int y = 0; y < height; y++)
			box_line(pixels + (gsize)y * stride, width, 4, radius,
				 scratch);
		for (int x = 0; x < width; x++)
			box_line(pixels + (gsize)x * 4, height, stride, radius,
				 scratch);
	}
	g_free(scratch);
}

void
polo_fx_dim(guint8 *pixels, int width, int height, int stride, double amount)
{
	double keep = 1.0 - CLAMP(amount, 0.0, 1.0);

	g_return_if_fail(pixels != NULL);
	for (int y = 0; y < height; y++) {
		guint8 *row = pixels + (gsize)y * stride;

		for (int x = 0; x < width; x++) {
			guint8 *p = row + x * 4;

			for (int c = 0; c < 3; c++)
				p[c] = (guint8)(p[c] * keep + 0.5);
		}
	}
}

void
polo_fx_downscale(const guint8 *src, int width, int height, int stride,
		  int factor, guint8 *dst, int dst_stride)
{
	int dw = width / factor, dh = height / factor;
	int area = factor * factor;

	g_return_if_fail(src != NULL && dst != NULL && factor > 0);
	for (int y = 0; y < dh; y++) {
		guint8 *out = dst + (gsize)y * dst_stride;

		for (int x = 0; x < dw; x++) {
			guint32 sum[4] = { 0, 0, 0, 0 };

			for (int j = 0; j < factor; j++) {
				const guint8 *in = src +
					(gsize)(y * factor + j) * stride +
					(gsize)x * factor * 4;

				for (int i = 0; i < factor * 4; i++)
					sum[i % 4] += in[i];
			}
			for (int c = 0; c < 4; c++)
				out[x * 4 + c] =
					(guint8)((sum[c] + area / 2) / area);
		}
	}
}
