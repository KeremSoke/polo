/*
 * Copyright (C) 2000 Eazel, Inc.
 * Copyright (C) 2007-2008 Red Hat, Inc.
 * Copyright (C) 2012 Jasmine Hassan <jasmine.aura@gmail.com>
 * Copyright (C) 2012-2021 MATE Developers
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * Ported from mate-desktop 1.28.2 libmate-desktop/mate-bg.c (commit
 * fee79b7559965d8ae3b4b8e213717b525c703c1b): draw_color_area,
 * pixbuf_draw_gradient, create_gradient, get_scaled_pixbuf,
 * pixbuf_clip_to_fit, pixbuf_scale_to_fit, pixbuf_scale_to_min,
 * pixbuf_blend, pixbuf_tile, draw_image_area, get_pixbuf_for_size,
 * get_as_pixbuf_for_size, blend, find_best_size, get_slide_timeout,
 * get_current_slide and the slideshow parser keep MateBG's behaviour,
 * quirks included (the parser's "hour - 1"). Dropped: GSettings handling
 * (src/settings/background.vala), the on-disk cache, thumbnails, file
 * monitors and the X root pixmap. New: drawing one output of the screen
 * at a time (tile phase and spanned offset from the screen origin).
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <pango/pango.h>

#include "bg.h"

#define CACHE_SIZE 4

typedef struct {
	double red, green, blue;
} BgColor;

typedef struct {
	gint width;
	gint height;
	char *file;
} FileSize;

typedef struct {
	double duration; /* in seconds */
	gboolean fixed;
	GSList *file1;
	GSList *file2; /* NULL if fixed is TRUE */
} Slide;

typedef struct {
	double start_time;
	double total_duration;
	GQueue *slides;
	gboolean has_multiple_sizes;
	/* used during parsing */
	struct tm start_tm;
	GQueue *stack;
} SlideShow;

struct _PoloBg {
	gint ref_count;
	char *filename;
	PoloBgPlacement placement;
	PoloBgColorType color_type;
	BgColor primary;
	BgColor secondary;

	/* Decoded images by file name, at most CACHE_SIZE (MateBG's). */
	GHashTable *cache;
	gboolean slideshow_tried;
	SlideShow *slideshow;
	guint timeout;
};

/* ---- Colours ----------------------------------------------------------- */

/* MateBG color_from_string: gdk_rgba_parse () falls back to
 * pango_color_parse () for everything MATE stores; black if unparsable. */
static void
color_from_string(const char *string, BgColor *color)
{
	PangoColor pc;

	if (!string || *string == '\0' || !pango_color_parse(&pc, string))
		pango_color_parse(&pc, "#000000");
	color->red = pc.red / 65535.0;
	color->green = pc.green / 65535.0;
	color->blue = pc.blue / 65535.0;
}

static guchar *
create_gradient(const BgColor *primary, const BgColor *secondary, int n_pixels)
{
	guchar *result = g_malloc(n_pixels * 3);
	int i;

	for (i = 0; i < n_pixels; ++i) {
		double ratio = (i + 0.5) / n_pixels;

		result[3 * i + 0] = (guchar)((primary->red * (1 - ratio) +
					      secondary->red * ratio) *
					     0x100);
		result[3 * i + 1] = (guchar)((primary->green * (1 - ratio) +
					      secondary->green * ratio) *
					     0x100);
		result[3 * i + 2] = (guchar)((primary->blue * (1 - ratio) +
					      secondary->blue * ratio) *
					     0x100);
	}
	return result;
}

static void
pixbuf_draw_gradient(GdkPixbuf *pixbuf, gboolean horizontal,
		     const BgColor *primary, const BgColor *secondary,
		     int extent, int offset)
{
	int width = gdk_pixbuf_get_width(pixbuf);
	int height = gdk_pixbuf_get_height(pixbuf);
	int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
	guchar *dst = gdk_pixbuf_get_pixels(pixbuf);
	int n_channels = 3;
	guchar *gradient = create_gradient(primary, secondary, extent);
	int i;

	if (horizontal) {
		for (i = 0; i < height; i++) {
			guchar *d = dst + rowstride * i;
			int j;

			for (j = 0; j < width; j++) {
				int g = CLAMP(j + offset, 0, extent - 1);

				memcpy(d + j * n_channels,
				       gradient + g * n_channels, n_channels);
			}
		}
	} else {
		for (i = 0; i < height; i++) {
			guchar *d = dst + rowstride * i;
			guchar *gb = gradient +
				n_channels * CLAMP(i + offset, 0, extent - 1);
			int j, k;

			for (j = width; j > 0; j--) {
				for (k = 0; k < n_channels; k++)
					*(d++) = gb[k];
			}
		}
	}
	g_free(gradient);
}

