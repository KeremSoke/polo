/*
 * Copyright (C) 2002, 2003 Red Hat, Inc.
 * Copyright (C) 2003, 2004 Rob Adams
 * Copyright (C) 2005, 2006 Elijah Newren
 * Copyright (C) 2001 Havoc Pennington, Anders Carlsson
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Ported from Marco 1.28.2 src/core/constraints.c (meta_window_constrain
 * and every constraint), with the helpers it calls from src/core/window.c
 * (meta_window_get_current_tile_area, adjust_for_gravity,
 * ensure_size_hints_satisfied) and src/core/screen.c
 * (meta_screen_get_xinerama_for_rect). place_window_if_needed runs in the
 * caller (src/policy/constraints.vala) before polo_marco_constrain ().
 * Windows have no _GTK_FRAME_EXTENTS: xdg-shell window geometry already
 * excludes client-side shadows.
 */

#include <math.h>
#include <stdlib.h>

#include "polo-marco.h"

typedef enum {
	PRIORITY_MINIMUM = 0,
	PRIORITY_ASPECT_RATIO = 0,
	PRIORITY_ENTIRELY_VISIBLE_ON_SINGLE_XINERAMA = 0,
	PRIORITY_ENTIRELY_VISIBLE_ON_WORKAREA = 1,
	PRIORITY_SIZE_HINTS_INCREMENTS = 1,
	PRIORITY_MAXIMIZATION = 2,
	PRIORITY_TILING = 2,
	PRIORITY_FULLSCREEN = 2,
	PRIORITY_SIZE_HINTS_LIMITS = 3,
	PRIORITY_TITLEBAR_VISIBLE = 4,
	PRIORITY_PARTIALLY_VISIBLE_ON_WORKAREA = 4,
	PRIORITY_MAXIMUM = 4
} ConstraintPriority;

typedef enum { ACTION_MOVE, ACTION_RESIZE, ACTION_MOVE_AND_RESIZE } ActionType;

typedef struct {
	PoloRect orig;
	PoloRect current;
	PoloFrameBorders borders_storage;
	const PoloFrameBorders *borders;
	ActionType action_type;
	gboolean is_user_action;

	int resize_gravity;
	PoloFixedDirections fixed_directions;

	PoloRect work_area_xinerama;
	PoloRect entire_xinerama;

	GList *usable_screen_region;
	GList *usable_xinerama_region;

	const PoloMarcoConstraintInput *input;
} ConstraintInfo;

typedef PoloMarcoConstraintWindow MetaWindow;

#define META_WINDOW_MAXIMIZED(w)                                               \
	((w)->maximized_horizontally && (w)->maximized_vertically)
#define META_WINDOW_SIDE_TILED(w)                                              \
	((w)->tiled &&                                                         \
	 ((w)->tile_mode == POLO_TILE_LEFT ||                                  \
	  (w)->tile_mode == POLO_TILE_RIGHT))
#define META_WINDOW_CORNER_TILED(w)                                            \
	((w)->tiled &&                                                         \
	 ((w)->tile_mode == POLO_TILE_BOTTOM_RIGHT ||                          \
	  (w)->tile_mode == POLO_TILE_BOTTOM_LEFT ||                           \
	  (w)->tile_mode == POLO_TILE_TOP_RIGHT ||                             \
	  (w)->tile_mode == POLO_TILE_TOP_LEFT))
#define META_WINDOW_TILED(w)                                                   \
	(META_WINDOW_SIDE_TILED(w) || META_WINDOW_CORNER_TILED(w))

static gboolean
constrain_modal_dialog(MetaWindow *window, ConstraintInfo *info,
		       ConstraintPriority priority, gboolean check_only);
static gboolean
constrain_maximization(MetaWindow *window, ConstraintInfo *info,
		       ConstraintPriority priority, gboolean check_only);
static gboolean
constrain_tiling(MetaWindow *window, ConstraintInfo *info,
		 ConstraintPriority priority, gboolean check_only);
static gboolean
constrain_fullscreen(MetaWindow *window, ConstraintInfo *info,
		     ConstraintPriority priority, gboolean check_only);
static gboolean
constrain_size_increments(MetaWindow *window, ConstraintInfo *info,
			  ConstraintPriority priority, gboolean check_only);
static gboolean
constrain_size_limits(MetaWindow *window, ConstraintInfo *info,
		      ConstraintPriority priority, gboolean check_only);
static gboolean
constrain_aspect_ratio(MetaWindow *window, ConstraintInfo *info,
		       ConstraintPriority priority, gboolean check_only);
static gboolean
constrain_to_single_xinerama(MetaWindow *window, ConstraintInfo *info,
			     ConstraintPriority priority, gboolean check_only);
static gboolean
constrain_fully_onscreen(MetaWindow *window, ConstraintInfo *info,
			 ConstraintPriority priority, gboolean check_only);
static gboolean
constrain_titlebar_visible(MetaWindow *window, ConstraintInfo *info,
			   ConstraintPriority priority, gboolean check_only);
static gboolean
constrain_partially_onscreen(MetaWindow *window, ConstraintInfo *info,
			     ConstraintPriority priority, gboolean check_only);

static void
setup_constraint_info(ConstraintInfo *info,
		      const PoloMarcoConstraintInput *input,
		      PoloMoveResizeFlags flags, int resize_gravity,
		      const PoloRect *orig, PoloRect *new_rect);
