/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Shared declarations for theme.c and theme-draw.c, which Marco keeps in one
 * file.
 */

#ifndef POLO_MARCO_THEME_PRIVATE_H
#define POLO_MARCO_THEME_PRIVATE_H

#include "theme-parser.h"

#define GDK_COLOR_RGBA(color)                                                  \
	((guint32)(0xff | ((guint32)((color).red * 255) << 24) |               \
		   ((guint32)((color).green * 255) << 16) |                    \
		   ((guint32)((color).blue * 255) << 8)))

#define GDK_COLOR_RGB(color)                                                   \
	((guint32)(((guint32)((color).red * 255) << 16) |                      \
		   ((guint32)((color).green * 255) << 8) |                     \
		   ((int)((color).blue * 255))))

#define CLAMP_UCHAR(v) ((guchar)(CLAMP(((int)v), (int)0, (int)255)))
#define INTENSITY(r, g, b) ((r) * 0.30 + (g) * 0.59 + (b) * 0.11)
#define LIGHTNESS_MULT 1.3
#define DARKNESS_MULT 0.7

PoloFrameStyle *
theme_get_style(PoloTheme *theme, PoloFrameType type, PoloFrameFlags flags);

PoloDrawOpList *
get_button(PoloFrameStyle *style, PoloButtonType type, PoloButtonState state);

cairo_surface_t *
polo_marco_surface_from_pixbuf(const GdkPixbuf *pixbuf);

void
polo_marco_cairo_set_source_pixbuf(cairo_t *cr, const GdkPixbuf *pixbuf,
				   double x, double y);

gboolean
polo_marco_cairo_has_clip(cairo_t *cr);

void
polo_marco_render_arrow(const PoloThemeColors *colors, PoloStateFlags state,
			cairo_t *cr, double angle, double x, double y,
			double size);

void
polo_marco_render_box(const PoloThemeColors *colors, PoloStateFlags state,
		      cairo_t *cr, double x, double y, double width,
		      double height);

void
polo_marco_render_vline(const PoloThemeColors *colors, PoloStateFlags state,
			cairo_t *cr, double x, double y1, double y2);

void
polo_marco_gradient_add_alpha(GdkPixbuf *pixbuf, const guchar *alphas,
			      int n_alphas, PoloGradientType type);

#endif
