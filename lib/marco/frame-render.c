/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What Marco's frames.c does around meta_theme_draw_frame (): the title's
 * font and layout, drawn into memory Polo owns, one piece of a frame at a
 * time.
 */

#include <math.h>
#include <string.h>

#include <pango/pangocairo.h>

#include "theme-private.h"

/* meta_gtk_widget_get_font_desc (): the GTK font, the titlebar font over it
 * unless the system font is used, scaled by the theme. */
static PangoFontDescription *
title_font(PoloTheme *theme, PoloFrameType type, PoloFrameFlags flags,
	   const char *system_font, const char *titlebar_font)
{
	PangoFontDescription *font = pango_font_description_from_string(
		system_font && *system_font ? system_font : "Sans 10");
	double scale = polo_marco_theme_get_title_scale(theme, type, flags);
	int size;

	if (titlebar_font && *titlebar_font) {
		PangoFontDescription *override =
			pango_font_description_from_string(titlebar_font);

		pango_font_description_merge(font, override, TRUE);
		pango_font_description_free(override);
	}
	size = pango_font_description_get_size(font);
	pango_font_description_set_size(font, MAX((int)(scale * size), 1));
	return font;
}

static PangoContext *
title_context(double dpi)
{
	PangoContext *context = pango_font_map_create_context(
		pango_cairo_font_map_get_default());

	pango_cairo_context_set_resolution(context, dpi);
	return context;
}

/* meta_pango_font_desc_get_text_height () */
static int
text_height(PangoContext *context, const PangoFontDescription *font)
{
	PangoFontMetrics *metrics = pango_context_get_metrics(
		context, font, pango_context_get_language(context));
	int height = PANGO_PIXELS(pango_font_metrics_get_ascent(metrics) +
				  pango_font_metrics_get_descent(metrics));

	pango_font_metrics_unref(metrics);
	return height;
}

int
polo_marco_frame_text_height(PoloTheme *theme, PoloFrameType type,
			     PoloFrameFlags flags, const char *system_font,
			     const char *titlebar_font, double dpi)
{
	PangoFontDescription *font =
		title_font(theme, type, flags, system_font, titlebar_font);
	PangoContext *context = title_context(dpi);
	int height = text_height(context, font);

	g_object_unref(context);
	pango_font_description_free(font);
	return height;
}

/* Match frames.c get_visible_region(): how far a rounded corner cuts into a
 * row. */
static int
corner_inset(guint corner, int row)
{
	const float radius = sqrt(corner) + corner;

	return (int)floor(
		0.5 + radius -
		sqrt(radius * radius -
		     (radius - (row + 0.5)) * (radius - (row + 0.5))));
}

/* Clear the invisible borders and rounded corners Marco removes by shaping its
 * frame window in frames.c get_visible_region(). */
