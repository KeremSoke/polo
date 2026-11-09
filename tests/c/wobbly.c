/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests ported from picom wobbly branch
 * 5ab12b3142be247034568976c88c40a9dac424dd.
 */

#include <math.h>

#include <glib.h>

#include "wobbly-private.h"

static PoloFxVec2
rest_point(PoloFxOrigin origin, PoloFxSize size, int i, int j)
{
	return (PoloFxVec2){ .x = origin.x + (double)size.width * i / 3.0,
			     .y = origin.y + (double)size.height * j / 3.0 };
}

static void
test_wobbly_surface_settles(void)
{
	PoloFxWobbly state = { 0 };
	PoloFxWobblyParameters params = {
		.stiffness = 0.15,
		.drag = 0.80,
		.move_factor = 0.10,
		.max_acceleration = 1000.0,
		.max_velocity = 1000.0,
		.stop_acceleration = 0.5,
		.stop_velocity = 0.5,
		.x_tessellation = 20,
		.y_tessellation = 20,
	};
	PoloFxOrigin old_origin = { 40, 30 }, origin = { 100, 55 };
	PoloFxSize size = { 600, 400 };
	polo_fx_wobbly_start(&state, old_origin, size, params, 0.5, 0.0);
	PoloFxVec2 center = polo_fx_wobbly_point(state.position, 0.5, 0.5);
	g_assert_true(fabs(center.x - 340.0) < 0.001);
	g_assert_true(fabs(center.y - 230.0) < 0.001);
	polo_fx_wobbly_advance(&state, origin, size, 16.0);
	center = polo_fx_wobbly_point(state.position, 0.5, 0.5);
	g_assert_true(center.x > 340.0 && center.x < 400.0);
	int frames = 0;
	while (polo_fx_wobbly_advance(&state, origin, size, 16.0) &&
	       frames++ < 300) {
	}
	g_assert_false(state.active);
	for (int index = 0; index < WOBBLY_POINT_COUNT; index++) {
		PoloFxVec2 target =
			rest_point(origin, size, index % WOBBLY_GRID_SIZE,
				   index / WOBBLY_GRID_SIZE);
		g_assert_true(state.position[index].x == target.x);
		g_assert_true(state.position[index].y == target.y);
		g_assert_true(state.velocity[index].x == 0.0);
		g_assert_true(state.velocity[index].y == 0.0);
	}
	center = polo_fx_wobbly_point(state.position, 0.5, 0.5);
	g_assert_true(fabs(center.x - 400.0) < 3.0);
	g_assert_true(fabs(center.y - 255.0) < 3.0);
}

static void
test_wobbly_stiffness_changes_response(void)
{
	PoloFxWobblyParameters params = {
		.stiffness = 0.01,
		.drag = 0.80,
		.move_factor = 0.10,
		.max_acceleration = 1000.0,
		.max_velocity = 1000.0,
		.stop_acceleration = 0.5,
		.stop_velocity = 0.5,
		.x_tessellation = 12,
		.y_tessellation = 24,
	};
	PoloFxWobbly soft = { 0 }, firm = { 0 };
	PoloFxSize size = { 600, 400 };
	PoloFxOrigin zero = { 0, 0 }, moved = { 30, 0 };
	polo_fx_wobbly_start(&soft, zero, size, params, 0.5, 0.0);
	params.stiffness = 0.03;
	polo_fx_wobbly_start(&firm, zero, size, params, 0.5, 0.0);
	polo_fx_wobbly_advance(&soft, moved, size, 10.0);
	polo_fx_wobbly_advance(&firm, moved, size, 10.0);
	g_assert_true(firm.position[1].x > soft.position[1].x);
	g_assert_cmpint(soft.parameters.x_tessellation, ==, 12);
	g_assert_cmpint(soft.parameters.y_tessellation, ==, 24);
}

static void
test_wobbly_presets(void)
{
	PoloFxWobblyParameters p0 = polo_fx_wobbly_parameters_for_level(0);
	PoloFxWobblyParameters p4 = polo_fx_wobbly_parameters_for_level(4);
	g_assert_cmpfloat(p0.stiffness, ==, 0.15);
	g_assert_cmpfloat(p0.drag, ==, 0.80);
	g_assert_cmpfloat(p0.move_factor, ==, 0.10);
	g_assert_cmpfloat(p4.stiffness, ==, 0.01);
	g_assert_cmpfloat(p4.drag, ==, 0.97);
	g_assert_cmpfloat(p4.move_factor, ==, 0.25);
}

static void
test_wobbly_fill_grid(void)
{
	PoloFxWobbly *state = polo_fx_wobbly_new();
	PoloFxGrid *grid = polo_fx_grid_new(3, 3);
	PoloFxWobblyParameters params = polo_fx_wobbly_parameters_for_level(0);
	PoloFxOrigin origin = { 10, 20 };
	PoloFxSize size = { 30, 60 };
	polo_fx_wobbly_start(state, origin, size, params, 0.5, 0.0);
	polo_fx_wobbly_fill_grid(state, grid);
	g_assert_cmpfloat(grid->vertices[0], ==, 10.0f);
	g_assert_cmpfloat(grid->vertices[1], ==, 20.0f);
	g_assert_cmpfloat(grid->vertices[2], ==, 0.0f);
	g_assert_cmpfloat(grid->vertices[3], ==, 0.0f);
	guint last = (grid->cols * grid->rows - 1) * 4;
	g_assert_cmpfloat(grid->vertices[last], ==, 40.0f);
	g_assert_cmpfloat(grid->vertices[last + 1], ==, 80.0f);
	g_assert_cmpfloat(grid->vertices[last + 2], ==, 1.0f);
	g_assert_cmpfloat(grid->vertices[last + 3], ==, 1.0f);
	polo_fx_grid_free(grid);
	polo_fx_wobbly_free(state);
}

static void
test_wobbly_odd_shapes(void)
{
	const PoloFxSize sizes[] = { { 1, 1 },    { 1, 2000 },     { 2000, 1 },
				     { 0, 0 },    { 0, 400 },      { 400, 0 },
				     { -5, 400 }, { 32767, 32767 } };
	PoloFxWobblyParameters params = polo_fx_wobbly_parameters_for_level(4);
	PoloFxGrid *grid = polo_fx_grid_new(40, 40);

	for (gsize n = 0; n < G_N_ELEMENTS(sizes); n++) {
		PoloFxWobbly state = { 0 };
		PoloFxOrigin origin = { 10, 10 };

		polo_fx_wobbly_start(&state, origin, sizes[n], params, 0.0,
				     1.0);
		for (int frame = 0; frame < 400; frame++) {
			origin.x += frame < 30 ? 25 : 0;
			polo_fx_wobbly_advance(&state, origin, sizes[n], 16.0);
			polo_fx_wobbly_fill_grid(&state, grid);
			for (int i = 0; i < grid->n_floats; i++)
				g_assert_true(isfinite(grid->vertices[i]));
		}
		g_assert_false(state.active);
	}
	polo_fx_grid_free(grid);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/wobbly/surface-settles", test_wobbly_surface_settles);
	g_test_add_func("/wobbly/stiffness-changes-response",
			test_wobbly_stiffness_changes_response);
	g_test_add_func("/wobbly/presets", test_wobbly_presets);
	g_test_add_func("/wobbly/fill-grid", test_wobbly_fill_grid);
	g_test_add_func("/wobbly/odd-shapes", test_wobbly_odd_shapes);
	return g_test_run();
}
