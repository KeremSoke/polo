/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat, Inc.
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2005 Elijah Newren
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Ported from Marco 1.28.2 src/core/place.c. MetaWindow, the display and
 * the screen are replaced by PoloMarcoPlaceInput; the X pointer query by
 * its pointer position.
 */

#include <math.h>
#include <stdlib.h>

#include "polo-marco.h"

typedef enum {
	META_LEFT,
	META_RIGHT,
	META_TOP,
	META_BOTTOM
} MetaWindowDirection;

#define CASCADE_FUZZ 15
#define CASCADE_INTERVAL 50

static gint
northwestcmp(gconstpointer a, gconstpointer b)
{
	const PoloMarcoWindowInfo *aw = a;
	const PoloMarcoWindowInfo *bw = b;
	double from_origin_a;
	double from_origin_b;
	int ax, ay, bx, by;

	ax = aw->frame.x;
	ay = aw->frame.y;
	bx = bw->frame.x;
	by = bw->frame.y;

	from_origin_a = sqrt((double)(ax * ax + ay * ay));
	from_origin_b = sqrt((double)(bx * bx + by * by));

	if (from_origin_a < from_origin_b)
		return -1;
	else if (from_origin_a > from_origin_b)
		return 1;
	else
		return 0;
}

static gboolean
place_by_pointer(const PoloMarcoPlaceInput *input,
		 PoloPlacementMode placement_mode, int *new_x, int *new_y,
		 PoloMarcoPlaceResult *result)
{
	const PoloMarcoWindowInfo *window = input->window;
	const PoloFrameBorders *borders = input->borders;
	int window_width, window_height;

	window_width = window->frame.width;
	window_height = window->frame.height;

	if (borders) {
		*new_x = input->pointer_x + borders->left - window_width / 2;
		*new_y = input->pointer_y + borders->top - window_height / 2;
	} else {
		*new_x = input->pointer_x - window_width / 2;
		*new_y = input->pointer_y - window_height / 2;
	}

	if (placement_mode == POLO_PLACEMENT_MANUAL)
		result->move_after_placement = TRUE;

	return TRUE;
}

static const PoloRect *
work_area_for_xinerama(const PoloMarcoPlaceInput *input, int xinerama)
{
	return &input->xineramas[xinerama].work_area;
}

static void
find_next_cascade(const PoloMarcoPlaceInput *input, GList *windows, int x,
		  int y, int *new_x, int *new_y)
{
	const PoloMarcoWindowInfo *window = input->window;
	const PoloFrameBorders *borders = input->borders;
	GList *tmp;
	GList *sorted;
	int cascade_x, cascade_y;
	int x_threshold, y_threshold;
	int window_width, window_height;
	int cascade_stage;
	PoloRect work_area;

	sorted = g_list_copy(windows);
	sorted = g_list_sort(sorted, northwestcmp);

	if (borders) {
		x_threshold = MAX(borders->left, CASCADE_FUZZ);
		y_threshold = MAX(borders->top, CASCADE_FUZZ);
	} else {
		x_threshold = CASCADE_FUZZ;
		y_threshold = CASCADE_FUZZ;
	}

	work_area = *work_area_for_xinerama(input, input->current_xinerama);

	cascade_x = MAX(0, work_area.x);
	cascade_y = MAX(0, work_area.y);

	window_width = window->frame.width;
	window_height = window->frame.height;

	cascade_stage = 0;
	tmp = sorted;
	while (tmp != NULL) {
		const PoloMarcoWindowInfo *w;
		int wx, wy;

		w = tmp->data;

		wx = w->frame.x;
		wy = w->frame.y;

		if (ABS(wx - cascade_x) < x_threshold &&
		    ABS(wy - cascade_y) < y_threshold) {
			wx = w->position.x;
			wy = w->position.y;
			cascade_x = wx;
			cascade_y = wy;

			if (((cascade_x + window_width) >
			     (work_area.x + work_area.width)) ||
			    ((cascade_y + window_height) >
			     (work_area.y + work_area.height))) {
				cascade_x = MAX(0, work_area.x);
				cascade_y = MAX(0, work_area.y);

				cascade_stage += 1;
				cascade_x += CASCADE_INTERVAL * cascade_stage;

				if ((cascade_x + window_width) <
				    (work_area.x + work_area.width)) {
					tmp = sorted;
					continue;
				} else {
					cascade_x = MAX(0, work_area.x);
					break;
				}
			}
		}

		tmp = tmp->next;
	}

	g_list_free(sorted);

	if (borders == NULL) {
		*new_x = cascade_x;
		*new_y = cascade_y;
	} else {
		*new_x = cascade_x + borders->left;
		*new_y = cascade_y + borders->top;
	}
}

