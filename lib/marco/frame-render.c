/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What Marco's frames.c does around meta_theme_draw_frame (): the title's
 * font and layout, drawn into memory Polo owns, one piece of a frame at a
 * time.
 */

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
