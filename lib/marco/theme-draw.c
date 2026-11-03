/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Marco Theme Rendering */

/*
 * Copyright (C) 2001 Havoc Pennington
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file theme.c    Making Marco look pretty
 *
 * The window decorations drawn by Marco are described by files on disk
 * known internally as "themes" (externally as "window border themes" on
 * http://art.gnome.org/themes/marco/ or "Marco themes"). This file
 * contains most of the code necessary to support themes; it does not
 * contain the XML parser, which is in theme-parser.c.
 *
 * \bug This is a big file with lots of different subsystems, which might
 * be better split out into separate files.
 */

/**
 * \defgroup tokenizer   The theme expression tokenizer
 *
 * Themes can use a simple expression language to represent the values of
 * things. This is the tokeniser used for that language.
 *
 * \bug We could remove almost all this code by using GScanner instead,
 * but we would also have to find every expression in every existing theme
 * we could and make sure the parse trees were the same.
 */

/**
 * \defgroup parser  The theme expression parser
 *
 * Themes can use a simple expression language to represent the values of
 * things. This is the parser used for that language.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <pango/pangocairo.h>

#include "theme-private.h"

static void
gtk_style_shade(PoloRGBA *a, PoloRGBA *b, gdouble k);
static void
rgb_to_hls(gdouble *r, gdouble *g, gdouble *b);
static void
hls_to_rgb(gdouble *h, gdouble *l, gdouble *s);

static cairo_surface_t *
scale_surface(cairo_surface_t *surface, gdouble old_width, gdouble old_height,
	      gdouble new_width, gdouble new_height, gboolean vertical_stripes,
	      gboolean horizontal_stripes)
{
	gdouble scale_x;
	gdouble scale_y;
	cairo_content_t content;
	gdouble width;
	gdouble height;
	cairo_surface_t *scaled;
	cairo_t *cr;

	scale_x = new_width / old_width;
	scale_y = new_height / old_height;

	if (horizontal_stripes && !vertical_stripes) {
		new_width = old_width;
		scale_x = 1.0;
	} else if (vertical_stripes && !horizontal_stripes) {
		new_height = old_height;
		scale_y = 1.0;
	}

	content = CAIRO_CONTENT_COLOR_ALPHA;
	width = ceil(new_width);
	height = ceil(new_height);

	scaled = cairo_surface_create_similar(surface, content, (int)width,
					      (int)height);
	cr = cairo_create(scaled);

	cairo_scale(cr, scale_x, scale_y);
	cairo_set_source_surface(cr, surface, 0, 0);

	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);

	cairo_paint(cr);
	cairo_destroy(cr);

	return scaled;
}
static cairo_surface_t *
get_surface_from_pixbuf(GdkPixbuf *pixbuf, PoloImageFillType fill_type,
			gdouble width, gdouble height,
			gboolean vertical_stripes, gboolean horizontal_stripes)
{
	gdouble pixbuf_width;
	gdouble pixbuf_height;
	cairo_surface_t *surface;
	cairo_content_t content;
	cairo_surface_t *copy;
	cairo_t *cr;

	pixbuf_width = gdk_pixbuf_get_width(pixbuf);
	pixbuf_height = gdk_pixbuf_get_height(pixbuf);
	surface = polo_marco_surface_from_pixbuf(pixbuf);

	if (pixbuf_width == width && pixbuf_height == height) {
		return surface;
	}

	if (fill_type != POLO_IMAGE_FILL_TILE) {
		cairo_surface_t *scaled;

		scaled = scale_surface(surface, pixbuf_width, pixbuf_height,
				       width, height, vertical_stripes,
				       horizontal_stripes);

		cairo_surface_destroy(surface);
		surface = scaled;
	}

	content = CAIRO_CONTENT_COLOR_ALPHA;
	width = ceil(width);
	height = ceil(height);

	copy = cairo_surface_create_similar(surface, content, (int)width,
					    (int)height);
	cr = cairo_create(copy);

	cairo_set_source_surface(cr, surface, 0, 0);

	if (fill_type == POLO_IMAGE_FILL_TILE || vertical_stripes ||
	    horizontal_stripes) {
		cairo_pattern_set_extend(cairo_get_source(cr),
					 CAIRO_EXTEND_REPEAT);
	}

	cairo_paint(cr);
	cairo_destroy(cr);

	cairo_surface_destroy(surface);

	return copy;
}
static GdkPixbuf *
colorize_pixbuf(GdkPixbuf *orig, PoloRGBA *new_color)
{
	GdkPixbuf *pixbuf;
	double intensity;
	int x, y;
	const guchar *src;
	guchar *dest;
	int orig_rowstride;
	int dest_rowstride;
	int width, height;
	gboolean has_alpha;
	const guchar *src_pixels;
	guchar *dest_pixels;

	pixbuf = gdk_pixbuf_new(
		gdk_pixbuf_get_colorspace(orig), gdk_pixbuf_get_has_alpha(orig),
		gdk_pixbuf_get_bits_per_sample(orig),
		gdk_pixbuf_get_width(orig), gdk_pixbuf_get_height(orig));

	if (pixbuf == NULL)
		return NULL;

	orig_rowstride = gdk_pixbuf_get_rowstride(orig);
	dest_rowstride = gdk_pixbuf_get_rowstride(pixbuf);
	width = gdk_pixbuf_get_width(pixbuf);
	height = gdk_pixbuf_get_height(pixbuf);
	has_alpha = gdk_pixbuf_get_has_alpha(orig);
	src_pixels = gdk_pixbuf_get_pixels(orig);
	dest_pixels = gdk_pixbuf_get_pixels(pixbuf);

	for (y = 0; y < height; y++) {
		src = src_pixels + y * orig_rowstride;
		dest = dest_pixels + y * dest_rowstride;

		for (x = 0; x < width; x++) {
			double dr, dg, db;

			intensity = INTENSITY(src[0], src[1], src[2]) / 255.0;

			if (intensity <= 0.5) {
				/* Go from black at intensity = 0.0 to new_color
				 * at intensity = 0.5 */
				dr = new_color->red * intensity * 2.0;
				dg = new_color->green * intensity * 2.0;
				db = new_color->blue * intensity * 2.0;
			} else {
				/* Go from new_color at intensity = 0.5 to white
				 * at intensity = 1.0 */
				dr = new_color->red +
					(1.0 - new_color->red) *
						(intensity - 0.5) * 2.0;
				dg = new_color->green +
					(1.0 - new_color->green) *
						(intensity - 0.5) * 2.0;
				db = new_color->blue +
					(1.0 - new_color->blue) *
						(intensity - 0.5) * 2.0;
			}

			dest[0] = CLAMP_UCHAR(255 * dr);
			dest[1] = CLAMP_UCHAR(255 * dg);
			dest[2] = CLAMP_UCHAR(255 * db);

			if (has_alpha) {
				dest[3] = src[3];
				src += 4;
				dest += 4;
			} else {
				src += 3;
				dest += 3;
			}
		}
	}

	return pixbuf;
}
static void
color_composite(const PoloRGBA *bg, const PoloRGBA *fg, double alpha,
		PoloRGBA *color)
{
	*color = *bg;
	color->red = color->red + (fg->red - color->red) * alpha;
	color->green = color->green + (fg->green - color->green) * alpha;
	color->blue = color->blue + (fg->blue - color->blue) * alpha;
}
static PoloButtonType
map_button_function_to_type(PoloButtonFunction function)
{
	switch (function) {
	case POLO_BUTTON_FUNCTION_SHADE:
		return POLO_BUTTON_TYPE_SHADE;
	case POLO_BUTTON_FUNCTION_ABOVE:
		return POLO_BUTTON_TYPE_ABOVE;
	case POLO_BUTTON_FUNCTION_STICK:
		return POLO_BUTTON_TYPE_STICK;
	case POLO_BUTTON_FUNCTION_UNSHADE:
		return POLO_BUTTON_TYPE_UNSHADE;
	case POLO_BUTTON_FUNCTION_UNABOVE:
		return POLO_BUTTON_TYPE_UNABOVE;
	case POLO_BUTTON_FUNCTION_UNSTICK:
		return POLO_BUTTON_TYPE_UNSTICK;
	case POLO_BUTTON_FUNCTION_MENU:
		return POLO_BUTTON_TYPE_MENU;
	case POLO_BUTTON_FUNCTION_APPMENU:
		return POLO_BUTTON_TYPE_APPMENU;
	case POLO_BUTTON_FUNCTION_MINIMIZE:
		return POLO_BUTTON_TYPE_MINIMIZE;
	case POLO_BUTTON_FUNCTION_MAXIMIZE:
		return POLO_BUTTON_TYPE_MAXIMIZE;
	case POLO_BUTTON_FUNCTION_CLOSE:
		return POLO_BUTTON_TYPE_CLOSE;
	case POLO_BUTTON_FUNCTION_LAST:
		return POLO_BUTTON_TYPE_LAST;
	}

	return POLO_BUTTON_TYPE_LAST;
}
static cairo_pattern_t *
create_cairo_pattern_from_gradient_spec(const PoloGradientSpec *spec,
					const PoloAlphaGradientSpec *alpha_spec,
					PoloThemeColors *context)
{
	gint n_colors;
	cairo_pattern_t *pattern;
	GSList *tmp;
	gint i;

	n_colors = g_slist_length(spec->color_specs);
	if (n_colors == 0)
		return NULL;

	if (alpha_spec != NULL && alpha_spec->n_alphas != 1)
		g_assert(n_colors == alpha_spec->n_alphas);

	if (spec->type == POLO_GRADIENT_HORIZONTAL)
		pattern = cairo_pattern_create_linear(0, 0, 1, 0);
	else if (spec->type == POLO_GRADIENT_VERTICAL)
		pattern = cairo_pattern_create_linear(0, 0, 0, 1);
	else if (spec->type == POLO_GRADIENT_DIAGONAL)
		pattern = cairo_pattern_create_linear(0, 0, 1, 1);
	else
		g_assert_not_reached();

	i = 0;
	tmp = spec->color_specs;
	while (tmp != NULL) {
		PoloRGBA color;

		polo_marco_color_spec_render(tmp->data, context, &color);

		if (alpha_spec != NULL) {
			gdouble alpha;

			if (alpha_spec->n_alphas == 1)
				alpha = alpha_spec->alphas[0] / 255.0;
			else
				alpha = alpha_spec->alphas[i] / 255.0;

			cairo_pattern_add_color_stop_rgba(
				pattern, i / (gfloat)(n_colors - 1), color.red,
				color.green, color.blue, alpha);
		} else
			cairo_pattern_add_color_stop_rgb(
				pattern, i / (gfloat)(n_colors - 1), color.red,
				color.green, color.blue);

		tmp = tmp->next;
		++i;
	}

	if (cairo_pattern_status(pattern) != CAIRO_STATUS_SUCCESS) {
		cairo_pattern_destroy(pattern);
		return NULL;
	}

	return pattern;
}
void
polo_marco_gradient_spec_render(const PoloGradientSpec *spec,
				const PoloAlphaGradientSpec *alpha_spec,
				cairo_t *cr, PoloThemeColors *context, gint x,
				gint y, gint width, gint height)
{
	cairo_pattern_t *pattern;

	pattern = create_cairo_pattern_from_gradient_spec(spec, alpha_spec,
							  context);
	if (pattern == NULL)
		return;

	cairo_save(cr);

	cairo_rectangle(cr, x, y, width, height);

	cairo_translate(cr, x, y);
	cairo_scale(cr, width, height);

	cairo_set_source(cr, pattern);
	cairo_fill(cr);
	cairo_pattern_destroy(pattern);

	cairo_restore(cr);
}
cairo_pattern_t *
polo_marco_alpha_gradient_spec_get_mask(const PoloAlphaGradientSpec *spec)
{
	gint n_alphas;
	cairo_pattern_t *pattern;
	gint i;

	/* Hardcoded in theme-parser.c */
	g_assert(spec->type == POLO_GRADIENT_HORIZONTAL);

	n_alphas = spec->n_alphas;
	if (n_alphas == 0)
		return NULL;

	if (n_alphas == 1)
		return cairo_pattern_create_rgba(0, 0, 0,
						 spec->alphas[0] / 255.0);

	pattern = cairo_pattern_create_linear(0, 0, 1, 0);

	for (i = 0; i < n_alphas; i++)
		cairo_pattern_add_color_stop_rgba(
			pattern, i / (gfloat)(n_alphas - 1), 0, 0, 0,
			spec->alphas[i] / 255.0);

	if (cairo_pattern_status(pattern) != CAIRO_STATUS_SUCCESS) {
		cairo_pattern_destroy(pattern);
		return NULL;
	}

	return pattern;
}
static void
polo_marco_set_color_from_style(PoloRGBA *color, PoloThemeColors *context,
				PoloStateFlags state,
				PoloGtkColorComponent component)
{
	if (!polo_marco_theme_colors_get(context, component, state, color))
		*color = (PoloRGBA){ 0, 0, 0, 1 };
}

