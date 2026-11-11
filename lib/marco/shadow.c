/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco-compatible window shadows. Gaussian alpha masks are copied into ARGB
 * pieces around the frame.
 */

#include <math.h>
#include <string.h>

#include "shadow.h"
#include "theme-private.h"

typedef struct {
	int size;
	double *data;
} conv;

typedef struct {
	conv *gaussian_map;
	guchar *shadow_corner;
	guchar *shadow_top;
} shadow;

struct _PoloShadowMask {
	int scale;
	int width, height; /* in pixels at scale; the rest is unscaled */
	int x, y; /* mask corner relative to the frame window's corner */
	int extent_left, extent_top, extent_right, extent_bottom;
	int corner_top, corner_bottom;
	guchar *data;
};

#define SHADOW_SMALL_RADIUS 3.0
#define SHADOW_MEDIUM_RADIUS 6.0
#define SHADOW_LARGE_RADIUS 12.0

#define SHADOW_SMALL_OFFSET_X (SHADOW_SMALL_RADIUS * -3 / 2)
#define SHADOW_SMALL_OFFSET_Y (SHADOW_SMALL_RADIUS * -3 / 2)
#define SHADOW_MEDIUM_OFFSET_X (SHADOW_MEDIUM_RADIUS * -3 / 2)
#define SHADOW_MEDIUM_OFFSET_Y (SHADOW_MEDIUM_RADIUS * -5 / 4)
#define SHADOW_LARGE_OFFSET_X -15
#define SHADOW_LARGE_OFFSET_Y -15

static const double radii[POLO_WINDOW_SHADOW_LAST] = {
	[POLO_WINDOW_SHADOW_SMALL] = SHADOW_SMALL_RADIUS,
	[POLO_WINDOW_SHADOW_MEDIUM] = SHADOW_MEDIUM_RADIUS,
	[POLO_WINDOW_SHADOW_LARGE] = SHADOW_LARGE_RADIUS,
};

static const double shadow_offsets_x[POLO_WINDOW_SHADOW_LAST] = {
	[POLO_WINDOW_SHADOW_SMALL] = SHADOW_SMALL_OFFSET_X,
	[POLO_WINDOW_SHADOW_MEDIUM] = SHADOW_MEDIUM_OFFSET_X,
	[POLO_WINDOW_SHADOW_LARGE] = SHADOW_LARGE_OFFSET_X,
};

static const double shadow_offsets_y[POLO_WINDOW_SHADOW_LAST] = {
	[POLO_WINDOW_SHADOW_SMALL] = SHADOW_SMALL_OFFSET_Y,
	[POLO_WINDOW_SHADOW_MEDIUM] = SHADOW_MEDIUM_OFFSET_Y,
	[POLO_WINDOW_SHADOW_LARGE] = SHADOW_LARGE_OFFSET_Y,
};

#define MAX_SCALE 8
#define MAX_MASK_PIXELS ((guint64)1 << 26)

static shadow *shadows[POLO_WINDOW_SHADOW_LAST][MAX_SCALE];

static double
gaussian(double r, double x, double y)
{
	return ((1 / (sqrt(2 * G_PI * r))) *
		exp((-(x * x + y * y)) / (2 * r * r)));
}

static conv *
make_gaussian_map(double r)
{
	conv *c;
	int size, centre;
	int x, y;
	double t, g;

	size = ((int)ceil((r * 3)) + 1) & ~1;
	centre = size / 2;
	c = g_malloc0(sizeof(conv) + size * size * sizeof(double));
	c->size = size;
	c->data = (double *)(c + 1);
	t = 0.0;

	for (y = 0; y < size; y++) {
		for (x = 0; x < size; x++) {
			g = gaussian(r, (double)(x - centre),
				     (double)(y - centre));
			t += g;
			c->data[y * size + x] = g;
		}
	}

	for (y = 0; y < size; y++)
		for (x = 0; x < size; x++)
			c->data[y * size + x] /= t;

	return c;
}