static void
clear_outside_visible_region(cairo_t *cr, const PoloFrameGeometry *fgeom,
			     PoloFrameFlags flags)
{
	int window_height = (flags & POLO_FRAME_SHADED)
		? fgeom->borders.total.top
		: fgeom->height;
	cairo_rectangle_int_t frame_rect = {
		fgeom->borders.invisible.left,
		fgeom->borders.invisible.top,
		fgeom->width - fgeom->borders.invisible.right -
			fgeom->borders.invisible.left,
		window_height - fgeom->borders.invisible.bottom -
			fgeom->borders.invisible.top,
	};
	cairo_rectangle_int_t all = { 0, 0, fgeom->width, window_height };
	cairo_region_t *hidden = cairo_region_create_rectangle(&all);
	cairo_region_t *visible = cairo_region_create_rectangle(&frame_rect);
	cairo_rectangle_int_t row;
	guint i;

	for (i = 0; i < fgeom->top_left_corner_rounded_radius; i++) {
		row = (cairo_rectangle_int_t){
			frame_rect.x, frame_rect.y + (int)i,
			corner_inset(fgeom->top_left_corner_rounded_radius, i),
			1
		};
		cairo_region_subtract_rectangle(visible, &row);
	}
	for (i = 0; i < fgeom->top_right_corner_rounded_radius; i++) {
		int width =
			corner_inset(fgeom->top_right_corner_rounded_radius, i);

		row = (cairo_rectangle_int_t){ frame_rect.x + frame_rect.width -
						       width,
					       frame_rect.y + (int)i, width,
					       1 };
		cairo_region_subtract_rectangle(visible, &row);
	}
	for (i = 0; i < fgeom->bottom_left_corner_rounded_radius; i++) {
		row = (cairo_rectangle_int_t){
			frame_rect.x,
			frame_rect.y + frame_rect.height - (int)i - 1,
			corner_inset(fgeom->bottom_left_corner_rounded_radius,
				     i),
			1
		};
		cairo_region_subtract_rectangle(visible, &row);
	}
	for (i = 0; i < fgeom->bottom_right_corner_rounded_radius; i++) {
		int width = corner_inset(
			fgeom->bottom_right_corner_rounded_radius, i);

		row = (cairo_rectangle_int_t){
			frame_rect.x + frame_rect.width - width,
			frame_rect.y + frame_rect.height - (int)i - 1, width, 1
		};
		cairo_region_subtract_rectangle(visible, &row);
	}
	cairo_region_subtract(hidden, visible);

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	for (i = 0; i < (guint)cairo_region_num_rectangles(hidden); i++) {
		cairo_rectangle_int_t r;

		cairo_region_get_rectangle(hidden, (int)i, &r);
		cairo_rectangle(cr, r.x, r.y, r.width, r.height);
	}
	cairo_fill(cr);
	cairo_restore(cr);
	cairo_region_destroy(visible);
	cairo_region_destroy(hidden);
}

void
polo_marco_frame_render(PoloTheme *theme, PoloThemeColors *colors,
			PoloFrameType type, PoloFrameFlags flags,
			int client_width, int client_height, const char *title,
			const char *system_font, const char *titlebar_font,
			double dpi, const PoloButtonLayout *button_layout,
			const PoloButtonState *button_states, int piece_x,
			int piece_y, guint8 *pixels, int width, int height,
			int stride)
{
	PangoFontDescription *font =
		title_font(theme, type, flags, system_font, titlebar_font);
	PangoContext *context = title_context(dpi);
	PangoLayout *layout = pango_layout_new(context);
	PoloButtonState states[POLO_BUTTON_TYPE_LAST];
	PoloFrameGeometry fgeom;
	cairo_surface_t *surface;
	cairo_t *cr;

	memcpy(states, button_states, sizeof(states));
	/* meta_frames_ensure_layout () */
	pango_layout_set_text(layout, title ? title : "", -1);
	pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
	pango_layout_set_auto_dir(layout, FALSE);
	pango_layout_set_single_paragraph_mode(layout, TRUE);
	pango_layout_set_font_description(layout, font);

	surface = cairo_image_surface_create_for_data(
		pixels, CAIRO_FORMAT_ARGB32, width, height, stride);
	cr = cairo_create(surface);
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_translate(cr, -piece_x, -piece_y);
	pango_cairo_update_layout(cr, layout);
	polo_marco_theme_draw_frame(theme, colors, cr, type, flags,
				    client_width, client_height, layout,
				    text_height(context, font), button_layout,
				    states, NULL, NULL);
	polo_marco_theme_calc_geometry(theme, type, text_height(context, font),
				       flags, client_width, client_height,
				       button_layout, &fgeom);
	clear_outside_visible_region(cr, &fgeom, flags);
	cairo_destroy(cr);
	cairo_surface_flush(surface);
	cairo_surface_destroy(surface);
	g_object_unref(layout);
	g_object_unref(context);
	pango_font_description_free(font);
}

void
polo_marco_frame_release_fonts(void)
{
	pango_cairo_font_map_set_default(NULL);
}
