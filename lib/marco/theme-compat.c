/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <string.h>

#include "theme-compat.h"

/* gdkrgba.c: parse_rgb_value () */
static gboolean
parse_rgb_value(const char *str, char **endp, double *number)
{
	const char *p;

	*number = g_ascii_strtod(str, endp);
	if (*endp == str)
		return FALSE;
	p = *endp;
	while (g_ascii_isspace(*p))
		p++;
	if (*p == '%') {
		*endp = (char *)(p + 1);
		*number = CLAMP(*number / 100., 0., 1.);
	} else {
		*number = CLAMP(*number / 255., 0., 1.);
	}
	return TRUE;
}

static void
skip_space(const char **p)
{
	while (g_ascii_isspace(**p))
		(*p)++;
}

/* gdk_rgba_parse () in GTK 3 */
gboolean
polo_marco_rgba_parse(PoloRGBA *rgba, const char *spec)
{
	gboolean has_alpha;
	double r, g, b, a = 1;
	const char *str = spec;
	char *p;

	if (strncmp(str, "rgba", 4) == 0) {
		has_alpha = TRUE;
		str += 4;
	} else if (strncmp(str, "rgb", 3) == 0) {
		has_alpha = FALSE;
		str += 3;
	} else {
		PangoColor color;

		if (!pango_color_parse(&color, str))
			return FALSE;
		if (rgba) {
			rgba->red = color.red / 65535.;
			rgba->green = color.green / 65535.;
			rgba->blue = color.blue / 65535.;
			rgba->alpha = 1;
		}
		return TRUE;
	}

	skip_space(&str);
	if (*str != '(')
		return FALSE;
	str++;
	skip_space(&str);
	if (!parse_rgb_value(str, &p, &r))
		return FALSE;
	str = p;
	skip_space(&str);
	if (*str != ',')
		return FALSE;
	str++;
	skip_space(&str);
	if (!parse_rgb_value(str, &p, &g))
		return FALSE;
	str = p;
	skip_space(&str);
	if (*str != ',')
		return FALSE;
	str++;
	skip_space(&str);
	if (!parse_rgb_value(str, &p, &b))
		return FALSE;
	str = p;
	skip_space(&str);
	if (has_alpha) {
		if (*str != ',')
			return FALSE;
		str++;
		skip_space(&str);
		a = g_ascii_strtod(str, &p);
		if (p == str)
			return FALSE;
		str = p;
		skip_space(&str);
	}
	if (*str != ')')
		return FALSE;
	str++;
	skip_space(&str);
	if (*str != '\0')
		return FALSE;
	if (rgba) {
		rgba->red = CLAMP(r, 0, 1);
		rgba->green = CLAMP(g, 0, 1);
		rgba->blue = CLAMP(b, 0, 1);
		rgba->alpha = CLAMP(a, 0, 1);
	}
	return TRUE;
}

/* common.c: meta_frame_borders_clear () */
void
polo_marco_theme_borders_clear(PoloThemeBorders *self)
{
	memset(self, 0, sizeof(*self));
}

struct _PoloThemeColors {
	PoloRGBA table[POLO_THEME_COLOR_COMPONENTS][POLO_THEME_COLOR_STATES];
	gboolean have_table;
	GHashTable *custom;
};

PoloThemeColors *
polo_marco_theme_colors_new(void)
{
	PoloThemeColors *colors = g_new0(PoloThemeColors, 1);

	colors->custom =
		g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	return colors;
}

void
polo_marco_theme_colors_free(PoloThemeColors *colors)
{
	if (!colors)
		return;
	g_hash_table_unref(colors->custom);
	g_free(colors);
}

gboolean
polo_marco_theme_colors_set_table(PoloThemeColors *colors, const float *values,
				  int n_values)
{
	int n = POLO_THEME_COLOR_COMPONENTS * POLO_THEME_COLOR_STATES;

	if (n_values != n * 4)
		return FALSE;
	for (int i = 0; i < n; i++) {
		PoloRGBA *c = &colors->table[i / POLO_THEME_COLOR_STATES]
					    [i % POLO_THEME_COLOR_STATES];

		c->red = values[i * 4];
		c->green = values[i * 4 + 1];
		c->blue = values[i * 4 + 2];
		c->alpha = values[i * 4 + 3];
	}
	colors->have_table = TRUE;
	return TRUE;
}

void
polo_marco_theme_colors_set_custom(PoloThemeColors *colors, const char *name,
				   const PoloRGBA *color)
{
	if (color)
		g_hash_table_replace(colors->custom, g_strdup(name),
				     g_memdup2(color, sizeof(*color)));
	else
		g_hash_table_remove(colors->custom, name);
}

