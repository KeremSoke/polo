/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The cube's geometry against Wayfire's: the front side fills the output at
 * rest, turns bring the next side in front, sides facing away are culled,
 * the deformations and lighting of its shaders, and grids for G1.
 */

#include <math.h>

#include <glib.h>

#include "polo-fx.h"

#define WAYFIRE_Z_OFFSET_NEAR 0.89567

static PoloFxCube
rest(int sides, double width, double height)
{
	return (PoloFxCube){ .sides = sides,
			     .offset_z = polo_fx_cube_rest_offset_z(sides),
			     .zoom = 1.0,
			     .origin = { 100, 50 },
			     .size = { width, height } };
}

static void
assert_point(const PoloFxCube *cube, int side, double lx, double ly, double sx,
	     double sy, double tolerance)
{
	PoloFxVec2 screen;

	g_assert_true(polo_fx_cube_point(cube, side, (PoloFxVec2){ lx, ly },
					 &screen));
	g_assert_cmpfloat_with_epsilon(screen.x, sx, tolerance);
	g_assert_cmpfloat_with_epsilon(screen.y, sy, tolerance);
}

static void
assert_fills_output(const PoloFxCube *cube, int side, double tolerance)
{
	double w = cube->size.x, h = cube->size.y;
	double x = cube->origin.x, y = cube->origin.y;

	assert_point(cube, side, 0, 0, x, y, tolerance);
	assert_point(cube, side, w, 0, x + w, y, tolerance);
	assert_point(cube, side, 0, h, x, y + h, tolerance);
	assert_point(cube, side, w, h, x + w, y + h, tolerance);
	assert_point(cube, side, w / 2, h / 2, x + w / 2, y + h / 2, tolerance);
}

static void
test_rest_fills_output(void)
{
	const int sides[] = { 3, 4, 8 };
	const double sizes[][2] = { { 1024, 768 }, { 1920, 1080 } };

	for (guint i = 0; i < G_N_ELEMENTS(sides); i++) {
		for (guint j = 0; j < G_N_ELEMENTS(sizes); j++) {
			PoloFxCube cube =
				rest(sides[i], sizes[j][0], sizes[j][1]);

			assert_fills_output(&cube, 0, 0.01);
			g_assert_true(polo_fx_cube_side_facing(&cube, 0));
		}
	}
}

static void
test_wayfire_constant_misses(void)
{
	PoloFxCube cube = rest(4, 1920, 1080);
	PoloFxVec2 screen;

	cube.offset_z = 0.5 / tan(G_PI / 4) + WAYFIRE_Z_OFFSET_NEAR;
	polo_fx_cube_point(&cube, 0, (PoloFxVec2){ 0, 0 }, &screen);
	g_assert_cmpfloat(fabs(screen.x - 100), >, 0.5);
	g_assert_cmpfloat(fabs(screen.x - 100), <, 1.0);
}

static void
test_two_sides_gap(void)
{
	PoloFxCube cube = rest(2, 1024, 768);
	PoloFxVec2 screen;

	polo_fx_cube_point(&cube, 0, (PoloFxVec2){ 0, 0 }, &screen);
	g_assert_cmpfloat(screen.x, <, 100);
	g_assert_cmpfloat(screen.x, >, 99);
	cube.rotation = -G_PI;
	g_assert_true(polo_fx_cube_side_facing(&cube, 1));
	g_assert_false(polo_fx_cube_side_facing(&cube, 0));
}

static void
test_turns(void)
{
	PoloFxCube cube = rest(4, 1024, 768);
	int order[4];

	cube.rotation = -polo_fx_cube_side_angle(4);
	assert_fills_output(&cube, 1, 0.01);
	g_assert_true(polo_fx_cube_side_facing(&cube, 1));
	for (int side = 0; side < 4; side++)
		if (side != 1)
			g_assert_false(polo_fx_cube_side_facing(&cube, side));
	polo_fx_cube_side_order(&cube, order);
	g_assert_cmpint(order[0], ==, 3);
	g_assert_cmpint(order[3], ==, 1);

	cube.rotation = -3 * polo_fx_cube_side_angle(4);
	assert_fills_output(&cube, 3, 0.01);
	cube.rotation = polo_fx_cube_side_angle(4);
	assert_fills_output(&cube, 3, 0.01);
}

