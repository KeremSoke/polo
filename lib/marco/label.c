/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cairo.h>
#include <pango/pangocairo.h>

#include "polo-marco.h"

#define LABEL_PADDING 24

static PangoLayout *
label_layout(cairo_t *cr, const char *text, const char *font)
{
	PangoLayout *layout = pango_cairo_create_layout(cr);
	PangoFontDescription *description =
		pango_font_description_from_string(font ? font : "Sans 14");

	pango_layout_set_font_description(layout, description);
	pango_font_description_free(description);
	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	pango_layout_set_text(layout, text, -1);
	return layout;
}

void
polo_marco_label_size(const char *text, const char *font, int *width,
		      int *height)
{
	cairo_surface_t *surface =
		cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t *cr = cairo_create(surface);
	PangoLayout *layout = label_layout(cr, text, font);
	int w, h;

	pango_layout_get_pixel_size(layout, &w, &h);
	*width = w + 2 * LABEL_PADDING;
	*height = h + 2 * LABEL_PADDING;
	g_object_unref(layout);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
}

void
polo_marco_label_render(const char *text, const char *font, int width,
			int height, guint8 *pixels, int stride)
{
	cairo_surface_t *surface = cairo_image_surface_create_for_data(
		pixels, CAIRO_FORMAT_ARGB32, width, height, stride);
	cairo_t *cr = cairo_create(surface);
	PangoLayout *layout = label_layout(cr, text, font);
	int w, h;

	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	pango_layout_get_pixel_size(layout, &w, &h);
	cairo_move_to(cr, (width - w) / 2.0, (height - h) / 2.0);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	pango_cairo_show_layout(cr, layout);
	cairo_surface_flush(surface);
	g_object_unref(layout);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
}