static guchar
sum_gaussian(conv *map, double opacity, int x, int y, int width, int height)
{
	double *g_data, *g_line;
	double v;
	int fx, fy;
	int fx_start, fx_end;
	int fy_start, fy_end;
	int g_size, centre;

	g_line = map->data;
	g_size = map->size;
	centre = g_size / 2;
	fx_start = centre - x;
	if (fx_start < 0)
		fx_start = 0;

	fx_end = width + centre - x;
	if (fx_end > g_size)
		fx_end = g_size;

	fy_start = centre - y;
	if (fy_start < 0)
		fy_start = 0;

	fy_end = height + centre - y;
	if (fy_end > g_size)
		fy_end = g_size;

	g_line = g_line + fy_start * g_size + fx_start;

	v = 0.0;
	for (fy = fy_start; fy < fy_end; fy++) {
		g_data = g_line;
		g_line += g_size;

		for (fx = fx_start; fx < fx_end; fx++)
			v += *g_data++;
	}

	if (v > 1.0)
		v = 1.0;

	return ((guchar)(v * opacity * 255.0));
}

static void
presum_gaussian(shadow *shad)
{
	int centre;
	int opacity, x, y;
	int msize;
	conv *map;

	map = shad->gaussian_map;
	msize = map->size;
	centre = map->size / 2;

	shad->shadow_corner =
		(guchar *)(g_malloc((msize + 1) * (msize + 1) * 26));
	shad->shadow_top = (guchar *)(g_malloc((msize + 1) * 26));

	for (x = 0; x <= msize; x++) {
		shad->shadow_top[25 * (msize + 1) + x] = sum_gaussian(
			map, 1, x - centre, centre, msize * 2, msize * 2);
		for (opacity = 0; opacity < 25; opacity++) {
			shad->shadow_top[opacity * (msize + 1) + x] =
				shad->shadow_top[25 * (msize + 1) + x] *
				opacity / 25;
		}

		for (y = 0; y <= x; y++) {
			shad->shadow_corner[25 * (msize + 1) * (msize + 1) +
					    y * (msize + 1) + x] =
				sum_gaussian(map, 1, x - centre, y - centre,
					     msize * 2, msize * 2);

			shad->shadow_corner[25 * (msize + 1) * (msize + 1) +
					    x * (msize + 1) + y] =
				shad->shadow_corner[25 * (msize + 1) *
							    (msize + 1) +
						    y * (msize + 1) + x];

			for (opacity = 0; opacity < 25; opacity++) {
				shad->shadow_corner[opacity * (msize + 1) *
							    (msize + 1) +
						    y * (msize + 1) + x] =
					shad->shadow_corner
						[opacity * (msize + 1) *
							 (msize + 1) +
						 x * (msize + 1) + y] =
						shad->shadow_corner
							[25 * (msize + 1) *
								 (msize + 1) +
							 y * (msize + 1) + x] *
					opacity / 25;
			}
		}
	}
}

/* Cache one Gaussian map for each shadow type. */
static shadow *
shadow_for(PoloWindowShadow type, int scale)
{
	if (!shadows[type][scale - 1]) {
		shadow *shad = g_new0(shadow, 1);

		shad->gaussian_map = make_gaussian_map(radii[type] * scale);
		presum_gaussian(shad);
		shadows[type][scale - 1] = shad;
	}
	return shadows[type][scale - 1];
}

