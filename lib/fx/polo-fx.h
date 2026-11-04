/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * libpolo-fx: per-frame effect mathematics (Wobbly Windows, Magic Lamp,
 * deformation grids), ported from my picom wobbly branch.
 *
 * C side of vapi/polo-fx.vapi. Must not include libweston, Wayland or X
 * headers.
 */

#ifndef POLO_FX_H
#define POLO_FX_H

#include <glib.h>

G_BEGIN_DECLS

const char *
polo_fx_source(void);

#define POLO_FX_GRID_MAX_VERTICES 65535
#define POLO_FX_WOBBLY_MARGIN 100

typedef struct {
	guint cols;
	guint rows;
	int n_floats;
	float *vertices;
} PoloFxGrid;

typedef struct {
	double x;
	double y;
} PoloFxVec2;

typedef struct {
	int x;
	int y;
} PoloFxOrigin;

typedef struct {
	int width;
	int height;
} PoloFxSize;

typedef struct {
	double stiffness;
	double drag;
	double move_factor;
	double min_velocity;
	double max_velocity;
	double stop_velocity;
	double min_acceleration;
	double max_acceleration;
	double stop_acceleration;
	int x_tessellation;
	int y_tessellation;
} PoloFxWobblyParameters;

#define POLO_FX_WOBBLY_PRESET_COUNT 5

typedef struct _PoloFxWobbly PoloFxWobbly;

/* NULL unless 2 <= cols, 2 <= rows and cols * rows <= the limit. */
PoloFxGrid *
polo_fx_grid_new(guint cols, guint rows);

void
polo_fx_grid_free(PoloFxGrid *grid);

void
polo_fx_grid_set_rect(PoloFxGrid *grid, float x, float y, float width,
		      float height);

void
polo_fx_grid_set_bezier(PoloFxGrid *grid, const PoloFxVec2 points[16]);

void
polo_fx_grid_set_subgrid(PoloFxGrid *grid, const PoloFxGrid *source,
		 float u0, float v0, float u1, float v1);

PoloFxWobbly *
polo_fx_wobbly_new(void);

PoloFxWobblyParameters
polo_fx_wobbly_parameters_for_level(int level);

void
polo_fx_wobbly_free(PoloFxWobbly *wobbly);

void
polo_fx_wobbly_start(PoloFxWobbly *wobbly, PoloFxOrigin origin,
		     PoloFxSize size, PoloFxWobblyParameters parameters,
		     double anchor_u, double anchor_v);

gboolean
polo_fx_wobbly_advance(PoloFxWobbly *wobbly, PoloFxOrigin origin,
		       PoloFxSize size, double elapsed_ms);

PoloFxVec2
polo_fx_wobbly_point(const PoloFxVec2 *points, double x, double y);

void
polo_fx_wobbly_fill_grid(const PoloFxWobbly *wobbly, PoloFxGrid *grid);

void
polo_fx_wobbly_set_ease_out(PoloFxWobbly *wobbly, gboolean ease);

void
polo_fx_wobbly_set_fade(PoloFxWobbly *wobbly, double elapsed_ms,
			double total_ms);

G_END_DECLS

#endif /* POLO_FX_H */