static void
update_onscreen_requirements(MetaWindow *window, ConstraintInfo *info);
static void
extend_by_frame(MetaWindow *window, PoloRect *rect,
		const PoloFrameBorders *borders);
static void
unextend_by_frame(MetaWindow *window, PoloRect *rect,
		  const PoloFrameBorders *borders);
static inline void
get_size_limits(const MetaWindow *window, const PoloFrameBorders *borders,
		gboolean include_frame, PoloRect *min_size, PoloRect *max_size);
static void
can_resize_tiled(guint tile_mode, int resize_gravity,
		 gboolean *resize_horizontally, gboolean *resize_vertically);

typedef gboolean (*ConstraintFunc)(MetaWindow *window, ConstraintInfo *info,
				   ConstraintPriority priority,
				   gboolean check_only);

typedef struct {
	ConstraintFunc func;
	const char *name;
} Constraint;

static const Constraint all_constraints[] = {
	{ constrain_modal_dialog, "constrain_modal_dialog" },
	{ constrain_maximization, "constrain_maximization" },
	{ constrain_tiling, "constrain_tiling" },
	{ constrain_fullscreen, "constrain_fullscreen" },
	{ constrain_size_increments, "constrain_size_increments" },
	{ constrain_size_limits, "constrain_size_limits" },
	{ constrain_aspect_ratio, "constrain_aspect_ratio" },
	{ constrain_to_single_xinerama, "constrain_to_single_xinerama" },
	{ constrain_fully_onscreen, "constrain_fully_onscreen" },
	{ constrain_titlebar_visible, "constrain_titlebar_visible" },
	{ constrain_partially_onscreen, "constrain_partially_onscreen" },
	{ NULL, NULL }
};

static gboolean
do_all_constraints(MetaWindow *window, ConstraintInfo *info,
		   ConstraintPriority priority, gboolean check_only)
{
	const Constraint *constraint;
	gboolean satisfied;

	constraint = &all_constraints[0];
	satisfied = TRUE;
	while (constraint->func != NULL) {
		satisfied = satisfied &&
			(*constraint->func)(window, info, priority, check_only);

		if (check_only && !satisfied)
			return FALSE;
		++constraint;
	}

	return TRUE;
}

void
polo_marco_constrain(const PoloMarcoConstraintInput *input,
		     PoloMoveResizeFlags flags, PoloGravity resize_gravity,
		     const PoloRect *orig, PoloRect *new_rect)
{
	MetaWindow *window = input->window;
	ConstraintInfo info;
	ConstraintPriority priority = PRIORITY_MINIMUM;
	gboolean satisfied = FALSE;

	setup_constraint_info(&info, input, flags, resize_gravity, orig,
			      new_rect);

	while (!satisfied && priority <= PRIORITY_MAXIMUM) {
		gboolean check_only = TRUE;

		do_all_constraints(window, &info, priority, !check_only);

		satisfied =
			do_all_constraints(window, &info, priority, check_only);

		priority++;
	}

	*new_rect = info.current;

	update_onscreen_requirements(window, &info);
}

int
polo_marco_xinerama_for_rect(const PoloMarcoXinerama *xineramas,
			     int n_xineramas, const PoloRect *rect)
{
	int i;
	int best_xinerama, xinerama_score;

	if (n_xineramas == 1)
		return 0;

	best_xinerama = 0;
	xinerama_score = 0;

	for (i = 0; i < n_xineramas; i++) {
		PoloRect dest;
		if (polo_marco_rect_intersect(&xineramas[i].rect, rect,
					      &dest)) {
			int cur = polo_marco_rect_area(&dest);
			if (cur > xinerama_score) {
				xinerama_score = cur;
				best_xinerama = i;
			}
		}
	}

	return best_xinerama;
}

static void
setup_constraint_info(ConstraintInfo *info,
		      const PoloMarcoConstraintInput *input,
		      PoloMoveResizeFlags flags, int resize_gravity,
		      const PoloRect *orig, PoloRect *new_rect)
{
	MetaWindow *window = input->window;
	int xinerama;

	info->input = input;
	info->orig = *orig;
	info->current = *new_rect;

	if (input->borders && !window->fullscreen) {
		info->borders = input->borders;
	} else {
		info->borders_storage.left = 0;
		info->borders_storage.right = 0;
		info->borders_storage.top = 0;
		info->borders_storage.bottom = 0;
		info->borders = &info->borders_storage;
	}

	if (flags & POLO_IS_MOVE_ACTION && flags & POLO_IS_RESIZE_ACTION)
		info->action_type = ACTION_MOVE_AND_RESIZE;
	else if (flags & POLO_IS_RESIZE_ACTION)
		info->action_type = ACTION_RESIZE;
	else if (flags & POLO_IS_MOVE_ACTION)
		info->action_type = ACTION_MOVE;
	else
		g_error("polo_marco_constrain called without a move or "
			"resize action");

	info->is_user_action = (flags & POLO_IS_USER_ACTION) != 0;

	info->resize_gravity = resize_gravity;

	info->fixed_directions = POLO_FIXED_DIRECTION_NONE;
	if (orig->x == new_rect->x &&
	    orig->x + orig->width == new_rect->x + new_rect->width &&
	    (orig->y != new_rect->y ||
	     orig->y + orig->height != new_rect->y + new_rect->height)) {
		info->fixed_directions = POLO_FIXED_DIRECTION_X;
	}
	if (orig->y == new_rect->y &&
	    orig->y + orig->height == new_rect->y + new_rect->height &&
	    (orig->x != new_rect->x ||
	     orig->x + orig->width != new_rect->x + new_rect->width)) {
		info->fixed_directions = POLO_FIXED_DIRECTION_Y;
	}
	if (!info->is_user_action)
		info->fixed_directions = POLO_FIXED_DIRECTION_NONE;

	xinerama = polo_marco_xinerama_for_rect(
		input->xineramas, input->n_xineramas, &info->current);
	info->work_area_xinerama = input->xineramas[xinerama].work_area;

	if (!window->fullscreen || window->fullscreen_monitors[0] == -1) {
		info->entire_xinerama = input->xineramas[xinerama].rect;
	} else {
		int i = 0;
		long monitor;

		monitor = window->fullscreen_monitors[i];
		info->entire_xinerama = input->xineramas[monitor].rect;
		for (i = 1; i <= 3; i++) {
			monitor = window->fullscreen_monitors[i];
			polo_marco_rect_union(&info->entire_xinerama,
					      &input->xineramas[monitor].rect,
					      &info->entire_xinerama);
		}
	}

	info->usable_screen_region = input->usable_screen_region;
	info->usable_xinerama_region = input->usable_xinerama_regions[xinerama];
}

