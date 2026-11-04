#pragma once

#include "polo-fx.h"

#define WOBBLY_GRID_SIZE 4
#define WOBBLY_POINT_COUNT (WOBBLY_GRID_SIZE * WOBBLY_GRID_SIZE)
#define WOBBLY_MARGIN 100
/* polo_fx_wobbly_set_ease_out (): the share of the bend kept per 16 ms, and
 * the largest offset, in pixels, left when it settles. */
#define WOBBLY_EASE_KEEP 0.8
#define WOBBLY_EASE_DONE 0.5

struct _PoloFxWobbly {
	PoloFxVec2 position[WOBBLY_POINT_COUNT];
	PoloFxVec2 velocity[WOBBLY_POINT_COUNT];
	PoloFxWobblyParameters parameters;
	int anchor_row;
	int anchor_col_first;
	int anchor_col_last;
	gboolean ease_out;

	double fade_elapsed_ms;
	double fade_total_ms;

	PoloFxOrigin origin;
	PoloFxSize size;
	gboolean active;
};