static void
find_most_freespace(const PoloMarcoPlaceInput *input,
		    const PoloMarcoWindowInfo *focus_window, int x, int y,
		    int *new_x, int *new_y)
{
	const PoloMarcoWindowInfo *window = input->window;
	const PoloFrameBorders *borders = input->borders;
	MetaWindowDirection side;
	int max_area;
	int max_width, max_height, left, right, top, bottom;
	int left_space, right_space, top_space, bottom_space;
	int frame_size_left, frame_size_top;
	PoloRect work_area;
	PoloRect avoid;
	PoloRect outer;

	frame_size_left = borders ? borders->left : 0;
	frame_size_top = borders ? borders->top : 0;

	work_area =
		*work_area_for_xinerama(input, input->focus_window_xinerama);
	avoid = focus_window->outer;
	outer = window->outer;

	max_width = MIN(avoid.width, outer.width);
	max_height = MIN(avoid.height, outer.height);
	left_space = avoid.x - work_area.x;
	right_space = work_area.width - (avoid.x + avoid.width - work_area.x);
	top_space = avoid.y - work_area.y;
	bottom_space =
		work_area.height - (avoid.y + avoid.height - work_area.y);
	left = MIN(left_space, outer.width);
	right = MIN(right_space, outer.width);
	top = MIN(top_space, outer.height);
	bottom = MIN(bottom_space, outer.height);

	side = META_LEFT;
	max_area = left * max_height;
	if (right * max_height > max_area) {
		side = META_RIGHT;
		max_area = right * max_height;
	}
	if (top * max_width > max_area) {
		side = META_TOP;
		max_area = top * max_width;
	}
	if (bottom * max_width > max_area) {
		side = META_BOTTOM;
		max_area = bottom * max_width;
	}

	if (max_area == 0)
		return;

	switch (side) {
	case META_LEFT:
		*new_y = avoid.y + frame_size_top;
		if (left_space > outer.width)
			*new_x = avoid.x - outer.width + frame_size_left;
		else
			*new_x = work_area.x + frame_size_left;
		break;
	case META_RIGHT:
		*new_y = avoid.y + frame_size_top;
		if (right_space > outer.width)
			*new_x = avoid.x + avoid.width + frame_size_left;
		else
			*new_x = work_area.x + work_area.width - outer.width +
				frame_size_left;
		break;
	case META_TOP:
		*new_x = avoid.x + frame_size_left;
		if (top_space > outer.height)
			*new_y = avoid.y - outer.height + frame_size_top;
		else
			*new_y = work_area.y + frame_size_top;
		break;
	case META_BOTTOM:
		*new_x = avoid.x + frame_size_left;
		if (bottom_space > outer.height)
			*new_y = avoid.y + avoid.height + frame_size_top;
		else
			*new_y = work_area.y + work_area.height - outer.height +
				frame_size_top;
		break;
	}
}

static gboolean
same_application(const PoloMarcoWindowInfo *window,
		 const PoloMarcoWindowInfo *other)
{
	return window->group != 0 && other->group != 0 &&
		window->group == other->group;
}