static void
update_onscreen_requirements(MetaWindow *window, ConstraintInfo *info)
{
	if (window->type == POLO_WINDOW_DESKTOP ||
	    window->type == POLO_WINDOW_DOCK)
		return;

	if (window->fullscreen)
		return;

	extend_by_frame(window, &info->current, info->borders);

	window->require_fully_onscreen =
		(polo_marco_rect_contained_in_region(info->usable_screen_region,
						     &info->current) != FALSE);

	window->require_on_single_xinerama =
		(polo_marco_rect_contained_in_region(
			 info->usable_xinerama_region, &info->current) !=
		 FALSE);

	if (window->has_frame && window->decorated) {
		PoloRect titlebar_rect;

		titlebar_rect = info->current;
		titlebar_rect.height = info->borders->top;
		window->require_titlebar_visible =
			(polo_marco_rect_overlaps_with_region(
				 info->usable_screen_region, &titlebar_rect) !=
			 FALSE);
	}

	unextend_by_frame(window, &info->current, info->borders);
}

static void
extend_by_frame(MetaWindow *window, PoloRect *rect,
		const PoloFrameBorders *borders)
{
	if (window->has_frame) {
		rect->x -= borders->left;
		rect->y -= borders->top;
		rect->width += borders->left + borders->right;
		rect->height += borders->top + borders->bottom;
	}
}

static void
unextend_by_frame(MetaWindow *window, PoloRect *rect,
		  const PoloFrameBorders *borders)
{
	if (window->has_frame) {
		rect->x += borders->left;
		rect->y += borders->top;
		rect->width -= borders->left + borders->right;
		rect->height -= borders->top + borders->bottom;
	}
}

static inline void
get_size_limits(const MetaWindow *window, const PoloFrameBorders *borders,
		gboolean include_frame, PoloRect *min_size, PoloRect *max_size)
{
	min_size->width = window->size_hints.min_width;
	min_size->height = window->size_hints.min_height;
	max_size->width = window->size_hints.max_width;
	max_size->height = window->size_hints.max_height;

	if (include_frame && window->has_frame) {
		int fw;
		int fh;

		fw = borders->left + borders->right;
		fh = borders->top + borders->bottom;

		min_size->width += fw;
		min_size->height += fh;

		if (max_size->width < (G_MAXINT - fw))
			max_size->width += fw;
		else
			max_size->width = G_MAXINT;

		if (max_size->height < (G_MAXINT - fh))
			max_size->height += fh;
		else
			max_size->height = G_MAXINT;
	}
}

static gboolean
constrain_modal_dialog(MetaWindow *window, ConstraintInfo *info,
		       ConstraintPriority priority, gboolean check_only)
{
	return TRUE;
}

static gboolean
constrain_maximization(MetaWindow *window, ConstraintInfo *info,
		       ConstraintPriority priority, gboolean check_only)
{
	PoloRect target_size;
	PoloRect min_size, max_size;
	gboolean hminbad, vminbad;
	gboolean horiz_equal, vert_equal;
	gboolean constraint_already_satisfied;

	if (priority > PRIORITY_MAXIMIZATION)
		return TRUE;

	if ((!window->maximized_horizontally &&
	     !window->maximized_vertically) ||
	    META_WINDOW_TILED(window))
		return TRUE;

	if (META_WINDOW_MAXIMIZED(window)) {
		target_size = info->work_area_xinerama;
	} else {
		PoloDirection direction;

		if (window->maximized_horizontally)
			direction = POLO_DIRECTION_HORIZONTAL;
		else
			direction = POLO_DIRECTION_VERTICAL;

		target_size = info->current;
		extend_by_frame(window, &target_size, info->borders);
		polo_marco_rect_expand_to_avoiding_struts(
			&target_size, &info->entire_xinerama, direction,
			info->input->all_struts);
	}
	unextend_by_frame(window, &target_size, info->borders);

	get_size_limits(window, info->borders, FALSE, &min_size, &max_size);
	hminbad = target_size.width < min_size.width &&
		window->maximized_horizontally;
	vminbad = target_size.height < min_size.height &&
		window->maximized_vertically;
	if (hminbad || vminbad)
		return TRUE;

	horiz_equal = target_size.x == info->current.x &&
		target_size.width == info->current.width;
	vert_equal = target_size.y == info->current.y &&
		target_size.height == info->current.height;
	constraint_already_satisfied =
		(horiz_equal || !window->maximized_horizontally) &&
		(vert_equal || !window->maximized_vertically);
	if (check_only || constraint_already_satisfied)
		return constraint_already_satisfied;

	if (window->maximized_horizontally) {
		info->current.x = target_size.x;
		info->current.width = target_size.width;
	}
	if (window->maximized_vertically) {
		info->current.y = target_size.y;
		info->current.height = target_size.height;
	}
	return TRUE;
}

