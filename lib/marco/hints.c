/*
 * Copyright (C) 2001, 2002, 2003 Red Hat, Inc.
 * Copyright (C) 2004, 2005 Elijah Newren
 * Copyright (C) 2009 Thomas Thurman
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Ported from Marco 1.28.2 src/core/window-props.c (meta_set_normal_hints).
 */

#include "polo-marco.h"

#define MAX_HINT 32767

static int
bounded(int value)
{
	return CLAMP(value, 0, MAX_HINT);
}

static int
bounded_max(int value)
{
	return value > MAX_HINT ? G_MAXINT : MAX(value, 0);
}

void
polo_marco_normalize_size_hints(PoloSizeHints *hints)
{
	double minr, maxr;
	int minw, minh, maxw, maxh;
	int basew, baseh, winc, hinc;

	hints->min_width = bounded(hints->min_width);
	hints->min_height = bounded(hints->min_height);
	hints->base_width = bounded(hints->base_width);
	hints->base_height = bounded(hints->base_height);
	hints->width_inc = bounded(hints->width_inc);
	hints->height_inc = bounded(hints->height_inc);
	hints->max_width = bounded_max(hints->max_width);
	hints->max_height = bounded_max(hints->max_height);

	if (hints->flags & POLO_SIZE_HINT_P_BASE_SIZE)
		;
	else if (hints->flags & POLO_SIZE_HINT_P_MIN_SIZE) {
		hints->base_width = hints->min_width;
		hints->base_height = hints->min_height;
	} else {
		hints->base_width = 0;
		hints->base_height = 0;
	}
	hints->flags |= POLO_SIZE_HINT_P_BASE_SIZE;

	if (hints->flags & POLO_SIZE_HINT_P_MIN_SIZE)
		;
	else if (hints->flags & POLO_SIZE_HINT_P_BASE_SIZE) {
		hints->min_width = hints->base_width;
		hints->min_height = hints->base_height;
	} else {
		hints->min_width = 0;
		hints->min_height = 0;
	}
	hints->flags |= POLO_SIZE_HINT_P_MIN_SIZE;

	if (!(hints->flags & POLO_SIZE_HINT_P_MAX_SIZE)) {
		hints->max_width = G_MAXINT;
		hints->max_height = G_MAXINT;
		hints->flags |= POLO_SIZE_HINT_P_MAX_SIZE;
	}

	if (!(hints->flags & POLO_SIZE_HINT_P_RESIZE_INC)) {
		hints->width_inc = 1;
		hints->height_inc = 1;
		hints->flags |= POLO_SIZE_HINT_P_RESIZE_INC;
	}

	if (!(hints->flags & POLO_SIZE_HINT_P_ASPECT)) {
		hints->min_aspect.x = 1;
		hints->min_aspect.y = G_MAXINT;
		hints->max_aspect.x = G_MAXINT;
		hints->max_aspect.y = 1;
		hints->flags |= POLO_SIZE_HINT_P_ASPECT;
	}

	if (!(hints->flags & POLO_SIZE_HINT_P_WIN_GRAVITY)) {
		hints->win_gravity = POLO_GRAVITY_NORTH_WEST;
		hints->flags |= POLO_SIZE_HINT_P_WIN_GRAVITY;
	}

	if (hints->min_width < 1)
		hints->min_width = 1;
	if (hints->max_width < 1)
		hints->max_width = 1;
	if (hints->min_height < 1)
		hints->min_height = 1;
	if (hints->max_height < 1)
		hints->max_height = 1;

	if (hints->width_inc < 1)
		hints->width_inc = 1;
	if (hints->height_inc < 1)
		hints->height_inc = 1;
	if (hints->min_aspect.y < 1)
		hints->min_aspect.y = 1;
	if (hints->max_aspect.y < 1)
		hints->max_aspect.y = 1;

	minw = hints->min_width;
	minh = hints->min_height;
	maxw = hints->max_width;
	maxh = hints->max_height;
	basew = hints->base_width;
	baseh = hints->base_height;
	winc = hints->width_inc;
	hinc = hints->height_inc;

	if ((minw - basew) % winc != 0) {
		hints->min_width = basew + ((minw - basew) / winc + 1) * winc;
		minw = hints->min_width;
	}
	if (maxw != G_MAXINT && (maxw - basew) % winc != 0) {
		hints->max_width = basew + ((maxw - basew) / winc) * winc;
		maxw = hints->max_width;
	}
	if ((minh - baseh) % hinc != 0) {
		hints->min_height = baseh + ((minh - baseh) / hinc + 1) * hinc;
		minh = hints->min_height;
	}
	if (maxh != G_MAXINT && (maxh - baseh) % hinc != 0) {
		hints->max_height = baseh + ((maxh - baseh) / hinc) * hinc;
		maxh = hints->max_height;
	}

	if (hints->max_width < hints->min_width)
		maxw = hints->max_width = hints->min_width;
	if (hints->max_height < hints->min_height)
		maxh = hints->max_height = hints->min_height;

	minr = hints->min_aspect.x / (double)hints->min_aspect.y;
	maxr = hints->max_aspect.x / (double)hints->max_aspect.y;
	if (minr > maxr) {
		hints->min_aspect.x = 1;
		hints->min_aspect.y = G_MAXINT;
		hints->max_aspect.x = G_MAXINT;
		hints->max_aspect.y = 1;
	} else {
		if (minh > 0 && minr > (maxw / (double)minh)) {
			hints->min_aspect.x = 1;
			hints->min_aspect.y = G_MAXINT;
		}
		if (maxr < (minw / (double)maxh)) {
			hints->max_aspect.x = G_MAXINT;
			hints->max_aspect.y = 1;
		}
	}
}