static void
avoid_being_obscured_as_second_modal_dialog(const PoloMarcoPlaceInput *input,
					    int *x, int *y)
{
	const PoloMarcoWindowInfo *window = input->window;
	const PoloMarcoWindowInfo *focus_window = input->focus_window;
	PoloRect overlap;

	if (window->denied_focus_and_not_transient && window->wm_state_modal &&
	    focus_window != NULL && same_application(window, focus_window) &&
	    polo_marco_rect_intersect(&window->rect, &focus_window->rect,
				      &overlap)) {
		find_most_freespace(input, focus_window, *x, *y, x, y);
	}
}

static gboolean
rectangle_overlaps_some_window(const PoloRect *rect, GList *windows)
{
	GList *tmp;
	PoloRect dest;

	tmp = windows;
	while (tmp != NULL) {
		const PoloMarcoWindowInfo *other = tmp->data;

		switch (other->type) {
		case POLO_WINDOW_DOCK:
		case POLO_WINDOW_SPLASHSCREEN:
		case POLO_WINDOW_DESKTOP:
		case POLO_WINDOW_DIALOG:
		case POLO_WINDOW_MODAL_DIALOG:
			break;

		case POLO_WINDOW_NORMAL:
		case POLO_WINDOW_UTILITY:
		case POLO_WINDOW_TOOLBAR:
		case POLO_WINDOW_MENU:
			if (polo_marco_rect_intersect(rect, &other->outer,
						      &dest))
				return TRUE;
			break;
		}

		tmp = tmp->next;
	}

	return FALSE;
}

static gint
leftmost_cmp(gconstpointer a, gconstpointer b)
{
	const PoloMarcoWindowInfo *aw = a;
	const PoloMarcoWindowInfo *bw = b;
	int ax, bx;

	ax = aw->frame.x;
	bx = bw->frame.x;

	if (ax < bx)
		return -1;
	else if (ax > bx)
		return 1;
	else
		return 0;
}

static gint
topmost_cmp(gconstpointer a, gconstpointer b)
{
	const PoloMarcoWindowInfo *aw = a;
	const PoloMarcoWindowInfo *bw = b;
	int ay, by;

	ay = aw->frame.y;
	by = bw->frame.y;

	if (ay < by)
		return -1;
	else if (ay > by)
		return 1;
	else
		return 0;
}

static void
center_tile_rect_in_area(PoloRect *rect, const PoloRect *work_area)
{
	int fluff;

	fluff = (work_area->width % (rect->width + 1)) / 2;
	rect->x = work_area->x + fluff;
	fluff = (work_area->height % (rect->height + 1)) / 3;
	rect->y = work_area->y + fluff;
}

static void
center_rect_in_area(PoloRect *rect, const PoloRect *work_area)
{
	rect->x = work_area->x + ((work_area->width - rect->width) / 2);
	rect->y = work_area->y + ((work_area->height - rect->height) / 2);
}