void
polo_marco_get_current_tile_area(const PoloMarcoConstraintInput *input,
				 PoloRect *tile_area)
{
	MetaWindow *window = input->window;
	int tile_monitor_number;
	int width;
	double tile_ratio;

	g_return_if_fail(window->tile_mode != POLO_TILE_NONE);

	tile_monitor_number = window->tile_monitor_number;
	if (tile_monitor_number < 0 ||
	    tile_monitor_number >= input->n_xineramas)
		tile_monitor_number = 0;

	*tile_area = input->xineramas[tile_monitor_number].work_area;

	width = tile_area->width;

	switch (window->tile_cycle) {
	case POLO_TILE_CYCLE_33:
		tile_ratio = 1 / 3.0;
		break;
	case POLO_TILE_CYCLE_25:
		tile_ratio = 1 / 4.0;
		break;
	case POLO_TILE_CYCLE_100:
		tile_ratio = 1 / 1.0;
		break;
	case POLO_TILE_CYCLE_75:
		tile_ratio = 3 / 4.0;
		break;
	case POLO_TILE_CYCLE_66:
		tile_ratio = 2 / 3.0;
		break;
	case POLO_TILE_CYCLE_50:
	case POLO_TILE_CYCLE_NONE:
	default:
		tile_ratio = 1 / 2.0;
		break;
	}

	if (window->tile_mode != POLO_TILE_NONE &&
	    window->tile_mode != POLO_TILE_MAXIMIZED)
		width = (int)(tile_area->width * tile_ratio);

	if (window->tile_mode == POLO_TILE_BOTTOM_LEFT ||
	    window->tile_mode == POLO_TILE_BOTTOM_RIGHT ||
	    window->tile_mode == POLO_TILE_TOP_LEFT ||
	    window->tile_mode == POLO_TILE_TOP_RIGHT)
		tile_area->height /= 2;

	if (window->tile_mode == POLO_TILE_RIGHT ||
	    window->tile_mode == POLO_TILE_TOP_RIGHT ||
	    window->tile_mode == POLO_TILE_BOTTOM_RIGHT)
		tile_area->x += tile_area->width - width;

	if (window->tile_mode == POLO_TILE_BOTTOM_LEFT ||
	    window->tile_mode == POLO_TILE_BOTTOM_RIGHT)
		tile_area->y += tile_area->height;

	tile_area->width = width;
}

static gboolean
constrain_tiling(MetaWindow *window, ConstraintInfo *info,
		 ConstraintPriority priority, gboolean check_only)
{
	PoloRect target_size;
	PoloRect min_size, max_size;
	gboolean hminbad, vminbad;
	gboolean horiz_equal, vert_equal;
	gboolean constraint_already_satisfied;
	gboolean allow_resize_horizontally = FALSE;
	gboolean allow_resize_vertically = FALSE;

	if (priority > PRIORITY_TILING)
		return TRUE;

	if (!META_WINDOW_TILED(window))
		return TRUE;

	polo_marco_get_current_tile_area(info->input, &target_size);
	unextend_by_frame(window, &target_size, info->borders);

	get_size_limits(window, info->borders, FALSE, &min_size, &max_size);
	hminbad = target_size.width < min_size.width;
	vminbad = target_size.height < min_size.height;
	if (hminbad || vminbad)
		return TRUE;

	horiz_equal = target_size.x == info->current.x &&
		target_size.width == info->current.width;
	vert_equal = target_size.y == info->current.y &&
		target_size.height == info->current.height;
	constraint_already_satisfied = horiz_equal && vert_equal;
	if (check_only || constraint_already_satisfied)
		return constraint_already_satisfied;

	if (info->is_user_action) {
		can_resize_tiled(window->tile_mode, info->resize_gravity,
				 &allow_resize_horizontally,
				 &allow_resize_vertically);

		window->tile_resized = TRUE;
	}

	if (window->tile_resized) {
		target_size.y = info->orig.y;
		target_size.height = info->orig.height;
		target_size.x = info->orig.x;
		target_size.width = info->orig.width;
	}

	if (!allow_resize_vertically) {
		info->current.y = target_size.y;
		info->current.height = target_size.height;
	}

	if (!allow_resize_horizontally) {
		info->current.x = target_size.x;
		info->current.width = target_size.width;
	}

	return TRUE;
}

