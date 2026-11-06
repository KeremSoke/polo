/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests ported from picom wobbly branch src/wm/magic_lamp.c's
 * reference points, and as geometry the lamp checks of tests/magic_lamp.py
 * (Xvfb, a 300x240 frame at 100,100 and icons at its four sides).
 */

#include <math.h>

#include <glib.h>

#include "polo-fx.h"

static const PoloFxVec2 FRAME_ORIGIN = { 100, 100 };
static const PoloFxVec2 FRAME_SIZE = { 300, 240 };

static PoloFxMagicLamp
frame_lamp(double x, double y, double width, double height,
	   PoloFxMagicLampDirection direction)
{
	return (PoloFxMagicLamp){ .origin = FRAME_ORIGIN,
				  .size = FRAME_SIZE,
				  .target = { x, y },
				  .target_size = { width, height },
				  .direction = direction };
}

static void
test_reference_and_endpoints(void)
{
	PoloFxMagicLamp m = {
		.origin = { 100, 200 },
		.size = { 400, 300 },
		.target = { 700, 800 },
		.target_size = { 40, 20 },
		.direction = POLO_FX_MAGIC_LAMP_BOTTOM,
	};
	PoloFxVec2 point = polo_fx_magic_lamp_point(&m, (PoloFxVec2){ 200, 150 });

	g_assert_true(point.x == 300 && point.y == 350);
	m.progress = 0.5;
	point = polo_fx_magic_lamp_point(&m, (PoloFxVec2){ 200, 150 });
	/* KWin's bottom edge: offset = 750 * .5 * .75^3 = 158.203125. */
	g_assert_cmpfloat_with_epsilon(point.y, 508.203125, 1e-6);
	g_assert_cmpfloat_with_epsilon(point.x, 447.65625, 1e-6);
	for (int direction = 0; direction < 4; direction++) {
		m.direction = (PoloFxMagicLampDirection)direction;
		m.progress = 1;
		point = polo_fx_magic_lamp_point(&m, (PoloFxVec2){ 200, 150 });
		g_assert_true(isfinite(point.x) && isfinite(point.y));
		if (direction == POLO_FX_MAGIC_LAMP_BOTTOM ||
		    direction == POLO_FX_MAGIC_LAMP_TOP) {
			g_assert_true(point.x == 720);
			g_assert_true(point.y ==
				      (direction == POLO_FX_MAGIC_LAMP_BOTTOM ? 800
									      : 820));
		} else {
			g_assert_true(point.y == 810);
			g_assert_true(point.x ==
				      (direction == POLO_FX_MAGIC_LAMP_LEFT ? 740
									    : 700));
		}
	}
}

static void
test_degenerate_and_reversal(void)
{
	PoloFxMagicLamp m = { .size = { 400, 300 }, .target = { 0, 300 } };

	for (int direction = 0; direction < 4; direction++) {
		m.direction = (PoloFxMagicLampDirection)direction;
		for (int i = 0; i <= 100; i++) {
			m.progress = (double)i / 100;
			PoloFxVec2 point = polo_fx_magic_lamp_point(
				&m, (PoloFxVec2){ 0, 300 });

			g_assert_true(isfinite(point.x) && isfinite(point.y));
		}
	}
	double p = polo_fx_magic_lamp_advance(0, FALSE, 100, 250);

	g_assert_cmpfloat_with_epsilon(p, 0.4, 1e-9);
	p = polo_fx_magic_lamp_advance(p, TRUE, 100, 250);
	g_assert_true(p == 0);
	double slow = 0, fast = 0;

	for (int i = 0; i < 10; i++)
		slow = polo_fx_magic_lamp_advance(slow, FALSE, 10, 250);
	for (int i = 0; i < 20; i++)
		fast = polo_fx_magic_lamp_advance(fast, FALSE, 5, 250);
	g_assert_cmpfloat_with_epsilon(slow, fast, 1e-9);
}

static void
outline(const PoloFxMagicLamp *m, double *min_x, double *min_y, double *max_x,
	double *max_y, double *area)
{
	PoloFxGrid *grid = polo_fx_grid_new(31, 25);
	GArray *ring = g_array_new(FALSE, FALSE, sizeof(PoloFxVec2));
	guint cols = grid->cols, rows = grid->rows;

	polo_fx_magic_lamp_fill_grid(m, grid);
#define AT(c, r)                                                             \
	((PoloFxVec2){ grid->vertices[((r) * cols + (c)) * 4],                 \
		       grid->vertices[((r) * cols + (c)) * 4 + 1] })
	for (guint c = 0; c < cols; c++)
		g_array_append_val(ring, AT(c, 0));
	for (guint r = 1; r < rows; r++)
		g_array_append_val(ring, AT(cols - 1, r));
	for (guint c = cols - 1; c-- > 0;)
		g_array_append_val(ring, AT(c, rows - 1));
	for (guint r = rows - 1; r-- > 1;)
		g_array_append_val(ring, AT(0, r));
#undef AT
	*min_x = *min_y = INFINITY;
	*max_x = *max_y = -INFINITY;
	*area = 0;
	for (guint i = 0; i < ring->len; i++) {
		PoloFxVec2 a = g_array_index(ring, PoloFxVec2, i);
		PoloFxVec2 b = g_array_index(ring, PoloFxVec2,
					     (i + 1) % ring->len);

		*min_x = fmin(*min_x, a.x);
		*min_y = fmin(*min_y, a.y);
		*max_x = fmax(*max_x, a.x);
		*max_y = fmax(*max_y, a.y);
		*area += a.x * b.y - b.x * a.y;
	}
	*area = fabs(*area) / 2;
	g_array_unref(ring);
	polo_fx_grid_free(grid);
}