static gboolean
find_first_fit(const PoloMarcoPlaceInput *input, GList *windows, int xinerama,
	       int x, int y, int *new_x, int *new_y)
{
	const PoloMarcoWindowInfo *window = input->window;
	const PoloFrameBorders *borders = input->borders;
	int retval;
	GList *below_sorted;
	GList *right_sorted;
	GList *tmp;
	PoloRect rect;
	PoloRect work_area;

	retval = FALSE;

	below_sorted = g_list_copy(windows);
	below_sorted = g_list_sort(below_sorted, leftmost_cmp);
	below_sorted = g_list_sort(below_sorted, topmost_cmp);

	right_sorted = g_list_copy(windows);
	right_sorted = g_list_sort(right_sorted, topmost_cmp);
	right_sorted = g_list_sort(right_sorted, leftmost_cmp);

	rect.x = 0;
	rect.y = 0;
	rect.width = window->rect.width;
	rect.height = window->rect.height;

	if (borders) {
		rect.width += borders->left + borders->right;
		rect.height += borders->top + borders->bottom;
	}

	work_area = *work_area_for_xinerama(input, xinerama);

	if (input->prefs.center_new_windows)
		center_rect_in_area(&rect, &work_area);
	else
		center_tile_rect_in_area(&rect, &work_area);

	if (polo_marco_rect_contains_rect(&work_area, &rect) &&
	    (input->prefs.center_new_windows ||
	     !rectangle_overlaps_some_window(&rect, windows))) {
		*new_x = rect.x;
		*new_y = rect.y;
		if (borders) {
			*new_x += borders->left;
			*new_y += borders->top;
		}

		retval = TRUE;

		goto out;
	}

	tmp = below_sorted;
	while (tmp != NULL) {
		const PoloMarcoWindowInfo *w = tmp->data;
		PoloRect outer_rect = w->outer;

		rect.x = outer_rect.x;
		rect.y = outer_rect.y + outer_rect.height;

		if (polo_marco_rect_contains_rect(&work_area, &rect) &&
		    !rectangle_overlaps_some_window(&rect, below_sorted)) {
			*new_x = rect.x;
			*new_y = rect.y;
			if (borders) {
				*new_x += borders->left;
				*new_y += borders->top;
			}

			retval = TRUE;

			goto out;
		}

		tmp = tmp->next;
	}

	tmp = right_sorted;
	while (tmp != NULL) {
		const PoloMarcoWindowInfo *w = tmp->data;
		PoloRect outer_rect = w->outer;

		rect.x = outer_rect.x + outer_rect.width;
		rect.y = outer_rect.y;

		if (polo_marco_rect_contains_rect(&work_area, &rect) &&
		    !rectangle_overlaps_some_window(&rect, right_sorted)) {
			*new_x = rect.x;
			*new_y = rect.y;
			if (borders) {
				*new_x += borders->left;
				*new_y += borders->top;
			}

			retval = TRUE;

			goto out;
		}

		tmp = tmp->next;
	}

out:

	g_list_free(below_sorted);
	g_list_free(right_sorted);
	return retval;
}