static void
can_resize_tiled(guint tile_mode, int resize_gravity,
		 gboolean *resize_horizontally, gboolean *resize_vertically)
{
	switch (tile_mode) {
	case POLO_TILE_RIGHT:
		if (resize_gravity == POLO_GRAVITY_EAST)
			*resize_horizontally = TRUE;
		break;
	case POLO_TILE_LEFT:
		if (resize_gravity == POLO_GRAVITY_WEST)
			*resize_horizontally = TRUE;
		break;
	case POLO_TILE_TOP_RIGHT:
		switch (resize_gravity) {
		case POLO_GRAVITY_EAST:
			*resize_horizontally = TRUE;
			break;
		case POLO_GRAVITY_NORTH:
			*resize_vertically = TRUE;
			break;
		case POLO_GRAVITY_NORTH_EAST:
			*resize_horizontally = TRUE;
			*resize_vertically = TRUE;
			break;
		}
		break;
	case POLO_TILE_TOP_LEFT:
		switch (resize_gravity) {
		case POLO_GRAVITY_WEST:
			*resize_horizontally = TRUE;
			break;
		case POLO_GRAVITY_NORTH:
			*resize_vertically = TRUE;
			break;
		case POLO_GRAVITY_NORTH_WEST:
			*resize_horizontally = TRUE;
			*resize_vertically = TRUE;
			break;
		}
		break;
	case POLO_TILE_BOTTOM_RIGHT:
		switch (resize_gravity) {
		case POLO_GRAVITY_EAST:
			*resize_horizontally = TRUE;
			break;
		case POLO_GRAVITY_SOUTH:
			*resize_vertically = TRUE;
			break;
		case POLO_GRAVITY_SOUTH_EAST:
			*resize_horizontally = TRUE;
			*resize_vertically = TRUE;
			break;
		}
		break;
	case POLO_TILE_BOTTOM_LEFT:
		switch (resize_gravity) {
		case POLO_GRAVITY_WEST:
			*resize_horizontally = TRUE;
			break;
		case POLO_GRAVITY_SOUTH:
			*resize_vertically = TRUE;
			break;
		case POLO_GRAVITY_SOUTH_WEST:
			*resize_horizontally = TRUE;
			*resize_vertically = TRUE;
			break;
		}
		break;
	}
}

static gboolean
constrain_fullscreen(MetaWindow *window, ConstraintInfo *info,
		     ConstraintPriority priority, gboolean check_only)
{
	PoloRect min_size, max_size, xinerama;
	gboolean too_big, too_small, constraint_already_satisfied;

	if (priority > PRIORITY_FULLSCREEN)
		return TRUE;

	if (!window->fullscreen)
		return TRUE;

	xinerama = info->entire_xinerama;

	get_size_limits(window, info->borders, FALSE, &min_size, &max_size);
	too_big = !polo_marco_rect_could_fit_rect(&xinerama, &min_size);
	too_small = !polo_marco_rect_could_fit_rect(&max_size, &xinerama);
	if (too_big || too_small)
		return TRUE;

	constraint_already_satisfied =
		polo_marco_rect_equal(&info->current, &xinerama);
	if (check_only || constraint_already_satisfied)
		return constraint_already_satisfied;

	info->current = xinerama;
	return TRUE;
}

static gboolean
constrain_size_increments(MetaWindow *window, ConstraintInfo *info,
			  ConstraintPriority priority, gboolean check_only)
{
	int bh, hi, bw, wi, extra_height, extra_width;
	int new_width, new_height;
	gboolean constraint_already_satisfied;
	PoloRect *start_rect;

	if (priority > PRIORITY_SIZE_HINTS_INCREMENTS)
		return TRUE;

	if (META_WINDOW_MAXIMIZED(window) || window->fullscreen ||
	    META_WINDOW_TILED(window) || info->action_type == ACTION_MOVE)
		return TRUE;

	bh = window->size_hints.base_height;
	hi = window->size_hints.height_inc;
	bw = window->size_hints.base_width;
	wi = window->size_hints.width_inc;
	extra_height = (info->current.height - bh) % hi;
	extra_width = (info->current.width - bw) % wi;
	if (window->maximized_horizontally)
		extra_width *= 0;
	if (window->maximized_vertically)
		extra_height *= 0;
	constraint_already_satisfied = (extra_height == 0 && extra_width == 0);

	if (check_only || constraint_already_satisfied)
		return constraint_already_satisfied;

	new_width = info->current.width - extra_width;
	new_height = info->current.height - extra_height;

	if (new_width < window->size_hints.min_width)
		new_width +=
			((window->size_hints.min_width - new_width) / wi + 1) *
			wi;
	if (new_height < window->size_hints.min_height)
		new_height +=
			((window->size_hints.min_height - new_height) / hi +
			 1) *
			hi;

	if (info->action_type == ACTION_MOVE_AND_RESIZE)
		start_rect = &info->current;
	else
		start_rect = &info->orig;

	polo_marco_rect_resize_with_gravity(start_rect, &info->current,
					    info->resize_gravity, new_width,
					    new_height);
	return TRUE;
}

static gboolean
constrain_size_limits(MetaWindow *window, ConstraintInfo *info,
		      ConstraintPriority priority, gboolean check_only)
{
	PoloRect min_size, max_size;
	gboolean too_big, too_small, constraint_already_satisfied;
	int new_width, new_height;
	PoloRect *start_rect;

	if (priority > PRIORITY_SIZE_HINTS_LIMITS)
		return TRUE;

	if (info->action_type == ACTION_MOVE)
		return TRUE;

	get_size_limits(window, info->borders, FALSE, &min_size, &max_size);
	if (window->maximized_horizontally)
		max_size.width = MAX(max_size.width, info->current.width);
	if (window->maximized_vertically)
		max_size.height = MAX(max_size.height, info->current.height);
	too_small = !polo_marco_rect_could_fit_rect(&info->current, &min_size);
	too_big = !polo_marco_rect_could_fit_rect(&max_size, &info->current);
	constraint_already_satisfied = !too_big && !too_small;
	if (check_only || constraint_already_satisfied)
		return constraint_already_satisfied;

	new_width = CLAMP(info->current.width, min_size.width, max_size.width);
	new_height =
		CLAMP(info->current.height, min_size.height, max_size.height);

	if (info->action_type == ACTION_MOVE_AND_RESIZE)
		start_rect = &info->current;
	else
		start_rect = &info->orig;

	polo_marco_rect_resize_with_gravity(start_rect, &info->current,
					    info->resize_gravity, new_width,
					    new_height);
	return TRUE;
}