static void
draw_color_area(PoloBg *bg, GdkPixbuf *dest, int extent_width,
		int extent_height, int offset_x, int offset_y)
{
	guint32 pixel;

	switch (bg->color_type) {
	case POLO_BG_COLOR_SOLID:
		pixel = ((guint)(bg->primary.red * 0xff) << 24) |
			((guint)(bg->primary.green * 0xff) << 16) |
			((guint)(bg->primary.blue * 0xff) << 8) | (0xff);
		gdk_pixbuf_fill(dest, pixel);
		break;
	case POLO_BG_COLOR_H_GRADIENT:
		pixbuf_draw_gradient(dest, TRUE, &bg->primary, &bg->secondary,
				     extent_width, offset_x);
		break;
	case POLO_BG_COLOR_V_GRADIENT:
		pixbuf_draw_gradient(dest, FALSE, &bg->primary, &bg->secondary,
				     extent_height, offset_y);
		break;
	default:
		break;
	}
}

/* ---- Pixbuf helpers --------------------------------------------------------
 */

static GdkPixbuf *
pixbuf_clip_to_fit(GdkPixbuf *src, int max_width, int max_height)
{
	int src_width = gdk_pixbuf_get_width(src);
	int src_height = gdk_pixbuf_get_height(src);
	int w, h, src_x, src_y;
	GdkPixbuf *pixbuf;

	if (src_width < max_width && src_height < max_height)
		return g_object_ref(src);

	w = MIN(src_width, max_width);
	h = MIN(src_height, max_height);
	pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
				gdk_pixbuf_get_has_alpha(src), 8, w, h);
	src_x = (src_width - w) / 2;
	src_y = (src_height - h) / 2;
	gdk_pixbuf_copy_area(src, src_x, src_y, w, h, pixbuf, 0, 0);
	return pixbuf;
}

static GdkPixbuf *
pixbuf_scale_to_fit(GdkPixbuf *src, int max_width, int max_height)
{
	int src_width = gdk_pixbuf_get_width(src);
	int src_height = gdk_pixbuf_get_height(src);
	double factor = MIN(max_width / (double)src_width,
			    max_height / (double)src_height);
	int new_width = floor(src_width * factor + 0.5);
	int new_height = floor(src_height * factor + 0.5);

	return gdk_pixbuf_scale_simple(src, new_width, new_height,
				       GDK_INTERP_BILINEAR);
}

static GdkPixbuf *
pixbuf_scale_to_min(GdkPixbuf *src, int min_width, int min_height)
{
	int src_width = gdk_pixbuf_get_width(src);
	int src_height = gdk_pixbuf_get_height(src);
	double factor = MAX(min_width / (double)src_width,
			    min_height / (double)src_height);
	int new_width = floor(src_width * factor + 0.5);
	int new_height = floor(src_height * factor + 0.5);
	GdkPixbuf *dest;

	dest = gdk_pixbuf_new(GDK_COLORSPACE_RGB, gdk_pixbuf_get_has_alpha(src),
			      8, min_width, min_height);
	if (!dest)
		return NULL;
	gdk_pixbuf_scale(src, dest, 0, 0, min_width, min_height,
			 (new_width - min_width) / -2,
			 (new_height - min_height) / -2, factor, factor,
			 GDK_INTERP_BILINEAR);
	return dest;
}