static void
polo_marco_set_custom_color_from_style(PoloRGBA *color,
				       PoloThemeColors *context,
				       char *color_name,
				       PoloColorSpec *fallback)
{
	if (!polo_marco_theme_colors_lookup_custom(context, color_name, color))
		polo_marco_color_spec_render(fallback, context, color);
}

void
polo_marco_color_spec_render(PoloColorSpec *spec, PoloThemeColors *style,
			     PoloRGBA *color)
{
	g_return_if_fail(spec != NULL);

	switch (spec->type) {
	case POLO_COLOR_SPEC_BASIC:
		*color = spec->data.basic.color;
		break;

	case POLO_COLOR_SPEC_GTK:
		polo_marco_set_color_from_style(color, style,
						spec->data.gtk.state,
						spec->data.gtk.component);
		break;

	case POLO_COLOR_SPEC_GTK_CUSTOM:
		polo_marco_set_custom_color_from_style(
			color, style, spec->data.gtkcustom.color_name,
			spec->data.gtkcustom.fallback);
		break;

	case POLO_COLOR_SPEC_BLEND: {
		PoloRGBA bg, fg;

		polo_marco_color_spec_render(spec->data.blend.background, style,
					     &bg);
		polo_marco_color_spec_render(spec->data.blend.foreground, style,
					     &fg);

		color_composite(&bg, &fg, spec->data.blend.alpha,
				&spec->data.blend.color);

		*color = spec->data.blend.color;
	} break;

	case POLO_COLOR_SPEC_SHADE: {
		polo_marco_color_spec_render(spec->data.shade.base, style,
					     &spec->data.shade.color);

		gtk_style_shade(&spec->data.shade.color,
				&spec->data.shade.color,
				spec->data.shade.factor);

		*color = spec->data.shade.color;
	} break;
	}
}
static int
parse_x_position_unchecked(PoloDrawSpec *spec, const PoloPositionExprEnv *env)
{
	int retval;
	GError *error;

	retval = 0;
	error = NULL;
	if (!polo_marco_parse_position_expression(spec, env, &retval, NULL,
						  &error)) {
		polo_marco_warning(_("Theme contained an expression that "
				     "resulted in an error: %s\n"),
				   error->message);

		g_error_free(error);
	}

	return retval;
}
static int
parse_y_position_unchecked(PoloDrawSpec *spec, const PoloPositionExprEnv *env)
{
	int retval;
	GError *error;

	retval = 0;
	error = NULL;
	if (!polo_marco_parse_position_expression(spec, env, NULL, &retval,
						  &error)) {
		polo_marco_warning(_("Theme contained an expression that "
				     "resulted in an error: %s\n"),
				   error->message);

		g_error_free(error);
	}

	return retval;
}
static int
parse_size_unchecked(PoloDrawSpec *spec, PoloPositionExprEnv *env)
{
	int retval;
	GError *error;

	retval = 0;
	error = NULL;
	if (!polo_marco_parse_size_expression(spec, env, &retval, &error)) {
		polo_marco_warning(_("Theme contained an expression that "
				     "resulted in an error: %s\n"),
				   error->message);

		g_error_free(error);
	}

	return retval;
}
static GdkPixbuf *
apply_alpha(GdkPixbuf *pixbuf, PoloAlphaGradientSpec *spec, gboolean force_copy)
{
	GdkPixbuf *new_pixbuf;
	gboolean needs_alpha;

	g_return_val_if_fail(GDK_IS_PIXBUF(pixbuf), NULL);

	needs_alpha = spec && (spec->n_alphas > 1 || spec->alphas[0] != 0xff);

	if (!needs_alpha)
		return pixbuf;

	if (!gdk_pixbuf_get_has_alpha(pixbuf)) {
		new_pixbuf = gdk_pixbuf_add_alpha(pixbuf, FALSE, 0, 0, 0);
		g_object_unref(G_OBJECT(pixbuf));
		pixbuf = new_pixbuf;
	} else if (force_copy) {
		new_pixbuf = gdk_pixbuf_copy(pixbuf);
		g_object_unref(G_OBJECT(pixbuf));
		pixbuf = new_pixbuf;
	}

	g_assert(gdk_pixbuf_get_has_alpha(pixbuf));

	polo_marco_gradient_add_alpha(pixbuf, spec->alphas, spec->n_alphas,
				      spec->type);

	return pixbuf;
}
static GdkPixbuf *
pixbuf_tile(GdkPixbuf *tile, int width, int height)
{
	GdkPixbuf *pixbuf;
	int tile_width;
	int tile_height;
	int i, j;

	tile_width = gdk_pixbuf_get_width(tile);
	tile_height = gdk_pixbuf_get_height(tile);

	pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
				gdk_pixbuf_get_has_alpha(tile), 8, width,
				height);

	i = 0;
	while (i < width) {
		j = 0;
		while (j < height) {
			int w, h;

			w = MIN(tile_width, width - i);
			h = MIN(tile_height, height - j);

			gdk_pixbuf_copy_area(tile, 0, 0, w, h, pixbuf, i, j);

			j += tile_height;
		}

		i += tile_width;
	}

	return pixbuf;
}
static GdkPixbuf *
replicate_rows(GdkPixbuf *src, int src_x, int src_y, int width, int height)
{
	unsigned int n_channels = gdk_pixbuf_get_n_channels(src);
	unsigned int src_rowstride = gdk_pixbuf_get_rowstride(src);
	unsigned char *pixels = (gdk_pixbuf_get_pixels(src) +
				 src_y * src_rowstride + src_x * n_channels);
	unsigned char *dest_pixels;
	GdkPixbuf *result;
	unsigned int dest_rowstride;
	int i;

	result = gdk_pixbuf_new(GDK_COLORSPACE_RGB, n_channels == 4, 8, width,
				height);
	dest_rowstride = gdk_pixbuf_get_rowstride(result);
	dest_pixels = gdk_pixbuf_get_pixels(result);

	for (i = 0; i < height; i++)
		memcpy(dest_pixels + dest_rowstride * i, pixels,
		       n_channels * width);

	return result;
}
static GdkPixbuf *
replicate_cols(GdkPixbuf *src, int src_x, int src_y, int width, int height)
{
	unsigned int n_channels = gdk_pixbuf_get_n_channels(src);
	unsigned int src_rowstride = gdk_pixbuf_get_rowstride(src);
	unsigned char *pixels = (gdk_pixbuf_get_pixels(src) +
				 src_y * src_rowstride + src_x * n_channels);
	unsigned char *dest_pixels;
	GdkPixbuf *result;
	unsigned int dest_rowstride;
	int i, j;

	result = gdk_pixbuf_new(GDK_COLORSPACE_RGB, n_channels == 4, 8, width,
				height);
	dest_rowstride = gdk_pixbuf_get_rowstride(result);
	dest_pixels = gdk_pixbuf_get_pixels(result);

	for (i = 0; i < height; i++) {
		unsigned char *p = dest_pixels + dest_rowstride * i;
		unsigned char *q = pixels + src_rowstride * i;

		unsigned char r = *(q++);
		unsigned char g = *(q++);
		unsigned char b = *(q++);

		if (n_channels == 4) {
			unsigned char a;

			a = *(q++);

			for (j = 0; j < width; j++) {
				*(p++) = r;
				*(p++) = g;
				*(p++) = b;
				*(p++) = a;
			}
		} else {
			for (j = 0; j < width; j++) {
				*(p++) = r;
				*(p++) = g;
				*(p++) = b;
			}
		}
	}

	return result;
}
static GdkPixbuf *
scale_and_alpha_pixbuf(GdkPixbuf *src, PoloAlphaGradientSpec *alpha_spec,
		       PoloImageFillType fill_type, int width, int height,
		       gboolean vertical_stripes, gboolean horizontal_stripes)
{
	GdkPixbuf *pixbuf;
	GdkPixbuf *temp_pixbuf;

	pixbuf = NULL;

	pixbuf = src;

	if (gdk_pixbuf_get_width(pixbuf) == width &&
	    gdk_pixbuf_get_height(pixbuf) == height) {
		g_object_ref(G_OBJECT(pixbuf));
	} else {
		if (fill_type == POLO_IMAGE_FILL_TILE) {
			pixbuf = pixbuf_tile(pixbuf, width, height);
		} else {
			int src_h, src_w, dest_h, dest_w;
			src_h = gdk_pixbuf_get_height(src);
			src_w = gdk_pixbuf_get_width(src);

			/* prefer to replicate_cols if possible, as that
			 * is faster (no memory reads)
			 */
			if (horizontal_stripes) {
				dest_w = gdk_pixbuf_get_width(src);
				dest_h = height;
			} else if (vertical_stripes) {
				dest_w = width;
				dest_h = gdk_pixbuf_get_height(src);
			}

			else {
				dest_w = width;
				dest_h = height;
			}

			if (dest_w == src_w && dest_h == src_h) {
				temp_pixbuf = src;
				g_object_ref(G_OBJECT(temp_pixbuf));
			} else {
				temp_pixbuf = gdk_pixbuf_scale_simple(
					src, dest_w, dest_h,
					GDK_INTERP_BILINEAR);
			}

			/* prefer to replicate_cols if possible, as that
			 * is faster (no memory reads)
			 */
			if (horizontal_stripes) {
				pixbuf = replicate_cols(temp_pixbuf, 0, 0,
							width, height);
				g_object_unref(G_OBJECT(temp_pixbuf));
			} else if (vertical_stripes) {
				pixbuf = replicate_rows(temp_pixbuf, 0, 0,
							width, height);
				g_object_unref(G_OBJECT(temp_pixbuf));
			} else {
				pixbuf = temp_pixbuf;
			}
		}
	}

	if (pixbuf)
		pixbuf = apply_alpha(pixbuf, alpha_spec, pixbuf == src);

	return pixbuf;
}
static GdkPixbuf *
draw_op_as_pixbuf(const PoloDrawOp *op, PoloThemeColors *style,
		  const PoloDrawInfo *info, int width, int height)
{
	/* Try to get the op as a pixbuf, assuming w/h in the op
	 * matches the width/height passed in. return NULL
	 * if the op can't be converted to an equivalent pixbuf.
	 */
	GdkPixbuf *pixbuf;

	pixbuf = NULL;

	switch (op->type) {
	case POLO_DRAW_LINE:
		break;

	case POLO_DRAW_RECTANGLE:
		if (op->data.rectangle.filled) {
			PoloRGBA color;

			polo_marco_color_spec_render(
				op->data.rectangle.color_spec, style, &color);

			pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
						width, height);

			gdk_pixbuf_fill(pixbuf, GDK_COLOR_RGBA(color));
		}
		break;

	case POLO_DRAW_ARC:
		break;

	case POLO_DRAW_CLIP:
		break;

	case POLO_DRAW_TINT: {
		PoloRGBA color;
		guint32 rgba;
		gboolean has_alpha;

		polo_marco_color_spec_render(op->data.rectangle.color_spec,
					     style, &color);

		has_alpha = op->data.tint.alpha_spec &&
			(op->data.tint.alpha_spec->n_alphas > 1 ||
			 op->data.tint.alpha_spec->alphas[0] != 0xff);

		pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, has_alpha, 8, width,
					height);

		if (!has_alpha) {
			rgba = GDK_COLOR_RGBA(color);

			gdk_pixbuf_fill(pixbuf, rgba);
		} else if (op->data.tint.alpha_spec->n_alphas == 1) {
			rgba = GDK_COLOR_RGBA(color);
			rgba &= ~0xff;
			rgba |= op->data.tint.alpha_spec->alphas[0];

			gdk_pixbuf_fill(pixbuf, rgba);
		} else {
			rgba = GDK_COLOR_RGBA(color);

			gdk_pixbuf_fill(pixbuf, rgba);

			polo_marco_gradient_add_alpha(
				pixbuf, op->data.tint.alpha_spec->alphas,
				op->data.tint.alpha_spec->n_alphas,
				op->data.tint.alpha_spec->type);
		}
	} break;

	case POLO_DRAW_IMAGE: {
		if (op->data.image.colorize_spec) {
			PoloRGBA color;

			polo_marco_color_spec_render(
				op->data.image.colorize_spec, style, &color);

			if (op->data.image.colorize_cache_pixbuf == NULL ||
			    op->data.image.colorize_cache_pixel !=
				    GDK_COLOR_RGB(color)) {
				if (op->data.image.colorize_cache_pixbuf)
					g_object_unref(G_OBJECT(
						op->data.image
							.colorize_cache_pixbuf));

				/* const cast here */
				((PoloDrawOp *)op)
					->data.image.colorize_cache_pixbuf =
					colorize_pixbuf(op->data.image.pixbuf,
							&color);
				((PoloDrawOp *)op)
					->data.image.colorize_cache_pixel =
					GDK_COLOR_RGB(color);
			}

			if (op->data.image.colorize_cache_pixbuf) {
				pixbuf = scale_and_alpha_pixbuf(
					op->data.image.colorize_cache_pixbuf,
					op->data.image.alpha_spec,
					op->data.image.fill_type, width, height,
					op->data.image.vertical_stripes,
					op->data.image.horizontal_stripes);
			}
		} else {
			pixbuf = scale_and_alpha_pixbuf(
				op->data.image.pixbuf,
				op->data.image.alpha_spec,
				op->data.image.fill_type, width, height,
				op->data.image.vertical_stripes,
				op->data.image.horizontal_stripes);
		}
		break;
	}

	case POLO_DRAW_GRADIENT:
	case POLO_DRAW_GTK_ARROW:
	case POLO_DRAW_GTK_BOX:
	case POLO_DRAW_GTK_VLINE:
		break;

	case POLO_DRAW_ICON:
		if (info->mini_icon &&
		    width <= gdk_pixbuf_get_width(info->mini_icon) &&
		    height <= gdk_pixbuf_get_height(info->mini_icon))
			pixbuf = scale_and_alpha_pixbuf(
				info->mini_icon, op->data.icon.alpha_spec,
				op->data.icon.fill_type, width, height, FALSE,
				FALSE);
		else if (info->icon)
			pixbuf = scale_and_alpha_pixbuf(
				info->icon, op->data.icon.alpha_spec,
				op->data.icon.fill_type, width, height, FALSE,
				FALSE);
		break;

	case POLO_DRAW_TITLE:
		break;

	case POLO_DRAW_OP_LIST:
		break;

	case POLO_DRAW_TILE:
		break;
	}

	return pixbuf;
}
static cairo_surface_t *
draw_op_as_surface(const PoloDrawOp *op, PoloThemeColors *style,
		   const PoloDrawInfo *info, gdouble width, gdouble height)
{
	cairo_surface_t *surface;

	surface = NULL;

	switch (op->type) {
	case POLO_DRAW_IMAGE: {
		if (op->data.image.colorize_spec) {
			PoloRGBA color;

			polo_marco_color_spec_render(
				op->data.image.colorize_spec, style, &color);

			if (op->data.image.colorize_cache_pixbuf == NULL ||
			    op->data.image.colorize_cache_pixel !=
				    GDK_COLOR_RGB(color)) {
				if (op->data.image.colorize_cache_pixbuf)
					g_object_unref(G_OBJECT(
						op->data.image
							.colorize_cache_pixbuf));

				/* const cast here */
				((PoloDrawOp *)op)
					->data.image.colorize_cache_pixbuf =
					colorize_pixbuf(op->data.image.pixbuf,
							&color);
				((PoloDrawOp *)op)
					->data.image.colorize_cache_pixel =
					GDK_COLOR_RGB(color);
			}

			if (op->data.image.colorize_cache_pixbuf) {
				surface = get_surface_from_pixbuf(
					op->data.image.colorize_cache_pixbuf,
					op->data.image.fill_type, width, height,
					op->data.image.vertical_stripes,
					op->data.image.horizontal_stripes);
			}
		} else {
			surface = get_surface_from_pixbuf(
				op->data.image.pixbuf, op->data.image.fill_type,
				width, height, op->data.image.vertical_stripes,
				op->data.image.horizontal_stripes);
		}
		break;
	}

	case POLO_DRAW_ICON:
		if (info->mini_icon &&
		    width <= gdk_pixbuf_get_width(info->mini_icon) &&
		    height <= gdk_pixbuf_get_height(info->mini_icon))
			surface = get_surface_from_pixbuf(
				info->mini_icon, op->data.icon.fill_type, width,
				height, FALSE, FALSE);
		else if (info->icon)
			surface = get_surface_from_pixbuf(
				info->icon, op->data.icon.fill_type, width,
				height, FALSE, FALSE);
		break;

	case POLO_DRAW_TINT:
	case POLO_DRAW_LINE:
	case POLO_DRAW_RECTANGLE:
	case POLO_DRAW_ARC:
	case POLO_DRAW_CLIP:
	case POLO_DRAW_GRADIENT:
	case POLO_DRAW_GTK_ARROW:
	case POLO_DRAW_GTK_BOX:
	case POLO_DRAW_GTK_VLINE:
	case POLO_DRAW_TITLE:
	case POLO_DRAW_OP_LIST:
	case POLO_DRAW_TILE:
		break;

	default:
		break;
	}

	return surface;
}
static void
fill_env(PoloPositionExprEnv *env, const PoloDrawInfo *info,
	 PoloRect logical_region)
{
	/* FIXME this stuff could be raised into draw_op_list_draw() probably
	 */
	env->rect = logical_region;
	env->object_width = -1;
	env->object_height = -1;
	if (info->fgeom) {
		env->left_width = info->fgeom->borders.visible.left;
		env->right_width = info->fgeom->borders.visible.right;
		env->top_height = info->fgeom->borders.visible.top;
		env->bottom_height = info->fgeom->borders.visible.bottom;
		env->frame_x_center = info->fgeom->width / 2 - logical_region.x;
		env->frame_y_center =
			info->fgeom->height / 2 - logical_region.y;
	} else {
		env->left_width = 0;
		env->right_width = 0;
		env->top_height = 0;
		env->bottom_height = 0;
		env->frame_x_center = 0;
		env->frame_y_center = 0;
	}

	env->mini_icon_width =
		info->mini_icon ? gdk_pixbuf_get_width(info->mini_icon) : 0;
	env->mini_icon_height =
		info->mini_icon ? gdk_pixbuf_get_height(info->mini_icon) : 0;
	env->icon_width = info->icon ? gdk_pixbuf_get_width(info->icon) : 0;
	env->icon_height = info->icon ? gdk_pixbuf_get_height(info->icon) : 0;

	env->title_width = info->title_layout_width;
	env->title_height = info->title_layout_height;
	env->theme = info->theme;
}
/* This code was originally rendering anti-aliased using X primitives, and
 * now has been switched to draw anti-aliased using cairo. In general, the
 * closest correspondence between X rendering and cairo rendering is given
 * by offsetting the geometry by 0.5 pixels in both directions before rendering
 * with cairo. This is because X samples at the upper left corner of the
 * pixel while cairo averages over the entire pixel. However, in the cases
 * where the X rendering was an exact rectangle with no "jaggies"
 * we need to be a bit careful about applying the offset. We want to produce
 * the exact same pixel-aligned rectangle, rather than a rectangle with
 * fuzz around the edges.
 */
