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
