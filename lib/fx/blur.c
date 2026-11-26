/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>

#include "polo-fx.h"

static void
box_row(guint8 *line, int count, int radius, guint32 *scratch)
{
	int window = 2 * radius + 1;
	guint32 sum[4] = { 0, 0, 0, 0 };

	for (int i = -radius; i <= radius; i++)
		for (int c = 0; c < 4; c++)
			sum[c] += line[CLAMP(i, 0, count - 1) * 4 + c];
	for (int i = 0; i < count; i++) {
		const guint8 *in = line + MIN(i + radius + 1, count - 1) * 4;
		const guint8 *out = line + MAX(i - radius, 0) * 4;

		for (int c = 0; c < 4; c++) {
			scratch[i * 4 + c] = (sum[c] + window / 2) / window;
			sum[c] += in[c];
			sum[c] -= out[c];
		}
	}
	for (int i = 0; i < count * 4; i++)
		line[i] = (guint8)scratch[i];
}

static void
box_columns(guint8 *pixels, int width, int height, int stride, int radius,
	    guint8 *copy, guint32 *sums)
{
	int window = 2 * radius + 1, bytes = width * 4;

	for (int y = 0; y < height; y++)
		memcpy(copy + (gsize)y * bytes, pixels + (gsize)y * stride,
		       bytes);
	memset(sums, 0, (gsize)bytes * sizeof *sums);
	for (int i = -radius; i <= radius; i++) {
		const guint8 *row =
			copy + (gsize)CLAMP(i, 0, height - 1) * bytes;

		for (int x = 0; x < bytes; x++)
			sums[x] += row[x];
	}
	for (int y = 0; y < height; y++) {
		guint8 *out = pixels + (gsize)y * stride;
		const guint8 *in =
			copy + (gsize)MIN(y + radius + 1, height - 1) * bytes;
		const guint8 *old = copy + (gsize)MAX(y - radius, 0) * bytes;

		for (int x = 0; x < bytes; x++) {
			out[x] = (guint8)((sums[x] + window / 2) / window);
			sums[x] += in[x];
			sums[x] -= old[x];
		}
	}
}

void
polo_fx_blur(guint8 *pixels, int width, int height, int stride, int radius)
{
	guint32 *scratch, *sums;
	guint8 *copy;

	g_return_if_fail(pixels != NULL);
	if (radius <= 0 || width <= 0 || height <= 0)
		return;
	scratch = g_new(guint32, (gsize)width * 4);
	sums = g_new(guint32, (gsize)width * 4);
	copy = g_malloc((gsize)width * 4 * height);
	for (int pass = 0; pass < 3; pass++) {
		for (int y = 0; y < height; y++)
			box_row(pixels + (gsize)y * stride, width, radius,
				scratch);
		box_columns(pixels, width, height, stride, radius, copy, sums);
	}
	g_free(copy);
	g_free(sums);
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

				for (int i = 0; i < factor; i++, in += 4) {
					sum[0] += in[0];
					sum[1] += in[1];
					sum[2] += in[2];
					sum[3] += in[3];
				}
			}
			for (int c = 0; c < 4; c++)
				out[x * 4 + c] =
					(guint8)((sum[c] + area / 2) / area);
		}
	}
}