static void
polo_marco_draw_op_draw_with_env(const PoloDrawOp *op,
				 PoloThemeColors *style_gtk, cairo_t *cr,
				 const PoloDrawInfo *info, PoloRect rect,
				 PoloPositionExprEnv *env)
{
	PoloRGBA color;

	cairo_save(cr);

	cairo_set_line_width(cr, 1.0);

	switch (op->type) {
	case POLO_DRAW_LINE: {
		int x1, x2, y1, y2;

		polo_marco_color_spec_render(op->data.line.color_spec,
					     style_gtk, &color);
		cairo_set_source_rgba(cr, color.red, color.green, color.blue,
				      color.alpha);

		if (op->data.line.width > 0)
			cairo_set_line_width(cr, op->data.line.width);

		if (op->data.line.dash_on_length > 0 &&
		    op->data.line.dash_off_length > 0) {
			double dash_list[2];
			dash_list[0] = op->data.line.dash_on_length;
			dash_list[1] = op->data.line.dash_off_length;
			cairo_set_dash(cr, dash_list, 2, 0);
		}

		x1 = parse_x_position_unchecked(op->data.line.x1, env);
		y1 = parse_y_position_unchecked(op->data.line.y1, env);

		if (!op->data.line.x2 && !op->data.line.y2 &&
		    op->data.line.width == 0) {
			cairo_rectangle(cr, x1, y1, 1, 1);
			cairo_fill(cr);
		} else {
			if (op->data.line.x2)
				x2 = parse_x_position_unchecked(
					op->data.line.x2, env);
			else
				x2 = x1;

			if (op->data.line.y2)
				y2 = parse_y_position_unchecked(
					op->data.line.y2, env);
			else
				y2 = y1;

			/* This is one of the cases where we are matching the
			 * exact pixel aligned rectangle produced by X; for
			 * zero-width lines the generic algorithm produces the
			 * right result so we don't need to handle them here.
			 */
			if ((y1 == y2 || x1 == x2) &&
			    op->data.line.width != 0) {
				double offset =
					op->data.line.width % 2 ? .5 : 0;

				if (y1 == y2) {
					cairo_move_to(cr, x1, y1 + offset);
					cairo_line_to(cr, x2, y2 + offset);
				} else {
					cairo_move_to(cr, x1 + offset, y1);
					cairo_line_to(cr, x2 + offset, y2);
				}
			} else {
				/* zero-width lines include both end-points in
				 * X, unlike wide lines */
				if (op->data.line.width == 0)
					cairo_set_line_cap(
						cr, CAIRO_LINE_CAP_SQUARE);

				cairo_move_to(cr, x1 + .5, y1 + .5);
				cairo_line_to(cr, x2 + .5, y2 + .5);
			}
			cairo_stroke(cr);
		}
	} break;

	case POLO_DRAW_RECTANGLE: {
		int rx, ry, rwidth, rheight;

		polo_marco_color_spec_render(op->data.rectangle.color_spec,
					     style_gtk, &color);
		cairo_set_source_rgba(cr, color.red, color.green, color.blue,
				      color.alpha);

		rx = parse_x_position_unchecked(op->data.rectangle.x, env);
		ry = parse_y_position_unchecked(op->data.rectangle.y, env);
		rwidth = parse_size_unchecked(op->data.rectangle.width, env);
		rheight = parse_size_unchecked(op->data.rectangle.height, env);

		/* Filled and stroked rectangles are the other cases
		 * we pixel-align to X rasterization
		 */
		if (op->data.rectangle.filled) {
			cairo_rectangle(cr, rx, ry, rwidth, rheight);
			cairo_fill(cr);
		} else {
			cairo_rectangle(cr, rx + .5, ry + .5, rwidth, rheight);
			cairo_stroke(cr);
		}
	} break;

	case POLO_DRAW_ARC: {
		int rx, ry, rwidth, rheight;
		double start_angle, end_angle;
		double center_x, center_y;

		polo_marco_color_spec_render(op->data.arc.color_spec, style_gtk,
					     &color);
		cairo_set_source_rgba(cr, color.red, color.green, color.blue,
				      color.alpha);

		rx = parse_x_position_unchecked(op->data.arc.x, env);
		ry = parse_y_position_unchecked(op->data.arc.y, env);
		rwidth = parse_size_unchecked(op->data.arc.width, env);
		rheight = parse_size_unchecked(op->data.arc.height, env);

		start_angle = op->data.arc.start_angle * (M_PI / 180.) -
			(.5 * M_PI); /* start at 12 instead of 3 oclock */
		end_angle =
			start_angle + op->data.arc.extent_angle * (M_PI / 180.);
		center_x = rx + (double)rwidth / 2. + .5;
		center_y = ry + (double)rheight / 2. + .5;

		cairo_save(cr);

		cairo_translate(cr, center_x, center_y);
		cairo_scale(cr, (double)rwidth / 2., (double)rheight / 2.);

		if (op->data.arc.extent_angle >= 0)
			cairo_arc(cr, 0, 0, 1, start_angle, end_angle);
		else
			cairo_arc_negative(cr, 0, 0, 1, start_angle, end_angle);

		cairo_restore(cr);

		if (op->data.arc.filled) {
			cairo_line_to(cr, center_x, center_y);
			cairo_fill(cr);
		} else
			cairo_stroke(cr);
	} break;

	case POLO_DRAW_CLIP:
		break;

	case POLO_DRAW_TINT: {
		int rx, ry, rwidth, rheight;
		gboolean needs_alpha;

		needs_alpha = op->data.tint.alpha_spec &&
			(op->data.tint.alpha_spec->n_alphas > 1 ||
			 op->data.tint.alpha_spec->alphas[0] != 0xff);

		rx = parse_x_position_unchecked(op->data.tint.x, env);
		ry = parse_y_position_unchecked(op->data.tint.y, env);
		rwidth = parse_size_unchecked(op->data.tint.width, env);
		rheight = parse_size_unchecked(op->data.tint.height, env);

		if (!needs_alpha) {
			polo_marco_color_spec_render(op->data.tint.color_spec,
						     style_gtk, &color);
			cairo_set_source_rgba(cr, color.red, color.green,
					      color.blue, color.alpha);

			cairo_rectangle(cr, rx, ry, rwidth, rheight);
			cairo_fill(cr);
		} else {
			GdkPixbuf *pixbuf;

			pixbuf = draw_op_as_pixbuf(op, style_gtk, info, rwidth,
						   rheight);

			if (pixbuf) {
				polo_marco_cairo_set_source_pixbuf(cr, pixbuf,
								   rx, ry);
				cairo_paint(cr);

				g_object_unref(G_OBJECT(pixbuf));
			}
		}
	} break;

	case POLO_DRAW_GRADIENT: {
		int rx, ry, rwidth, rheight;

		rx = parse_x_position_unchecked(op->data.gradient.x, env);
		ry = parse_y_position_unchecked(op->data.gradient.y, env);
		rwidth = parse_size_unchecked(op->data.gradient.width, env);
		rheight = parse_size_unchecked(op->data.gradient.height, env);

		polo_marco_gradient_spec_render(op->data.gradient.gradient_spec,
						op->data.gradient.alpha_spec,
						cr, style_gtk, rx, ry, rwidth,
						rheight);
	} break;

	case POLO_DRAW_IMAGE: {
		gint scale;
		gdouble rx, ry, rwidth, rheight;
		cairo_surface_t *surface;

		scale = 1;
		cairo_scale(cr, 1.0 / scale, 1.0 / scale);

		if (op->data.image.pixbuf) {
			env->object_width =
				gdk_pixbuf_get_width(op->data.image.pixbuf) /
				scale;
			env->object_height =
				gdk_pixbuf_get_height(op->data.image.pixbuf) /
				scale;
		}

		rwidth =
			parse_size_unchecked(op->data.image.width, env) * scale;
		rheight = parse_size_unchecked(op->data.image.height, env) *
			scale;

		surface = draw_op_as_surface(op, style_gtk, info, rwidth,
					     rheight);

		if (surface) {
			rx = parse_x_position_unchecked(op->data.image.x, env) *
				scale;
			ry = parse_y_position_unchecked(op->data.image.y, env) *
				scale;

			cairo_set_source_surface(cr, surface, rx, ry);

			if (op->data.image.alpha_spec) {
				cairo_pattern_t *pattern;

				cairo_translate(cr, rx, ry);
				cairo_scale(cr, rwidth, rheight);

				pattern =
					polo_marco_alpha_gradient_spec_get_mask(
						op->data.image.alpha_spec);
				cairo_mask(cr, pattern);

				cairo_pattern_destroy(pattern);
			} else {
				cairo_paint(cr);
			}

			cairo_surface_destroy(surface);
		}
	} break;

	case POLO_DRAW_GTK_ARROW: {
		int rx, ry, rwidth, rheight;

		rx = parse_x_position_unchecked(op->data.gtk_arrow.x, env);
		ry = parse_y_position_unchecked(op->data.gtk_arrow.y, env);
		rwidth = parse_size_unchecked(op->data.gtk_arrow.width, env);
		rheight = parse_size_unchecked(op->data.gtk_arrow.height, env);

		double size = MAX(rwidth, rheight), angle = 0;

		switch (op->data.gtk_arrow.arrow) {
		case POLO_ARROW_UP:
			angle = 0;
			break;
		case POLO_ARROW_RIGHT:
			angle = M_PI / 2;
			break;
		case POLO_ARROW_DOWN:
			angle = M_PI;
			break;
		case POLO_ARROW_LEFT:
			angle = 3 * M_PI / 2;
			break;
		case POLO_ARROW_NONE:
			return;
		}

		polo_marco_render_arrow(style_gtk, op->data.gtk_arrow.state, cr,
					angle, rx, ry, size);
	} break;

	case POLO_DRAW_GTK_BOX: {
		int rx, ry, rwidth, rheight;

		rx = parse_x_position_unchecked(op->data.gtk_box.x, env);
		ry = parse_y_position_unchecked(op->data.gtk_box.y, env);
		rwidth = parse_size_unchecked(op->data.gtk_box.width, env);
		rheight = parse_size_unchecked(op->data.gtk_box.height, env);

		polo_marco_render_box(style_gtk, op->data.gtk_box.state, cr, rx,
				      ry, rwidth, rheight);
	} break;

	case POLO_DRAW_GTK_VLINE: {
		int rx, ry1, ry2;

		rx = parse_x_position_unchecked(op->data.gtk_vline.x, env);
		ry1 = parse_y_position_unchecked(op->data.gtk_vline.y1, env);
		ry2 = parse_y_position_unchecked(op->data.gtk_vline.y2, env);

		polo_marco_render_vline(style_gtk, op->data.gtk_vline.state, cr,
					rx, ry1, ry2);
	} break;

	case POLO_DRAW_ICON: {
		gint scale;
		gdouble rx, ry, rwidth, rheight;
		cairo_surface_t *surface;

		scale = 1;
		cairo_scale(cr, 1.0 / scale, 1.0 / scale);

		rwidth = parse_size_unchecked(op->data.icon.width, env) * scale;
		rheight =
			parse_size_unchecked(op->data.icon.height, env) * scale;

		surface = draw_op_as_surface(op, style_gtk, info, rwidth,
					     rheight);

		if (surface) {
			rx = parse_x_position_unchecked(op->data.icon.x, env) *
				scale;
			ry = parse_y_position_unchecked(op->data.icon.y, env) *
				scale;

			cairo_set_source_surface(cr, surface, rx, ry);

			if (op->data.icon.alpha_spec) {
				cairo_pattern_t *pattern;

				cairo_translate(cr, rx, ry);
				cairo_scale(cr, rwidth, rheight);

				pattern =
					polo_marco_alpha_gradient_spec_get_mask(
						op->data.icon.alpha_spec);
				cairo_mask(cr, pattern);

				cairo_pattern_destroy(pattern);
			} else {
				cairo_paint(cr);
			}

			cairo_surface_destroy(surface);
		}
	} break;

	case POLO_DRAW_TITLE:
		if (info->title_layout) {
			int rx, ry;
			PangoRectangle ink_rect, logical_rect;

			polo_marco_color_spec_render(op->data.title.color_spec,
						     style_gtk, &color);
			cairo_set_source_rgba(cr, color.red, color.green,
					      color.blue, color.alpha);

			rx = parse_x_position_unchecked(op->data.title.x, env);
			ry = parse_y_position_unchecked(op->data.title.y, env);

			if (op->data.title.ellipsize_width) {
				int ellipsize_width;
				int right_bearing;

				ellipsize_width = parse_x_position_unchecked(
					op->data.title.ellipsize_width, env);
				/* HACK: parse_x_position_unchecked adds in
				 * env->rect.x, subtract out again */
				ellipsize_width -= env->rect.x;

				pango_layout_set_width(info->title_layout, -1);
				pango_layout_get_pixel_extents(
					info->title_layout, &ink_rect,
					&logical_rect);

				/* Pango's idea of ellipsization is with respect
				 * to the logical rect. correct for this, by
				 * reducing the ellipsization width by the
				 * overflow of the un-ellipsized text on the
				 * right... it's always the visual right we want
				 * regardless of bidi, since since the X we pass
				 * in to cairo_move_to() is always the left edge
				 * of the line.
				 */
				right_bearing = (ink_rect.x + ink_rect.width) -
					(logical_rect.x + logical_rect.width);
				right_bearing = MAX(right_bearing, 0);

				ellipsize_width -= right_bearing;
				ellipsize_width = MAX(ellipsize_width, 0);

				/* Only ellipsizing when necessary is a
				 * performance optimization -
				 * pango_layout_set_width() will force a
				 * relayout if it isn't the same as the current
				 * width of -1.
				 */
				if (ellipsize_width < logical_rect.width)
					pango_layout_set_width(
						info->title_layout,
						PANGO_SCALE * ellipsize_width);
			} else if (rx - env->rect.x + env->title_width >=
				   env->rect.width) {
				const double alpha_margin = 30.0;
				int text_space = env->rect.x + env->rect.width -
					(rx - env->rect.x) - env->right_width;

				double startalpha = 1.0 -
					(alpha_margin / ((double)text_space));

				cairo_pattern_t *linpat;
				linpat = cairo_pattern_create_linear(
					rx, ry, text_space, env->title_height);
				cairo_pattern_add_color_stop_rgba(
					linpat, 0, color.red, color.green,
					color.blue, color.alpha);
				cairo_pattern_add_color_stop_rgba(
					linpat, startalpha, color.red,
					color.green, color.blue, color.alpha);
				cairo_pattern_add_color_stop_rgba(
					linpat, 1, color.red, color.green,
					color.blue, 0);
				cairo_set_source(cr, linpat);
				cairo_pattern_destroy(linpat);
			}

			cairo_move_to(cr, rx, ry);
			pango_cairo_show_layout(cr, info->title_layout);

			/* Remove any ellipsization we might have set; will
			 * short-circuit if the width is already -1 */
			pango_layout_set_width(info->title_layout, -1);
		}
		break;

	case POLO_DRAW_OP_LIST: {
		PoloRect d_rect;

		d_rect.x = parse_x_position_unchecked(op->data.op_list.x, env);
		d_rect.y = parse_y_position_unchecked(op->data.op_list.y, env);
		d_rect.width =
			parse_size_unchecked(op->data.op_list.width, env);
		d_rect.height =
			parse_size_unchecked(op->data.op_list.height, env);

		polo_marco_draw_op_list_draw_with_style(
			op->data.op_list.op_list, style_gtk, cr, info, d_rect);
	} break;

	case POLO_DRAW_TILE: {
		int rx, ry, rwidth, rheight;
		int tile_xoffset, tile_yoffset;
		PoloRect tile;

		rx = parse_x_position_unchecked(op->data.tile.x, env);
		ry = parse_y_position_unchecked(op->data.tile.y, env);
		rwidth = parse_size_unchecked(op->data.tile.width, env);
		rheight = parse_size_unchecked(op->data.tile.height, env);

		cairo_save(cr);

		cairo_rectangle(cr, rx, ry, rwidth, rheight);
		cairo_clip(cr);

		tile_xoffset = parse_x_position_unchecked(
			op->data.tile.tile_xoffset, env);
		tile_yoffset = parse_y_position_unchecked(
			op->data.tile.tile_yoffset, env);
		/* tile offset should not include x/y */
		tile_xoffset -= rect.x;
		tile_yoffset -= rect.y;

		tile.width =
			parse_size_unchecked(op->data.tile.tile_width, env);
		tile.height =
			parse_size_unchecked(op->data.tile.tile_height, env);

		tile.x = rx - tile_xoffset;

		while (tile.x < (rx + rwidth)) {
			tile.y = ry - tile_yoffset;
			while (tile.y < (ry + rheight)) {
				polo_marco_draw_op_list_draw_with_style(
					op->data.tile.op_list, style_gtk, cr,
					info, tile);

				tile.y += tile.height;
			}

			tile.x += tile.width;
		}
		cairo_restore(cr);
	} break;
	}

	cairo_restore(cr);
}
void
polo_marco_draw_op_draw_with_style(const PoloDrawOp *op,
				   PoloThemeColors *style_gtk, cairo_t *cr,
				   const PoloDrawInfo *info,
				   PoloRect logical_region)
{
	PoloPositionExprEnv env;

	fill_env(&env, info, logical_region);

	polo_marco_draw_op_draw_with_env(op, style_gtk, cr, info,
					 logical_region, &env);
}
void
polo_marco_draw_op_list_draw_with_style(const PoloDrawOpList *op_list,
					PoloThemeColors *style_gtk, cairo_t *cr,
					const PoloDrawInfo *info, PoloRect rect)
{
	/* BOOKMARK */

	int i;
	PoloPositionExprEnv env;

	if (op_list->n_ops == 0)
		return;

	fill_env(&env, info, rect);

	/* FIXME this can be optimized, potentially a lot, by
	 * compressing multiple ops when possible. For example,
	 * anything convertible to a pixbuf can be composited
	 * client-side, and putting a color tint over a pixbuf
	 * can be done without creating the solid-color pixbuf.
	 *
	 * To implement this my plan is to have the idea of a
	 * compiled draw op (with the string expressions already
	 * evaluated), we make an array of those, and then fold
	 * adjacent items when possible.
	 */

	cairo_save(cr);

	for (i = 0; i < op_list->n_ops; i++) {
		PoloDrawOp *op = op_list->ops[i];

		if (op->type == POLO_DRAW_CLIP) {
			cairo_restore(cr);

			cairo_rectangle(
				cr,
				parse_x_position_unchecked(op->data.clip.x,
							   &env),
				parse_y_position_unchecked(op->data.clip.y,
							   &env),
				parse_size_unchecked(op->data.clip.width, &env),
				parse_size_unchecked(op->data.clip.height,
						     &env));
			cairo_clip(cr);

			cairo_save(cr);
		} else if (polo_marco_cairo_has_clip(cr)) {
			polo_marco_draw_op_draw_with_env(op, style_gtk, cr,
							 info, rect, &env);
		}
	}

	cairo_restore(cr);
}
static PoloButtonState
map_button_state(PoloButtonType button_type, const PoloFrameGeometry *fgeom,
		 int middle_bg_offset,
		 PoloButtonState button_states[POLO_BUTTON_TYPE_LAST])
{
	PoloButtonFunction function = POLO_BUTTON_FUNCTION_LAST;

	switch (button_type) {
	/* First handle functions, which map directly */
	case POLO_BUTTON_TYPE_SHADE:
	case POLO_BUTTON_TYPE_ABOVE:
	case POLO_BUTTON_TYPE_STICK:
	case POLO_BUTTON_TYPE_UNSHADE:
	case POLO_BUTTON_TYPE_UNABOVE:
	case POLO_BUTTON_TYPE_UNSTICK:
	case POLO_BUTTON_TYPE_MENU:
	case POLO_BUTTON_TYPE_APPMENU:
	case POLO_BUTTON_TYPE_MINIMIZE:
	case POLO_BUTTON_TYPE_MAXIMIZE:
	case POLO_BUTTON_TYPE_CLOSE:
		return button_states[button_type];

	/* Map position buttons to the corresponding function */
	case POLO_BUTTON_TYPE_RIGHT_LEFT_BACKGROUND:
	case POLO_BUTTON_TYPE_RIGHT_SINGLE_BACKGROUND:
		if (fgeom->n_right_buttons > 0)
			function = fgeom->button_layout.right_buttons[0];
		break;
	case POLO_BUTTON_TYPE_RIGHT_RIGHT_BACKGROUND:
		if (fgeom->n_right_buttons > 0)
			function =
				fgeom->button_layout
					.right_buttons[fgeom->n_right_buttons -
						       1];
		break;
	case POLO_BUTTON_TYPE_RIGHT_MIDDLE_BACKGROUND:
		if (middle_bg_offset + 1 < fgeom->n_right_buttons)
			function = fgeom->button_layout
					   .right_buttons[middle_bg_offset + 1];
		break;
	case POLO_BUTTON_TYPE_LEFT_LEFT_BACKGROUND:
	case POLO_BUTTON_TYPE_LEFT_SINGLE_BACKGROUND:
		if (fgeom->n_left_buttons > 0)
			function = fgeom->button_layout.left_buttons[0];
		break;
	case POLO_BUTTON_TYPE_LEFT_RIGHT_BACKGROUND:
		if (fgeom->n_left_buttons > 0)
			function = fgeom->button_layout
					   .left_buttons[fgeom->n_left_buttons -
							 1];
		break;
	case POLO_BUTTON_TYPE_LEFT_MIDDLE_BACKGROUND:
		if (middle_bg_offset + 1 < fgeom->n_left_buttons)
			function = fgeom->button_layout
					   .left_buttons[middle_bg_offset + 1];
		break;
	case POLO_BUTTON_TYPE_LAST:
		break;
	}

	if (function != POLO_BUTTON_FUNCTION_LAST)
		return button_states[map_button_function_to_type(function)];

	return POLO_BUTTON_STATE_LAST;
}
static void
get_button_rect(PoloButtonType type, const PoloFrameGeometry *fgeom,
		int middle_background_offset, PoloRect *rect)
{
	switch (type) {
	case POLO_BUTTON_TYPE_LEFT_LEFT_BACKGROUND:
		*rect = fgeom->left_left_background;
		break;

	case POLO_BUTTON_TYPE_LEFT_MIDDLE_BACKGROUND:
		*rect = fgeom->left_middle_backgrounds
				[middle_background_offset];
		break;

	case POLO_BUTTON_TYPE_LEFT_RIGHT_BACKGROUND:
		*rect = fgeom->left_right_background;
		break;

	case POLO_BUTTON_TYPE_LEFT_SINGLE_BACKGROUND:
		*rect = fgeom->left_single_background;
		break;

	case POLO_BUTTON_TYPE_RIGHT_LEFT_BACKGROUND:
		*rect = fgeom->right_left_background;
		break;

	case POLO_BUTTON_TYPE_RIGHT_MIDDLE_BACKGROUND:
		*rect = fgeom->right_middle_backgrounds
				[middle_background_offset];
		break;

	case POLO_BUTTON_TYPE_RIGHT_RIGHT_BACKGROUND:
		*rect = fgeom->right_right_background;
		break;

	case POLO_BUTTON_TYPE_RIGHT_SINGLE_BACKGROUND:
		*rect = fgeom->right_single_background;
		break;

	case POLO_BUTTON_TYPE_CLOSE:
		*rect = fgeom->close_rect.visible;
		break;

	case POLO_BUTTON_TYPE_SHADE:
		*rect = fgeom->shade_rect.visible;
		break;

	case POLO_BUTTON_TYPE_UNSHADE:
		*rect = fgeom->unshade_rect.visible;
		break;

	case POLO_BUTTON_TYPE_ABOVE:
		*rect = fgeom->above_rect.visible;
		break;

	case POLO_BUTTON_TYPE_UNABOVE:
		*rect = fgeom->unabove_rect.visible;
		break;

	case POLO_BUTTON_TYPE_STICK:
		*rect = fgeom->stick_rect.visible;
		break;

	case POLO_BUTTON_TYPE_UNSTICK:
		*rect = fgeom->unstick_rect.visible;
		break;

	case POLO_BUTTON_TYPE_MAXIMIZE:
		*rect = fgeom->max_rect.visible;
		break;

	case POLO_BUTTON_TYPE_MINIMIZE:
		*rect = fgeom->min_rect.visible;
		break;

	case POLO_BUTTON_TYPE_MENU:
		*rect = fgeom->menu_rect.visible;
		break;

	case POLO_BUTTON_TYPE_APPMENU:
		*rect = fgeom->appmenu_rect.visible;
		break;

	case POLO_BUTTON_TYPE_LAST:
		g_assert_not_reached();
		break;
	}
}
void
polo_marco_frame_style_draw_with_style(
	PoloTheme *theme, PoloFrameStyle *style, PoloThemeColors *style_gtk,
	cairo_t *cr, const PoloFrameGeometry *fgeom, int client_width,
	int client_height, PangoLayout *title_layout, int text_height,
	PoloButtonState button_states[POLO_BUTTON_TYPE_LAST],
	GdkPixbuf *mini_icon, GdkPixbuf *icon)
{
	/* BOOKMARK */
	int i, j;
	PoloRect visible_rect;
	PoloRect titlebar_rect;
	PoloRect left_titlebar_edge;
	PoloRect right_titlebar_edge;
	PoloRect bottom_titlebar_edge;
	PoloRect top_titlebar_edge;
	PoloRect left_edge, right_edge, bottom_edge;
	PangoRectangle extents;
	PoloDrawInfo draw_info;
	const PoloThemeBorders *borders;

	borders = &fgeom->borders;

	visible_rect.x = borders->invisible.left;
	visible_rect.y = borders->invisible.top;
	visible_rect.width = fgeom->width - borders->invisible.left -
		borders->invisible.right;
	visible_rect.height = fgeom->height - borders->invisible.top -
		borders->invisible.bottom;

	titlebar_rect.x = visible_rect.x;
	titlebar_rect.y = visible_rect.y;
	titlebar_rect.width = visible_rect.width;
	titlebar_rect.height = borders->visible.top;

	left_titlebar_edge.x = titlebar_rect.x;
	left_titlebar_edge.y = titlebar_rect.y + fgeom->top_titlebar_edge;
	left_titlebar_edge.width = fgeom->left_titlebar_edge;
	left_titlebar_edge.height = titlebar_rect.height -
		fgeom->top_titlebar_edge - fgeom->bottom_titlebar_edge;

	right_titlebar_edge.y = left_titlebar_edge.y;
	right_titlebar_edge.height = left_titlebar_edge.height;
	right_titlebar_edge.width = fgeom->right_titlebar_edge;
	right_titlebar_edge.x = titlebar_rect.x + titlebar_rect.width -
		right_titlebar_edge.width;

	top_titlebar_edge.x = titlebar_rect.x;
	top_titlebar_edge.y = titlebar_rect.y;
	top_titlebar_edge.width = titlebar_rect.width;
	top_titlebar_edge.height = fgeom->top_titlebar_edge;

	bottom_titlebar_edge.x = titlebar_rect.x;
	bottom_titlebar_edge.width = titlebar_rect.width;
	bottom_titlebar_edge.height = fgeom->bottom_titlebar_edge;
	bottom_titlebar_edge.y = titlebar_rect.y + titlebar_rect.height -
		bottom_titlebar_edge.height;

	left_edge.x = visible_rect.x;
	left_edge.y = visible_rect.y + borders->visible.top;
	left_edge.width = borders->visible.left;
	left_edge.height = visible_rect.height - borders->visible.top -
		borders->visible.bottom;

	right_edge.x =
		visible_rect.x + visible_rect.width - borders->visible.right;
	right_edge.y = visible_rect.y + borders->visible.top;
	right_edge.width = borders->visible.right;
	right_edge.height = visible_rect.height - borders->visible.top -
		borders->visible.bottom;

	bottom_edge.x = visible_rect.x;
	bottom_edge.y =
		visible_rect.y + visible_rect.height - borders->visible.bottom;
	bottom_edge.width = visible_rect.width;
	bottom_edge.height = borders->visible.bottom;

	if (title_layout)
		pango_layout_get_pixel_extents(title_layout, NULL, &extents);

	draw_info.mini_icon = mini_icon;
	draw_info.icon = icon;
	draw_info.title_layout = title_layout;
	draw_info.title_layout_width = title_layout ? extents.width : 0;
	draw_info.title_layout_height = title_layout ? extents.height : 0;
	draw_info.fgeom = fgeom;
	draw_info.theme = theme;

	/* The enum is in the order the pieces should be rendered. */
	i = 0;
	while (i < POLO_FRAME_PIECE_LAST) {
		PoloRect rect;

		switch ((PoloFramePiece)i) {
		case POLO_FRAME_PIECE_ENTIRE_BACKGROUND:
			rect = visible_rect;
			break;

		case POLO_FRAME_PIECE_TITLEBAR:
			rect = titlebar_rect;
			break;

		case POLO_FRAME_PIECE_LEFT_TITLEBAR_EDGE:
			rect = left_titlebar_edge;
			break;

		case POLO_FRAME_PIECE_RIGHT_TITLEBAR_EDGE:
			rect = right_titlebar_edge;
			break;

		case POLO_FRAME_PIECE_TOP_TITLEBAR_EDGE:
			rect = top_titlebar_edge;
			break;

		case POLO_FRAME_PIECE_BOTTOM_TITLEBAR_EDGE:
			rect = bottom_titlebar_edge;
			break;

		case POLO_FRAME_PIECE_TITLEBAR_MIDDLE:
			rect.x =
				left_titlebar_edge.x + left_titlebar_edge.width;
			rect.y = top_titlebar_edge.y + top_titlebar_edge.height;
			rect.width = titlebar_rect.width -
				left_titlebar_edge.width -
				right_titlebar_edge.width;
			rect.height = titlebar_rect.height -
				top_titlebar_edge.height -
				bottom_titlebar_edge.height;
			break;

		case POLO_FRAME_PIECE_TITLE:
			rect = fgeom->title_rect;
			break;

		case POLO_FRAME_PIECE_LEFT_EDGE:
			rect = left_edge;
			break;

		case POLO_FRAME_PIECE_RIGHT_EDGE:
			rect = right_edge;
			break;

		case POLO_FRAME_PIECE_BOTTOM_EDGE:
			rect = bottom_edge;
			break;

		case POLO_FRAME_PIECE_OVERLAY:
			rect = visible_rect;
			break;

		case POLO_FRAME_PIECE_LAST:
			g_assert_not_reached();
			break;
		}

		cairo_save(cr);

		cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
		cairo_clip(cr);

		if (polo_marco_cairo_has_clip(cr)) {
			PoloDrawOpList *op_list;
			PoloFrameStyle *parent;

			parent = style;
			op_list = NULL;
			while (parent && op_list == NULL) {
				op_list = parent->pieces[i];
				parent = parent->parent;
			}

			if (op_list) {
				PoloRect m_rect;
				m_rect = polo_marco_rect(rect.x, rect.y,
							 rect.width,
							 rect.height);
				polo_marco_draw_op_list_draw_with_style(
					op_list, style_gtk, cr, &draw_info,
					m_rect);
			}
		}

		cairo_restore(cr);

		/* Draw buttons just before overlay */
		if ((i + 1) == POLO_FRAME_PIECE_OVERLAY) {
			PoloDrawOpList *op_list;
			int middle_bg_offset;

			middle_bg_offset = 0;
			j = 0;
			while (j < POLO_BUTTON_TYPE_LAST) {
				PoloButtonState button_state;

				get_button_rect(j, fgeom, middle_bg_offset,
						&rect);

				button_state = map_button_state(
					j, fgeom, middle_bg_offset,
					button_states);
				op_list = get_button(style, j, button_state);

				if (op_list) {
					cairo_save(cr);
					cairo_rectangle(cr, rect.x, rect.y,
							rect.width,
							rect.height);
					cairo_clip(cr);

					if (polo_marco_cairo_has_clip(cr)) {
						PoloRect m_rect;

						m_rect = polo_marco_rect(
							rect.x, rect.y,
							rect.width,
							rect.height);

						polo_marco_draw_op_list_draw_with_style(
							op_list, style_gtk, cr,
							&draw_info, m_rect);
					}

					cairo_restore(cr);
				}

				/* MIDDLE_BACKGROUND type may get drawn more
				 * than once */
				if ((j == POLO_BUTTON_TYPE_RIGHT_MIDDLE_BACKGROUND ||
				     j == POLO_BUTTON_TYPE_LEFT_MIDDLE_BACKGROUND) &&
				    middle_bg_offset + 1 <
					    MAX_MIDDLE_BACKGROUNDS) {
					++middle_bg_offset;
				} else {
					middle_bg_offset = 0;
					++j;
				}
			}
		}

		++i;
	}
}
double
polo_marco_theme_get_title_scale(PoloTheme *theme, PoloFrameType type,
				 PoloFrameFlags flags)
{
	PoloFrameStyle *style;

	g_return_val_if_fail(type < POLO_FRAME_TYPE_LAST, 1.0);

	style = theme_get_style(theme, type, flags);

	/* Parser is not supposed to allow this currently */
	if (style == NULL)
		return 1.0;

	return style->layout->title_scale;
}
void
polo_marco_theme_draw_frame(
	PoloTheme *theme, PoloThemeColors *style_gtk, cairo_t *cr,
	PoloFrameType type, PoloFrameFlags flags, int client_width,
	int client_height, PangoLayout *title_layout, int text_height,
	const PoloButtonLayout *button_layout,
	PoloButtonState button_states[POLO_BUTTON_TYPE_LAST],
	GdkPixbuf *mini_icon, GdkPixbuf *icon)
{
	PoloFrameGeometry fgeom;
	PoloFrameStyle *style;

	g_return_if_fail(type < POLO_FRAME_TYPE_LAST);

	style = theme_get_style(theme, type, flags);

	/* Parser is not supposed to allow this currently */
	if (style == NULL)
		return;

	polo_marco_frame_layout_calc_geometry(style->layout, text_height, flags,
					      client_width, client_height,
					      button_layout, &fgeom, theme);

	polo_marco_frame_style_draw_with_style(
		theme, style, style_gtk, cr, &fgeom, client_width,
		client_height, title_layout, text_height, button_states,
		mini_icon, icon);
}
/**
 * Takes a colour "a", scales the lightness and saturation by a certain amount,
 * and sets "b" to the resulting colour.
 * gtkstyle.c cut-and-pastage.
 *
 * \param a  the starting colour
 * \param b  [out] the resulting colour
 * \param k  amount to scale lightness and saturation by
 */