static gboolean
constrain_aspect_ratio(MetaWindow *window, ConstraintInfo *info,
		       ConstraintPriority priority, gboolean check_only)
{
	double minr, maxr;
	gboolean constraints_are_inconsistent, constraint_already_satisfied;
	int fudge;
	double new_width, new_height;
	double best_width, best_height;
	double alt_width, alt_height;
	PoloRect *start_rect;

	if (priority > PRIORITY_ASPECT_RATIO)
		return TRUE;

	minr = window->size_hints.min_aspect.x /
		(double)window->size_hints.min_aspect.y;
	maxr = window->size_hints.max_aspect.x /
		(double)window->size_hints.max_aspect.y;
	constraints_are_inconsistent = minr > maxr;
	if (constraints_are_inconsistent || META_WINDOW_MAXIMIZED(window) ||
	    window->fullscreen || META_WINDOW_SIDE_TILED(window) ||
	    info->action_type == ACTION_MOVE)
		return TRUE;

	switch (info->resize_gravity) {
	case POLO_GRAVITY_WEST:
	case POLO_GRAVITY_NORTH:
	case POLO_GRAVITY_SOUTH:
	case POLO_GRAVITY_EAST:
		fudge = 2;
		break;

	case POLO_GRAVITY_NORTH_WEST:
	case POLO_GRAVITY_SOUTH_WEST:
	case POLO_GRAVITY_CENTER:
	case POLO_GRAVITY_NORTH_EAST:
	case POLO_GRAVITY_SOUTH_EAST:
	case POLO_GRAVITY_STATIC:
	default:
		fudge = 1;
		break;
	}
	constraint_already_satisfied =
		info->current.width - (info->current.height * minr) >
			-minr * fudge &&
		info->current.width - (info->current.height * maxr) <
			maxr * fudge;
	if (check_only || constraint_already_satisfied)
		return constraint_already_satisfied;

	new_width = (double)info->current.width;
	new_height = (double)info->current.height;

	switch (info->resize_gravity) {
	case POLO_GRAVITY_WEST:
	case POLO_GRAVITY_EAST:
		new_height =
			CLAMP(new_height, new_width / maxr, new_width / minr);
		break;

	case POLO_GRAVITY_NORTH:
	case POLO_GRAVITY_SOUTH:
		new_width =
			CLAMP(new_width, new_height * minr, new_height * maxr);
		break;

	case POLO_GRAVITY_NORTH_WEST:
	case POLO_GRAVITY_SOUTH_WEST:
	case POLO_GRAVITY_CENTER:
	case POLO_GRAVITY_NORTH_EAST:
	case POLO_GRAVITY_SOUTH_EAST:
	case POLO_GRAVITY_STATIC:
	default:
		alt_width =
			CLAMP(new_width, new_height * minr, new_height * maxr);
		alt_height =
			CLAMP(new_height, new_width / maxr, new_width / minr);

		polo_marco_rect_find_linepoint_closest_to_point(
			alt_width, new_height, new_width, alt_height, new_width,
			new_height, &best_width, &best_height);

		new_width = round(best_width);
		new_height = round(best_height);

		break;
	}

	if (info->action_type == ACTION_MOVE_AND_RESIZE)
		start_rect = &info->current;
	else
		start_rect = &info->orig;

	polo_marco_rect_resize_with_gravity(start_rect, &info->current,
					    info->resize_gravity,
					    (int)new_width, (int)new_height);

	return TRUE;
}

static gboolean
do_screen_and_xinerama_relative_constraints(MetaWindow *window,
					    GList *region_spanning_rectangles,
					    ConstraintInfo *info,
					    gboolean check_only)
{
	gboolean exit_early = FALSE, constraint_satisfied;
	PoloRect how_far_it_can_be_smushed, min_size, max_size;

	how_far_it_can_be_smushed = info->current;
	get_size_limits(window, info->borders, TRUE, &min_size, &max_size);
	extend_by_frame(window, &info->current, info->borders);

	if (info->action_type != ACTION_MOVE) {
		if (!(info->fixed_directions & POLO_FIXED_DIRECTION_X))
			how_far_it_can_be_smushed.width = min_size.width;

		if (!(info->fixed_directions & POLO_FIXED_DIRECTION_Y))
			how_far_it_can_be_smushed.height = min_size.height;
	}
	if (!polo_marco_rect_could_fit_in_region(region_spanning_rectangles,
						 &how_far_it_can_be_smushed))
		exit_early = TRUE;

	constraint_satisfied = polo_marco_rect_contained_in_region(
		region_spanning_rectangles, &info->current);
	if (exit_early || constraint_satisfied || check_only) {
		unextend_by_frame(window, &info->current, info->borders);
		return constraint_satisfied;
	}

	if (info->action_type != ACTION_MOVE)
		polo_marco_rect_clamp_to_fit_into_region(
			region_spanning_rectangles, info->fixed_directions,
			&info->current, &min_size);

	if (info->is_user_action && info->action_type == ACTION_RESIZE)
		polo_marco_rect_clip_to_region(region_spanning_rectangles,
					       info->fixed_directions,
					       &info->current);
	else
		polo_marco_rect_shove_into_region(region_spanning_rectangles,
						  info->fixed_directions,
						  &info->current);

	unextend_by_frame(window, &info->current, info->borders);
	return TRUE;
}