static GdkPixbuf *
get_scaled_pixbuf(PoloBgPlacement placement, GdkPixbuf *pixbuf, int width,
		  int height, int *x, int *y, int *w, int *h)
{
	GdkPixbuf *new;

	switch (placement) {
	case POLO_BG_PLACEMENT_SPANNED:
		new = pixbuf_scale_to_fit(pixbuf, width, height);
		break;
	case POLO_BG_PLACEMENT_ZOOMED:
		new = pixbuf_scale_to_min(pixbuf, width, height);
		break;
	case POLO_BG_PLACEMENT_FILL_SCREEN:
		new = gdk_pixbuf_scale_simple(pixbuf, width, height,
					      GDK_INTERP_BILINEAR);
		break;
	case POLO_BG_PLACEMENT_SCALED:
		new = pixbuf_scale_to_fit(pixbuf, width, height);
		break;
	case POLO_BG_PLACEMENT_CENTERED:
	case POLO_BG_PLACEMENT_TILED:
	default:
		new = pixbuf_clip_to_fit(pixbuf, width, height);
		break;
	}
	*w = gdk_pixbuf_get_width(new);
	*h = gdk_pixbuf_get_height(new);
	*x = (width - *w) / 2;
	*y = (height - *h) / 2;
	return new;
}

static void
pixbuf_blend(GdkPixbuf *src, GdkPixbuf *dest, int src_x, int src_y,
	     int src_width, int src_height, int dest_x, int dest_y,
	     double alpha)
{
	int dest_width = gdk_pixbuf_get_width(dest);
	int dest_height = gdk_pixbuf_get_height(dest);
	int offset_x = dest_x - src_x;
	int offset_y = dest_y - src_y;

	if (src_width < 0)
		src_width = gdk_pixbuf_get_width(src);
	if (src_height < 0)
		src_height = gdk_pixbuf_get_height(src);
	/* Polo: an image left of or above dest (spanned, tile phase) keeps
	 * only the part inside dest. MateBG never draws there. */
	if (dest_x < 0) {
		src_width += dest_x;
		dest_x = 0;
	}
	if (dest_y < 0) {
		src_height += dest_y;
		dest_y = 0;
	}
	if (dest_x + src_width > dest_width)
		src_width = dest_width - dest_x;
	if (dest_y + src_height > dest_height)
		src_height = dest_height - dest_y;
	if (src_width <= 0 || src_height <= 0)
		return;
	gdk_pixbuf_composite(src, dest, dest_x, dest_y, src_width, src_height,
			     offset_x, offset_y, 1, 1, GDK_INTERP_NEAREST,
			     alpha * 0xFF + 0.5);
}

static void
pixbuf_tile(GdkPixbuf *src, GdkPixbuf *dest, int origin_x, int origin_y)
{
	int dest_width = gdk_pixbuf_get_width(dest);
	int dest_height = gdk_pixbuf_get_height(dest);
	int tile_width = gdk_pixbuf_get_width(src);
	int tile_height = gdk_pixbuf_get_height(src);
	int x, y;

	for (y = -(origin_y % tile_height); y < dest_height; y += tile_height) {
		for (x = -(origin_x % tile_width); x < dest_width;
		     x += tile_width)
			pixbuf_blend(src, dest, 0, 0, tile_width, tile_height,
				     x, y, 1.0);
	}
}

/* ---- Images and slideshows --------------------------------------------- */

static GdkPixbuf *
load_pixbuf(PoloBg *bg, const char *filename, int best_width, int best_height)
{
	GdkPixbufFormat *format;
	GdkPixbuf *pixbuf, *rotated;
	gchar *name = NULL;

	pixbuf = g_hash_table_lookup(bg->cache, filename);
	if (pixbuf)
		return g_object_ref(pixbuf);

	format = gdk_pixbuf_get_file_info(filename, NULL, NULL);
	if (format)
		name = gdk_pixbuf_format_get_name(format);
	if (g_strcmp0(name, "svg") == 0 && best_width > 0 && best_height > 0 &&
	    (bg->placement == POLO_BG_PLACEMENT_FILL_SCREEN ||
	     bg->placement == POLO_BG_PLACEMENT_SCALED ||
	     bg->placement == POLO_BG_PLACEMENT_ZOOMED))
		pixbuf = gdk_pixbuf_new_from_file_at_size(filename, best_width,
							  best_height, NULL);
	else
		pixbuf = gdk_pixbuf_new_from_file(filename, NULL);
	g_free(name);
	if (!pixbuf)
		return NULL;

	rotated = gdk_pixbuf_apply_embedded_orientation(pixbuf);
	g_object_unref(pixbuf);
	if (g_hash_table_size(bg->cache) >= CACHE_SIZE)
		g_hash_table_remove_all(bg->cache);
	g_hash_table_insert(bg->cache, g_strdup(filename),
			    g_object_ref(rotated));
	return rotated;
}

