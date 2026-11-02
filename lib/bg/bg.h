/*
 * Copyright (C) 2000 Eazel, Inc.
 * Copyright (C) 2007-2008 Red Hat, Inc.
 * Copyright (C) 2012 Jasmine Hassan <jasmine.aura@gmail.com>
 * Copyright (C) 2012-2021 MATE Developers
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * Desktop background drawing, ported from mate-desktop 1.28.2
 * libmate-desktop/mate-bg.c: colours and gradients, the six
 * picture placements, and XML slideshows. Unlike MateBG, which draws the
 * whole X screen into one GdkPixbuf, Polo draws one output at a time into
 * that output's buffer; root-window semantics (per-monitor colours and
 * images, screen-wide tiling and spanning) are kept.
 */

#ifndef POLO_BG_H
#define POLO_BG_H

#include <glib.h>

G_BEGIN_DECLS

/* org.mate.background color-shading-type */
typedef enum {
	POLO_BG_COLOR_SOLID,
	POLO_BG_COLOR_H_GRADIENT,
	POLO_BG_COLOR_V_GRADIENT
} PoloBgColorType;

/* org.mate.background picture-options (MateBGPlacement) */
typedef enum {
	POLO_BG_PLACEMENT_TILED,
	POLO_BG_PLACEMENT_ZOOMED,
	POLO_BG_PLACEMENT_CENTERED,
	POLO_BG_PLACEMENT_SCALED,
	POLO_BG_PLACEMENT_FILL_SCREEN,
	POLO_BG_PLACEMENT_SPANNED
} PoloBgPlacement;

typedef struct _PoloBg PoloBg;

/* A background description; immutable once drawing starts, safe to draw
 * from one worker thread at a time. Reference counted. */
PoloBg *
polo_bg_new(void);

PoloBg *
polo_bg_ref(PoloBg *bg);

void
polo_bg_unref(PoloBg *bg);

/* Colours as org.mate.background stores them ("#rrggbb", "#rrrrggggbbbb",
 * X11 names); unparsable strings mean black, as in MateBG. */
void
polo_bg_set_color(PoloBg *bg, PoloBgColorType type, const char *primary,
		  const char *secondary);

void
polo_bg_set_placement(PoloBg *bg, PoloBgPlacement placement);

/* An image, an XML slideshow, or NULL for colours only. */
void
polo_bg_set_filename(PoloBg *bg, const char *filename);

/*
 * Draw the part of the background that covers one output into data: an
 * ARGB32 premultiplied buffer of width x height pixels (the output's
 * logical size times scale) with the given stride. output_x/y and
 * screen_width/height are logical; screen is the bounding box of all
 * outputs, used for tiling and spanning. Blocking: decodes the image.
 */
void
polo_bg_draw_output(PoloBg *bg, guint8 *data, int width, int height,
		    int stride, int scale, int output_x, int output_y,
		    int screen_width, int screen_height);

/* Seconds until a slideshow needs redrawing, G_MAXUINT if never. Valid
 * after a draw. */
guint
polo_bg_get_timeout(PoloBg *bg);

G_END_DECLS

#endif