static void
test_culling_every_step(void)
{
	for (int sides = 3; sides <= 6; sides++) {
		for (int step = 0; step < 64; step++) {
			for (int tilt = -1; tilt <= 1; tilt++) {
				PoloFxCube cube = rest(sides, 1024, 768);
				int order[6], facing = 0, nearest = 0;

				cube.rotation = step * 2 * G_PI / 64;
				cube.offset_y = tilt * 0.6;
				for (int side = 0; side < sides; side++) {
					facing += polo_fx_cube_side_facing(
						&cube, side);
					if (polo_fx_cube_side_depth(&cube,
								    side) <
					    polo_fx_cube_side_depth(&cube,
								    nearest))
						nearest = side;
				}
				g_assert_cmpint(facing, >=, 1);
				g_assert_cmpint(facing, <=, (sides + 1) / 2);
				g_assert_true(polo_fx_cube_side_facing(
					&cube, nearest));
				polo_fx_cube_side_order(&cube, order);
				g_assert_true(polo_fx_cube_side_facing(
					&cube, order[sides - 1]));
				g_assert_false(polo_fx_cube_side_facing(
					&cube, order[0]));
				for (int i = 1; i < sides; i++)
					g_assert_true(
						polo_fx_cube_side_facing(
							&cube, order[i]) >=
						polo_fx_cube_side_facing(
							&cube, order[i - 1]));
			}
		}
	}
}

static void
expected_corner(double scale, double width, double height, double *sx,
		double *sy)
{
	double f = 1.0 / tan(22.5);
	double x = -0.5 * scale, z = 0.5 * scale;
	double w = (0.5 + 0.5 * f) - z;

	*sx = 100 + (f * x / w + 1) / 2 * width;
	*sy = 50 + (1 - f * 0.5 / w) / 2 * height;
}

static void
test_deformations(void)
{
	PoloFxCube cube = rest(4, 1024, 768);
	double sx, sy;

	cube.deform = POLO_FX_CUBE_DEFORM_CYLINDER;
	cube.ease = 0.0;
	assert_fills_output(&cube, 0, 0.01);

	cube.ease = 1.0;
	expected_corner(0.5 / sqrt(0.5), 1024, 768, &sx, &sy);
	assert_point(&cube, 0, 0, 0, sx, sy, 0.01);
	assert_point(&cube, 0, 512, 384, 612, 434, 0.01);

	cube.deform = POLO_FX_CUBE_DEFORM_STAR;
	expected_corner(sqrt(0.5) / 0.5, 1024, 768, &sx, &sy);
	assert_point(&cube, 0, 0, 0, sx, sy, 0.01);

	cube.ease = 0.5;
	expected_corner(pow(sqrt(0.5) / 0.5, 0.5), 1024, 768, &sx, &sy);
	assert_point(&cube, 0, 0, 0, sx, sy, 0.01);
}

static void
test_shade(void)
{
	PoloFxCube cube = rest(4, 1024, 768);
	double angled = 0.3 + 0.7 * pow(cos(G_PI / 4), 1.5);

	g_assert_cmpfloat_with_epsilon(
		polo_fx_cube_shade(&cube, 0, (PoloFxVec2){ 512, 384 }), 1.0,
		1e-6);
	g_assert_cmpfloat_with_epsilon(
		polo_fx_cube_shade(&cube, 0, (PoloFxVec2){ 0, 0 }), 1.0, 1e-6);

	cube.rotation = G_PI / 4;
	g_assert_cmpfloat_with_epsilon(
		polo_fx_cube_shade(&cube, 0, (PoloFxVec2){ 512, 384 }), angled,
		1e-3);

	cube.rotation = 0;
	cube.deform = POLO_FX_CUBE_DEFORM_CYLINDER;
	cube.ease = 1.0;
	g_assert_cmpfloat_with_epsilon(
		polo_fx_cube_shade(&cube, 0, (PoloFxVec2){ 512, 384 }), 1.0,
		1e-3);
	g_assert_cmpfloat_with_epsilon(
		polo_fx_cube_shade(&cube, 0, (PoloFxVec2){ 0, 384 }), angled,
		0.01);
}