static double
now(void)
{
	return (double)(g_get_real_time() / (double)G_USEC_PER_SEC);
}

static Slide *
get_current_slide(SlideShow *show, double *alpha)
{
	double delta = fmod(now() - show->start_time, show->total_duration);
	double elapsed = 0;
	GList *list;

	if (delta < 0)
		delta += show->total_duration;
	for (list = show->slides->head; list != NULL; list = list->next) {
		Slide *slide = list->data;

		if (elapsed + slide->duration > delta) {
			if (alpha)
				*alpha = (delta - elapsed) /
					(double)slide->duration;
			return slide;
		}
		elapsed += slide->duration;
	}
	/* fmod keeps delta below the total; float rounding only. */
	return show->slides->tail->data;
}

static GdkPixbuf *
blend(GdkPixbuf *p1, GdkPixbuf *p2, double alpha)
{
	GdkPixbuf *result = gdk_pixbuf_copy(p1);
	GdkPixbuf *tmp;

	if (gdk_pixbuf_get_width(p2) != gdk_pixbuf_get_width(p1) ||
	    gdk_pixbuf_get_height(p2) != gdk_pixbuf_get_height(p1))
		tmp = gdk_pixbuf_scale_simple(p2, gdk_pixbuf_get_width(p1),
					      gdk_pixbuf_get_height(p1),
					      GDK_INTERP_BILINEAR);
	else
		tmp = g_object_ref(p2);
	pixbuf_blend(tmp, result, 0, 0, -1, -1, 0, 0, alpha);
	g_object_unref(tmp);
	return result;
}

static double
get_slide_timeout(Slide *slide)
{
	/* 64 steps per transition, as MateBG. */
	return slide->fixed ? slide->duration : slide->duration / 64.0;
}

static FileSize *
find_best_size(GSList *sizes, gint width, gint height)
{
	gdouble a = width / (gdouble)height, d, distance = 10000.0;
	FileSize *best = NULL;
	GSList *s;
	gint pass;

	for (pass = 0; pass < 2; pass++) {
		for (s = sizes; s; s = s->next) {
			FileSize *size = s->data;

			if (pass == 0 &&
			    (size->width < width || size->height < height))
				continue;
			d = fabs(a - size->width / (gdouble)size->height);
			if (d < distance) {
				distance = d;
				best = size;
			} else if (d == distance) {
				if (best &&
				    (abs(size->width - width) <
				     abs(best->width - width)))
					best = size;
			}
		}
		if (best)
			break;
	}
	return best;
}

/* -- The slideshow parser (MateBG's, unchanged in behaviour) -- */

static void
handle_start_element(GMarkupParseContext *context, const gchar *name,
		     const gchar **attr_names, const gchar **attr_values,
		     gpointer user_data, GError **err)
{
	SlideShow *parser = user_data;
	gint i;

	if (strcmp(name, "static") == 0 || strcmp(name, "transition") == 0) {
		Slide *slide = g_new0(Slide, 1);

		if (strcmp(name, "static") == 0)
			slide->fixed = TRUE;
		g_queue_push_tail(parser->slides, slide);
	} else if (strcmp(name, "size") == 0 && parser->slides->tail) {
		Slide *slide = parser->slides->tail->data;
		FileSize *size = g_new0(FileSize, 1);

		for (i = 0; attr_names[i]; i++) {
			if (strcmp(attr_names[i], "width") == 0)
				size->width = atoi(attr_values[i]);
			else if (strcmp(attr_names[i], "height") == 0)
				size->height = atoi(attr_values[i]);
		}
		if (parser->stack->tail &&
		    (strcmp(parser->stack->tail->data, "file") == 0 ||
		     strcmp(parser->stack->tail->data, "from") == 0))
			slide->file1 = g_slist_prepend(slide->file1, size);
		else if (parser->stack->tail &&
			 strcmp(parser->stack->tail->data, "to") == 0)
			slide->file2 = g_slist_prepend(slide->file2, size);
		else
			g_free(size);
	}
	g_queue_push_tail(parser->stack, g_strdup(name));
}

