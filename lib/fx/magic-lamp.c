/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Based on KWin's Magic Lamp effect.
 * SPDX-FileCopyrightText: 2008 Martin Gräßlin <mgraesslin@kde.org>
 * Vertex displacement adapted from KWin's Magic Lamp effect, based on Rivo
 * Laks' minimize animation (kwin/src/plugins/magiclamp/magiclamp.cpp). The
 * timeline and degenerate-geometry guards come from picom.
 */

#include <math.h>

#include "polo-fx.h"

#define CELL_SIZE 40
#define MAX_CELLS 254

double
polo_fx_magic_lamp_advance(double progress, gboolean restoring,
			   double elapsed_ms, double duration_ms)
{
	double step = fmax(0.0, elapsed_ms) / fmax(duration_ms, 0.001);

	return fmax(0.0, fmin(1.0, progress + (restoring ? -step : step)));
}

PoloFxVec2
polo_fx_magic_lamp_point(const PoloFxMagicLamp *m, PoloFxVec2 source)
{
	double p = fmax(0.0, fmin(1.0, m->progress));
	PoloFxVec2 point = { .x = m->origin.x + source.x,
			     .y = m->origin.y + source.y };

	if (p == 0 || m->size.x <= 0 || m->size.y <= 0)
		return point;

	gboolean horizontal = m->direction == POLO_FX_MAGIC_LAMP_LEFT ||
		m->direction == POLO_FX_MAGIC_LAMP_RIGHT;
	gboolean forward = m->direction == POLO_FX_MAGIC_LAMP_BOTTOM ||
		m->direction == POLO_FX_MAGIC_LAMP_RIGHT;
	double length = horizontal ? m->size.x : m->size.y;
	double a = horizontal ? source.x : source.y;
	double origin = horizontal ? m->origin.x : m->origin.y;
	double target = horizontal ? m->target.x : m->target.y;
	double target_length = horizontal ? m->target_size.x : m->target_size.y;
	double edge = target + (forward ? 0 : target_length) - origin;
	double factor =
		(forward ? a + (length - a) * p : length - a + a * p) / length;
	double distance = forward ? edge - a : a - edge;
	double offset = (forward ? edge + a : length + a - edge) * p * factor *
		factor * factor;
	/* A maximised window or the pointer fallback can touch the target
	 * exactly: avoid 0/0, and keep overlapping or stale icon rectangles
	 * from blowing up the mesh. */
	double q =
		distance > 1e-6 ? fmax(0.0, fmin(1.0, offset / distance)) : p;
	double moved =
		forward ? fmin(edge, a + offset) : fmax(edge, a - offset);

	if (p == 1) {
		q = 1;
		moved = edge;
	}
	if (horizontal) {
		point.x = origin + moved;
		point.y += (m->target.y +
			    m->target_size.y * source.y / m->size.y - point.y) *
			q;
	} else {
		point.y = origin + moved;
		point.x += (m->target.x +
			    m->target_size.x * source.x / m->size.x - point.x) *
			q;
	}
	return point;
}

static guint
cells(double length)
{
	double count = ceil(length / CELL_SIZE);

	return (guint)fmax(1.0, fmin(MAX_CELLS, count));
}

void
polo_fx_magic_lamp_grid_size(const PoloFxMagicLamp *lamp, guint *cols,
			     guint *rows)
{
	*cols = cells(lamp->size.x) + 1;
	*rows = cells(lamp->size.y) + 1;
}

void
polo_fx_magic_lamp_fill_grid(const PoloFxMagicLamp *lamp, PoloFxGrid *grid)
{
	float *vertex;

	g_return_if_fail(lamp != NULL && grid != NULL);
	vertex = grid->vertices;
	for (guint row = 0; row < grid->rows; row++) {
		double v = (double)row / (grid->rows - 1);

		for (guint col = 0; col < grid->cols; col++) {
			double u = (double)col / (grid->cols - 1);
			PoloFxVec2 point = polo_fx_magic_lamp_point(
				lamp,
				(PoloFxVec2){ u * lamp->size.x,
					      v * lamp->size.y });

			*vertex++ = (float)point.x;
			*vertex++ = (float)point.y;
			*vertex++ = (float)u;
			*vertex++ = (float)v;
		}
	}
}