/**
 * Converts a red/green/blue triplet to a hue/lightness/saturation triplet.
 *
 * \param r  on input, red; on output, hue
 * \param g  on input, green; on output, lightness
 * \param b  on input, blue; on output, saturation
 */
/**
 * Converts a hue/lightness/saturation triplet to a red/green/blue triplet.
 *
 * \param h  on input, hue; on output, red
 * \param l  on input, lightness; on output, green
 * \param s  on input, saturation; on output, blue
 */
/* These are some functions I'm saving to use in optimizing
 * PoloDrawOpList, namely to pre-composite pixbufs on client side
 * prior to rendering to the server
 */

static void
gtk_style_shade(PoloRGBA *a, PoloRGBA *b, gdouble k)
{
	gdouble red;
	gdouble green;
	gdouble blue;

	red = a->red;
	green = a->green;
	blue = a->blue;

	rgb_to_hls(&red, &green, &blue);

	green *= k;
	if (green > 1.0)
		green = 1.0;
	else if (green < 0.0)
		green = 0.0;

	blue *= k;
	if (blue > 1.0)
		blue = 1.0;
	else if (blue < 0.0)
		blue = 0.0;

	hls_to_rgb(&red, &green, &blue);

	b->red = red;
	b->green = green;
	b->blue = blue;
}

