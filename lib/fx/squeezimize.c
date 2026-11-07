/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wayfire's squeezimize (plugins/animate/squeezimize.hpp, commit e268d4e5):
 * the window squeezes into its minimise target, narrowing towards it along
 * a sigmoid while sliding in. Wayfire's fragment shader maps each screen
 * point back into the window; here the same mapping is inverted in closed
 * form, so the window's points go forward onto a G1 grid. Targets on the
 * left or right use the top and bottom shader on swapped axes.
 *
 * Copyright (c) 2024 Scott Moreau
 * SPDX-License-Identifier: MIT
 */

#include "polo-fx.h"

#include <math.h>

#define GRID_ALONG 96

typedef struct {
	double gx, gy, gw, gh;
	double sbx, sby, sbz, sbw;
	double tbx, tby, tbz, tbw;
	gboolean upward;
	gboolean swapped;
	double progress;
} Frame;

static PoloFxVec2
swap(PoloFxVec2 v, gboolean swapped)
{
	return swapped ? (PoloFxVec2){ v.y, v.x } : v;
}

static Frame
frame_of(const PoloFxSqueezimize *sq)
{
	gboolean swapped = sq->direction == POLO_FX_MAGIC_LAMP_LEFT ||
		sq->direction == POLO_FX_MAGIC_LAMP_RIGHT;
	PoloFxVec2 so = swap(sq->origin, swapped);
	PoloFxVec2 ss = swap(sq->size, swapped);
	PoloFxVec2 to = swap(sq->target, swapped);
	PoloFxVec2 ts = swap(sq->target_size, swapped);
	Frame f = { 0 };

	f.swapped = swapped;
	f.upward = sq->direction == POLO_FX_MAGIC_LAMP_TOP ||
		sq->direction == POLO_FX_MAGIC_LAMP_LEFT;
	f.progress = CLAMP(sq->progress, 0.0, 1.0);
	f.gx = MIN(so.x, to.x);
	f.gy = MIN(so.y, to.y);
	f.gw = MAX(MAX(ss.x, ts.x),
		   MAX(to.x + ts.x - so.x, so.x + ss.x - to.x));
	f.gh = MAX(MAX(ss.y, ts.y),
		   MAX(to.y + ts.y - so.y, so.y + ss.y - to.y));
	f.gw = MAX(f.gw, 1.0);
	f.gh = MAX(f.gh, 1.0);
	f.sbx = (so.x - f.gx) / f.gw;
	f.sby = (so.y - f.gy) / f.gh;
	f.sbz = (so.x + ss.x - f.gx) / f.gw;
	f.sbw = (so.y + ss.y - f.gy) / f.gh;
	f.tbx = (to.x - f.gx) / f.gw;
	f.tby = (to.y - f.gy) / f.gh;
	f.tbz = (to.x + ts.x - f.gx) / f.gw;
	f.tbw = (to.y + ts.y - f.gy) / f.gh;
	return f;
}

static double
sigmoid(const Frame *f, double uv_y)
{
	double y = f->upward ? uv_y : 1.0 - uv_y;
	double span = f->upward ? f->sbw - f->tbw : f->tbw - f->sby;

	if (fabs(span) < 1e-9)
		span = span < 0 ? -1e-9 : 1e-9;
	return 1.0 / (1.0 + pow(2.718, -(y * (1.0 / span) * 15.0 - 10.0)));
}

static double
shift(const Frame *f)
{
	double inv_h = 1.0 / (f->sbw - f->sby);
	double p2 = f->progress * f->progress;

	return f->upward ? -p2 * (inv_h - f->tbw)
			 : p2 * (inv_h - f->tby + f->tbw);
}

static double
squeeze(const Frame *f)
{
	double p = CLAMP(f->progress, 0.0, 0.25) * 4.0;

	return p * p;
}

gboolean
polo_fx_squeezimize_texture_at(const PoloFxSqueezimize *sq, PoloFxVec2 screen,
			       PoloFxVec2 *window)
{
	Frame f;
	PoloFxVec2 s;
	double uv_x, uv_y, inv_h, tex_y, t, sx0, sx1, tex_x;

	g_return_val_if_fail(sq != NULL && window != NULL, FALSE);
	f = frame_of(sq);
	s = swap(screen, f.swapped);
	uv_x = (s.x - f.gx) / f.gw;
	uv_y = 1.0 - (s.y - f.gy) / f.gh;
	inv_h = 1.0 / (f.sbw - f.sby);
	tex_y = inv_h * (uv_y - 1.0 + f.sbw) + shift(&f);
	t = sigmoid(&f, uv_y) * squeeze(&f);
	sx0 = f.sbx + (f.tbx - f.sbx) * t;
	sx1 = f.sbz + (f.tbz - f.sbz) * t;
	tex_x = (uv_x - sx0) / (sx1 - sx0);
	*window = swap((PoloFxVec2){ tex_x, 1.0 - tex_y }, f.swapped);
	return tex_x >= 0.0 && tex_x <= 1.0 && tex_y >= 0.0 && tex_y <= 1.0;
}

PoloFxVec2
polo_fx_squeezimize_point(const PoloFxSqueezimize *sq, PoloFxVec2 window)
{
	Frame f;
	PoloFxVec2 w;
	double inv_h, uv_y, t, sx0, sx1, uv_x;

	g_return_val_if_fail(sq != NULL, window);
	f = frame_of(sq);
	w = swap(window, f.swapped);
	inv_h = 1.0 / (f.sbw - f.sby);
	uv_y = ((1.0 - w.y) - shift(&f)) / inv_h + 1.0 - f.sbw;
	t = sigmoid(&f, uv_y) * squeeze(&f);
	sx0 = f.sbx + (f.tbx - f.sbx) * t;
	sx1 = f.sbz + (f.tbz - f.sbz) * t;
	uv_x = sx0 + w.x * (sx1 - sx0);
	return swap(
		(PoloFxVec2){ f.gx + uv_x * f.gw, f.gy + (1.0 - uv_y) * f.gh },
		f.swapped);
}

void
polo_fx_squeezimize_grid_size(const PoloFxSqueezimize *sq, guint *cols,
			      guint *rows)
{
	gboolean swapped;

	g_return_if_fail(sq != NULL && cols != NULL && rows != NULL);
	swapped = sq->direction == POLO_FX_MAGIC_LAMP_LEFT ||
		sq->direction == POLO_FX_MAGIC_LAMP_RIGHT;
	*cols = swapped ? GRID_ALONG : 2;
	*rows = swapped ? 2 : GRID_ALONG;
}

void
polo_fx_squeezimize_fill_grid(const PoloFxSqueezimize *sq, PoloFxGrid *grid)
{
	float *vertex;

	g_return_if_fail(sq != NULL && grid != NULL);
	vertex = grid->vertices;
	for (guint row = 0; row < grid->rows; row++) {
		double v = (double)row / (grid->rows - 1);

		for (guint col = 0; col < grid->cols; col++) {
			double u = (double)col / (grid->cols - 1);
			PoloFxVec2 screen = polo_fx_squeezimize_point(
				sq, (PoloFxVec2){ u, v });

			*vertex++ = (float)screen.x;
			*vertex++ = (float)screen.y;
			*vertex++ = (float)u;
			*vertex++ = (float)v;
		}
	}
}

double
polo_fx_squeezimize_advance(double progress, gboolean restoring,
			    double elapsed_ms, double duration_ms)
{
	double step = MAX(elapsed_ms, 0.0) / MAX(duration_ms, 1.0);

	return CLAMP(progress + (restoring ? -step : step), 0.0, 1.0);
}