static void
test_fill_shade(void)
{
	PoloFxCube cube = rest(4, 1024, 768);
	guint32 pixels[8 * 6];
	guint32 expected =
		lround((1 - (0.3 + 0.7 * pow(cos(G_PI / 4), 1.5))) * 255);

	polo_fx_cube_fill_shade(&cube, 0, (guint8 *)pixels, 8, 6, 8 * 4);
	for (int i = 0; i < 8 * 6; i++)
		g_assert_cmphex(pixels[i], ==, 0);
	cube.rotation = G_PI / 4;
	polo_fx_cube_fill_shade(&cube, 0, (guint8 *)pixels, 8, 6, 8 * 4);
	g_assert_cmpuint(pixels[3 * 8 + 4] >> 24, ==, expected);
	g_assert_cmphex(pixels[3 * 8 + 4] & 0xffffff, ==, 0);
}

static void
test_grids(void)
{
	PoloFxCube cube = rest(4, 1024, 768);
	PoloFxGrid *grid;
	guint cols, rows;
	float *last;

	polo_fx_cube_grid_size(&cube, 300, 200, 32, &cols, &rows);
	g_assert_cmpuint(cols, ==, 11);
	g_assert_cmpuint(rows, ==, 8);
	cube.deform = POLO_FX_CUBE_DEFORM_STAR;
	polo_fx_cube_grid_size(&cube, 1024, 200, 64, &cols, &rows);
	g_assert_cmpuint(cols, ==, 31);
	g_assert_cmpuint(rows, ==, 9);
	cube.deform = POLO_FX_CUBE_DEFORM_NONE;
	polo_fx_cube_grid_size(&cube, 100000, 0.5, 1, &cols, &rows);
	g_assert_cmpuint(cols * rows, <=, POLO_FX_GRID_MAX_VERTICES);
	g_assert_cmpuint(rows, ==, 2);

	polo_fx_cube_grid_size(&cube, 150, 100, 32, &cols, &rows);
	grid = polo_fx_grid_new(cols, rows);
	g_assert_true(polo_fx_cube_fill_grid(&cube, 0, -50, 10, 200, 100, 0, 10,
					     150, 100, grid));
	g_assert_cmpfloat_with_epsilon(grid->vertices[0], 100, 0.01);
	g_assert_cmpfloat_with_epsilon(grid->vertices[1], 60, 0.01);
	g_assert_cmpfloat_with_epsilon(grid->vertices[2], 0.25, 1e-6);
	g_assert_cmpfloat_with_epsilon(grid->vertices[3], 0.0, 1e-6);
	last = grid->vertices + grid->n_floats - 4;
	g_assert_cmpfloat_with_epsilon(last[0], 250, 0.01);
	g_assert_cmpfloat_with_epsilon(last[1], 160, 0.01);
	g_assert_cmpfloat_with_epsilon(last[2], 1.0, 1e-6);
	g_assert_cmpfloat_with_epsilon(last[3], 1.0, 1e-6);

	cube.rotation = -polo_fx_cube_side_angle(4) / 2;
	g_assert_true(polo_fx_cube_fill_grid(&cube, 1, 0, 0, 150, 100, 0, 0,
					     150, 100, grid));
	g_assert_cmpfloat(grid->vertices[0], >, 100);
	polo_fx_grid_free(grid);
}

static void
test_behind_camera(void)
{
	PoloFxCube cube = rest(4, 1024, 768);
	PoloFxGrid *grid = polo_fx_grid_new(2, 2);
	PoloFxVec2 screen;
	gboolean any_behind = FALSE;

	cube.zoom = 0.1;
	for (int side = 0; side < 4; side++)
		any_behind |= !polo_fx_cube_point(
			&cube, side, (PoloFxVec2){ 0, 0 }, &screen);
	g_assert_true(any_behind);
	g_assert_false(polo_fx_cube_fill_grid(&cube, 0, 0, 0, 1024, 768, 0, 0,
					      1024, 768, grid));
	polo_fx_grid_free(grid);
}

