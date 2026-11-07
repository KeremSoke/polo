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
	polo_marco_label_size_padded(text, font, LABEL_PADDING, width, height);
}

void
polo_marco_label_size_padded(const char *text, const char *font, int padding,
			     int *width, int *height)
{
	cairo_surface_t *surface =
		cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t *cr = cairo_create(surface);
	PangoLayout *layout = label_layout(cr, text, font);
	int w, h;

	pango_layout_get_pixel_size(layout, &w, &h);
	*width = w + 2 * padding;
	*height = h + 2 * padding;
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

void
polo_marco_label_render_osd(const char *text, const char *font, double radius,
			    double opacity, int width, int height,
			    guint8 *pixels, int stride)
{
	cairo_surface_t *surface = cairo_image_surface_create_for_data(
		pixels, CAIRO_FORMAT_ARGB32, width, height, stride);
	cairo_t *cr = cairo_create(surface);
	PangoLayout *layout = label_layout(cr, text, font);
	double x = 0.5, y = 0.5, w = width - 1.0, h = height - 1.0;
	double r = CLAMP(radius, 0.0, MIN(w, h) / 2);
	int tw, th;

	opacity = CLAMP(opacity, 0.0, 1.0);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
	cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 38 / 255.0, 38 / 255.0, 38 / 255.0, opacity);
	cairo_fill_preserve(cr);
	cairo_set_line_width(cr, 1);
	cairo_set_source_rgba(cr, 238 / 255.0, 238 / 255.0, 236 / 255.0,
			      opacity / 2);
	cairo_stroke(cr);
	pango_layout_get_pixel_size(layout, &tw, &th);
	cairo_move_to(cr, (width - tw) / 2.0, (height - th) / 2.0);
	cairo_set_source_rgba(cr, 238 / 255.0, 238 / 255.0, 236 / 255.0, 1);
	pango_cairo_show_layout(cr, layout);
	cairo_surface_flush(surface);
	g_object_unref(layout);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
}