static void
handle_end_element(GMarkupParseContext *context, const gchar *name,
		   gpointer user_data, GError **err)
{
	SlideShow *parser = user_data;

	g_free(g_queue_pop_tail(parser->stack));
}

/* The element stack, innermost first, is exactly s1, s2, …, NULL. */
static gboolean
stack_is(SlideShow *parser, const char *s1, ...)
{
	GList *stack = NULL, *l1, *l2;
	const char *s;
	va_list args;

	stack = g_list_prepend(stack, (gpointer)s1);
	va_start(args, s1);
	while ((s = va_arg(args, const char *)))
		stack = g_list_prepend(stack, (gpointer)s);
	va_end(args);

	l1 = stack;
	l2 = parser->stack->head;
	while (l1 && l2) {
		if (strcmp(l1->data, l2->data) != 0) {
			g_list_free(stack);
			return FALSE;
		}
		l1 = l1->next;
		l2 = l2->next;
	}
	g_list_free(stack);
	return !l1 && !l2;
}

static int
parse_int(const char *text)
{
	return strtol(text, NULL, 0);
}

static FileSize *
file_size_new(const char *text)
{
	FileSize *fs = g_new(FileSize, 1);

	fs->width = -1;
	fs->height = -1;
	fs->file = g_strdup(text);
	return fs;
}

static gboolean
is_blank(const char *text)
{
	for (; *text; text++) {
		if (!g_ascii_isspace(*text))
			return FALSE;
	}
	return TRUE;
}

static void
handle_text(GMarkupParseContext *context, const gchar *text, gsize text_len,
	    gpointer user_data, GError **err)
{
	SlideShow *parser = user_data;
	Slide *slide = parser->slides->tail ? parser->slides->tail->data : NULL;
	FileSize *fs;

	if (stack_is(parser, "year", "starttime", "background", NULL))
		parser->start_tm.tm_year = parse_int(text) - 1900;
	else if (stack_is(parser, "month", "starttime", "background", NULL))
		parser->start_tm.tm_mon = parse_int(text) - 1;
	else if (stack_is(parser, "day", "starttime", "background", NULL))
		parser->start_tm.tm_mday = parse_int(text);
	else if (stack_is(parser, "hour", "starttime", "background", NULL))
		parser->start_tm.tm_hour = parse_int(text) - 1; /* sic */
	else if (stack_is(parser, "minute", "starttime", "background", NULL))
		parser->start_tm.tm_min = parse_int(text);
	else if (stack_is(parser, "second", "starttime", "background", NULL))
		parser->start_tm.tm_sec = parse_int(text);
	else if (stack_is(parser, "duration", "static", "background", NULL) ||
		 stack_is(parser, "duration", "transition", "background",
			  NULL)) {
		if (!slide)
			return;
		slide->duration = g_strtod(text, NULL);
		parser->total_duration += slide->duration;
	} else if (stack_is(parser, "file", "static", "background", NULL) ||
		   stack_is(parser, "from", "transition", "background", NULL)) {
		if (!slide || is_blank(text))
			return;
		slide->file1 =
			g_slist_prepend(slide->file1, file_size_new(text));
		if (slide->file1->next)
			parser->has_multiple_sizes = TRUE;
	} else if (stack_is(parser, "size", "file", "static", "background",
			    NULL) ||
		   stack_is(parser, "size", "from", "transition", "background",
			    NULL)) {
		if (!slide || !slide->file1)
			return;
		fs = slide->file1->data;
		g_free(fs->file);
		fs->file = g_strdup(text);
		if (slide->file1->next)
			parser->has_multiple_sizes = TRUE;
	} else if (stack_is(parser, "to", "transition", "background", NULL)) {
		if (!slide || is_blank(text))
			return;
		slide->file2 =
			g_slist_prepend(slide->file2, file_size_new(text));
		if (slide->file2->next)
			parser->has_multiple_sizes = TRUE;
	} else if (stack_is(parser, "size", "to", "transition", "background",
			    NULL)) {
		if (!slide || !slide->file2)
			return;
		fs = slide->file2->data;
		g_free(fs->file);
		fs->file = g_strdup(text);
		if (slide->file2->next)
			parser->has_multiple_sizes = TRUE;
	}
}