/* Map Marco's state flags to GtkStateType; the highest-priority flag wins. */
static int
state_index(PoloStateFlags state)
{
	if (state & POLO_STATE_FLAG_INSENSITIVE)
		return 4;
	if (state & POLO_STATE_FLAG_SELECTED)
		return 3;
	if (state & POLO_STATE_FLAG_PRELIGHT)
		return 2;
	if (state & POLO_STATE_FLAG_ACTIVE)
		return 1;
	return 0;
}

gboolean
polo_marco_theme_colors_get(const PoloThemeColors *colors, int component,
			    PoloStateFlags state, PoloRGBA *color)
{
	if (!colors || !colors->have_table || component < 0 ||
	    component >= POLO_THEME_COLOR_COMPONENTS)
		return FALSE;
	*color = colors->table[component][state_index(state)];
	return TRUE;
}

gboolean
polo_marco_theme_colors_lookup_custom(const PoloThemeColors *colors,
				      const char *name, PoloRGBA *color)
{
	const PoloRGBA *found =
		colors ? g_hash_table_lookup(colors->custom, name) : NULL;

	if (!found)
		return FALSE;
	*color = *found;
	return TRUE;
}

/* Matches gdk_cairo_surface_create_from_pixbuf() at scale 1. */
cairo_surface_t *
polo_marco_surface_from_pixbuf(const GdkPixbuf *pixbuf)
{
	int width = gdk_pixbuf_get_width(pixbuf);
	int height = gdk_pixbuf_get_height(pixbuf);
	int channels = gdk_pixbuf_get_n_channels(pixbuf);
	int src_stride = gdk_pixbuf_get_rowstride(pixbuf);
	const guchar *src = gdk_pixbuf_read_pixels(pixbuf);
	cairo_surface_t *surface = cairo_image_surface_create(
		channels == 3 ? CAIRO_FORMAT_RGB24 : CAIRO_FORMAT_ARGB32, width,
		height);
	int stride = cairo_image_surface_get_stride(surface);
	guchar *dst;

	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
		return surface;
	cairo_surface_flush(surface);
	dst = cairo_image_surface_get_data(surface);
	for (int y = 0; y < height; y++) {
		const guchar *s = src + y * src_stride;
		guint32 *d = (guint32 *)(dst + y * stride);

		for (int x = 0; x < width; x++, s += channels) {
			guint32 a = channels == 4 ? s[3] : 0xff;
			guint32 r = s[0] * a / 0xff;
			guint32 g = s[1] * a / 0xff;
			guint32 b = s[2] * a / 0xff;

			d[x] = a << 24 | r << 16 | g << 8 | b;
		}
	}
	cairo_surface_mark_dirty(surface);
	return surface;
}

/* Matches gdk_cairo_set_source_pixbuf(). */
void
polo_marco_cairo_set_source_pixbuf(cairo_t *cr, const GdkPixbuf *pixbuf,
				   double x, double y)
{
	cairo_surface_t *surface = polo_marco_surface_from_pixbuf(pixbuf);

	cairo_set_source_surface(cr, surface, x, y);
	cairo_surface_destroy(surface);
}

/* Matches gdk_cairo_get_clip_rectangle(cr, NULL). */
gboolean
polo_marco_cairo_has_clip(cairo_t *cr)
{
	double x1, y1, x2, y2;

	cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
	return x2 > x1 && y2 > y1;
}

static void
set_colour(cairo_t *cr, const PoloThemeColors *colors, int component,
	   PoloStateFlags state)
{
	PoloRGBA c = { 0, 0, 0, 1 };

	polo_marco_theme_colors_get(colors, component, state, &c);
	cairo_set_source_rgba(cr, c.red, c.green, c.blue, c.alpha);
}

/* GTK 3 renders a box as a borderless window: backgrounds use the background
 * colour, while lines and arrows use the foreground colour. */
void
polo_marco_render_box(const PoloThemeColors *colors, PoloStateFlags state,
		      cairo_t *cr, double x, double y, double width,
		      double height)
{
	cairo_save(cr);
	set_colour(cr, colors, 1, state);
	cairo_rectangle(cr, x, y, width, height);
	cairo_fill(cr);
	cairo_restore(cr);
}

void
polo_marco_render_vline(const PoloThemeColors *colors, PoloStateFlags state,
			cairo_t *cr, double x, double y1, double y2)
{
	cairo_save(cr);
	set_colour(cr, colors, 0, state);
	cairo_set_line_width(cr, 1);
	cairo_move_to(cr, x + 0.5, y1);
	cairo_line_to(cr, x + 0.5, y2);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void
polo_marco_render_arrow(const PoloThemeColors *colors, PoloStateFlags state,
			cairo_t *cr, double angle, double x, double y,
			double size)
{
	cairo_save(cr);
	set_colour(cr, colors, 0, state);
	cairo_translate(cr, x + size / 2, y + size / 2);
	cairo_rotate(cr, angle);
	cairo_move_to(cr, 0, -size / 4);
	cairo_line_to(cr, size / 3, size / 4);
	cairo_line_to(cr, -size / 3, size / 4);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_restore(cr);
}