void
polo_marco_place(const PoloMarcoPlaceInput *input, int x, int y,
		 PoloMarcoPlaceResult *result)
{
	const PoloMarcoWindowInfo *window = input->window;
	GList *windows;
	const PoloMarcoXinerama *xi;
	PoloPlacementMode placement_mode;
	int i;

	result->maximize_after_placement = FALSE;
	result->move_after_placement = FALSE;

	windows = NULL;

	switch (window->type) {
	case POLO_WINDOW_NORMAL:
	case POLO_WINDOW_DIALOG:
	case POLO_WINDOW_MODAL_DIALOG:
	case POLO_WINDOW_SPLASHSCREEN:
		break;

	case POLO_WINDOW_DESKTOP:
	case POLO_WINDOW_DOCK:
	case POLO_WINDOW_TOOLBAR:
	case POLO_WINDOW_MENU:
	case POLO_WINDOW_UTILITY:
		goto done_no_constraints;
	}

	if (input->prefs.disable_workarounds) {
		switch (window->type) {
		case POLO_WINDOW_NORMAL:
			if (window->size_hints_flags &
			    POLO_SIZE_HINT_US_POSITION)
				goto done;
			break;

		case POLO_WINDOW_DIALOG:
		case POLO_WINDOW_MODAL_DIALOG:
		case POLO_WINDOW_SPLASHSCREEN:
			break;

		case POLO_WINDOW_DESKTOP:
		case POLO_WINDOW_DOCK:
		case POLO_WINDOW_TOOLBAR:
		case POLO_WINDOW_MENU:
		case POLO_WINDOW_UTILITY:
			if (window->size_hints_flags &
			    POLO_SIZE_HINT_P_POSITION)
				goto done_no_constraints;
			break;
		}
	} else {
		if ((window->size_hints_flags & POLO_SIZE_HINT_P_POSITION) ||
		    (window->size_hints_flags & POLO_SIZE_HINT_US_POSITION)) {
			avoid_being_obscured_as_second_modal_dialog(input, &x,
								    &y);
			goto done_no_constraints;
		}
	}

	if ((window->type == POLO_WINDOW_DIALOG ||
	     window->type == POLO_WINDOW_MODAL_DIALOG) &&
	    input->parent != NULL) {
		const PoloMarcoWindowInfo *parent = input->parent;
		int w;

		x = parent->position.x;
		y = parent->position.y;
		w = parent->rect.width;

		x = x + w / 2;
		x -= window->rect.width / 2;

		y += (parent->rect.height - window->rect.height) / 3;

		if (input->borders)
			y += input->borders->top;

		avoid_being_obscured_as_second_modal_dialog(input, &x, &y);

		goto done;
	}

	if (window->type == POLO_WINDOW_DIALOG ||
	    window->type == POLO_WINDOW_MODAL_DIALOG ||
	    window->type == POLO_WINDOW_SPLASHSCREEN) {
		int w, h;

		xi = &input->xineramas[input->current_xinerama];

		w = xi->rect.width;
		h = xi->rect.height;

		x = (w - window->rect.width) / 2;
		y = (h - window->rect.height) / 2;

		x += xi->rect.x;
		y += xi->rect.y;

		goto done_check_denied_focus;
	}

	for (i = 0; i < input->n_windows; i++) {
		const PoloMarcoWindowInfo *w = &input->windows[i];

		if (w->showing_on_its_workspace &&
		    (window->workspace == w->workspace ||
		     window->on_all_workspaces || w->on_all_workspaces))
			windows = g_list_prepend(windows, (gpointer)w);
	}

	xi = &input->xineramas[input->current_xinerama];

	x = xi->rect.x;
	y = xi->rect.y;

	placement_mode = input->prefs.placement_mode;

	if (placement_mode == POLO_PLACEMENT_POINTER ||
	    placement_mode == POLO_PLACEMENT_MANUAL) {
		if (place_by_pointer(input, placement_mode, &x, &y, result))
			goto done_check_denied_focus;
	}

	if (find_first_fit(input, windows, input->current_xinerama, x, y, &x,
			   &y))
		goto done_check_denied_focus;

	if (window->has_maximize_func && window->decorated &&
	    !window->fullscreen) {
		const PoloRect *workarea =
			work_area_for_xinerama(input, input->current_xinerama);
		PoloRect outer = window->outer;

		if (outer.width >= workarea->width &&
		    outer.height >= workarea->height)
			result->maximize_after_placement = TRUE;
	}

	if (!input->prefs.center_new_windows &&
	    (x == xi->rect.x && y == xi->rect.y))
		find_next_cascade(input, windows, x, y, &x, &y);

done_check_denied_focus:
	if (window->denied_focus_and_not_transient &&
	    input->focus_window != NULL) {
		gboolean found_fit;
		const PoloMarcoWindowInfo *focus_window = input->focus_window;
		PoloRect overlap;

		xi = &input->xineramas[input->current_xinerama];

		found_fit = !polo_marco_rect_intersect(
			&window->rect, &focus_window->rect, &overlap);

		if (!input->prefs.center_new_windows && !found_fit) {
			GList *focus_window_list;
			focus_window_list =
				g_list_prepend(NULL, (gpointer)focus_window);

			x = xi->rect.x;
			y = xi->rect.y;

			found_fit = find_first_fit(input, focus_window_list,
						   input->current_xinerama, x,
						   y, &x, &y);
			g_list_free(focus_window_list);
		}

		if (!found_fit)
			find_most_freespace(input, focus_window, x, y, &x, &y);
	}

done:
	g_list_free(windows);

done_no_constraints:

	result->x = x;
	result->y = y;
}
