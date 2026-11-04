/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Based on KWin's Wobbly Windows effect.
 * SPDX-FileCopyrightText: 2008 Cédric Borgese <cedric.borgese@gmail.com>
 * The 4x4 control surface and default spring parameters follow KWin's effect.
 * The middle of the title edge is held during a move.
 * Ported from picom wobbly branch 5ab12b3142be247034568976c88c40a9dac424dd.
 */

#include <math.h>

#include "wobbly-private.h"

static double
limit(double value, double low, double high)
{
	return fmax(low, fmin(value, high));
}

static double
limit_motion(double value, double minimum, double maximum)
{
	if (fabs(value) < minimum)
		return 0.0;
	return limit(value, -maximum, maximum);
}

static void
smooth_grid(PoloFxVec2 *values)
{
	PoloFxVec2 smoothed[WOBBLY_POINT_COUNT];
	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			int index = j * 4 + i;
			PoloFxVec2 sum = { .x = values[index].x * 3.0,
					   .y = values[index].y * 3.0 };
			int weight = 3;
			for (int y = j - 1; y <= j + 1; y++) {
				for (int x = i - 1; x <= i + 1; x++) {
					if (x < 0 || y < 0 || x >= 4 ||
					    y >= 4 || (x == i && y == j))
						continue;
					sum.x += values[y * 4 + x].x;
					sum.y += values[y * 4 + x].y;
					weight++;
				}
			}
			smoothed[index] = (PoloFxVec2){ .x = sum.x / weight,
							.y = sum.y / weight };
		}
	}
	for (int i = 0; i < WOBBLY_POINT_COUNT; i++)
		values[i] = smoothed[i];
}

static PoloFxVec2
rest_point(PoloFxOrigin origin, PoloFxSize size, int i, int j)
{
	return (PoloFxVec2){ .x = origin.x + (double)size.width * i / 3.0,
			     .y = origin.y + (double)size.height * j / 3.0 };
}

PoloFxWobbly *
polo_fx_wobbly_new(void)
{
	return g_new0(PoloFxWobbly, 1);
}

void
polo_fx_wobbly_free(PoloFxWobbly *wobbly)
{
	g_free(wobbly);
}

void
polo_fx_wobbly_start(PoloFxWobbly *state, PoloFxOrigin old_origin,
		     PoloFxSize old_size, PoloFxWobblyParameters parameters,
		     double anchor_u, double anchor_v)
{
	g_return_if_fail(state != NULL);
	state->active = TRUE;
	state->origin = old_origin;
	state->size = old_size;
	state->parameters = parameters;
	anchor_u = limit(anchor_u, 0.0, 1.0) * (WOBBLY_GRID_SIZE - 1);
	anchor_v = limit(anchor_v, 0.0, 1.0) * (WOBBLY_GRID_SIZE - 1);
	state->anchor_row =
		CLAMP((int)round(anchor_v), 0, WOBBLY_GRID_SIZE - 1);
	state->anchor_col_first =
		CLAMP((int)floor(anchor_u), 0, WOBBLY_GRID_SIZE - 1);
	state->anchor_col_last =
		CLAMP((int)ceil(anchor_u), 0, WOBBLY_GRID_SIZE - 1);
	for (int j = 0; j < WOBBLY_GRID_SIZE; j++) {
		for (int i = 0; i < WOBBLY_GRID_SIZE; i++) {
			int index = j * WOBBLY_GRID_SIZE + i;
			state->position[index] =
				rest_point(old_origin, old_size, i, j);
			state->velocity[index] = (PoloFxVec2){ 0 };
		}
	}
}

void
polo_fx_wobbly_set_ease_out(PoloFxWobbly *wobbly, gboolean ease)
{
	g_return_if_fail(wobbly != NULL);
	wobbly->ease_out = ease;
}