static void
test_skydome(void)
{
	PoloFxCube cube = rest(4, 1024, 768);
	PoloFxVec2 centre = { 612, 434 };
	PoloFxVec2 uv = polo_fx_cube_sky_uv(&cube, centre, FALSE);
	PoloFxGrid *grid = polo_fx_grid_new(129, 129);

	g_assert_cmpfloat_with_epsilon(uv.x, 0.75, 1e-6);
	g_assert_cmpfloat_with_epsilon(uv.y, 0.5, 1e-6);
	uv = polo_fx_cube_sky_uv(&cube, centre, TRUE);
	g_assert_cmpfloat_with_epsilon(uv.x, 0.5, 1e-6);
	cube.rotation = G_PI / 2;
	uv = polo_fx_cube_sky_uv(&cube, centre, FALSE);
	g_assert_cmpfloat_with_epsilon(uv.x, 0.0, 1e-6);
	cube.offset_y = 0.5;
	uv = polo_fx_cube_sky_uv(&cube, centre, FALSE);
	g_assert_cmpfloat(uv.y, >, 0.5);
	polo_fx_cube_fill_sky_grid(&cube, FALSE, grid);
	g_assert_cmpfloat_with_epsilon(grid->vertices[0], 100, 1e-6);
	g_assert_cmpfloat_with_epsilon(grid->vertices[1], 50, 1e-6);
	for (int i = 0; i < grid->n_floats; i++)
		g_assert_true(isfinite(grid->vertices[i]));
	polo_fx_grid_free(grid);
}

static void
test_cubemap(void)
{
	const double directions[6][3] = { { 1, 0, 0 }, { -1, 0, 0 },
					  { 0, 1, 0 }, { 0, -1, 0 },
					  { 0, 0, 1 }, { 0, 0, -1 } };
	const int tiles[6][2] = { { 2, 1 }, { 0, 1 }, { 1, 0 },
				  { 1, 2 }, { 1, 1 }, { 3, 1 } };
	for (int i = 0; i < 6; i++) {
		PoloFxVec2 uv = polo_fx_cube_cubemap_uv(
			directions[i][0], directions[i][1], directions[i][2]);
		g_assert_cmpfloat_with_epsilon(uv.x, (tiles[i][0] + 0.5) / 4,
					       1e-6);
		g_assert_cmpfloat_with_epsilon(uv.y, (tiles[i][1] + 0.5) / 3,
					       1e-6);
	}
	guint32 source[12], result[128 * 64];
	for (int i = 0; i < 12; i++)
		source[i] = 0xff000000u | (guint32)i;
	g_assert_true(polo_fx_cube_cubemap_equirect((const guint8 *)source, 4,
						    3, 16, (guint8 *)result,
						    128, 64, 128 * 4));
	/* Looking along +Z samples the front cell; -Z samples the back. */
	g_assert_cmphex(result[32 * 128 + 32], ==, source[5]);
	g_assert_cmphex(result[32 * 128 + 96], ==, source[7]);
	g_assert_true(polo_fx_cube_cubemap_face((const guint8 *)source, 4, 3,
						16, (guint8 *)result, 128, 64,
						128 * 4, 4));
	g_assert_cmphex(result[32 * 128 + 32], ==, source[5]);
	g_assert_cmphex(result[32 * 128 + 96], ==, 0);
	g_assert_true(polo_fx_cube_cubemap_face((const guint8 *)source, 4, 3,
						16, (guint8 *)result, 128, 64,
						128 * 4, 5));
	g_assert_cmphex(result[32 * 128 + 32], ==, 0);
	g_assert_cmphex(result[32 * 128 + 96], ==, source[7]);
	g_assert_false(polo_fx_cube_cubemap_equirect((const guint8 *)source, 4,
						     4, 16, (guint8 *)result,
						     128, 64, 128 * 4));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/cube/rest", test_rest_fills_output);
	g_test_add_func("/cube/wayfire-constant", test_wayfire_constant_misses);
	g_test_add_func("/cube/two-sides", test_two_sides_gap);
	g_test_add_func("/cube/turns", test_turns);
	g_test_add_func("/cube/culling", test_culling_every_step);
	g_test_add_func("/cube/deformations", test_deformations);
	g_test_add_func("/cube/shade", test_shade);
	g_test_add_func("/cube/fill-shade", test_fill_shade);
	g_test_add_func("/cube/grids", test_grids);
	g_test_add_func("/cube/behind-camera", test_behind_camera);
	g_test_add_func("/cube/skydome", test_skydome);
	g_test_add_func("/cube/cubemap", test_cubemap);
	return g_test_run();
}
