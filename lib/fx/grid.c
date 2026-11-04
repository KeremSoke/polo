/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "polo-fx.h"

#include <math.h>

PoloFxGrid *
polo_fx_grid_new(guint cols, guint rows)
{
	PoloFxGrid *grid;

	g_return_val_if_fail(cols >= 2 && rows >= 2, NULL);
	g_return_val_if_fail((guint64)cols * rows <= POLO_FX_GRID_MAX_VERTICES,
			     NULL);
	grid = g_new0(PoloFxGrid, 1);
	grid->cols = cols;
	grid->rows = rows;
	grid->n_floats = (int)(cols * rows * 4);
	grid->vertices = g_new0(float, grid->n_floats);
	return grid;
}

void
polo_fx_grid_free(PoloFxGrid *grid)
{
	if (!grid)
		return;
	g_free(grid->vertices);
	g_free(grid);
}

void
polo_fx_grid_set_rect(PoloFxGrid *grid, float x, float y, float width,
		      float height)
{
	float *v = grid->vertices;

	for (guint r = 0; r < grid->rows; r++) {
		float t = (float)r / (float)(grid->rows - 1);

		for (guint c = 0; c < grid->cols; c++) {
			float s = (float)c / (float)(grid->cols - 1);

			*v++ = x + s * width;
			*v++ = y + t * height;
			*v++ = s;
			*v++ = t;
		}
	}
}

static PoloFxVec2
bezier_point(const PoloFxVec2 points[16], double x, double y)
{
	double u = 1.0 - x, v = 1.0 - y;
	double bx[4] = { u * u * u, 3.0 * x * u * u, 3.0 * x * x * u,
			 x * x * x };
	double by[4] = { v * v * v, 3.0 * y * v * v, 3.0 * y * y * v,
			 y * y * y };
	PoloFxVec2 result = { 0 };
	for (int row = 0; row < 4; row++) {
		for (int col = 0; col < 4; col++) {
			double weight = bx[col] * by[row];
			result.x += points[row * 4 + col].x * weight;
			result.y += points[row * 4 + col].y * weight;
		}
	}
	return result;
}

void
polo_fx_grid_set_bezier(PoloFxGrid *grid, const PoloFxVec2 points[16])
{
	g_return_if_fail(grid != NULL && points != NULL);
	float *vertex = grid->vertices;
	for (guint row = 0; row < grid->rows; row++) {
		double v = (double)row / (grid->rows - 1);
		for (guint col = 0; col < grid->cols; col++) {
			double u = (double)col / (grid->cols - 1);
			PoloFxVec2 point = bezier_point(points, u, v);
			*vertex++ = point.x;
			*vertex++ = point.y;
			*vertex++ = u;
			*vertex++ = v;
		}
	}
}

static void
sample_grid(const PoloFxGrid *grid, double u, double v, float *x, float *y)
{
	double gx = CLAMP(u, 0.0, 1.0) * (grid->cols - 1);
	double gy = CLAMP(v, 0.0, 1.0) * (grid->rows - 1);
	guint x0 = (guint)floor(gx), y0 = (guint)floor(gy);
	guint x1 = MIN(x0 + 1, grid->cols - 1);
	guint y1 = MIN(y0 + 1, grid->rows - 1);
	double tx = gx - x0, ty = gy - y0;
	const float *a = grid->vertices + (y0 * grid->cols + x0) * 4;
	const float *b = grid->vertices + (y0 * grid->cols + x1) * 4;
	const float *c = grid->vertices + (y1 * grid->cols + x0) * 4;
	const float *d = grid->vertices + (y1 * grid->cols + x1) * 4;
	*x = (float)((a[0] * (1.0 - tx) + b[0] * tx) * (1.0 - ty) +
		     (c[0] * (1.0 - tx) + d[0] * tx) * ty);
	*y = (float)((a[1] * (1.0 - tx) + b[1] * tx) * (1.0 - ty) +
		     (c[1] * (1.0 - tx) + d[1] * tx) * ty);
}

void
polo_fx_grid_set_subgrid(PoloFxGrid *grid, const PoloFxGrid *source, float u0,
			 float v0, float u1, float v1)
{
	g_return_if_fail(grid != NULL && source != NULL);
	float *vertex = grid->vertices;
	for (guint row = 0; row < grid->rows; row++) {
		float t = (float)row / (grid->rows - 1);
		double v = v0 + t * (v1 - v0);
		for (guint col = 0; col < grid->cols; col++) {
			float s = (float)col / (grid->cols - 1);
			double u = u0 + s * (u1 - u0);
			sample_grid(source, u, v, vertex, vertex + 1);
			vertex[2] = s;
			vertex[3] = t;
			vertex += 4;
		}
	}
}
