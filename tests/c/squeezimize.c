/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Squeezimize's forward grid against Wayfire's shader: every window point
 * the grid puts on screen is the one the shader samples there, towards
 * each of the four edges and through the whole animation.
 */

#include <math.h>

#include <glib.h>

#include "polo-fx.h"

static PoloFxSqueezimize
towards(PoloFxMagicLampDirection direction, double progress)
{
	const PoloFxVec2 targets[] = {
		{ 300, 740 }, { 330, 0 }, { 0, 280 }, { 980, 300 }
	};
	const PoloFxVec2 sizes[] = {
		{ 48, 28 }, { 48, 28 }, { 28, 48 }, { 28, 48 }
	};

	return (PoloFxSqueezimize){ .progress = progress,
				    .direction = direction,
				    .origin = { 200, 150 },
				    .size = { 500, 360 },
				    .target = targets[direction],
				    .target_size = sizes[direction] };
}

static void
test_rest(void)
{
	for (int direction = 0; direction < 4; direction++) {
		PoloFxSqueezimize sq = towards(direction, 0.0);

		for (int i = 0; i <= 4; i++) {
			for (int j = 0; j <= 4; j++) {
				PoloFxVec2 p = polo_fx_squeezimize_point(
					&sq, (PoloFxVec2){ i / 4.0, j / 4.0 });

				g_assert_cmpfloat_with_epsilon(
					p.x, 200 + 500 * i / 4.0, 1e-6);
				g_assert_cmpfloat_with_epsilon(
					p.y, 150 + 360 * j / 4.0, 1e-6);
			}
		}
	}
}

static void
test_forward_matches_shader(void)
{
	for (int direction = 0; direction < 4; direction++) {
		for (int step = 0; step <= 10; step++) {
			PoloFxSqueezimize sq = towards(direction, step / 10.0);

			for (int i = 0; i <= 8; i++) {
				for (int j = 0; j <= 8; j++) {
					PoloFxVec2 uv = { i / 8.0, j / 8.0 };
					PoloFxVec2 screen =
						polo_fx_squeezimize_point(&sq,
									  uv);
					PoloFxVec2 back;

					polo_fx_squeezimize_texture_at(
						&sq, screen, &back);
					g_assert_cmpfloat_with_epsilon(
						back.x, uv.x, 1e-6);
					g_assert_cmpfloat_with_epsilon(
						back.y, uv.y, 1e-6);
				}
			}
		}
	}
}

/* Fully squeezed, the edge nearest the target is as wide as the target. */
static void
test_squeezed(void)
{
	PoloFxSqueezimize down = towards(POLO_FX_MAGIC_LAMP_BOTTOM, 1.0);
	PoloFxSqueezimize left = towards(POLO_FX_MAGIC_LAMP_LEFT, 1.0);
	PoloFxVec2 a = polo_fx_squeezimize_point(&down, (PoloFxVec2){ 0, 1 });
	PoloFxVec2 b = polo_fx_squeezimize_point(&down, (PoloFxVec2){ 1, 1 });
	PoloFxVec2 c = polo_fx_squeezimize_point(&left, (PoloFxVec2){ 0, 0 });
	PoloFxVec2 d = polo_fx_squeezimize_point(&left, (PoloFxVec2){ 0, 1 });

	g_assert_cmpfloat(fabs((b.x - a.x) - 48), <, 1.0);
	g_assert_cmpfloat(fabs(a.x - 300), <, 1.0);
	g_assert_cmpfloat(fabs((d.y - c.y) - 48), <, 1.0);
	g_assert_cmpfloat(fabs(c.y - 280), <, 1.0);
}

static void
test_grid(void)
{
	PoloFxSqueezimize sq = towards(POLO_FX_MAGIC_LAMP_BOTTOM, 0.5);
	guint cols, rows;
	PoloFxGrid *grid;

	polo_fx_squeezimize_grid_size(&sq, &cols, &rows);
	g_assert_cmpuint(cols, ==, 2);
	g_assert_cmpuint(rows, ==, 96);
	sq.direction = POLO_FX_MAGIC_LAMP_RIGHT;
	polo_fx_squeezimize_grid_size(&sq, &cols, &rows);
	g_assert_cmpuint(cols, ==, 96);
	g_assert_cmpuint(rows, ==, 2);
	grid = polo_fx_grid_new(cols, rows);
	polo_fx_squeezimize_fill_grid(&sq, grid);
	for (guint i = 0; i < cols * rows; i++) {
		float *v = grid->vertices + i * 4;
		PoloFxVec2 p = polo_fx_squeezimize_point(
			&sq, (PoloFxVec2){ v[2], v[3] });

		g_assert_cmpfloat_with_epsilon(v[0], p.x, 1e-3);
		g_assert_cmpfloat_with_epsilon(v[1], p.y, 1e-3);
	}
	polo_fx_grid_free(grid);
}

static void
test_advance(void)
{
	g_assert_cmpfloat_with_epsilon(
		polo_fx_squeezimize_advance(0.0, FALSE, 75, 150), 0.5, 1e-9);
	g_assert_cmpfloat_with_epsilon(
		polo_fx_squeezimize_advance(0.5, TRUE, 300, 150), 0.0, 1e-9);
	g_assert_cmpfloat_with_epsilon(
		polo_fx_squeezimize_advance(0.9, FALSE, 300, 150), 1.0, 1e-9);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/squeezimize/rest", test_rest);
	g_test_add_func("/squeezimize/forward-matches-shader",
			test_forward_matches_shader);
	g_test_add_func("/squeezimize/squeezed", test_squeezed);
	g_test_add_func("/squeezimize/grid", test_grid);
	g_test_add_func("/squeezimize/advance", test_advance);
	return g_test_run();
}
