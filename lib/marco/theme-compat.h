/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What Marco's theme code takes from GTK, GDK and the rest of Marco: the
 * types with their values, without the libraries.
 */

#ifndef POLO_MARCO_THEME_COMPAT_H
#define POLO_MARCO_THEME_COMPAT_H

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <pango/pango.h>

#include "polo-marco.h"

#define _(String) g_dgettext("marco", String)
#define N_(String) (String)

/* Where Marco 1.28 is installed on the distributions Polo supports. */
#define MARCO_DATADIR "/usr/share"

#define POLO_DEBUG_THEMES 0
#define polo_marco_topic(topic, ...) g_debug(__VA_ARGS__)
#define polo_marco_is_debugging() FALSE
#define polo_marco_warning(...) g_warning(__VA_ARGS__)
#define polo_marco_bug(...) g_error(__VA_ARGS__)

/* GtkBorder */
typedef struct {
	gint16 left;
	gint16 right;
	gint16 top;
	gint16 bottom;
} PoloBorder;

/* GdkRGBA */
typedef struct {
	gdouble red;
	gdouble green;
	gdouble blue;
	gdouble alpha;
} PoloRGBA;

gboolean
polo_marco_rgba_parse(PoloRGBA *rgba, const char *spec);

/* GtkStateFlags */
typedef enum {
	POLO_STATE_FLAG_NORMAL = 0,
	POLO_STATE_FLAG_ACTIVE = 1 << 0,
	POLO_STATE_FLAG_PRELIGHT = 1 << 1,
	POLO_STATE_FLAG_SELECTED = 1 << 2,
	POLO_STATE_FLAG_INSENSITIVE = 1 << 3,
	POLO_STATE_FLAG_INCONSISTENT = 1 << 4,
	POLO_STATE_FLAG_FOCUSED = 1 << 5,
	POLO_STATE_FLAG_BACKDROP = 1 << 6,
} PoloStateFlags;

/* GtkShadowType */
typedef enum {
	POLO_SHADOW_NONE,
	POLO_SHADOW_IN,
	POLO_SHADOW_OUT,
	POLO_SHADOW_ETCHED_IN,
	POLO_SHADOW_ETCHED_OUT,
} PoloShadowType;

/* GtkArrowType */
typedef enum {
	POLO_ARROW_UP,
	POLO_ARROW_DOWN,
	POLO_ARROW_LEFT,
	POLO_ARROW_RIGHT,
	POLO_ARROW_NONE,
} PoloArrowType;

/* The GTK colours a theme can name, resolved by whoever draws (M11.3). */
typedef struct _PoloThemeColors PoloThemeColors;

/* Marco's common.h */
typedef enum {
	POLO_FRAME_ALLOWS_DELETE = 1 << 0,
	POLO_FRAME_ALLOWS_MENU = 1 << 1,
	POLO_FRAME_ALLOWS_APPMENU = 1 << 2,
	POLO_FRAME_ALLOWS_MINIMIZE = 1 << 3,
	POLO_FRAME_ALLOWS_MAXIMIZE = 1 << 4,
	POLO_FRAME_ALLOWS_VERTICAL_RESIZE = 1 << 5,
	POLO_FRAME_ALLOWS_HORIZONTAL_RESIZE = 1 << 6,
	POLO_FRAME_HAS_FOCUS = 1 << 7,
	POLO_FRAME_SHADED = 1 << 8,
	POLO_FRAME_STUCK = 1 << 9,
	POLO_FRAME_MAXIMIZED = 1 << 10,
	POLO_FRAME_ALLOWS_SHADE = 1 << 11,
	POLO_FRAME_ALLOWS_MOVE = 1 << 12,
	POLO_FRAME_FULLSCREEN = 1 << 13,
	POLO_FRAME_IS_FLASHING = 1 << 14,
	POLO_FRAME_ABOVE = 1 << 15,
	POLO_FRAME_TILED_LEFT = 1 << 16,
	POLO_FRAME_TILED_RIGHT = 1 << 17,
} PoloFrameFlags;

typedef enum {
	POLO_FRAME_TYPE_NORMAL,
	POLO_FRAME_TYPE_DIALOG,
	POLO_FRAME_TYPE_MODAL_DIALOG,
	POLO_FRAME_TYPE_UTILITY,
	POLO_FRAME_TYPE_MENU,
	POLO_FRAME_TYPE_BORDER,
	POLO_FRAME_TYPE_ATTACHED,
	POLO_FRAME_TYPE_LAST,
} PoloFrameType;

typedef struct {
	PoloBorder visible;
	PoloBorder shadow;
	PoloBorder resize;
	PoloBorder invisible;
	PoloBorder total;
} PoloThemeBorders;

void
polo_marco_theme_borders_clear(PoloThemeBorders *self);

/* Marco's gradient.h */
typedef enum {
	POLO_GRADIENT_VERTICAL,
	POLO_GRADIENT_HORIZONTAL,
	POLO_GRADIENT_DIAGONAL,
	POLO_GRADIENT_LAST,
} PoloGradientType;

#endif