static gboolean
ease_out(PoloFxWobbly *state, PoloFxOrigin origin, PoloFxSize size, double ms)
{
	double velocity_sum = 0.0, worst = 0.0;
	double keep = pow(WOBBLY_EASE_KEEP, ms / 16.0);

	for (int index = 0; index < WOBBLY_POINT_COUNT; index++)
		velocity_sum += fabs(state->velocity[index].x) +
			fabs(state->velocity[index].y);
	if (velocity_sum >= state->parameters.stop_velocity)
		return TRUE;
	for (int index = 0; index < WOBBLY_POINT_COUNT; index++) {
		PoloFxVec2 target =
			rest_point(origin, size, index % WOBBLY_GRID_SIZE,
				   index / WOBBLY_GRID_SIZE);
		PoloFxVec2 *position = &state->position[index];

		position->x = target.x + (position->x - target.x) * keep;
		position->y = target.y + (position->y - target.y) * keep;
		state->velocity[index] = (PoloFxVec2){ 0 };
		worst = fmax(worst,
			     fmax(fabs(position->x - target.x),
				  fabs(position->y - target.y)));
	}
	return worst >= WOBBLY_EASE_DONE;
}

static gboolean
wobbly_step(PoloFxWobbly *state, PoloFxOrigin origin, PoloFxSize size,
	    double ms)
{
	const PoloFxWobblyParameters *params = &state->parameters;
	PoloFxVec2 acceleration[WOBBLY_POINT_COUNT];
	double acceleration_sum = 0.0;
	double velocity_sum = 0.0;
	for (int j = 0; j < WOBBLY_GRID_SIZE; j++) {
		for (int i = 0; i < WOBBLY_GRID_SIZE; i++) {
			int index = j * WOBBLY_GRID_SIZE + i;
			PoloFxVec2 pos = state->position[index];
			PoloFxVec2 target = rest_point(origin, size, i, j);

			if (j == state->anchor_row &&
			    (i == state->anchor_col_first ||
			     i == state->anchor_col_last)) {
				acceleration[index] = (PoloFxVec2){
					(target.x - pos.x) * params->stiffness,
					(target.y - pos.y) * params->stiffness
				};
				continue;
			}
			PoloFxVec2 force = { 0 };
			int neighbors = 0;
			const int di[] = { -1, 1, 0, 0 };
			const int dj[] = { 0, 0, -1, 1 };
			for (int n = 0; n < 4; n++) {
				int ni = i + di[n], nj = j + dj[n];
				if (ni < 0 || ni == WOBBLY_GRID_SIZE ||
				    nj < 0 || nj == WOBBLY_GRID_SIZE)
					continue;
				PoloFxVec2 other =
					state->position[nj * WOBBLY_GRID_SIZE +
							ni];
				PoloFxVec2 rest =
					rest_point(origin, size, ni, nj);
				force.x +=
					other.x - pos.x - (rest.x - target.x);
				force.y +=
					other.y - pos.y - (rest.y - target.y);
				neighbors++;
			}
			acceleration[index] = (PoloFxVec2){
				force.x * params->stiffness / neighbors,
				force.y * params->stiffness / neighbors
			};
		}
	}
	smooth_grid(acceleration);
	for (int index = 0; index < WOBBLY_POINT_COUNT; index++) {
		PoloFxVec2 *velocity = &state->velocity[index];
		double ax = limit_motion(acceleration[index].x,
					 params->min_acceleration,
					 params->max_acceleration);
		double ay = limit_motion(acceleration[index].y,
					 params->min_acceleration,
					 params->max_acceleration);
		acceleration_sum += fabs(ax) + fabs(ay);
		velocity->x = ax * ms + velocity->x * params->drag;
		velocity->y = ay * ms + velocity->y * params->drag;
	}
	smooth_grid(state->velocity);
	for (int index = 0; index < WOBBLY_POINT_COUNT; index++) {
		PoloFxVec2 target =
			rest_point(origin, size, index % 4, index / 4);
		PoloFxVec2 *velocity = &state->velocity[index];
		PoloFxVec2 *position = &state->position[index];
		velocity->x = limit_motion(velocity->x, params->min_velocity,
					   params->max_velocity);
		velocity->y = limit_motion(velocity->y, params->min_velocity,
					   params->max_velocity);
		position->x += velocity->x * ms * params->move_factor;
		position->y += velocity->y * ms * params->move_factor;
		position->x = limit(position->x, target.x - WOBBLY_MARGIN,
				    target.x + WOBBLY_MARGIN);
		position->y = limit(position->y, target.y - WOBBLY_MARGIN,
				    target.y + WOBBLY_MARGIN);
		velocity_sum += fabs(velocity->x) + fabs(velocity->y);
	}
	return acceleration_sum >= params->stop_acceleration ||
		velocity_sum >= params->stop_velocity;
}