static void
rgb_to_hls(gdouble *r, gdouble *g, gdouble *b)
{
	gdouble min;
	gdouble max;
	gdouble red;
	gdouble green;
	gdouble blue;
	gdouble h, l, s;
	gdouble delta;

	red = *r;
	green = *g;
	blue = *b;

	if (red > green) {
		if (red > blue)
			max = red;
		else
			max = blue;

		if (green < blue)
			min = green;
		else
			min = blue;
	} else {
		if (green > blue)
			max = green;
		else
			max = blue;

		if (red < blue)
			min = red;
		else
			min = blue;
	}

	l = (max + min) / 2;
	s = 0;
	h = 0;

	if (max != min) {
		if (l <= 0.5)
			s = (max - min) / (max + min);
		else
			s = (max - min) / (2 - max - min);

		delta = max - min;
		if (red == max)
			h = (green - blue) / delta;
		else if (green == max)
			h = 2 + (blue - red) / delta;
		else if (blue == max)
			h = 4 + (red - green) / delta;

		h *= 60;
		if (h < 0.0)
			h += 360;
	}

	*r = h;
	*g = l;
	*b = s;
}

static void
hls_to_rgb(gdouble *h, gdouble *l, gdouble *s)
{
	gdouble hue;
	gdouble lightness;
	gdouble saturation;
	gdouble m1, m2;
	gdouble r, g, b;

	lightness = *l;
	saturation = *s;

	if (lightness <= 0.5)
		m2 = lightness * (1 + saturation);
	else
		m2 = lightness + saturation - lightness * saturation;
	m1 = 2 * lightness - m2;

	if (saturation == 0) {
		*h = lightness;
		*l = lightness;
		*s = lightness;
	} else {
		hue = *h + 120;
		while (hue > 360)
			hue -= 360;
		while (hue < 0)
			hue += 360;

		if (hue < 60)
			r = m1 + (m2 - m1) * hue / 60;
		else if (hue < 180)
			r = m2;
		else if (hue < 240)
			r = m1 + (m2 - m1) * (240 - hue) / 60;
		else
			r = m1;

		hue = *h;
		while (hue > 360)
			hue -= 360;
		while (hue < 0)
			hue += 360;

		if (hue < 60)
			g = m1 + (m2 - m1) * hue / 60;
		else if (hue < 180)
			g = m2;
		else if (hue < 240)
			g = m1 + (m2 - m1) * (240 - hue) / 60;
		else
			g = m1;

		hue = *h - 120;
		while (hue > 360)
			hue -= 360;
		while (hue < 0)
			hue += 360;

		if (hue < 60)
			b = m1 + (m2 - m1) * hue / 60;
		else if (hue < 180)
			b = m2;
		else if (hue < 240)
			b = m1 + (m2 - m1) * (240 - hue) / 60;
		else
			b = m1;

		*h = r;
		*l = g;
		*s = b;
	}
}