static gboolean
constrain_to_single_xinerama(MetaWindow *window, ConstraintInfo *info,
			     ConstraintPriority priority, gboolean check_only)
{
	if (priority > PRIORITY_ENTIRELY_VISIBLE_ON_SINGLE_XINERAMA)
		return TRUE;

	if (window->type == POLO_WINDOW_DESKTOP ||
	    window->type == POLO_WINDOW_DOCK || info->input->n_xineramas == 1 ||
	    !window->require_on_single_xinerama || !window->has_frame ||
	    info->is_user_action)
		return TRUE;

	return do_screen_and_xinerama_relative_constraints(
		window, info->usable_xinerama_region, info, check_only);
}

static gboolean
constrain_fully_onscreen(MetaWindow *window, ConstraintInfo *info,
			 ConstraintPriority priority, gboolean check_only)
{
	if (priority > PRIORITY_ENTIRELY_VISIBLE_ON_WORKAREA)
		return TRUE;

	if (window->type == POLO_WINDOW_DESKTOP ||
	    window->type == POLO_WINDOW_DOCK || window->fullscreen ||
	    !window->require_fully_onscreen || info->is_user_action)
		return TRUE;

	return do_screen_and_xinerama_relative_constraints(
		window, info->usable_screen_region, info, check_only);
}

static gboolean
constrain_titlebar_visible(MetaWindow *window, ConstraintInfo *info,
			   ConstraintPriority priority, gboolean check_only)
{
	gboolean unconstrained_user_action;
	gboolean retval;
	int bottom_amount;
	int horiz_amount_offscreen, vert_amount_offscreen;
	int horiz_amount_onscreen, vert_amount_onscreen;

	if (priority > PRIORITY_TITLEBAR_VISIBLE)
		return TRUE;

	unconstrained_user_action =
		info->is_user_action && !info->input->grab_frame_action;

	if (window->type == POLO_WINDOW_DESKTOP ||
	    window->type == POLO_WINDOW_DOCK || window->fullscreen ||
	    !window->require_titlebar_visible || unconstrained_user_action)
		return TRUE;

	horiz_amount_onscreen = info->current.width / 4;
	vert_amount_onscreen = info->current.height / 4;
	horiz_amount_onscreen = CLAMP(horiz_amount_onscreen, 10, 75);
	vert_amount_onscreen = CLAMP(vert_amount_onscreen, 10, 75);
	horiz_amount_offscreen = info->current.width - horiz_amount_onscreen;
	vert_amount_offscreen = info->current.height - vert_amount_onscreen;
	horiz_amount_offscreen = MAX(horiz_amount_offscreen, 0);
	vert_amount_offscreen = MAX(vert_amount_offscreen, 0);
	if (window->has_frame) {
		bottom_amount = info->current.height + info->borders->bottom;
		vert_amount_onscreen = info->borders->top;
	} else
		bottom_amount = vert_amount_offscreen;

	polo_marco_rect_expand_region_conditionally(
		info->usable_screen_region, horiz_amount_offscreen,
		horiz_amount_offscreen, 0, bottom_amount, horiz_amount_onscreen,
		vert_amount_onscreen);
	retval = do_screen_and_xinerama_relative_constraints(
		window, info->usable_screen_region, info, check_only);
	polo_marco_rect_expand_region_conditionally(
		info->usable_screen_region, -horiz_amount_offscreen,
		-horiz_amount_offscreen, 0, -bottom_amount,
		horiz_amount_onscreen, vert_amount_onscreen);

	return retval;
}

static gboolean
constrain_partially_onscreen(MetaWindow *window, ConstraintInfo *info,
			     ConstraintPriority priority, gboolean check_only)
{
	gboolean retval;
	int top_amount, bottom_amount;
	int horiz_amount_offscreen, vert_amount_offscreen;
	int horiz_amount_onscreen, vert_amount_onscreen;

	if (priority > PRIORITY_PARTIALLY_VISIBLE_ON_WORKAREA)
		return TRUE;

	if (window->type == POLO_WINDOW_DESKTOP ||
	    window->type == POLO_WINDOW_DOCK)
		return TRUE;

	horiz_amount_onscreen = info->current.width / 4;
	vert_amount_onscreen = info->current.height / 4;
	horiz_amount_onscreen = CLAMP(horiz_amount_onscreen, 10, 75);
	vert_amount_onscreen = CLAMP(vert_amount_onscreen, 10, 75);
	horiz_amount_offscreen = info->current.width - horiz_amount_onscreen;
	vert_amount_offscreen = info->current.height - vert_amount_onscreen;
	horiz_amount_offscreen = MAX(horiz_amount_offscreen, 0);
	vert_amount_offscreen = MAX(vert_amount_offscreen, 0);
	top_amount = vert_amount_offscreen;
	if (window->has_frame) {
		bottom_amount = info->current.height + info->borders->bottom;
		vert_amount_onscreen = info->borders->top;
	} else
		bottom_amount = vert_amount_offscreen;

	polo_marco_rect_expand_region_conditionally(
		info->usable_screen_region, horiz_amount_offscreen,
		horiz_amount_offscreen, top_amount, bottom_amount,
		horiz_amount_onscreen, vert_amount_onscreen);
	retval = do_screen_and_xinerama_relative_constraints(
		window, info->usable_screen_region, info, check_only);
	polo_marco_rect_expand_region_conditionally(
		info->usable_screen_region, -horiz_amount_offscreen,
		-horiz_amount_offscreen, -top_amount, -bottom_amount,
		horiz_amount_onscreen, vert_amount_onscreen);

	return retval;
}