gboolean
polo_fx_wobbly_advance(PoloFxWobbly *state, PoloFxOrigin origin,
		       PoloFxSize size, double elapsed_ms)
{
	g_return_val_if_fail(state != NULL, FALSE);
	if (!state->active)
		return FALSE;
	if (size.width <= 0 || size.height <= 0) {
		state->active = FALSE;
		return FALSE;
	}
	double remaining = limit(elapsed_ms, 1.0, 50.0);
	gboolean moving = TRUE;
	state->origin = origin;
	state->size = size;
	while (remaining > 0) {
		double step = fmin(remaining, 10.0);
		moving = wobbly_step(state, origin, size, step);
		remaining -= step;
	}
	if (moving && state->ease_out)
		moving = ease_out(state, origin, size,
				  limit(elapsed_ms, 1.0, 50.0));
	if (!moving) {
		state->active = FALSE;
		for (int index = 0; index < WOBBLY_POINT_COUNT; index++) {
			state->position[index] = rest_point(
				origin, size, index % WOBBLY_GRID_SIZE,
				index / WOBBLY_GRID_SIZE);
			state->velocity[index] = (PoloFxVec2){ 0 };
		}
	}
	return state->active;
}

PoloFxVec2
polo_fx_wobbly_point(const PoloFxVec2 *points, double x, double y)
{
	double u = 1.0 - x, v = 1.0 - y;
	double bx[4] = { u * u * u, 3.0 * x * u * u, 3.0 * x * x * u,
			 x * x * x };
	double by[4] = { v * v * v, 3.0 * y * v * v, 3.0 * y * y * v,
			 y * y * y };
	PoloFxVec2 result = { 0 };
	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			double weight = bx[i] * by[j];
			result.x += points[j * 4 + i].x * weight;
			result.y += points[j * 4 + i].y * weight;
		}
	}
	return result;
}

void
polo_fx_wobbly_set_fade(PoloFxWobbly *wobbly, double elapsed_ms,
			double total_ms)
{
	g_return_if_fail(wobbly != NULL);
	wobbly->fade_elapsed_ms = elapsed_ms;
	wobbly->fade_total_ms = total_ms;
}

static double
fade_share(const PoloFxWobbly *wobbly)
{
	double t, u;

	if (wobbly->fade_total_ms <= 0.0)
		return 1.0;
	t = wobbly->fade_elapsed_ms / wobbly->fade_total_ms;
	if (t <= 0.5)
		return 1.0;
	if (t >= 1.0)
		return 0.0;
	u = (t - 0.5) / 0.5;
	return 1.0 - u * u * (3.0 - 2.0 * u);
}

void
polo_fx_wobbly_fill_grid(const PoloFxWobbly *wobbly, PoloFxGrid *grid)
{
	PoloFxVec2 points[WOBBLY_POINT_COUNT];
	double share;

	g_return_if_fail(wobbly != NULL && grid != NULL);
	share = fade_share(wobbly);
	if (share >= 1.0) {
		polo_fx_grid_set_bezier(grid, wobbly->position);
		return;
	}
	for (int index = 0; index < WOBBLY_POINT_COUNT; index++) {
		PoloFxVec2 rest = rest_point(wobbly->origin, wobbly->size,
					     index % WOBBLY_GRID_SIZE,
					     index / WOBBLY_GRID_SIZE);

		points[index].x =
			rest.x + (wobbly->position[index].x - rest.x) * share;
		points[index].y =
			rest.y + (wobbly->position[index].y - rest.y) * share;
	}
	polo_fx_grid_set_bezier(grid, points);
}
