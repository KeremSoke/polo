/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Window icons for frames, from Marco's iconcache.c: the best size of a
 * _NET_WM_ICON, and pixels squared and scaled as scaled_from_pixdata () does.
 */

#include <string.h>

#include "theme-private.h"

/* find_largest_sizes () */
static gboolean
find_largest_sizes(const guint32 *data, gsize nitems, int *width, int *height)
{
	*width = 0;
	*height = 0;

	while (nitems > 0) {
		guint32 w, h;

		if (nitems < 3)
			return FALSE;

		w = data[0];
		h = data[1];

		if (w == 0 || h == 0 || w > 1024 || h > 1024 ||
		    nitems < (gsize)w * h + 2)
			return FALSE;

		*width = MAX((int)w, *width);
		*height = MAX((int)h, *height);

		data += (gsize)w * h + 2;
		nitems -= (gsize)w * h + 2;
	}

	return TRUE;
}

/* find_best_size () */
static gboolean
find_best_size(const guint32 *data, gsize nitems, int ideal_width,
	       int ideal_height, int *width, int *height, const guint32 **start)
{
	int best_w = 0;
	int best_h = 0;
	const guint32 *best_start = NULL;
	int max_width, max_height;

	*width = 0;
	*height = 0;
	*start = NULL;

	if (!find_largest_sizes(data, nitems, &max_width, &max_height))
		return FALSE;

	if (ideal_width < 0)
		ideal_width = max_width;
	if (ideal_height < 0)
		ideal_height = max_height;

	while (nitems > 0) {
		int w, h;
		gboolean replace = FALSE;

		if (nitems < 3)
			return FALSE;

		w = data[0];
		h = data[1];

		if (nitems < (gsize)w * h + 2)
			break;

		if (best_start == NULL) {
			replace = TRUE;
		} else {
			const int ideal_size = (ideal_width + ideal_height) / 2;
			int best_size = (best_w + best_h) / 2;
			int this_size = (w + h) / 2;

			if (best_size < ideal_size && this_size >= ideal_size)
				replace = TRUE;
			else if (best_size < ideal_size &&
				 this_size > best_size)
				replace = TRUE;
			else if (best_size > ideal_size &&
				 this_size >= ideal_size &&
				 this_size < best_size)
				replace = TRUE;
		}

		if (replace) {
			best_start = data + 2;
			best_w = w;
			best_h = h;
		}

		data += (gsize)w * h + 2;
		nitems -= (gsize)w * h + 2;
	}

	if (!best_start)
		return FALSE;
	*start = best_start;
	*width = best_w;
	*height = best_h;
	return TRUE;
}

static void
free_pixels(guchar *pixels, gpointer data)
{
	g_free(pixels);
}

/* scaled_from_pixdata (): centred on a square, then scaled to new_w x new_h. */
static GdkPixbuf *
scaled_from_pixdata(guchar *pixdata, int w, int h, int new_w, int new_h)
{
	GdkPixbuf *src;
	GdkPixbuf *dest;

	src = gdk_pixbuf_new_from_data(pixdata, GDK_COLORSPACE_RGB, TRUE, 8, w,
				       h, w * 4, free_pixels, NULL);
	if (src == NULL)
		return NULL;

	if (w != h) {
		GdkPixbuf *tmp;
		int size = MAX(w, h);

		tmp = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, size, size);
		if (tmp) {
			gdk_pixbuf_fill(tmp, 0);
			gdk_pixbuf_copy_area(src, 0, 0, w, h, tmp,
					     (size - w) / 2, (size - h) / 2);
			g_object_unref(src);
			src = tmp;
		}
	}

	if (gdk_pixbuf_get_width(src) != new_w ||
	    gdk_pixbuf_get_height(src) != new_h) {
		dest = gdk_pixbuf_scale_simple(src, new_w, new_h,
					       GDK_INTERP_BILINEAR);
		g_object_unref(src);
	} else {
		dest = src;
	}

	return dest;
}

GdkPixbuf *
polo_marco_icon_new_from_net_wm_icon(const guint32 *data, gsize n_items,
				     int size)
{
	const guint32 *best;
	guchar *pixdata, *p;
	int w, h;

	if (!data || size <= 0 ||
	    !find_best_size(data, n_items, size, size, &w, &h, &best))
		return NULL;

	/* argbdata_to_pixdata () */
	pixdata = g_new(guchar, (gsize)w * h * 4);
	p = pixdata;
	for (gsize i = 0; i < (gsize)w * h; i++) {
		guint32 argb = best[i];

		*p++ = (argb >> 16) & 0xff;
		*p++ = (argb >> 8) & 0xff;
		*p++ = argb & 0xff;
		*p++ = argb >> 24;
	}
	return scaled_from_pixdata(pixdata, w, h, size, size);
}

GdkPixbuf *
polo_marco_icon_new_from_argb32(const guint8 *pixels, int width, int height,
				int stride, int size)
{
	guchar *pixdata, *p;

	if (!pixels || width <= 0 || height <= 0 || stride < width * 4)
		return NULL;

	pixdata = g_new(guchar, (gsize)width * height * 4);
	p = pixdata;
	for (int y = 0; y < height; y++) {
		const guint32 *row =
			(const guint32 *)(pixels + (gsize)y * stride);

		for (int x = 0; x < width; x++) {
			guint32 argb = row[x];
			guint alpha = argb >> 24;

			for (int shift = 16; shift >= 0; shift -= 8) {
				guint c = (argb >> shift) & 0xff;

				*p++ = alpha
					? MIN(255,
					      (c * 255 + alpha / 2) / alpha)
					: 0;
			}
			*p++ = alpha;
		}
	}
	if (size <= 0)
		size = MAX(width, height);
	return scaled_from_pixdata(pixdata, width, height, size, size);
}