static void
file_sizes_free(GSList *sizes)
{
	for (GSList *l = sizes; l; l = l->next) {
		FileSize *size = l->data;

		g_free(size->file);
		g_free(size);
	}
	g_slist_free(sizes);
}

static void
slideshow_free(SlideShow *show)
{
	GList *list;

	if (!show)
		return;
	for (list = show->slides->head; list; list = list->next) {
		Slide *slide = list->data;

		file_sizes_free(slide->file1);
		file_sizes_free(slide->file2);
		g_free(slide);
	}
	g_queue_free(show->slides);
	g_queue_free_full(show->stack, g_free);
	g_free(show);
}

static SlideShow *
read_slideshow_file(const char *filename)
{
	GMarkupParser parser = {
		handle_start_element,
		handle_end_element,
		handle_text,
		NULL,
		NULL,
	};
	GMarkupParseContext *context;
	SlideShow *show;
	char *contents = NULL;
	gsize len;
	time_t epoch = 0;

	if (!filename || !g_file_get_contents(filename, &contents, &len, NULL))
		return NULL;

	show = g_new0(SlideShow, 1);
	localtime_r(&epoch, &show->start_tm);
	show->stack = g_queue_new();
	show->slides = g_queue_new();
	context = g_markup_parse_context_new(&parser, 0, show, NULL);
	if (!g_markup_parse_context_parse(context, contents, len, NULL) ||
	    !g_markup_parse_context_end_parse(context, NULL)) {
		slideshow_free(show);
		show = NULL;
	}
	g_markup_parse_context_free(context);
	g_free(contents);

	if (show) {
		guint num_items = g_queue_get_length(show->slides);

		show->start_time = (double)mktime(&show->start_tm);
		if (num_items == 0) {
			slideshow_free(show);
			show = NULL;
		} else if (num_items == 1) {
			Slide *slide = show->slides->head->data;

			slide->duration = show->total_duration = G_MAXUINT;
		}
	}
	return show;
}

static GdkPixbuf *
get_pixbuf_for_size(PoloBg *bg, gint best_width, gint best_height)
{
	GdkPixbuf *pixbuf;
	double alpha = 0;
	Slide *slide;
	FileSize *size;

	bg->timeout = G_MAXUINT;
	if (!bg->filename)
		return NULL;

	pixbuf = load_pixbuf(bg, bg->filename, best_width, best_height);
	if (pixbuf)
		return pixbuf;

	if (!bg->slideshow_tried) {
		bg->slideshow_tried = TRUE;
		bg->slideshow = read_slideshow_file(bg->filename);
	}
	if (!bg->slideshow)
		return NULL;

	slide = get_current_slide(bg->slideshow, &alpha);
	bg->timeout = (guint)get_slide_timeout(slide);
	size = find_best_size(slide->file1, best_width, best_height);
	if (!size)
		return NULL;
	if (slide->fixed)
		return load_pixbuf(bg, size->file, best_width, best_height);

	{
		FileSize *size2 =
			find_best_size(slide->file2, best_width, best_height);
		GdkPixbuf *p1 =
			load_pixbuf(bg, size->file, best_width, best_height);
		GdkPixbuf *p2 = size2
			? load_pixbuf(bg, size2->file, best_width, best_height)
			: NULL;

		pixbuf = (p1 && p2) ? blend(p1, p2, alpha) : NULL;
		g_clear_object(&p1);
		g_clear_object(&p2);
		return pixbuf;
	}
}

/* ---- PoloBg ------------------------------------------------------------- */