void
polo_marco_adjust_for_gravity(const PoloFrameBorders *borders, int border_width,
			      PoloGravity gravity, PoloRect *rect)
{
	int ref_x, ref_y;
	int bw;
	int child_x, child_y;
	int frame_width, frame_height;

	bw = border_width;

	if (borders) {
		child_x = borders->left;
		child_y = borders->top;
		frame_width = child_x + rect->width + borders->right;
		frame_height = child_y + rect->height + borders->bottom;
	} else {
		child_x = 0;
		child_y = 0;
		frame_width = rect->width;
		frame_height = rect->height;
	}

	switch (gravity) {
	case POLO_GRAVITY_NORTH_WEST:
		ref_x = rect->x;
		ref_y = rect->y;
		break;
	case POLO_GRAVITY_NORTH:
		ref_x = rect->x + rect->width / 2 + bw;
		ref_y = rect->y;
		break;
	case POLO_GRAVITY_NORTH_EAST:
		ref_x = rect->x + rect->width + bw * 2;
		ref_y = rect->y;
		break;
	case POLO_GRAVITY_WEST:
		ref_x = rect->x;
		ref_y = rect->y + rect->height / 2 + bw;
		break;
	case POLO_GRAVITY_CENTER:
		ref_x = rect->x + rect->width / 2 + bw;
		ref_y = rect->y + rect->height / 2 + bw;
		break;
	case POLO_GRAVITY_EAST:
		ref_x = rect->x + rect->width + bw * 2;
		ref_y = rect->y + rect->height / 2 + bw;
		break;
	case POLO_GRAVITY_SOUTH_WEST:
		ref_x = rect->x;
		ref_y = rect->y + rect->height + bw * 2;
		break;
	case POLO_GRAVITY_SOUTH:
		ref_x = rect->x + rect->width / 2 + bw;
		ref_y = rect->y + rect->height + bw * 2;
		break;
	case POLO_GRAVITY_SOUTH_EAST:
		ref_x = rect->x + rect->width + bw * 2;
		ref_y = rect->y + rect->height + bw * 2;
		break;
	case POLO_GRAVITY_STATIC:
	default:
		ref_x = rect->x;
		ref_y = rect->y;
		break;
	}

	switch (gravity) {
	case POLO_GRAVITY_NORTH_WEST:
		rect->x = ref_x + child_x;
		rect->y = ref_y + child_y;
		break;
	case POLO_GRAVITY_NORTH:
		rect->x = ref_x - frame_width / 2 + child_x;
		rect->y = ref_y + child_y;
		break;
	case POLO_GRAVITY_NORTH_EAST:
		rect->x = ref_x - frame_width + child_x;
		rect->y = ref_y + child_y;
		break;
	case POLO_GRAVITY_WEST:
		rect->x = ref_x + child_x;
		rect->y = ref_y - frame_height / 2 + child_y;
		break;
	case POLO_GRAVITY_CENTER:
		rect->x = ref_x - frame_width / 2 + child_x;
		rect->y = ref_y - frame_height / 2 + child_y;
		break;
	case POLO_GRAVITY_EAST:
		rect->x = ref_x - frame_width + child_x;
		rect->y = ref_y - frame_height / 2 + child_y;
		break;
	case POLO_GRAVITY_SOUTH_WEST:
		rect->x = ref_x + child_x;
		rect->y = ref_y - frame_height + child_y;
		break;
	case POLO_GRAVITY_SOUTH:
		rect->x = ref_x - frame_width / 2 + child_x;
		rect->y = ref_y - frame_height + child_y;
		break;
	case POLO_GRAVITY_SOUTH_EAST:
		rect->x = ref_x - frame_width + child_x;
		rect->y = ref_y - frame_height + child_y;
		break;
	case POLO_GRAVITY_STATIC:
	default:
		rect->x = ref_x;
		rect->y = ref_y;
		break;
	}
}

void
polo_marco_ensure_size_hints_satisfied(PoloRect *rect,
				       const PoloSizeHints *size_hints)
{
	int minw, minh, maxw, maxh;
	int basew, baseh, winc, hinc;
	int extra_width, extra_height;

	minw = size_hints->min_width;
	minh = size_hints->min_height;
	maxw = size_hints->max_width;
	maxh = size_hints->max_height;
	basew = size_hints->base_width;
	baseh = size_hints->base_height;
	winc = size_hints->width_inc;
	hinc = size_hints->height_inc;

	rect->width = CLAMP(rect->width, minw, maxw);
	rect->height = CLAMP(rect->height, minh, maxh);

	extra_width = (rect->width - basew) % winc;
	extra_height = (rect->height - baseh) % hinc;

	rect->width -= extra_width;
	rect->height -= extra_height;

	if (rect->width < minw)
		rect->width += ((minw - rect->width) / winc + 1) * winc;
	if (rect->height < minh)
		rect->height += ((minh - rect->height) / hinc + 1) * hinc;
}