/* Build the shadow's alpha mask. */
static guchar *
make_shadow(PoloWindowShadow type, int scale, double opacity, int width,
	    int height, int *swidth_out, int *sheight_out)
{
	guchar *data;
	shadow *shad;
	int msize;
	int ylimit, xlimit;
	int swidth, sheight;
	int centre;
	int x, y;
	guchar d;
	int x_diff;
	int opacity_int = (int)(opacity * 25);

	shad = shadow_for(type, scale);
	msize = shad->gaussian_map->size;
	swidth = width + msize;
	sheight = height + msize;
	centre = msize / 2;

	data = g_malloc((gsize)swidth * sheight);

	if (msize > 0)
		d = shad->shadow_top[opacity_int * (msize + 1) + msize];
	else
		d = sum_gaussian(shad->gaussian_map, opacity, centre, centre,
				 width, height);
	memset(data, d, (gsize)sheight * swidth);

	ylimit = msize;
	if (ylimit > sheight / 2)
		ylimit = (sheight + 1) / 2;

	xlimit = msize;
	if (xlimit > swidth / 2)
		xlimit = (swidth + 1) / 2;

	for (y = 0; y < ylimit; y++) {
		for (x = 0; x < xlimit; x++) {
			if (xlimit == msize && ylimit == msize)
				d = shad->shadow_corner[opacity_int *
								(msize + 1) *
								(msize + 1) +
							y * (msize + 1) + x];
			else
				d = sum_gaussian(shad->gaussian_map, opacity,
						 x - centre, y - centre, width,
						 height);

			data[y * swidth + x] = d;
			data[(sheight - y - 1) * swidth + x] = d;
			data[(sheight - y - 1) * swidth + (swidth - x - 1)] = d;
			data[y * swidth + (swidth - x - 1)] = d;
		}
	}

	x_diff = swidth - (msize * 2);
	if (x_diff > 0 && ylimit > 0) {
		for (y = 0; y < ylimit; y++) {
			if (ylimit == msize)
				d = shad->shadow_top[opacity_int * (msize + 1) +
						     y];
			else
				d = sum_gaussian(shad->gaussian_map, opacity,
						 centre, y - centre, width,
						 height);

			memset(&data[y * swidth + msize], d, x_diff);
			memset(&data[(sheight - y - 1) * swidth + msize], d,
			       x_diff);
		}
	}

	for (x = 0; x < xlimit; x++) {
		if (xlimit == msize)
			d = shad->shadow_top[opacity_int * (msize + 1) + x];
		else
			d = sum_gaussian(shad->gaussian_map, opacity,
					 x - centre, centre, width, height);

		for (y = msize; y < sheight - msize; y++) {
			data[y * swidth + x] = d;
			data[y * swidth + (swidth - x - 1)] = d;
		}
	}

	*swidth_out = swidth;
	*sheight_out = sheight;
	return data;
}

PoloShadowMask *
polo_marco_shadow_mask_new(PoloWindowShadow type, double opacity,
			   const PoloFrameGeometry *fgeom, PoloFrameFlags flags,
			   int scale)
{
	const PoloBorder *invisible = &fgeom->borders.invisible;
	int window_height = polo_marco_frame_window_height(fgeom, flags);
	int width = fgeom->width - invisible->left - invisible->right;
	int height = window_height - invisible->top - invisible->bottom;
	PoloShadowMask *mask;
	cairo_region_t *visible;
	int shadow_dx, shadow_dy, msize, i;

	g_return_val_if_fail(type < POLO_WINDOW_SHADOW_LAST, NULL);
	if (width <= 0 || height <= 0)
		return NULL;
	scale = CLAMP(scale, 1, MAX_SCALE);
	if ((guint64)width * height * scale * scale > MAX_MASK_PIXELS)
		return NULL;

	/* Scale the radius with the mask so the shadow looks the same on every
	 * output and Marco's constants still divide out exactly. */
	mask = g_new0(PoloShadowMask, 1);
	mask->scale = scale;
	mask->data = make_shadow(type, scale, opacity, width * scale,
				 height * scale, &mask->width, &mask->height);
	msize = shadow_for(type, scale)->gaussian_map->size / scale;
	/* Match cw->shadow_dx in Marco's win_extents(). */
	mask->x = (int)shadow_offsets_x[type] + invisible->left;
	mask->y = (int)shadow_offsets_y[type] + invisible->top;
	mask->extent_left = -(int)shadow_offsets_x[type];
	mask->extent_top = -(int)shadow_offsets_y[type];
	mask->extent_right = msize - mask->extent_left;
	mask->extent_bottom = msize - mask->extent_top;
	mask->corner_top = MAX(fgeom->top_left_corner_rounded_radius,
			       fgeom->top_right_corner_rounded_radius);
	mask->corner_bottom = MAX(fgeom->bottom_left_corner_rounded_radius,
				  fgeom->bottom_right_corner_rounded_radius);

	/* Marco's shadow_picture_clip() leaves nothing beneath the visible
	 * frame. */
	shadow_dx = -1 * (int)shadow_offsets_x[type] - invisible->left;
	shadow_dy = -1 * (int)shadow_offsets_y[type] - invisible->top;
	visible = polo_marco_frame_visible_region(fgeom, flags, scale);
	cairo_region_translate(visible, shadow_dx * scale, shadow_dy * scale);
	for (i = 0; i < cairo_region_num_rectangles(visible); i++) {
		cairo_rectangle_int_t r;
		int y;

		cairo_region_get_rectangle(visible, i, &r);
		for (y = MAX(r.y, 0); y < MIN(r.y + r.height, mask->height);
		     y++) {
			int x0 = MAX(r.x, 0);
			int x1 = MIN(r.x + r.width, mask->width);

			if (x1 > x0)
				memset(&mask->data[y * mask->width + x0], 0,
				       x1 - x0);
		}
	}
	cairo_region_destroy(visible);
	return mask;
}