PoloBg *
polo_bg_new(void)
{
	PoloBg *bg = g_new0(PoloBg, 1);

	bg->ref_count = 1;
	bg->cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
					  g_object_unref);
	bg->placement = POLO_BG_PLACEMENT_ZOOMED;
	bg->timeout = G_MAXUINT;
	return bg;
}

PoloBg *
polo_bg_ref(PoloBg *bg)
{
	g_atomic_int_inc(&bg->ref_count);
	return bg;
}

void
polo_bg_unref(PoloBg *bg)
{
	if (!bg || !g_atomic_int_dec_and_test(&bg->ref_count))
		return;
	g_free(bg->filename);
	g_hash_table_unref(bg->cache);
	slideshow_free(bg->slideshow);
	g_free(bg);
}

void
polo_bg_set_color(PoloBg *bg, PoloBgColorType type, const char *primary,
		  const char *secondary)
{
	bg->color_type = type;
	color_from_string(primary, &bg->primary);
	color_from_string(secondary, &bg->secondary);
}

void
polo_bg_set_placement(PoloBg *bg, PoloBgPlacement placement)
{
	bg->placement = placement;
}

void
polo_bg_set_filename(PoloBg *bg, const char *filename)
{
	g_free(bg->filename);
	bg->filename = filename && *filename ? g_strdup(filename) : NULL;
	bg->slideshow_tried = FALSE;
	g_clear_pointer(&bg->slideshow, slideshow_free);
}

guint
polo_bg_get_timeout(PoloBg *bg)
{
	return bg->timeout;
}

/* RGB(A) pixbuf → ARGB32 premultiplied (opaque: dest has no alpha). */
static void
copy_to_argb32(GdkPixbuf *src, guint8 *data, int width, int height, int stride)
{
	const guchar *pixels = gdk_pixbuf_get_pixels(src);
	int rowstride = gdk_pixbuf_get_rowstride(src);
	int n = gdk_pixbuf_get_n_channels(src);
	int x, y;

	for (y = 0; y < height; y++) {
		const guchar *s = pixels + y * rowstride;
		guint32 *d = (guint32 *)(data + y * stride);

		for (x = 0; x < width; x++, s += n)
			d[x] = 0xff000000u | ((guint32)s[0] << 16) |
				((guint32)s[1] << 8) | s[2];
	}
}

void
polo_bg_draw_output(PoloBg *bg, guint8 *data, int width, int height, int stride,
		    int scale, int output_x, int output_y, int screen_width,
		    int screen_height)
{
	GdkPixbuf *dest, *pixbuf, *scaled;
	int ox = output_x * scale, oy = output_y * scale;
	int x, y, w, h;

	g_return_if_fail(bg && data && width > 0 && height > 0);
	dest = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);
	if (!dest)
		return;

	if (bg->placement != POLO_BG_PLACEMENT_SPANNED) {
		/* MateBG root drawing: colour and image per monitor. */
		draw_color_area(bg, dest, width, height, 0, 0);
		pixbuf = get_pixbuf_for_size(bg, width, height);
		if (pixbuf) {
			scaled = get_scaled_pixbuf(bg->placement, pixbuf, width,
						   height, &x, &y, &w, &h);
			if (bg->placement == POLO_BG_PLACEMENT_TILED)
				/* Tiles start at the screen's origin. */
				pixbuf_tile(scaled, dest, ox, oy);
			else
				pixbuf_blend(scaled, dest, 0, 0, w, h, x, y,
					     1.0);
			g_object_unref(scaled);
			g_object_unref(pixbuf);
		}
	} else {
		/* Spanned: colour and image once over the whole screen, of
		 * which this output shows its part. */
		int sw = screen_width * scale, sh = screen_height * scale;

		draw_color_area(bg, dest, sw, sh, ox, oy);
		pixbuf = get_pixbuf_for_size(bg, sw, sh);
		if (pixbuf) {
			scaled = get_scaled_pixbuf(bg->placement, pixbuf, sw,
						   sh, &x, &y, &w, &h);
			pixbuf_blend(scaled, dest, 0, 0, w, h, x - ox, y - oy,
				     1.0);
			g_object_unref(scaled);
			g_object_unref(pixbuf);
		}
	}
	copy_to_argb32(dest, data, width, height, stride);
	g_object_unref(dest);
}