static void
test_midway_minimise(void)
{
	PoloFxMagicLamp m = frame_lamp(650, 570, 70, 30,
				       POLO_FX_MAGIC_LAMP_BOTTOM);
	double x0, y0, x1, y1, initial_area, area;
	double mx0, my0, mx1, my1;

	outline(&m, &x0, &y0, &x1, &y1, &initial_area);
	g_assert_cmpfloat_with_epsilon(initial_area, 300 * 240, 1e-3);
	m.progress = polo_fx_magic_lamp_advance(0, FALSE, 300, 800);
	outline(&m, &mx0, &my0, &mx1, &my1, &area);
	g_assert_cmpfloat(mx1, >, x1 + 20);
	g_assert_cmpfloat(my1, >, y1);

	PoloFxVec2 left = polo_fx_magic_lamp_point(&m, (PoloFxVec2){ 0, 240 });
	PoloFxVec2 right = polo_fx_magic_lamp_point(&m, (PoloFxVec2){ 300, 240 });

	g_assert_cmpfloat(right.x - left.x, <, 300);
	g_assert_cmpfloat(right.x - left.x, >=, 70 - 1e-6);
}

static void
test_collapses_into_icon(void)
{
	static const struct {
		double x, y, width, height;
		PoloFxMagicLampDirection direction;
	} icons[] = {
		{ 650, 570, 70, 30, POLO_FX_MAGIC_LAMP_BOTTOM },
		{ 0, 260, 30, 70, POLO_FX_MAGIC_LAMP_LEFT },
		{ 770, 260, 30, 70, POLO_FX_MAGIC_LAMP_RIGHT },
		{ 600, 0, 70, 30, POLO_FX_MAGIC_LAMP_TOP },
	};

	for (guint i = 0; i < G_N_ELEMENTS(icons); i++) {
		PoloFxMagicLamp m = frame_lamp(icons[i].x, icons[i].y,
					       icons[i].width, icons[i].height,
					       icons[i].direction);
		double x0, y0, x1, y1, area;

		for (int step = 0; step <= 20; step++) {
			m.progress = step / 20.0;
			outline(&m, &x0, &y0, &x1, &y1, &area);
			g_assert_true(isfinite(x0) && isfinite(y0) &&
				      isfinite(x1) && isfinite(y1));
		}
		g_assert_cmpfloat(x0, >=, icons[i].x - 1e-6);
		g_assert_cmpfloat(y0, >=, icons[i].y - 1e-6);
		g_assert_cmpfloat(x1, <=, icons[i].x + icons[i].width + 1e-6);
		g_assert_cmpfloat(y1, <=, icons[i].y + icons[i].height + 1e-6);
	}
}

static void
test_reversal_is_proportional(void)
{
	double p = 0;
	int frames = 0;

	for (int i = 0; i < 12; i++)
		p = polo_fx_magic_lamp_advance(p, FALSE, 16, 800);
	g_assert_cmpfloat_with_epsilon(p, 12 * 16 / 800.0, 1e-9);
	while (p > 0) {
		p = polo_fx_magic_lamp_advance(p, TRUE, 16, 800);
		frames++;
	}
	g_assert_cmpint(frames, ==, 12);
}

static void
test_grid(void)
{
	PoloFxMagicLamp m = frame_lamp(650, 570, 70, 30,
				       POLO_FX_MAGIC_LAMP_BOTTOM);
	guint cols, rows;

	polo_fx_magic_lamp_grid_size(&m, &cols, &rows);
	g_assert_cmpuint(cols, ==, 300 / 40 + 1 + 1);
	g_assert_cmpuint(rows, ==, 240 / 40 + 1);
	m.size = (PoloFxVec2){ 20000, 20000 };
	polo_fx_magic_lamp_grid_size(&m, &cols, &rows);
	g_assert_cmpuint((guint64)cols * rows, <=, POLO_FX_GRID_MAX_VERTICES);
	m.size = FRAME_SIZE;

	PoloFxGrid *grid = polo_fx_grid_new(5, 4);

	polo_fx_magic_lamp_fill_grid(&m, grid);
	for (guint r = 0; r < grid->rows; r++) {
		for (guint c = 0; c < grid->cols; c++) {
			float *v = &grid->vertices[(r * grid->cols + c) * 4];

			g_assert_cmpfloat_with_epsilon(v[0], 100 + 300 * c / 4.0, 1e-3);
			g_assert_cmpfloat_with_epsilon(v[1], 100 + 240 * r / 3.0, 1e-3);
			g_assert_cmpfloat_with_epsilon(v[2], c / 4.0, 1e-6);
			g_assert_cmpfloat_with_epsilon(v[3], r / 3.0, 1e-6);
		}
	}
	polo_fx_grid_free(grid);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/magic-lamp/reference-and-endpoints",
			test_reference_and_endpoints);
	g_test_add_func("/magic-lamp/degenerate-and-reversal",
			test_degenerate_and_reversal);
	g_test_add_func("/magic-lamp/midway-minimise", test_midway_minimise);
	g_test_add_func("/magic-lamp/collapses-into-icon",
			test_collapses_into_icon);
	g_test_add_func("/magic-lamp/reversal-is-proportional",
			test_reversal_is_proportional);
	g_test_add_func("/magic-lamp/grid", test_grid);
	return g_test_run();
}