void
polo_marco_shadow_mask_free(PoloShadowMask *mask)
{
	if (!mask)
		return;
	g_free(mask->data);
	g_free(mask);
}

void
polo_marco_shadow_mask_get_bounds(const PoloShadowMask *mask, int *x, int *y,
				  int *width, int *height)
{
	*x = mask->x;
	*y = mask->y;
	*width = mask->width / mask->scale;
	*height = mask->height / mask->scale;
}

/* Split the shadow ring around the frame. The top and bottom include rows
 * removed by rounded corners. */
void
polo_marco_shadow_mask_get_piece(const PoloShadowMask *mask,
				 PoloShadowPiece piece, int *x, int *y,
				 int *width, int *height)
{
	int mask_width = mask->width / mask->scale;
	int mask_height = mask->height / mask->scale;
	int top = MIN(mask->extent_top + mask->corner_top, mask_height);
	int bottom = MIN(mask->extent_bottom + mask->corner_bottom,
			 mask_height - top);
	int middle = mask_height - top - bottom;

	switch (piece) {
	case POLO_SHADOW_PIECE_TOP:
		*x = 0;
		*y = 0;
		*width = mask_width;
		*height = top;
		break;
	case POLO_SHADOW_PIECE_BOTTOM:
		*x = 0;
		*y = mask_height - bottom;
		*width = mask_width;
		*height = bottom;
		break;
	case POLO_SHADOW_PIECE_LEFT:
		*x = 0;
		*y = top;
		*width = MIN(mask->extent_left, mask_width);
		*height = middle;
		break;
	default:
		*width = MIN(mask->extent_right, mask_width);
		*x = mask_width - *width;
		*y = top;
		*height = middle;
		break;
	}
}

/* A black premultiplied shadow has only an alpha channel. */
void
polo_marco_shadow_mask_render(const PoloShadowMask *mask, int piece_x,
			      int piece_y, guint8 *pixels, int width,
			      int height, int stride)
{
	int x, y;

	for (y = 0; y < height; y++) {
		guint32 *row = (guint32 *)(pixels + y * stride);
		int my = piece_y * mask->scale + y;

		for (x = 0; x < width; x++) {
			int mx = piece_x * mask->scale + x;
			guint32 a = 0;

			if (mx >= 0 && mx < mask->width && my >= 0 &&
			    my < mask->height)
				a = mask->data[my * mask->width + mx];
			row[x] = a << 24;
		}
	}
}
