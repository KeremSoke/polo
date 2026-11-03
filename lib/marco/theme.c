/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Marco Theme Rendering */

/*
 * Copyright (C) 2001 Havoc Pennington
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file theme.c    Making Marco look pretty
 *
 * The window decorations drawn by Marco are described by files on disk
 * known internally as "themes" (externally as "window border themes" on
 * http://art.gnome.org/themes/marco/ or "Marco themes"). This file
 * contains most of the code necessary to support themes; it does not
 * contain the XML parser, which is in theme-parser.c.
 *
 * \bug This is a big file with lots of different subsystems, which might
 * be better split out into separate files.
 */

/**
 * \defgroup tokenizer   The theme expression tokenizer
 *
 * Themes can use a simple expression language to represent the values of
 * things. This is the tokeniser used for that language.
 *
 * \bug We could remove almost all this code by using GScanner instead,
 * but we would also have to find every expression in every existing theme
 * we could and make sure the parse trees were the same.
 */

/**
 * \defgroup parser  The theme expression parser
 *
 * Themes can use a simple expression language to represent the values of
 * things. This is the parser used for that language.
 */

#include "theme.h"
#include "theme-parser.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define DEBUG_FILL_STRUCT(s) memset((s), 0xef, sizeof(*(s)))

/**
 * Sets all the fields of a border to dummy values.
 *
 * \param border The border whose fields should be reset.
 */
static void
init_border(PoloBorder *border)
{
	border->top = -1;
	border->bottom = -1;
	border->left = -1;
	border->right = -1;
}

/**
 * Creates a new, empty PoloFrameLayout. The fields will be set to dummy
 * values.
 *
 * \return The newly created PoloFrameLayout.
 */
PoloFrameLayout *
polo_marco_frame_layout_new(void)
{
	PoloFrameLayout *layout;

	layout = g_new0(PoloFrameLayout, 1);

	layout->refcount = 1;

	/* Fill with -1 values to detect invalid themes */
	layout->left_width = -1;
	layout->right_width = -1;
	layout->bottom_height = -1;

	layout->invisible_resize_border.left = 10;
	layout->invisible_resize_border.right = 10;
	layout->invisible_resize_border.bottom = 10;
	layout->invisible_resize_border.top = 10;

	init_border(&layout->title_border);

	layout->title_vertical_pad = -1;

	layout->right_titlebar_edge = -1;
	layout->left_titlebar_edge = -1;

	layout->button_sizing = POLO_BUTTON_SIZING_LAST;
	layout->button_aspect = 1.0;
	layout->button_width = -1;
	layout->button_height = -1;

	layout->has_title = TRUE;
	layout->title_scale = 1.0;

	init_border(&layout->button_border);

	return layout;
}

/**
 *
 */
static gboolean
validate_border(const PoloBorder *border, const char **bad)
{
	*bad = NULL;

	if (border->top < 0)
		*bad = _("top");
	else if (border->bottom < 0)
		*bad = _("bottom");
	else if (border->left < 0)
		*bad = _("left");
	else if (border->right < 0)
		*bad = _("right");

	return *bad == NULL;
}

/**
 * Ensures that the theme supplied a particular dimension. When a
 * PoloFrameLayout is created, all its integer fields are set to -1
 * by polo_marco_frame_layout_new(). After an instance of this type
 * should have been initialised, this function checks that
 * a given field is not still at -1. It is never called directly, but
 * rather via the CHECK_GEOMETRY_VALUE and CHECK_GEOMETRY_BORDER
 * macros.
 *
 * \param      val    The value to check
 * \param      name   The name to use in the error message
 * \param[out] error  Set to an error if val was not initialised
 */
static gboolean
validate_geometry_value(int val, const char *name, GError **error)
{
	if (val < 0) {
		g_set_error(
			error, POLO_THEME_ERROR,
			POLO_THEME_ERROR_FRAME_GEOMETRY,
			_("frame geometry does not specify \"%s\" dimension"),
			name);
		return FALSE;
	} else
		return TRUE;
}

static gboolean
validate_geometry_border(const PoloBorder *border, const char *name,
			 GError **error)
{
	const char *bad;

	if (!validate_border(border, &bad)) {
		g_set_error(error, POLO_THEME_ERROR,
			    POLO_THEME_ERROR_FRAME_GEOMETRY,
			    _("frame geometry does not specify dimension "
			      "\"%s\" for border \"%s\""),
			    bad, name);
		return FALSE;
	} else
		return TRUE;
}

gboolean
polo_marco_frame_layout_validate(const PoloFrameLayout *layout, GError **error)
{
	g_return_val_if_fail(layout != NULL, FALSE);

#define CHECK_GEOMETRY_VALUE(vname)                                            \
	if (!validate_geometry_value(layout->vname, #vname, error))            \
	return FALSE

#define CHECK_GEOMETRY_BORDER(bname)                                           \
	if (!validate_geometry_border(&layout->bname, #bname, error))          \
	return FALSE

	CHECK_GEOMETRY_VALUE(left_width);
	CHECK_GEOMETRY_VALUE(right_width);
	CHECK_GEOMETRY_VALUE(bottom_height);

	CHECK_GEOMETRY_BORDER(title_border);

	CHECK_GEOMETRY_VALUE(title_vertical_pad);

	CHECK_GEOMETRY_VALUE(right_titlebar_edge);
	CHECK_GEOMETRY_VALUE(left_titlebar_edge);

	switch (layout->button_sizing) {
	case POLO_BUTTON_SIZING_ASPECT:
		if (layout->button_aspect < (0.1) ||
		    layout->button_aspect > (15.0)) {
			g_set_error(
				error, POLO_THEME_ERROR,
				POLO_THEME_ERROR_FRAME_GEOMETRY,
				_("Button aspect ratio %g is not reasonable"),
				layout->button_aspect);
			return FALSE;
		}
		break;
	case POLO_BUTTON_SIZING_FIXED:
		CHECK_GEOMETRY_VALUE(button_width);
		CHECK_GEOMETRY_VALUE(button_height);
		break;
	case POLO_BUTTON_SIZING_LAST:
		g_set_error(
			error, POLO_THEME_ERROR,
			POLO_THEME_ERROR_FRAME_GEOMETRY,
			_("Frame geometry does not specify size of buttons"));
		return FALSE;
	}

	CHECK_GEOMETRY_BORDER(button_border);

	return TRUE;
}

PoloFrameLayout *
polo_marco_frame_layout_copy(const PoloFrameLayout *src)
{
	PoloFrameLayout *layout;

	layout = g_new0(PoloFrameLayout, 1);

	*layout = *src;

	layout->refcount = 1;

	return layout;
}

void
polo_marco_frame_layout_ref(PoloFrameLayout *layout)
{
	g_return_if_fail(layout != NULL);

	layout->refcount += 1;
}

void
polo_marco_frame_layout_unref(PoloFrameLayout *layout)
{
	g_return_if_fail(layout != NULL);
	g_return_if_fail(layout->refcount > 0);

	layout->refcount -= 1;

	if (layout->refcount == 0) {
		DEBUG_FILL_STRUCT(layout);
		g_free(layout);
	}
}

void
polo_marco_frame_layout_get_borders(const PoloFrameLayout *layout,
				    int text_height, PoloFrameFlags flags,
				    PoloFrameType type,
				    PoloThemeBorders *borders)
{
	int buttons_height, title_height;

	polo_marco_theme_borders_clear(borders);

	/* For a full-screen window, we don't have any borders, visible or not.
	 */
	if (flags & POLO_FRAME_FULLSCREEN)
		return;

	g_return_if_fail(layout != NULL);

	if (!layout->has_title)
		text_height = 0;

	buttons_height = layout->button_height + layout->button_border.top +
		layout->button_border.bottom;
	title_height = text_height + layout->title_vertical_pad +
		layout->title_border.top + layout->title_border.bottom;

	borders->visible.top = MAX(buttons_height, title_height);
	borders->visible.left = layout->left_width;
	borders->visible.right = layout->right_width;
	borders->visible.bottom = layout->bottom_height;

	borders->shadow.top = 0;
	borders->shadow.left = 0;
	borders->shadow.right = 0;
	borders->shadow.bottom = 0;

	if (flags & POLO_FRAME_ALLOWS_HORIZONTAL_RESIZE) {
		borders->resize.left = layout->invisible_resize_border.left;
		borders->resize.right = layout->invisible_resize_border.right;
	}

	if (flags & POLO_FRAME_ALLOWS_VERTICAL_RESIZE) {
		borders->resize.bottom = layout->invisible_resize_border.bottom;

		if (type != POLO_FRAME_TYPE_ATTACHED)
			borders->resize.top =
				layout->invisible_resize_border.top;
	}

	borders->invisible.left =
		MAX(borders->shadow.left, borders->resize.left);
	borders->invisible.right =
		MAX(borders->shadow.right, borders->resize.right);
	borders->invisible.bottom =
		MAX(borders->shadow.bottom, borders->resize.bottom);
	borders->invisible.top = MAX(borders->shadow.top, borders->resize.top);

	/* Maximized and tiled windows should not have invisible borders on the
	 * sides that touch the screen edges */
	if (flags &
	    (POLO_FRAME_MAXIMIZED | POLO_FRAME_TILED_LEFT |
	     POLO_FRAME_TILED_RIGHT)) {
		borders->invisible.top = 0;
		borders->invisible.bottom = 0;
	}
	if (flags & (POLO_FRAME_MAXIMIZED | POLO_FRAME_TILED_LEFT))
		borders->invisible.left = 0;
	if (flags & (POLO_FRAME_MAXIMIZED | POLO_FRAME_TILED_RIGHT))
		borders->invisible.right = 0;

	if (type == POLO_FRAME_TYPE_ATTACHED)
		borders->invisible.top = 0;

	borders->total.left = borders->invisible.left + borders->visible.left;
	borders->total.right =
		borders->invisible.right + borders->visible.right;
	borders->total.bottom =
		borders->invisible.bottom + borders->visible.bottom;
	borders->total.top = borders->invisible.top + borders->visible.top;

	if (flags & POLO_FRAME_SHADED)
		borders->visible.bottom = borders->invisible.bottom = 0;
}

static PoloButtonSpace *
rect_for_function(PoloFrameGeometry *fgeom, PoloFrameFlags flags,
		  PoloButtonFunction function, PoloTheme *theme)
{
	/* Firstly, check version-specific things. */

	if (POLO_THEME_ALLOWS(theme, POLO_THEME_SHADE_STICK_ABOVE_BUTTONS)) {
		switch (function) {
		case POLO_BUTTON_FUNCTION_SHADE:
			if ((flags & POLO_FRAME_ALLOWS_SHADE) &&
			    !(flags & POLO_FRAME_SHADED))
				return &fgeom->shade_rect;
			else
				return NULL;
		case POLO_BUTTON_FUNCTION_ABOVE:
			if (!(flags & POLO_FRAME_ABOVE))
				return &fgeom->above_rect;
			else
				return NULL;
		case POLO_BUTTON_FUNCTION_STICK:
			if (!(flags & POLO_FRAME_STUCK))
				return &fgeom->stick_rect;
			else
				return NULL;
		case POLO_BUTTON_FUNCTION_UNSHADE:
			if ((flags & POLO_FRAME_ALLOWS_SHADE) &&
			    (flags & POLO_FRAME_SHADED))
				return &fgeom->unshade_rect;
			else
				return NULL;
		case POLO_BUTTON_FUNCTION_UNABOVE:
			if (flags & POLO_FRAME_ABOVE)
				return &fgeom->unabove_rect;
			else
				return NULL;
		case POLO_BUTTON_FUNCTION_UNSTICK:
			if (flags & POLO_FRAME_STUCK)
				return &fgeom->unstick_rect;
		default:
			/* just go on to the next switch block */;
		}
	}

	/* now consider the buttons which exist in all versions */

	switch (function) {
	case POLO_BUTTON_FUNCTION_MENU:
		if (flags & POLO_FRAME_ALLOWS_MENU)
			return &fgeom->menu_rect;
		else
			return NULL;
	case POLO_BUTTON_FUNCTION_APPMENU:
		if (flags & POLO_FRAME_ALLOWS_APPMENU)
			return &fgeom->appmenu_rect;
		else
			return NULL;
	case POLO_BUTTON_FUNCTION_MINIMIZE:
		if (flags & POLO_FRAME_ALLOWS_MINIMIZE)
			return &fgeom->min_rect;
		else
			return NULL;
	case POLO_BUTTON_FUNCTION_MAXIMIZE:
		if (flags & POLO_FRAME_ALLOWS_MAXIMIZE)
			return &fgeom->max_rect;
		else
			return NULL;
	case POLO_BUTTON_FUNCTION_CLOSE:
		if (flags & POLO_FRAME_ALLOWS_DELETE)
			return &fgeom->close_rect;
		else
			return NULL;
	case POLO_BUTTON_FUNCTION_STICK:
	case POLO_BUTTON_FUNCTION_SHADE:
	case POLO_BUTTON_FUNCTION_ABOVE:
	case POLO_BUTTON_FUNCTION_UNSTICK:
	case POLO_BUTTON_FUNCTION_UNSHADE:
	case POLO_BUTTON_FUNCTION_UNABOVE:
		/* we are being asked for a >v1 button which hasn't been handled
		 * yet, so obviously we're not in a theme which supports that
		 * version. therefore, we don't show the button. return NULL and
		 * all will be well.
		 */
		return NULL;

	case POLO_BUTTON_FUNCTION_LAST:
		return NULL;
	}

	return NULL;
}

static gboolean
strip_button(PoloButtonSpace *func_rects[POLO_MAX_BUTTONS_PER_CORNER],
	     PoloRect *bg_rects[POLO_MAX_BUTTONS_PER_CORNER], int *n_rects,
	     PoloButtonSpace *to_strip)
{
	int i;

	i = 0;
	while (i < *n_rects) {
		if (func_rects[i] == to_strip) {
			*n_rects -= 1;

			/* shift the other rects back in the array */
			while (i < *n_rects) {
				func_rects[i] = func_rects[i + 1];
				bg_rects[i] = bg_rects[i + 1];

				++i;
			}

			func_rects[i] = NULL;
			bg_rects[i] = NULL;

			return TRUE;
		}

		++i;
	}

	return FALSE; /* did not strip anything */
}

void
polo_marco_frame_layout_calc_geometry(const PoloFrameLayout *layout,
				      int text_height, PoloFrameFlags flags,
				      int client_width, int client_height,
				      const PoloButtonLayout *button_layout,
				      PoloFrameGeometry *fgeom,
				      PoloTheme *theme)
{
	int i, n_left, n_right, n_left_spacers, n_right_spacers;
	int x;
	int button_y;
	int title_right_edge;
	int width, height;
	int button_width, button_height;
	int min_size_for_rounding;

	/* the left/right rects in order; the max # of rects
	 * is the number of button functions
	 */
	PoloButtonSpace *left_func_rects[POLO_MAX_BUTTONS_PER_CORNER];
	PoloButtonSpace *right_func_rects[POLO_MAX_BUTTONS_PER_CORNER];
	PoloRect *left_bg_rects[POLO_MAX_BUTTONS_PER_CORNER];
	gboolean left_buttons_has_spacer[POLO_MAX_BUTTONS_PER_CORNER];
	PoloRect *right_bg_rects[POLO_MAX_BUTTONS_PER_CORNER];
	gboolean right_buttons_has_spacer[POLO_MAX_BUTTONS_PER_CORNER];

	PoloThemeBorders borders;

	polo_marco_frame_layout_get_borders(
		layout, text_height, flags,
		POLO_FRAME_TYPE_NORMAL, /* Will be updated when type parameter
					   is passed down */
		&borders);

	fgeom->borders = borders;

	width = client_width + borders.total.left + borders.total.right;

	height = ((flags & POLO_FRAME_SHADED) ? 0 : client_height) +
		borders.total.top + borders.total.bottom;

	fgeom->width = width;
	fgeom->height = height;

	fgeom->top_titlebar_edge = layout->title_border.top;
	fgeom->bottom_titlebar_edge = layout->title_border.bottom;
	fgeom->left_titlebar_edge = layout->left_titlebar_edge;
	fgeom->right_titlebar_edge = layout->right_titlebar_edge;

	switch (layout->button_sizing) {
	case POLO_BUTTON_SIZING_ASPECT:
		button_height = borders.visible.top -
			layout->button_border.top -
			layout->button_border.bottom;
		button_width =
			(int)(((double)button_height) / layout->button_aspect);
		break;
	case POLO_BUTTON_SIZING_FIXED:
		button_width = layout->button_width;
		button_height = layout->button_height;
		break;
	case POLO_BUTTON_SIZING_LAST:
		g_assert_not_reached();
	default:
		button_width = -1;
		button_height = -1;
	}

	/* FIXME all this code sort of pretends that duplicate buttons
	 * with the same function are allowed, but that breaks the
	 * code in frames.c, so isn't really allowed right now.
	 * Would need left_close_rect, right_close_rect, etc.
	 */

	/* Init all button rects to 0, lame hack */
	memset(ADDRESS_OF_BUTTON_RECTS(fgeom), '\0', LENGTH_OF_BUTTON_RECTS);

	n_left = 0;
	n_right = 0;
	n_left_spacers = 0;
	n_right_spacers = 0;

	if (!layout->hide_buttons) {
		/* Try to fill in rects */
		for (i = 0; i < POLO_MAX_BUTTONS_PER_CORNER &&
		     button_layout->left_buttons[i] !=
			     POLO_BUTTON_FUNCTION_LAST;
		     i++) {
			left_func_rects[n_left] = rect_for_function(
				fgeom, flags, button_layout->left_buttons[i],
				theme);
			if (left_func_rects[n_left] != NULL) {
				left_buttons_has_spacer[n_left] =
					button_layout
						->left_buttons_has_spacer[i];
				if (button_layout->left_buttons_has_spacer[i])
					++n_left_spacers;

				++n_left;
			}
		}

		for (i = 0; i < POLO_MAX_BUTTONS_PER_CORNER &&
		     button_layout->right_buttons[i] !=
			     POLO_BUTTON_FUNCTION_LAST;
		     i++) {
			right_func_rects[n_right] = rect_for_function(
				fgeom, flags, button_layout->right_buttons[i],
				theme);
			if (right_func_rects[n_right] != NULL) {
				right_buttons_has_spacer[n_right] =
					button_layout
						->right_buttons_has_spacer[i];
				if (button_layout->right_buttons_has_spacer[i])
					++n_right_spacers;

				++n_right;
			}
		}
	}

	for (i = 0; i < POLO_MAX_BUTTONS_PER_CORNER; i++) {
		left_bg_rects[i] = NULL;
		right_bg_rects[i] = NULL;
	}

	for (i = 0; i < n_left; i++) {
		if (n_left == 1)
			left_bg_rects[i] = &fgeom->left_single_background;
		else if (i == 0)
			left_bg_rects[i] = &fgeom->left_left_background;
		else if (i == (n_left - 1))
			left_bg_rects[i] = &fgeom->left_right_background;
		else
			left_bg_rects[i] =
				&fgeom->left_middle_backgrounds[i - 1];
	}

	for (i = 0; i < n_right; i++) {
		if (n_right == 1)
			right_bg_rects[i] = &fgeom->right_single_background;
		else if (i == (n_right - 1))
			right_bg_rects[i] = &fgeom->right_right_background;
		else if (i == 0)
			right_bg_rects[i] = &fgeom->right_left_background;
		else
			right_bg_rects[i] =
				&fgeom->right_middle_backgrounds[i - 1];
	}

	/* Be sure buttons fit */
	while (n_left > 0 || n_right > 0) {
		int space_used_by_buttons;
		int space_available;

		space_available = fgeom->width - layout->left_titlebar_edge -
			layout->right_titlebar_edge;

		space_used_by_buttons = 0;

		space_used_by_buttons += button_width * n_left;
		space_used_by_buttons +=
			(int)(0.75 * (double)(button_width * n_left_spacers));
		space_used_by_buttons += layout->button_border.left * n_left;
		space_used_by_buttons += layout->button_border.right * n_left;

		space_used_by_buttons += button_width * n_right;
		space_used_by_buttons +=
			(int)(0.75 * (double)(button_width * n_right_spacers));
		space_used_by_buttons += layout->button_border.left * n_right;
		space_used_by_buttons += layout->button_border.right * n_right;

		if (space_used_by_buttons <= space_available)
			break; /* Everything fits, bail out */

		/* First try to remove separators */
		if (n_left_spacers > 0) {
			left_buttons_has_spacer[--n_left_spacers] = FALSE;
			continue;
		} else if (n_right_spacers > 0) {
			right_buttons_has_spacer[--n_right_spacers] = FALSE;
			continue;
		}

		/* Otherwise we need to shave out a button. Shave
		 * above, stick, shade, min, max, close, then menu (menu is most
		 * useful); prefer the default button locations.
		 */
		if (strip_button(left_func_rects, left_bg_rects, &n_left,
				 &fgeom->above_rect))
			continue;
		else if (strip_button(right_func_rects, right_bg_rects,
				      &n_right, &fgeom->above_rect))
			continue;
		else if (strip_button(left_func_rects, left_bg_rects, &n_left,
				      &fgeom->stick_rect))
			continue;
		else if (strip_button(right_func_rects, right_bg_rects,
				      &n_right, &fgeom->stick_rect))
			continue;
		else if (strip_button(left_func_rects, left_bg_rects, &n_left,
				      &fgeom->shade_rect))
			continue;
		else if (strip_button(right_func_rects, right_bg_rects,
				      &n_right, &fgeom->shade_rect))
			continue;
		else if (strip_button(left_func_rects, left_bg_rects, &n_left,
				      &fgeom->min_rect))
			continue;
		else if (strip_button(right_func_rects, right_bg_rects,
				      &n_right, &fgeom->min_rect))
			continue;
		else if (strip_button(left_func_rects, left_bg_rects, &n_left,
				      &fgeom->max_rect))
			continue;
		else if (strip_button(right_func_rects, right_bg_rects,
				      &n_right, &fgeom->max_rect))
			continue;
		else if (strip_button(left_func_rects, left_bg_rects, &n_left,
				      &fgeom->close_rect))
			continue;
		else if (strip_button(right_func_rects, right_bg_rects,
				      &n_right, &fgeom->close_rect))
			continue;
		else if (strip_button(right_func_rects, right_bg_rects,
				      &n_right, &fgeom->menu_rect))
			continue;
		else if (strip_button(left_func_rects, left_bg_rects, &n_left,
				      &fgeom->menu_rect))
			continue;
		else if (strip_button(right_func_rects, right_bg_rects,
				      &n_right, &fgeom->appmenu_rect))
			continue;
		else if (strip_button(left_func_rects, left_bg_rects, &n_left,
				      &fgeom->appmenu_rect))
			continue;
		else {
			polo_marco_bug("Could not find a button to strip. "
				       "n_left = %d n_right = %d\n",
				       n_left, n_right);
		}
	}

	/* Save the button layout */
	fgeom->button_layout = *button_layout;
	fgeom->n_left_buttons = n_left;
	fgeom->n_right_buttons = n_right;

	/* center buttons vertically */
	button_y = (borders.visible.top -
		    (button_height + layout->button_border.top +
		     layout->button_border.bottom)) /
			2 +
		layout->button_border.top + borders.invisible.top;

	/* right edge of farthest-right button */
	x = width - layout->right_titlebar_edge - borders.invisible.right;

	i = n_right - 1;
	while (i >= 0) {
		PoloButtonSpace *rect;

		if (x < 0) /* if we go negative, leave the buttons we don't get
			      to as 0-width */
			break;

		rect = right_func_rects[i];
		rect->visible.x =
			x - layout->button_border.right - button_width;
		if (right_buttons_has_spacer[i])
			rect->visible.x -= (int)(0.75 * (double)button_width);

		rect->visible.y = button_y;
		rect->visible.width = button_width;
		rect->visible.height = button_height;

		if (flags & POLO_FRAME_MAXIMIZED ||
		    flags & POLO_FRAME_TILED_LEFT ||
		    flags & POLO_FRAME_TILED_RIGHT) {
			rect->clickable.x = rect->visible.x;
			rect->clickable.y = rect->visible.y;
			rect->clickable.width = button_width;
			rect->clickable.height = button_height;

			/* For the last right button on maximized/right-tiled
			 * windows, extend clickable area to the top-right edge
			 * (Fitts's law)*/
			if (i == n_right - 1 &&
			    (flags & POLO_FRAME_MAXIMIZED ||
			     flags & POLO_FRAME_TILED_RIGHT)) {
				rect->clickable.y = 0;
				rect->clickable.width +=
					layout->right_titlebar_edge +
					layout->right_width +
					layout->button_border.right;
				rect->clickable.height =
					button_y + button_height;
			}

		} else
			memmove(&(rect->clickable), &(rect->visible),
				sizeof(rect->clickable));

		*(right_bg_rects[i]) = rect->visible;

		x = rect->visible.x - layout->button_border.left;

		--i;
	}

	/* save right edge of titlebar for later use */
	title_right_edge = x - layout->title_border.right;

	/* Now x changes to be position from the left and we go through
	 * the left-side buttons
	 */
	x = layout->left_titlebar_edge + borders.invisible.left;
	for (i = 0; i < n_left; i++) {
		PoloButtonSpace *rect;

		rect = left_func_rects[i];

		rect->visible.x = x + layout->button_border.left;
		rect->visible.y = button_y;
		rect->visible.width = button_width;
		rect->visible.height = button_height;

		if (flags & POLO_FRAME_MAXIMIZED ||
		    flags & POLO_FRAME_TILED_LEFT ||
		    flags & POLO_FRAME_TILED_RIGHT) {
			rect->clickable.x = rect->visible.x;
			rect->clickable.y = rect->visible.y;
			rect->clickable.width = button_width;
			rect->clickable.height = button_height;

			/* For the first left button on maximized/left-tiled
			 * windows, extend clickable area to top-left corner
			 * (Fitts's law) */
			if (i == 0 &&
			    (flags & POLO_FRAME_MAXIMIZED ||
			     flags & POLO_FRAME_TILED_LEFT)) {
				rect->clickable.x = 0;
				rect->clickable.y = 0;
				rect->clickable.width +=
					layout->left_titlebar_edge +
					layout->left_width +
					layout->button_border.left;
				rect->clickable.height =
					button_y + button_height;
			}
		} else
			memmove(&(rect->clickable), &(rect->visible),
				sizeof(rect->clickable));

		x = rect->visible.x + rect->visible.width +
			layout->button_border.right;
		if (left_buttons_has_spacer[i])
			x += (int)(0.75 * (double)button_width);

		*(left_bg_rects[i]) = rect->visible;
	}

	/* We always fill as much vertical space as possible with title rect,
	 * rather than centering it like the buttons
	 */
	fgeom->title_rect.x = x + layout->title_border.left;
	fgeom->title_rect.y = layout->title_border.top + borders.invisible.top;
	fgeom->title_rect.width = title_right_edge - fgeom->title_rect.x;
	fgeom->title_rect.height = borders.visible.top -
		layout->title_border.top - layout->title_border.bottom;

	/* Nuke title if it won't fit */
	if (fgeom->title_rect.width < 0 || fgeom->title_rect.height < 0) {
		fgeom->title_rect.width = 0;
		fgeom->title_rect.height = 0;
	}

	if (flags & POLO_FRAME_SHADED)
		min_size_for_rounding = 0;
	else
		min_size_for_rounding = 5;

	fgeom->top_left_corner_rounded_radius = 0;
	fgeom->top_right_corner_rounded_radius = 0;
	fgeom->bottom_left_corner_rounded_radius = 0;
	fgeom->bottom_right_corner_rounded_radius = 0;

	if (borders.visible.top + borders.visible.left >= min_size_for_rounding)
		fgeom->top_left_corner_rounded_radius =
			layout->top_left_corner_rounded_radius;
	if (borders.visible.top + borders.visible.right >=
	    min_size_for_rounding)
		fgeom->top_right_corner_rounded_radius =
			layout->top_right_corner_rounded_radius;

	if (borders.visible.bottom + borders.visible.left >=
	    min_size_for_rounding)
		fgeom->bottom_left_corner_rounded_radius =
			layout->bottom_left_corner_rounded_radius;
	if (borders.visible.bottom + borders.visible.right >=
	    min_size_for_rounding)
		fgeom->bottom_right_corner_rounded_radius =
			layout->bottom_right_corner_rounded_radius;
}

PoloGradientSpec *
polo_marco_gradient_spec_new(PoloGradientType type)
{
	PoloGradientSpec *spec;

	spec = g_new(PoloGradientSpec, 1);

	spec->type = type;
	spec->color_specs = NULL;

	return spec;
}

static void
free_color_spec(gpointer spec, gpointer user_data)
{
	polo_marco_color_spec_free(spec);
}

void
polo_marco_gradient_spec_free(PoloGradientSpec *spec)
{
	g_return_if_fail(spec != NULL);

	g_slist_foreach(spec->color_specs, free_color_spec, NULL);
	g_slist_free(spec->color_specs);

	DEBUG_FILL_STRUCT(spec);
	g_free(spec);
}

gboolean
polo_marco_gradient_spec_validate(PoloGradientSpec *spec, GError **error)
{
	g_return_val_if_fail(spec != NULL, FALSE);

	if (g_slist_length(spec->color_specs) < 2) {
		g_set_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			    _("Gradients should have at least two colors"));
		return FALSE;
	}

	return TRUE;
}

PoloAlphaGradientSpec *
polo_marco_alpha_gradient_spec_new(PoloGradientType type, int n_alphas)
{
	PoloAlphaGradientSpec *spec;

	g_return_val_if_fail(n_alphas > 0, NULL);

	spec = g_new0(PoloAlphaGradientSpec, 1);

	spec->type = type;
	spec->alphas = g_new0(unsigned char, n_alphas);
	spec->n_alphas = n_alphas;

	return spec;
}

void
polo_marco_alpha_gradient_spec_free(PoloAlphaGradientSpec *spec)
{
	g_return_if_fail(spec != NULL);

	g_free(spec->alphas);
	g_free(spec);
}

PoloColorSpec *
polo_marco_color_spec_new(PoloColorSpecType type)
{
	PoloColorSpec *spec;
	PoloColorSpec dummy;
	int size;

	size = G_STRUCT_OFFSET(PoloColorSpec, data);

	switch (type) {
	case POLO_COLOR_SPEC_BASIC:
		size += sizeof(dummy.data.basic);
		break;

	case POLO_COLOR_SPEC_GTK:
		size += sizeof(dummy.data.gtk);
		break;

	case POLO_COLOR_SPEC_GTK_CUSTOM:
		size += sizeof(dummy.data.gtkcustom);
		break;

	case POLO_COLOR_SPEC_BLEND:
		size += sizeof(dummy.data.blend);
		break;

	case POLO_COLOR_SPEC_SHADE:
		size += sizeof(dummy.data.shade);
		break;
	}

	spec = g_malloc0(size);

	spec->type = type;

	return spec;
}

void
polo_marco_color_spec_free(PoloColorSpec *spec)
{
	g_return_if_fail(spec != NULL);

	switch (spec->type) {
	case POLO_COLOR_SPEC_BASIC:
		DEBUG_FILL_STRUCT(&spec->data.basic);
		break;

	case POLO_COLOR_SPEC_GTK:
		DEBUG_FILL_STRUCT(&spec->data.gtk);
		break;

	case POLO_COLOR_SPEC_GTK_CUSTOM:
		if (spec->data.gtkcustom.color_name)
			g_free(spec->data.gtkcustom.color_name);
		if (spec->data.gtkcustom.fallback)
			polo_marco_color_spec_free(
				spec->data.gtkcustom.fallback);
		DEBUG_FILL_STRUCT(&spec->data.gtkcustom);
		break;

	case POLO_COLOR_SPEC_BLEND:
		if (spec->data.blend.foreground)
			polo_marco_color_spec_free(spec->data.blend.foreground);
		if (spec->data.blend.background)
			polo_marco_color_spec_free(spec->data.blend.background);
		DEBUG_FILL_STRUCT(&spec->data.blend);
		break;

	case POLO_COLOR_SPEC_SHADE:
		if (spec->data.shade.base)
			polo_marco_color_spec_free(spec->data.shade.base);
		DEBUG_FILL_STRUCT(&spec->data.shade);
		break;
	}

	g_free(spec);
}

PoloColorSpec *
polo_marco_color_spec_new_from_string(const char *str, GError **err)
{
	PoloColorSpec *spec;

	spec = NULL;

	if (strncmp(str, "gtk:custom", 10) == 0) {
		const char *color_name_start, *fallback_str_start, *end;
		char *color_name;
		PoloColorSpec *fallback = NULL;
		static gboolean debug, debug_set = FALSE;

		if (!debug_set) {
			debug = g_getenv("MARCO_DISABLE_FALLBACK_COLOR") !=
				NULL;
			debug_set = TRUE;
		}

		if (str[10] != '(') {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("GTK custom color specification must "
				      "have color name and fallback in "
				      "parentheses, e.g. gtk:custom(foo,bar); "
				      "could not parse \"%s\""),
				    str);
			return NULL;
		}

		color_name_start = str + 11;

		fallback_str_start = color_name_start;
		while (*fallback_str_start && *fallback_str_start != ',') {
			if (!(g_ascii_isalnum(*fallback_str_start) ||
			      *fallback_str_start == '-' ||
			      *fallback_str_start == '_')) {
				g_set_error(
					err, POLO_THEME_ERROR,
					POLO_THEME_ERROR_FAILED,
					_("Invalid character '%c' in "
					  "color_name parameter of gtk:custom, "
					  "only A-Za-z0-9-_ are valid"),
					*fallback_str_start);
				return NULL;
			}
			fallback_str_start++;
		}
		fallback_str_start++;

		end = strrchr(str, ')');

		if (color_name_start == NULL || fallback_str_start == NULL ||
		    end == NULL) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Gtk:custom format is "
				      "\"gtk:custom(color_name,fallback)\", "
				      "\"%s\" does not fit the format"),
				    str);
			return NULL;
		}

		if (!debug) {
			char *fallback_str;
			fallback_str = g_strndup(fallback_str_start,
						 end - fallback_str_start);
			fallback = polo_marco_color_spec_new_from_string(
				fallback_str, err);
			g_free(fallback_str);
		} else {
			fallback = polo_marco_color_spec_new_from_string("pink",
									 err);
		}

		if (fallback == NULL)
			return NULL;

		color_name =
			g_strndup(color_name_start,
				  fallback_str_start - color_name_start - 1);

		spec = polo_marco_color_spec_new(POLO_COLOR_SPEC_GTK_CUSTOM);
		spec->data.gtkcustom.color_name = color_name;
		spec->data.gtkcustom.fallback = fallback;
	} else if (strncmp(str, "gtk:", 4) == 0) {
		/* GTK color */
		const char *bracket;
		const char *end_bracket;
		char *tmp;
		PoloStateFlags state;
		PoloGtkColorComponent component;

		bracket = str;
		while (*bracket && *bracket != '[')
			++bracket;

		if (*bracket == '\0') {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("GTK color specification must have the "
				      "state in brackets, e.g. gtk:fg[NORMAL] "
				      "where NORMAL is the state; could not "
				      "parse \"%s\""),
				    str);
			return NULL;
		}

		end_bracket = bracket;
		++end_bracket;
		while (*end_bracket && *end_bracket != ']')
			++end_bracket;

		if (*end_bracket == '\0') {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("GTK color specification must have a "
				      "close bracket after the state, e.g. "
				      "gtk:fg[NORMAL] where NORMAL is the "
				      "state; could not parse \"%s\""),
				    str);
			return NULL;
		}

		tmp = g_strndup(bracket + 1, end_bracket - bracket - 1);
		state = polo_marco_gtk_state_from_string(tmp);
		if (((int)state) == -1) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Did not understand state \"%s\" in "
				      "color specification"),
				    tmp);
			g_free(tmp);
			return NULL;
		}
		g_free(tmp);

		tmp = g_strndup(str + 4, bracket - str - 4);
		component = polo_marco_color_component_from_string(tmp);
		if (component == POLO_GTK_COLOR_LAST) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Did not understand color component "
				      "\"%s\" in color specification"),
				    tmp);
			g_free(tmp);
			return NULL;
		}
		g_free(tmp);

		spec = polo_marco_color_spec_new(POLO_COLOR_SPEC_GTK);
		spec->data.gtk.state = state;
		spec->data.gtk.component = component;
		g_assert(spec->data.gtk.component < POLO_GTK_COLOR_LAST);
	} else if (strncmp(str, "blend/", 6) == 0) {
		/* blend */
		char **split;
		double alpha;
		char *end;
		PoloColorSpec *fg;
		PoloColorSpec *bg;

		split = g_strsplit(str, "/", 4);

		if (split[0] == NULL || split[1] == NULL || split[2] == NULL ||
		    split[3] == NULL) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Blend format is "
				      "\"blend/bg_color/fg_color/alpha\", "
				      "\"%s\" does not fit the format"),
				    str);
			g_strfreev(split);
			return NULL;
		}

		alpha = g_ascii_strtod(split[3], &end);
		if (end == split[3]) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Could not parse alpha value \"%s\" in "
				      "blended color"),
				    split[3]);
			g_strfreev(split);
			return NULL;
		}

		if (alpha < (0.0 - 1e6) || alpha > (1.0 + 1e6)) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Alpha value \"%s\" in blended color is "
				      "not between 0.0 and 1.0"),
				    split[3]);
			g_strfreev(split);
			return NULL;
		}

		fg = NULL;
		bg = NULL;

		bg = polo_marco_color_spec_new_from_string(split[1], err);
		if (bg == NULL) {
			g_strfreev(split);
			return NULL;
		}

		fg = polo_marco_color_spec_new_from_string(split[2], err);
		if (fg == NULL) {
			polo_marco_color_spec_free(bg);
			g_strfreev(split);
			return NULL;
		}

		g_strfreev(split);

		spec = polo_marco_color_spec_new(POLO_COLOR_SPEC_BLEND);
		spec->data.blend.alpha = alpha;
		spec->data.blend.background = bg;
		spec->data.blend.foreground = fg;
	} else if (strncmp(str, "shade/", 6) == 0) {
		/* shade */
		char **split;
		double factor;
		char *end;
		PoloColorSpec *base;

		split = g_strsplit(str, "/", 3);

		if (split[0] == NULL || split[1] == NULL || split[2] == NULL) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Shade format is "
				      "\"shade/base_color/factor\", \"%s\" "
				      "does not fit the format"),
				    str);
			g_strfreev(split);
			return NULL;
		}

		factor = g_ascii_strtod(split[2], &end);
		if (end == split[2]) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Could not parse shade factor \"%s\" in "
				      "shaded color"),
				    split[2]);
			g_strfreev(split);
			return NULL;
		}

		if (factor < (0.0 - 1e6)) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Shade factor \"%s\" in shaded color is "
				      "negative"),
				    split[2]);
			g_strfreev(split);
			return NULL;
		}

		base = NULL;

		base = polo_marco_color_spec_new_from_string(split[1], err);
		if (base == NULL) {
			g_strfreev(split);
			return NULL;
		}

		g_strfreev(split);

		spec = polo_marco_color_spec_new(POLO_COLOR_SPEC_SHADE);
		spec->data.shade.factor = factor;
		spec->data.shade.base = base;
	} else {
		spec = polo_marco_color_spec_new(POLO_COLOR_SPEC_BASIC);

		if (!polo_marco_rgba_parse(&spec->data.basic.color, str)) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Could not parse color \"%s\""), str);
			polo_marco_color_spec_free(spec);
			return NULL;
		}
	}

	g_assert(spec);

	return spec;
}

PoloColorSpec *
polo_marco_color_spec_new_gtk(PoloGtkColorComponent component,
			      PoloStateFlags state)
{
	PoloColorSpec *spec;

	spec = polo_marco_color_spec_new(POLO_COLOR_SPEC_GTK);

	spec->data.gtk.component = component;
	spec->data.gtk.state = state;

	return spec;
}

/* Based on set_color() in gtkstyle.c */
#define LIGHTNESS_MULT 1.3
#define DARKNESS_MULT 0.7

/**
 * Represents an operation as a string.
 *
 * \param type  an operation, such as addition
 * \return  a string, such as "+"
 */
static const char *
op_name(PosOperatorType type)
{
	switch (type) {
	case POS_OP_ADD:
		return "+";
	case POS_OP_SUBTRACT:
		return "-";
	case POS_OP_MULTIPLY:
		return "*";
	case POS_OP_DIVIDE:
		return "/";
	case POS_OP_MOD:
		return "%";
	case POS_OP_MAX:
		return "`max`";
	case POS_OP_MIN:
		return "`min`";
	case POS_OP_NONE:
		break;
	}

	return "<unknown>";
}

/**
 * Parses a string and returns an operation.
 *
 * \param p  a pointer into a string representing an operation; part of an
 *           expression somewhere, so not null-terminated
 * \param len  set to the length of the string found. Set to 0 if none is.
 * \return  the operation found. If none was, returns POS_OP_NONE.
 */
static PosOperatorType
op_from_string(const char *p, int *len)
{
	*len = 0;

	switch (*p) {
	case '+':
		*len = 1;
		return POS_OP_ADD;
	case '-':
		*len = 1;
		return POS_OP_SUBTRACT;
	case '*':
		*len = 1;
		return POS_OP_MULTIPLY;
	case '/':
		*len = 1;
		return POS_OP_DIVIDE;
	case '%':
		*len = 1;
		return POS_OP_MOD;

	case '`':
		if (strncmp(p, "`max`", 5) == 0) {
			*len = 5;
			return POS_OP_MAX;
		} else if (strncmp(p, "`min`", 5) == 0) {
			*len = 5;
			return POS_OP_MIN;
		}
	}

	return POS_OP_NONE;
}

/**
 * Frees an array of tokens. All the tokens and their associated memory
 * will be freed.
 *
 * \param tokens  an array of tokens to be freed
 * \param n_tokens  how many tokens are in the array.
 */
static void
free_tokens(PosToken *tokens, int n_tokens)
{
	int i;

	/* n_tokens can be 0 since tokens may have been allocated more than
	 * it was initialized
	 */

	for (i = 0; i < n_tokens; i++)
		if (tokens[i].type == POS_TOKEN_VARIABLE)
			g_free(tokens[i].d.v.name);

	g_free(tokens);
}

/**
 * Tokenises a number in an expression.
 *
 * \param p  a pointer into a string representing an operation; part of an
 *           expression somewhere, so not null-terminated
 * \param end_return  set to a pointer to the end of the number found; but
 *                    not updated if no number was found at all
 * \param next  set to either an integer or a float token
 * \param[out] err  set to the problem if there was a problem
 * \return TRUE if a valid number was found, FALSE otherwise (and "err" will
 *         have been set)
 *
 * \bug The "while (*start)..." part: what's wrong with strchr-ish things?
 * \bug The name is wrong: it doesn't parse anything.
 * \ingroup tokenizer
 */
static gboolean
parse_number(const char *p, const char **end_return, PosToken *next,
	     GError **err)
{
	const char *start = p;
	char *end;
	gboolean is_float;
	char *num_str;

	while (*p && (*p == '.' || g_ascii_isdigit(*p)))
		++p;

	if (p == start) {
		char buf[7] = { '\0' };
		buf[g_unichar_to_utf8(g_utf8_get_char(p), buf)] = '\0';
		g_set_error(err, POLO_THEME_ERROR,
			    POLO_THEME_ERROR_BAD_CHARACTER,
			    _("Coordinate expression contains character '%s' "
			      "which is not allowed"),
			    buf);
		return FALSE;
	}

	*end_return = p;

	/* we need this to exclude floats like "1e6" */
	num_str = g_strndup(start, p - start);
	start = num_str;
	is_float = FALSE;
	while (*start) {
		if (*start == '.')
			is_float = TRUE;
		++start;
	}

	if (is_float) {
		next->type = POS_TOKEN_DOUBLE;
		next->d.d.val = g_ascii_strtod(num_str, &end);

		if (end == num_str) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Coordinate expression contains floating "
				      "point number '%s' which could not be "
				      "parsed"),
				    num_str);
			g_free(num_str);
			return FALSE;
		}
	} else {
		next->type = POS_TOKEN_INT;
		next->d.i.val = strtol(num_str, &end, 10);
		if (end == num_str) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Coordinate expression contains integer "
				      "'%s' which could not be parsed"),
				    num_str);
			g_free(num_str);
			return FALSE;
		}
	}

	g_free(num_str);

	g_assert(next->type == POS_TOKEN_INT || next->type == POS_TOKEN_DOUBLE);

	return TRUE;
}

/**
 * Whether a variable can validly appear as part of the name of a variable.
 */
#define IS_VARIABLE_CHAR(c) (g_ascii_isalpha((c)) || (c) == '_')

#if 0
static void
debug_print_tokens (PosToken *tokens,
                    int       n_tokens)
{
  int i;

  for (i = 0; i < n_tokens; i++)
    {
      PosToken *t = &tokens[i];

      g_print (" ");

      switch (t->type)
        {
        case POS_TOKEN_INT:
          g_print ("\"%d\"", t->d.i.val);
          break;
        case POS_TOKEN_DOUBLE:
          g_print ("\"%g\"", t->d.d.val);
          break;
        case POS_TOKEN_OPEN_PAREN:
          g_print ("\"(\"");
          break;
        case POS_TOKEN_CLOSE_PAREN:
          g_print ("\")\"");
          break;
        case POS_TOKEN_VARIABLE:
          g_print ("\"%s\"", t->d.v.name);
          break;
        case POS_TOKEN_OPERATOR:
          g_print ("\"%s\"", op_name (t->d.o.op));
          break;
        }
    }

  g_print ("\n");
}
#endif

/**
 * Tokenises an expression.
 *
 * \param      expr        The expression
 * \param[out] tokens_p    The resulting tokens
 * \param[out] n_tokens_p  The number of resulting tokens
 * \param[out] err  set to the problem if there was a problem
 *
 * \return  True if the expression was successfully tokenised; false otherwise.
 *
 * \ingroup tokenizer
 */
static gboolean
pos_tokenize(const char *expr, PosToken **tokens_p, int *n_tokens_p,
	     GError **err)
{
	PosToken *tokens;
	int n_tokens;
	int allocated;
	const char *p;

	*tokens_p = NULL;
	*n_tokens_p = 0;

	allocated = 3;
	n_tokens = 0;
	tokens = g_new(PosToken, allocated);

	p = expr;
	while (*p) {
		PosToken *next;
		int len;

		if (n_tokens == allocated) {
			allocated *= 2;
			tokens = g_renew(PosToken, tokens, allocated);
		}

		next = &tokens[n_tokens];

		switch (*p) {
		case '*':
		case '/':
		case '+':
		case '-': /* negative numbers aren't allowed so this is easy */
		case '%':
		case '`':
			next->type = POS_TOKEN_OPERATOR;
			next->d.o.op = op_from_string(p, &len);
			if (next->d.o.op != POS_OP_NONE) {
				++n_tokens;
				p = p + (len - 1); /* -1 since we ++p later */
			} else {
				g_set_error(err, POLO_THEME_ERROR,
					    POLO_THEME_ERROR_FAILED,
					    _("Coordinate expression contained "
					      "unknown operator at the start "
					      "of this text: \"%s\""),
					    p);

				goto error;
			}
			break;

		case '(':
			next->type = POS_TOKEN_OPEN_PAREN;
			++n_tokens;
			break;

		case ')':
			next->type = POS_TOKEN_CLOSE_PAREN;
			++n_tokens;
			break;

		case ' ':
		case '\t':
		case '\n':
			break;

		default:
			if (IS_VARIABLE_CHAR(*p)) {
				/* Assume variable */
				const char *start = p;
				while (*p && IS_VARIABLE_CHAR(*p))
					++p;
				g_assert(p != start);
				next->type = POS_TOKEN_VARIABLE;
				next->d.v.name = g_strndup(start, p - start);
				++n_tokens;
				--p; /* since we ++p again at the end of while
					loop */
			} else {
				/* Assume number */
				const char *end;

				if (!parse_number(p, &end, next, err))
					goto error;

				++n_tokens;
				p = end - 1; /* -1 since we ++p again at the end
						of while loop */
			}

			break;
		}

		++p;
	}

	if (n_tokens == 0) {
		g_set_error(
			err, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			_("Coordinate expression was empty or not understood"));

		goto error;
	}

	*tokens_p = tokens;
	*n_tokens_p = n_tokens;

	return TRUE;

error:
	g_assert(err == NULL || *err != NULL);

	free_tokens(tokens, n_tokens);
	return FALSE;
}

/**
 * The type of a PosExpr: either integer, double, or an operation.
 * \ingroup parser
 */
typedef enum { POS_EXPR_INT, POS_EXPR_DOUBLE, POS_EXPR_OPERATOR } PosExprType;

/**
 * Type and value of an expression in a parsed sequence. We don't
 * keep expressions in a tree; if this is of type POS_EXPR_OPERATOR,
 * the arguments of the operator will be in the array positions
 * immediately preceding and following this operator; they cannot
 * themselves be operators.
 *
 * \bug operator is char; it should really be of PosOperatorType.
 * \ingroup parser
 */
typedef struct {
	PosExprType type;
	union {
		double double_val;
		int int_val;
		char operator;
	} d;
} PosExpr;

#if 0
static void
debug_print_exprs (PosExpr *exprs,
                   int      n_exprs)
{
  int i;

  for (i = 0; i < n_exprs; i++)
    {
      switch (exprs[i].type)
        {
        case POS_EXPR_INT:
          g_print (" %d", exprs[i].d.int_val);
          break;
        case POS_EXPR_DOUBLE:
          g_print (" %g", exprs[i].d.double_val);
          break;
        case POS_EXPR_OPERATOR:
          g_print (" %s", op_name (exprs[i].d.operator));
          break;
        }
    }
  g_print ("\n");
}
#endif

static gboolean
do_operation(PosExpr *a, PosExpr *b, PosOperatorType op, GError **err)
{
	/* Promote types to double if required */
	if (a->type == POS_EXPR_DOUBLE || b->type == POS_EXPR_DOUBLE) {
		if (a->type != POS_EXPR_DOUBLE) {
			a->type = POS_EXPR_DOUBLE;
			a->d.double_val = a->d.int_val;
		}
		if (b->type != POS_EXPR_DOUBLE) {
			b->type = POS_EXPR_DOUBLE;
			b->d.double_val = b->d.int_val;
		}
	}

	g_assert(a->type == b->type);

	if (a->type == POS_EXPR_INT) {
		switch (op) {
		case POS_OP_MULTIPLY:
			a->d.int_val = a->d.int_val * b->d.int_val;
			break;
		case POS_OP_DIVIDE:
			if (b->d.int_val == 0) {
				g_set_error(err, POLO_THEME_ERROR,
					    POLO_THEME_ERROR_DIVIDE_BY_ZERO,
					    _("Coordinate expression results "
					      "in division by zero"));
				return FALSE;
			}
			a->d.int_val = a->d.int_val / b->d.int_val;
			break;
		case POS_OP_MOD:
			if (b->d.int_val == 0) {
				g_set_error(err, POLO_THEME_ERROR,
					    POLO_THEME_ERROR_DIVIDE_BY_ZERO,
					    _("Coordinate expression results "
					      "in division by zero"));
				return FALSE;
			}
			a->d.int_val = a->d.int_val % b->d.int_val;
			break;
		case POS_OP_ADD:
			a->d.int_val = a->d.int_val + b->d.int_val;
			break;
		case POS_OP_SUBTRACT:
			a->d.int_val = a->d.int_val - b->d.int_val;
			break;
		case POS_OP_MAX:
			a->d.int_val = MAX(a->d.int_val, b->d.int_val);
			break;
		case POS_OP_MIN:
			a->d.int_val = MIN(a->d.int_val, b->d.int_val);
			break;
		case POS_OP_NONE:
			g_assert_not_reached();
			break;
		}
	} else if (a->type == POS_EXPR_DOUBLE) {
		switch (op) {
		case POS_OP_MULTIPLY:
			a->d.double_val = a->d.double_val * b->d.double_val;
			break;
		case POS_OP_DIVIDE:
			if (b->d.double_val == 0.0) {
				g_set_error(err, POLO_THEME_ERROR,
					    POLO_THEME_ERROR_DIVIDE_BY_ZERO,
					    _("Coordinate expression results "
					      "in division by zero"));
				return FALSE;
			}
			a->d.double_val = a->d.double_val / b->d.double_val;
			break;
		case POS_OP_MOD:
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_MOD_ON_FLOAT,
				    _("Coordinate expression tries to use mod "
				      "operator on a floating-point number"));
			return FALSE;
		case POS_OP_ADD:
			a->d.double_val = a->d.double_val + b->d.double_val;
			break;
		case POS_OP_SUBTRACT:
			a->d.double_val = a->d.double_val - b->d.double_val;
			break;
		case POS_OP_MAX:
			a->d.double_val = MAX(a->d.double_val, b->d.double_val);
			break;
		case POS_OP_MIN:
			a->d.double_val = MIN(a->d.double_val, b->d.double_val);
			break;
		case POS_OP_NONE:
			g_assert_not_reached();
			break;
		}
	} else
		g_assert_not_reached();

	return TRUE;
}

static gboolean
do_operations(PosExpr *exprs, int *n_exprs, int precedence, GError **err)
{
	int i;

#if 0
  g_print ("Doing prec %d ops on %d exprs\n", precedence, *n_exprs);
  debug_print_exprs (exprs, *n_exprs);
#endif

	i = 1;
	while (i < *n_exprs) {
		gboolean compress;

		/* exprs[i-1] first operand
		 * exprs[i]   operator
		 * exprs[i+1] second operand
		 *
		 * we replace first operand with result of mul/div/mod,
		 * or skip over operator and second operand if we have
		 * an add/subtract
		 */

		if (exprs[i - 1].type == POS_EXPR_OPERATOR) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Coordinate expression has an operator "
				      "\"%s\" where an operand was expected"),
				    op_name(exprs[i - 1].d.operator));
			return FALSE;
		}

		if (exprs[i].type != POS_EXPR_OPERATOR) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Coordinate expression had an operand "
				      "where an operator was expected"));
			return FALSE;
		}

		if (i == (*n_exprs - 1)) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Coordinate expression ended with an "
				      "operator instead of an operand"));
			return FALSE;
		}

		g_assert((i + 1) < *n_exprs);

		if (exprs[i + 1].type == POS_EXPR_OPERATOR) {
			g_set_error(
				err, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
				_("Coordinate expression has operator \"%c\" "
				  "following operator \"%c\" with no operand "
				  "in between"),
				exprs[i + 1].d.operator, exprs[i].d.operator);
			return FALSE;
		}

		compress = FALSE;

		switch (precedence) {
		case 2:
			switch (exprs[i].d.operator) {
			case POS_OP_DIVIDE:
			case POS_OP_MOD:
			case POS_OP_MULTIPLY:
				compress = TRUE;
				if (!do_operation(&exprs[i - 1], &exprs[i + 1],
						  exprs[i].d.operator, err))
					return FALSE;
				break;
			}
			break;
		case 1:
			switch (exprs[i].d.operator) {
			case POS_OP_ADD:
			case POS_OP_SUBTRACT:
				compress = TRUE;
				if (!do_operation(&exprs[i - 1], &exprs[i + 1],
						  exprs[i].d.operator, err))
					return FALSE;
				break;
			}
			break;
			/* I have no rationale at all for making these
			 * low-precedence */
		case 0:
			switch (exprs[i].d.operator) {
			case POS_OP_MAX:
			case POS_OP_MIN:
				compress = TRUE;
				if (!do_operation(&exprs[i - 1], &exprs[i + 1],
						  exprs[i].d.operator, err))
					return FALSE;
				break;
			}
			break;
		}

		if (compress) {
			/* exprs[i-1] first operand (now result)
			 * exprs[i]   operator
			 * exprs[i+1] second operand
			 * exprs[i+2] new operator
			 *
			 * we move new operator just after first operand
			 */
			if ((i + 2) < *n_exprs) {
				memmove(&exprs[i], &exprs[i + 2],
					sizeof(PosExpr) * (*n_exprs - i - 2));
			}

			*n_exprs -= 2;
		} else {
			/* Skip operator and next operand */
			i += 2;
		}
	}

	return TRUE;
}

/**
 * There is a predefined set of variables which can appear in an expression.
 * Here we take a token representing a variable, and return the current value
 * of that variable in a particular environment.
 * (The value is always an integer.)
 *
 * There are supposedly some circumstances in which this function can be
 * called from outside Marco, in which case env->theme will be NULL, and
 * therefore we can't use it to find out quark values, so we do the comparison
 * using strcmp, which is slower.
 *
 * \param t  The token representing a variable
 * \param[out] result  The value of that variable; not set if the token did
 *                     not represent a known variable
 * \param env  The environment within which t should be evaluated
 * \param[out] err  set to the problem if there was a problem
 *
 * \return true if we found the variable asked for, false if we didn't
 *
 * \bug shouldn't t be const?
 * \bug we should perhaps consider some sort of lookup arrangement into an
 *      array; also, the duplication of code is unlovely; perhaps using glib
 *      string hashes instead of quarks would fix both problems?
 * \ingroup parser
 */
static gboolean
pos_eval_get_variable(PosToken *t, int *result, const PoloPositionExprEnv *env,
		      GError **err)
{
	if (env->theme) {
		if (t->d.v.name_quark == env->theme->quark_width)
			*result = env->rect.width;
		else if (t->d.v.name_quark == env->theme->quark_height)
			*result = env->rect.height;
		else if (env->object_width >= 0 &&
			 t->d.v.name_quark == env->theme->quark_object_width)
			*result = env->object_width;
		else if (env->object_height >= 0 &&
			 t->d.v.name_quark == env->theme->quark_object_height)
			*result = env->object_height;
		else if (t->d.v.name_quark == env->theme->quark_left_width)
			*result = env->left_width;
		else if (t->d.v.name_quark == env->theme->quark_right_width)
			*result = env->right_width;
		else if (t->d.v.name_quark == env->theme->quark_top_height)
			*result = env->top_height;
		else if (t->d.v.name_quark == env->theme->quark_bottom_height)
			*result = env->bottom_height;
		else if (t->d.v.name_quark == env->theme->quark_mini_icon_width)
			*result = env->mini_icon_width;
		else if (t->d.v.name_quark ==
			 env->theme->quark_mini_icon_height)
			*result = env->mini_icon_height;
		else if (t->d.v.name_quark == env->theme->quark_icon_width)
			*result = env->icon_width;
		else if (t->d.v.name_quark == env->theme->quark_icon_height)
			*result = env->icon_height;
		else if (t->d.v.name_quark == env->theme->quark_title_width)
			*result = env->title_width;
		else if (t->d.v.name_quark == env->theme->quark_title_height)
			*result = env->title_height;
		else if (t->d.v.name_quark == env->theme->quark_frame_x_center)
			*result = env->frame_x_center;
		else if (t->d.v.name_quark == env->theme->quark_frame_y_center)
			*result = env->frame_y_center;
		else {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_UNKNOWN_VARIABLE,
				    _("Coordinate expression had unknown "
				      "variable or constant \"%s\""),
				    t->d.v.name);
			return FALSE;
		}
	} else {
		if (strcmp(t->d.v.name, "width") == 0)
			*result = env->rect.width;
		else if (strcmp(t->d.v.name, "height") == 0)
			*result = env->rect.height;
		else if (env->object_width >= 0 &&
			 strcmp(t->d.v.name, "object_width") == 0)
			*result = env->object_width;
		else if (env->object_height >= 0 &&
			 strcmp(t->d.v.name, "object_height") == 0)
			*result = env->object_height;
		else if (strcmp(t->d.v.name, "left_width") == 0)
			*result = env->left_width;
		else if (strcmp(t->d.v.name, "right_width") == 0)
			*result = env->right_width;
		else if (strcmp(t->d.v.name, "top_height") == 0)
			*result = env->top_height;
		else if (strcmp(t->d.v.name, "bottom_height") == 0)
			*result = env->bottom_height;
		else if (strcmp(t->d.v.name, "mini_icon_width") == 0)
			*result = env->mini_icon_width;
		else if (strcmp(t->d.v.name, "mini_icon_height") == 0)
			*result = env->mini_icon_height;
		else if (strcmp(t->d.v.name, "icon_width") == 0)
			*result = env->icon_width;
		else if (strcmp(t->d.v.name, "icon_height") == 0)
			*result = env->icon_height;
		else if (strcmp(t->d.v.name, "title_width") == 0)
			*result = env->title_width;
		else if (strcmp(t->d.v.name, "title_height") == 0)
			*result = env->title_height;
		else if (strcmp(t->d.v.name, "frame_x_center") == 0)
			*result = env->frame_x_center;
		else if (strcmp(t->d.v.name, "frame_y_center") == 0)
			*result = env->frame_y_center;
		else {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_UNKNOWN_VARIABLE,
				    _("Coordinate expression had unknown "
				      "variable or constant \"%s\""),
				    t->d.v.name);
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * Evaluates a sequence of tokens within a particular environment context,
 * and returns the current value. May recur if parantheses are found.
 *
 * \param tokens  A list of tokens to evaluate.
 * \param n_tokens  How many tokens are in the list.
 * \param env  The environment context in which to evaluate the expression.
 * \param[out] result  The current value of the expression
 *
 * \bug Yes, we really do reparse the expression every time it's evaluated.
 *      We should keep the parse tree around all the time and just
 *      run the new values through it.
 * \ingroup parser
 */
static gboolean
pos_eval_helper(PosToken *tokens, int n_tokens, const PoloPositionExprEnv *env,
		PosExpr *result, GError **err)
{
	/* Lazy-ass hardcoded limit on number of terms in expression */
#define MAX_EXPRS 32
	int paren_level;
	int first_paren;
	int i;
	PosExpr exprs[MAX_EXPRS];
	int n_exprs;
	int precedence;

	/* Our first goal is to get a list of PosExpr, essentially
	 * substituting variables and handling parentheses.
	 */

	first_paren = 0;
	paren_level = 0;
	n_exprs = 0;
	for (i = 0; i < n_tokens; i++) {
		PosToken *t = &tokens[i];

		if (n_exprs >= MAX_EXPRS) {
			g_set_error(err, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    _("Coordinate expression parser overflowed "
				      "its buffer."));
			return FALSE;
		}

		if (paren_level == 0) {
			switch (t->type) {
			case POS_TOKEN_INT:
				exprs[n_exprs].type = POS_EXPR_INT;
				exprs[n_exprs].d.int_val = t->d.i.val;
				++n_exprs;
				break;

			case POS_TOKEN_DOUBLE:
				exprs[n_exprs].type = POS_EXPR_DOUBLE;
				exprs[n_exprs].d.double_val = t->d.d.val;
				++n_exprs;
				break;

			case POS_TOKEN_OPEN_PAREN:
				++paren_level;
				if (paren_level == 1)
					first_paren = i;
				break;

			case POS_TOKEN_CLOSE_PAREN:
				g_set_error(err, POLO_THEME_ERROR,
					    POLO_THEME_ERROR_BAD_PARENS,
					    _("Coordinate expression had a "
					      "close parenthesis with no open "
					      "parenthesis"));
				return FALSE;

			case POS_TOKEN_VARIABLE:
				exprs[n_exprs].type = POS_EXPR_INT;

				/* FIXME we should just dump all this crap
				 * in a hash, maybe keep width/height out
				 * for optimization purposes
				 */
				if (!pos_eval_get_variable(
					    t, &exprs[n_exprs].d.int_val, env,
					    err))
					return FALSE;

				++n_exprs;
				break;

			case POS_TOKEN_OPERATOR:
				exprs[n_exprs].type = POS_EXPR_OPERATOR;
				exprs[n_exprs].d.operator= t->d.o.op;
				++n_exprs;
				break;
			}
		} else {
			g_assert(paren_level > 0);

			switch (t->type) {
			case POS_TOKEN_INT:
			case POS_TOKEN_DOUBLE:
			case POS_TOKEN_VARIABLE:
			case POS_TOKEN_OPERATOR:
				break;

			case POS_TOKEN_OPEN_PAREN:
				++paren_level;
				break;

			case POS_TOKEN_CLOSE_PAREN:
				if (paren_level == 1) {
					/* We closed a toplevel paren group, so
					 * recurse */
					if (!pos_eval_helper(
						    &tokens[first_paren + 1],
						    i - first_paren - 1, env,
						    &exprs[n_exprs], err))
						return FALSE;

					++n_exprs;
				}

				--paren_level;
				break;
			}
		}
	}

	if (paren_level > 0) {
		g_set_error(err, POLO_THEME_ERROR, POLO_THEME_ERROR_BAD_PARENS,
			    _("Coordinate expression had an open parenthesis "
			      "with no close parenthesis"));
		return FALSE;
	}

	/* Now we have no parens and no vars; so we just do all the multiplies
	 * and divides, then all the add and subtract.
	 */
	if (n_exprs == 0) {
		g_set_error(err, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			    _("Coordinate expression doesn't seem to have any "
			      "operators or operands"));
		return FALSE;
	}

	/* precedence 1 ops */
	precedence = 2;
	while (precedence >= 0) {
		if (!do_operations(exprs, &n_exprs, precedence, err))
			return FALSE;
		--precedence;
	}

	g_assert(n_exprs == 1);

	*result = *exprs;

	return TRUE;
}

/*
 *   expr = int | double | expr * expr | expr / expr |
 *          expr + expr | expr - expr | (expr)
 *
 *   so very not worth fooling with bison, yet so very painful by hand.
 */
/**
 * Evaluates an expression.
 *
 * \param spec  The expression to evaluate.
 * \param env   The environment context to evaluate the expression in.
 * \param[out] val_p  The integer value of the expression; if the expression
 *                    is of type float, this will be rounded. If we return
 *                    FALSE because the expression is invalid, this will be
 *                    zero.
 * \param[out] err    The error, if anything went wrong.
 *
 * \return  True if we evaluated the expression successfully; false otherwise.
 *
 * \bug Shouldn't spec be const?
 * \ingroup parser
 */
static gboolean
pos_eval(PoloDrawSpec *spec, const PoloPositionExprEnv *env, int *val_p,
	 GError **err)
{
	PosExpr expr;

	*val_p = 0;

	if (pos_eval_helper(spec->tokens, spec->n_tokens, env, &expr, err)) {
		switch (expr.type) {
		case POS_EXPR_INT:
			*val_p = expr.d.int_val;
			break;
		case POS_EXPR_DOUBLE:
			*val_p = (int)expr.d.double_val;
			break;
		case POS_EXPR_OPERATOR:
			g_assert_not_reached();
			break;
		}
		return TRUE;
	} else {
		return FALSE;
	}
}

/* We always return both X and Y, but only one will be meaningful in
 * most contexts.
 */

gboolean
polo_marco_parse_position_expression(PoloDrawSpec *spec,
				     const PoloPositionExprEnv *env,
				     int *x_return, int *y_return, GError **err)
{
	/* All positions are in a coordinate system with x, y at the origin.
	 * The expression can have -, +, *, / as operators, floating point
	 * or integer constants, and the variables "width" and "height" and
	 * optionally "object_width" and object_height". Negative numbers
	 * aren't allowed.
	 */
	int val;

	if (spec->constant)
		val = spec->value;
	else {
		if (!pos_eval(spec, env, &spec->value, err)) {
			g_assert(err == NULL || *err != NULL);
			return FALSE;
		}

		val = spec->value;
	}

	if (x_return)
		*x_return = env->rect.x + val;
	if (y_return)
		*y_return = env->rect.y + val;

	return TRUE;
}

gboolean
polo_marco_parse_size_expression(PoloDrawSpec *spec,
				 const PoloPositionExprEnv *env,
				 int *val_return, GError **err)
{
	int val;

	if (spec->constant)
		val = spec->value;
	else {
		if (!pos_eval(spec, env, &spec->value, err)) {
			g_assert(err == NULL || *err != NULL);
			return FALSE;
		}

		val = spec->value;
	}

	if (val_return)
		*val_return =
			MAX(val, 1); /* require that sizes be at least 1x1 */

	return TRUE;
}

/* To do this we tokenize, replace variable tokens
 * that are constants, then reassemble. The purpose
 * here is to optimize expressions so we don't do hash
 * lookups to eval them. Obviously it's a tradeoff that
 * slows down theme load times.
 */
gboolean
polo_marco_theme_replace_constants(PoloTheme *theme, PosToken *tokens,
				   int n_tokens, GError **err)
{
	int i;
	double dval;
	int ival;
	gboolean is_constant = TRUE;

	/* Loop through tokenized string looking for variables to replace */
	for (i = 0; i < n_tokens; i++) {
		PosToken *t = &tokens[i];

		if (t->type == POS_TOKEN_VARIABLE) {
			if (polo_marco_theme_lookup_int_constant(
				    theme, t->d.v.name, &ival)) {
				g_free(t->d.v.name);
				t->type = POS_TOKEN_INT;
				t->d.i.val = ival;
			} else if (polo_marco_theme_lookup_float_constant(
					   theme, t->d.v.name, &dval)) {
				g_free(t->d.v.name);
				t->type = POS_TOKEN_DOUBLE;
				t->d.d.val = dval;
			} else {
				/* If we've found a variable that cannot be
				   replaced then the expression is not a
				   constant expression and we want to replace it
				   with a GQuark */

				t->d.v.name_quark =
					g_quark_from_string(t->d.v.name);
				is_constant = FALSE;
			}
		}
	}

	return is_constant;
}

void
polo_marco_draw_spec_free(PoloDrawSpec *spec)
{
	if (!spec)
		return;
	free_tokens(spec->tokens, spec->n_tokens);
	g_slice_free(PoloDrawSpec, spec);
}

PoloDrawSpec *
polo_marco_draw_spec_new(PoloTheme *theme, const char *expr, GError **error)
{
	PoloDrawSpec *spec;

	spec = g_slice_new0(PoloDrawSpec);

	pos_tokenize(expr, &spec->tokens, &spec->n_tokens, NULL);

	spec->constant = polo_marco_theme_replace_constants(
		theme, spec->tokens, spec->n_tokens, NULL);
	if (spec->constant) {
		if (!pos_eval(spec, NULL, &spec->value, error)) {
			polo_marco_draw_spec_free(spec);
			return NULL;
		}
	}

	return spec;
}

PoloDrawOp *
polo_marco_draw_op_new(PoloDrawType type)
{
	PoloDrawOp *op;
	PoloDrawOp dummy;
	int size;

	size = G_STRUCT_OFFSET(PoloDrawOp, data);

	switch (type) {
	case POLO_DRAW_LINE:
		size += sizeof(dummy.data.line);
		break;

	case POLO_DRAW_RECTANGLE:
		size += sizeof(dummy.data.rectangle);
		break;

	case POLO_DRAW_ARC:
		size += sizeof(dummy.data.arc);
		break;

	case POLO_DRAW_CLIP:
		size += sizeof(dummy.data.clip);
		break;

	case POLO_DRAW_TINT:
		size += sizeof(dummy.data.tint);
		break;

	case POLO_DRAW_GRADIENT:
		size += sizeof(dummy.data.gradient);
		break;

	case POLO_DRAW_IMAGE:
		size += sizeof(dummy.data.image);
		break;

	case POLO_DRAW_GTK_ARROW:
		size += sizeof(dummy.data.gtk_arrow);
		break;

	case POLO_DRAW_GTK_BOX:
		size += sizeof(dummy.data.gtk_box);
		break;

	case POLO_DRAW_GTK_VLINE:
		size += sizeof(dummy.data.gtk_vline);
		break;

	case POLO_DRAW_ICON:
		size += sizeof(dummy.data.icon);
		break;

	case POLO_DRAW_TITLE:
		size += sizeof(dummy.data.title);
		break;
	case POLO_DRAW_OP_LIST:
		size += sizeof(dummy.data.op_list);
		break;
	case POLO_DRAW_TILE:
		size += sizeof(dummy.data.tile);
		break;
	}

	op = g_malloc0(size);

	op->type = type;

	return op;
}

void
polo_marco_draw_op_free(PoloDrawOp *op)
{
	g_return_if_fail(op != NULL);

	switch (op->type) {
	case POLO_DRAW_LINE:
		if (op->data.line.color_spec)
			polo_marco_color_spec_free(op->data.line.color_spec);

		polo_marco_draw_spec_free(op->data.line.x1);
		polo_marco_draw_spec_free(op->data.line.y1);
		polo_marco_draw_spec_free(op->data.line.x2);
		polo_marco_draw_spec_free(op->data.line.y2);
		break;

	case POLO_DRAW_RECTANGLE:
		if (op->data.rectangle.color_spec)
			polo_marco_color_spec_free(
				op->data.rectangle.color_spec);

		polo_marco_draw_spec_free(op->data.rectangle.x);
		polo_marco_draw_spec_free(op->data.rectangle.y);
		polo_marco_draw_spec_free(op->data.rectangle.width);
		polo_marco_draw_spec_free(op->data.rectangle.height);
		break;

	case POLO_DRAW_ARC:
		if (op->data.arc.color_spec)
			polo_marco_color_spec_free(op->data.arc.color_spec);

		polo_marco_draw_spec_free(op->data.arc.x);
		polo_marco_draw_spec_free(op->data.arc.y);
		polo_marco_draw_spec_free(op->data.arc.width);
		polo_marco_draw_spec_free(op->data.arc.height);
		break;

	case POLO_DRAW_CLIP:
		polo_marco_draw_spec_free(op->data.clip.x);
		polo_marco_draw_spec_free(op->data.clip.y);
		polo_marco_draw_spec_free(op->data.clip.width);
		polo_marco_draw_spec_free(op->data.clip.height);
		break;

	case POLO_DRAW_TINT:
		if (op->data.tint.color_spec)
			polo_marco_color_spec_free(op->data.tint.color_spec);

		if (op->data.tint.alpha_spec)
			polo_marco_alpha_gradient_spec_free(
				op->data.tint.alpha_spec);

		polo_marco_draw_spec_free(op->data.tint.x);
		polo_marco_draw_spec_free(op->data.tint.y);
		polo_marco_draw_spec_free(op->data.tint.width);
		polo_marco_draw_spec_free(op->data.tint.height);
		break;

	case POLO_DRAW_GRADIENT:
		if (op->data.gradient.gradient_spec)
			polo_marco_gradient_spec_free(
				op->data.gradient.gradient_spec);

		if (op->data.gradient.alpha_spec)
			polo_marco_alpha_gradient_spec_free(
				op->data.gradient.alpha_spec);

		polo_marco_draw_spec_free(op->data.gradient.x);
		polo_marco_draw_spec_free(op->data.gradient.y);
		polo_marco_draw_spec_free(op->data.gradient.width);
		polo_marco_draw_spec_free(op->data.gradient.height);
		break;

	case POLO_DRAW_IMAGE:
		if (op->data.image.alpha_spec)
			polo_marco_alpha_gradient_spec_free(
				op->data.image.alpha_spec);

		if (op->data.image.pixbuf)
			g_object_unref(G_OBJECT(op->data.image.pixbuf));

		if (op->data.image.colorize_spec)
			polo_marco_color_spec_free(
				op->data.image.colorize_spec);

		if (op->data.image.colorize_cache_pixbuf)
			g_object_unref(
				G_OBJECT(op->data.image.colorize_cache_pixbuf));

		polo_marco_draw_spec_free(op->data.image.x);
		polo_marco_draw_spec_free(op->data.image.y);
		polo_marco_draw_spec_free(op->data.image.width);
		polo_marco_draw_spec_free(op->data.image.height);
		break;

	case POLO_DRAW_GTK_ARROW:
		polo_marco_draw_spec_free(op->data.gtk_arrow.x);
		polo_marco_draw_spec_free(op->data.gtk_arrow.y);
		polo_marco_draw_spec_free(op->data.gtk_arrow.width);
		polo_marco_draw_spec_free(op->data.gtk_arrow.height);
		break;

	case POLO_DRAW_GTK_BOX:
		polo_marco_draw_spec_free(op->data.gtk_box.x);
		polo_marco_draw_spec_free(op->data.gtk_box.y);
		polo_marco_draw_spec_free(op->data.gtk_box.width);
		polo_marco_draw_spec_free(op->data.gtk_box.height);
		break;

	case POLO_DRAW_GTK_VLINE:
		polo_marco_draw_spec_free(op->data.gtk_vline.x);
		polo_marco_draw_spec_free(op->data.gtk_vline.y1);
		polo_marco_draw_spec_free(op->data.gtk_vline.y2);
		break;

	case POLO_DRAW_ICON:
		if (op->data.icon.alpha_spec)
			polo_marco_alpha_gradient_spec_free(
				op->data.icon.alpha_spec);

		polo_marco_draw_spec_free(op->data.icon.x);
		polo_marco_draw_spec_free(op->data.icon.y);
		polo_marco_draw_spec_free(op->data.icon.width);
		polo_marco_draw_spec_free(op->data.icon.height);
		break;

	case POLO_DRAW_TITLE:
		if (op->data.title.color_spec)
			polo_marco_color_spec_free(op->data.title.color_spec);

		polo_marco_draw_spec_free(op->data.title.x);
		polo_marco_draw_spec_free(op->data.title.y);
		if (op->data.title.ellipsize_width)
			polo_marco_draw_spec_free(
				op->data.title.ellipsize_width);
		break;

	case POLO_DRAW_OP_LIST:
		if (op->data.op_list.op_list)
			polo_marco_draw_op_list_unref(op->data.op_list.op_list);

		polo_marco_draw_spec_free(op->data.op_list.x);
		polo_marco_draw_spec_free(op->data.op_list.y);
		polo_marco_draw_spec_free(op->data.op_list.width);
		polo_marco_draw_spec_free(op->data.op_list.height);
		break;

	case POLO_DRAW_TILE:
		if (op->data.tile.op_list)
			polo_marco_draw_op_list_unref(op->data.tile.op_list);

		polo_marco_draw_spec_free(op->data.tile.x);
		polo_marco_draw_spec_free(op->data.tile.y);
		polo_marco_draw_spec_free(op->data.tile.width);
		polo_marco_draw_spec_free(op->data.tile.height);
		polo_marco_draw_spec_free(op->data.tile.tile_xoffset);
		polo_marco_draw_spec_free(op->data.tile.tile_yoffset);
		polo_marco_draw_spec_free(op->data.tile.tile_width);
		polo_marco_draw_spec_free(op->data.tile.tile_height);
		break;
	}

	g_free(op);
}

PoloDrawOpList *
polo_marco_draw_op_list_new(int n_preallocs)
{
	PoloDrawOpList *op_list;

	g_return_val_if_fail(n_preallocs >= 0, NULL);

	op_list = g_new(PoloDrawOpList, 1);

	op_list->refcount = 1;
	op_list->n_allocated = n_preallocs;
	op_list->ops = g_new(PoloDrawOp *, op_list->n_allocated);
	op_list->n_ops = 0;

	return op_list;
}

void
polo_marco_draw_op_list_ref(PoloDrawOpList *op_list)
{
	g_return_if_fail(op_list != NULL);

	op_list->refcount += 1;
}

void
polo_marco_draw_op_list_unref(PoloDrawOpList *op_list)
{
	g_return_if_fail(op_list != NULL);
	g_return_if_fail(op_list->refcount > 0);

	op_list->refcount -= 1;

	if (op_list->refcount == 0) {
		int i;

		for (i = 0; i < op_list->n_ops; i++)
			polo_marco_draw_op_free(op_list->ops[i]);

		g_free(op_list->ops);

		DEBUG_FILL_STRUCT(op_list);
		g_free(op_list);
	}
}

void
polo_marco_draw_op_list_append(PoloDrawOpList *op_list, PoloDrawOp *op)
{
	if (op_list->n_ops == op_list->n_allocated) {
		op_list->n_allocated *= 2;
		op_list->ops = g_renew(PoloDrawOp *, op_list->ops,
				       op_list->n_allocated);
	}

	op_list->ops[op_list->n_ops] = op;
	op_list->n_ops += 1;
}

gboolean
polo_marco_draw_op_list_validate(PoloDrawOpList *op_list, GError **error)
{
	g_return_val_if_fail(op_list != NULL, FALSE);

	/* empty lists are OK, nothing else to check really */

	return TRUE;
}

/* This is not done in validate, since we wouldn't know the name
 * of the list to report the error. It might be nice to
 * store names inside the list sometime.
 */
gboolean
polo_marco_draw_op_list_contains(PoloDrawOpList *op_list, PoloDrawOpList *child)
{
	int i;

	/* mmm, huge tree recursion */

	for (i = 0; i < op_list->n_ops; i++) {
		if (op_list->ops[i]->type == POLO_DRAW_OP_LIST) {
			if (op_list->ops[i]->data.op_list.op_list == child)
				return TRUE;

			if (polo_marco_draw_op_list_contains(
				    op_list->ops[i]->data.op_list.op_list,
				    child))
				return TRUE;
		} else if (op_list->ops[i]->type == POLO_DRAW_TILE) {
			if (op_list->ops[i]->data.tile.op_list == child)
				return TRUE;

			if (polo_marco_draw_op_list_contains(
				    op_list->ops[i]->data.tile.op_list, child))
				return TRUE;
		}
	}

	return FALSE;
}

/**
 * Constructor for a PoloFrameStyle.
 *
 * \param parent  The parent style. Data not filled in here will be
 *                looked for in the parent style, and in its parent
 *                style, and so on.
 *
 * \return The newly-constructed style.
 */
PoloFrameStyle *
polo_marco_frame_style_new(PoloFrameStyle *parent)
{
	PoloFrameStyle *style;

	style = g_new0(PoloFrameStyle, 1);

	style->refcount = 1;

	/* Default alpha is fully opaque */
	style->window_background_alpha = 255;

	style->parent = parent;
	if (parent)
		polo_marco_frame_style_ref(parent);

	return style;
}

/**
 * Increases the reference count of a frame style.
 * If the style is NULL, this is a no-op.
 *
 * \param style  The style.
 */
void
polo_marco_frame_style_ref(PoloFrameStyle *style)
{
	g_return_if_fail(style != NULL);

	style->refcount += 1;
}

static void
free_button_ops(
	PoloDrawOpList *op_lists[POLO_BUTTON_TYPE_LAST][POLO_BUTTON_STATE_LAST])
{
	int i, j;

	for (i = 0; i < POLO_BUTTON_TYPE_LAST; i++)
		for (j = 0; j < POLO_BUTTON_STATE_LAST; j++)
			if (op_lists[i][j])
				polo_marco_draw_op_list_unref(op_lists[i][j]);
}

void
polo_marco_frame_style_unref(PoloFrameStyle *style)
{
	g_return_if_fail(style != NULL);
	g_return_if_fail(style->refcount > 0);

	style->refcount -= 1;

	if (style->refcount == 0) {
		int i;

		free_button_ops(style->buttons);

		for (i = 0; i < POLO_FRAME_PIECE_LAST; i++)
			if (style->pieces[i])
				polo_marco_draw_op_list_unref(style->pieces[i]);

		if (style->layout)
			polo_marco_frame_layout_unref(style->layout);

		if (style->window_background_color)
			polo_marco_color_spec_free(
				style->window_background_color);

		/* we hold a reference to any parent style */
		if (style->parent)
			polo_marco_frame_style_unref(style->parent);

		DEBUG_FILL_STRUCT(style);
		g_free(style);
	}
}

PoloDrawOpList *
get_button(PoloFrameStyle *style, PoloButtonType type, PoloButtonState state)
{
	PoloDrawOpList *op_list;
	PoloFrameStyle *parent;

	if (state >= POLO_BUTTON_STATE_LAST)
		return NULL;

	parent = style;
	op_list = NULL;
	while (parent && op_list == NULL) {
		op_list = parent->buttons[type][state];
		parent = parent->parent;
	}

	/* We fall back to the side buttons if we don't have
	 * single button backgrounds, and to middle button
	 * backgrounds if we don't have the ones on the sides
	 */

	if (op_list == NULL && type == POLO_BUTTON_TYPE_LEFT_SINGLE_BACKGROUND)
		return get_button(style, POLO_BUTTON_TYPE_LEFT_LEFT_BACKGROUND,
				  state);

	if (op_list == NULL && type == POLO_BUTTON_TYPE_RIGHT_SINGLE_BACKGROUND)
		return get_button(
			style, POLO_BUTTON_TYPE_RIGHT_RIGHT_BACKGROUND, state);

	if (op_list == NULL &&
	    (type == POLO_BUTTON_TYPE_LEFT_LEFT_BACKGROUND ||
	     type == POLO_BUTTON_TYPE_LEFT_RIGHT_BACKGROUND))
		return get_button(
			style, POLO_BUTTON_TYPE_LEFT_MIDDLE_BACKGROUND, state);

	if (op_list == NULL &&
	    (type == POLO_BUTTON_TYPE_RIGHT_LEFT_BACKGROUND ||
	     type == POLO_BUTTON_TYPE_RIGHT_RIGHT_BACKGROUND))
		return get_button(
			style, POLO_BUTTON_TYPE_RIGHT_MIDDLE_BACKGROUND, state);

	/* We fall back to normal if no prelight */
	if (op_list == NULL && state == POLO_BUTTON_STATE_PRELIGHT)
		return get_button(style, type, POLO_BUTTON_STATE_NORMAL);

	return op_list;
}

gboolean
polo_marco_frame_style_validate(PoloFrameStyle *style,
				guint current_theme_version, GError **error)
{
	int i, j;

	g_return_val_if_fail(style != NULL, FALSE);
	g_return_val_if_fail(style->layout != NULL, FALSE);

	for (i = 0; i < POLO_BUTTON_TYPE_LAST; i++) {
		/* for now the "positional" buttons are optional */
		if (i >= POLO_BUTTON_TYPE_CLOSE) {
			for (j = 0; j < POLO_BUTTON_STATE_LAST; j++) {
				if (get_button(style, i, j) == NULL &&
				    polo_marco_theme_earliest_version_with_button(
					    i) <= current_theme_version) {
					g_set_error(
						error, POLO_THEME_ERROR,
						POLO_THEME_ERROR_FAILED,
						_("<button function=\"%s\" "
						  "state=\"%s\" "
						  "draw_ops=\"whatever\"/> "
						  "must be specified for this "
						  "frame style"),
						polo_marco_button_type_to_string(
							i),
						polo_marco_button_state_to_string(
							j));
					return FALSE;
				}
			}
		}
	}

	return TRUE;
}

PoloFrameStyleSet *
polo_marco_frame_style_set_new(PoloFrameStyleSet *parent)
{
	PoloFrameStyleSet *style_set;

	style_set = g_new0(PoloFrameStyleSet, 1);

	style_set->parent = parent;
	if (parent)
		polo_marco_frame_style_set_ref(parent);

	style_set->refcount = 1;

	return style_set;
}

static void
free_focus_styles(PoloFrameStyle *focus_styles[POLO_FRAME_FOCUS_LAST])
{
	int i;

	for (i = 0; i < POLO_FRAME_FOCUS_LAST; i++)
		if (focus_styles[i])
			polo_marco_frame_style_unref(focus_styles[i]);
}

void
polo_marco_frame_style_set_ref(PoloFrameStyleSet *style_set)
{
	g_return_if_fail(style_set != NULL);

	style_set->refcount += 1;
}

void
polo_marco_frame_style_set_unref(PoloFrameStyleSet *style_set)
{
	g_return_if_fail(style_set != NULL);
	g_return_if_fail(style_set->refcount > 0);

	style_set->refcount -= 1;

	if (style_set->refcount == 0) {
		int i;

		for (i = 0; i < POLO_FRAME_RESIZE_LAST; i++) {
			free_focus_styles(style_set->normal_styles[i]);
			free_focus_styles(style_set->shaded_styles[i]);
		}

		free_focus_styles(style_set->maximized_styles);
		free_focus_styles(style_set->tiled_left_styles);
		free_focus_styles(style_set->tiled_right_styles);
		free_focus_styles(style_set->maximized_and_shaded_styles);
		free_focus_styles(style_set->tiled_left_and_shaded_styles);
		free_focus_styles(style_set->tiled_right_and_shaded_styles);

		if (style_set->parent)
			polo_marco_frame_style_set_unref(style_set->parent);

		DEBUG_FILL_STRUCT(style_set);
		g_free(style_set);
	}
}

static PoloFrameStyle *
get_style(PoloFrameStyleSet *style_set, PoloFrameState state,
	  PoloFrameResize resize, PoloFrameFocus focus)
{
	PoloFrameStyle *style;

	style = NULL;

	switch (state) {
	case POLO_FRAME_STATE_NORMAL:
	case POLO_FRAME_STATE_SHADED: {
		if (state == POLO_FRAME_STATE_SHADED)
			style = style_set->shaded_styles[resize][focus];
		else
			style = style_set->normal_styles[resize][focus];

		/* Try parent if we failed here */
		if (style == NULL && style_set->parent)
			style = get_style(style_set->parent, state, resize,
					  focus);

		/* Allow people to omit the vert/horz/none resize modes */
		if (style == NULL && resize != POLO_FRAME_RESIZE_BOTH)
			style = get_style(style_set, state,
					  POLO_FRAME_RESIZE_BOTH, focus);
	} break;
	default: {
		PoloFrameStyle **styles;

		styles = NULL;

		switch (state) {
		case POLO_FRAME_STATE_MAXIMIZED:
			styles = style_set->maximized_styles;
			break;
		case POLO_FRAME_STATE_TILED_LEFT:
			styles = style_set->tiled_left_styles;
			break;
		case POLO_FRAME_STATE_TILED_RIGHT:
			styles = style_set->tiled_right_styles;
			break;
		case POLO_FRAME_STATE_MAXIMIZED_AND_SHADED:
			styles = style_set->maximized_and_shaded_styles;
			break;
		case POLO_FRAME_STATE_TILED_LEFT_AND_SHADED:
			styles = style_set->tiled_left_and_shaded_styles;
			break;
		case POLO_FRAME_STATE_TILED_RIGHT_AND_SHADED:
			styles = style_set->tiled_right_and_shaded_styles;
			break;
		case POLO_FRAME_STATE_NORMAL:
		case POLO_FRAME_STATE_SHADED:
		case POLO_FRAME_STATE_LAST:
			g_assert_not_reached();
			break;
		}

		style = styles[focus];

		/* Tiled states are optional, try falling back to non-tiled
		 * states */
		if (style == NULL) {
			if (state == POLO_FRAME_STATE_TILED_LEFT ||
			    state == POLO_FRAME_STATE_TILED_RIGHT)
				style = get_style(style_set,
						  POLO_FRAME_STATE_NORMAL,
						  resize, focus);
			else if (
				state ==
					POLO_FRAME_STATE_TILED_LEFT_AND_SHADED ||
				state ==
					POLO_FRAME_STATE_TILED_RIGHT_AND_SHADED)
				style = get_style(style_set,
						  POLO_FRAME_STATE_SHADED,
						  resize, focus);
		}

		/* Try parent if we failed here */
		if (style == NULL && style_set->parent)
			style = get_style(style_set->parent, state, resize,
					  focus);
	}
	}

	return style;
}

static gboolean
check_state(PoloFrameStyleSet *style_set, PoloFrameState state, GError **error)
{
	int i;

	for (i = 0; i < POLO_FRAME_FOCUS_LAST; i++) {
		if (get_style(style_set, state, POLO_FRAME_RESIZE_NONE, i) ==
		    NULL) {
			/* Translators: This error occurs when a <frame> tag is
			 * missing in theme XML.  The "<frame ...>" is intended
			 * as a noun phrase, and the "missing" qualifies it. You
			 * should translate "whatever".
			 */
			g_set_error(
				error, POLO_THEME_ERROR,
				POLO_THEME_ERROR_FAILED,
				_("Missing <frame state=\"%s\" resize=\"%s\" "
				  "focus=\"%s\" style=\"whatever\"/>"),
				polo_marco_frame_state_to_string(state),
				polo_marco_frame_resize_to_string(
					POLO_FRAME_RESIZE_NONE),
				polo_marco_frame_focus_to_string(i));
			return FALSE;
		}
	}

	return TRUE;
}

gboolean
polo_marco_frame_style_set_validate(PoloFrameStyleSet *style_set,
				    GError **error)
{
	int i, j;

	g_return_val_if_fail(style_set != NULL, FALSE);

	for (i = 0; i < POLO_FRAME_RESIZE_LAST; i++)
		for (j = 0; j < POLO_FRAME_FOCUS_LAST; j++)
			if (get_style(style_set, POLO_FRAME_STATE_NORMAL, i,
				      j) == NULL) {
				g_set_error(
					error, POLO_THEME_ERROR,
					POLO_THEME_ERROR_FAILED,
					_("Missing <frame state=\"%s\" "
					  "resize=\"%s\" focus=\"%s\" "
					  "style=\"whatever\"/>"),
					polo_marco_frame_state_to_string(
						POLO_FRAME_STATE_NORMAL),
					polo_marco_frame_resize_to_string(i),
					polo_marco_frame_focus_to_string(j));
				return FALSE;
			}

	if (!check_state(style_set, POLO_FRAME_STATE_SHADED, error))
		return FALSE;

	if (!check_state(style_set, POLO_FRAME_STATE_MAXIMIZED, error))
		return FALSE;

	if (!check_state(style_set, POLO_FRAME_STATE_MAXIMIZED_AND_SHADED,
			 error))
		return FALSE;

	return TRUE;
}

PoloTheme *
polo_marco_theme_new(void)
{
	PoloTheme *theme;

	theme = g_new0(PoloTheme, 1);
	theme->custom_color_names =
		g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	theme->images_by_filename = g_hash_table_new_full(
		g_str_hash, g_str_equal, g_free, g_object_unref);

	theme->layouts_by_name = g_hash_table_new_full(
		g_str_hash, g_str_equal, g_free,
		(GDestroyNotify)polo_marco_frame_layout_unref);

	theme->draw_op_lists_by_name = g_hash_table_new_full(
		g_str_hash, g_str_equal, g_free,
		(GDestroyNotify)polo_marco_draw_op_list_unref);

	theme->styles_by_name = g_hash_table_new_full(
		g_str_hash, g_str_equal, g_free,
		(GDestroyNotify)polo_marco_frame_style_unref);

	theme->style_sets_by_name = g_hash_table_new_full(
		g_str_hash, g_str_equal, g_free,
		(GDestroyNotify)polo_marco_frame_style_set_unref);

	/* Create our variable quarks so we can look up variables without
	   having to strcmp for the names */
	theme->quark_width = g_quark_from_static_string("width");
	theme->quark_height = g_quark_from_static_string("height");
	theme->quark_object_width = g_quark_from_static_string("object_width");
	theme->quark_object_height =
		g_quark_from_static_string("object_height");
	theme->quark_left_width = g_quark_from_static_string("left_width");
	theme->quark_right_width = g_quark_from_static_string("right_width");
	theme->quark_top_height = g_quark_from_static_string("top_height");
	theme->quark_bottom_height =
		g_quark_from_static_string("bottom_height");
	theme->quark_mini_icon_width =
		g_quark_from_static_string("mini_icon_width");
	theme->quark_mini_icon_height =
		g_quark_from_static_string("mini_icon_height");
	theme->quark_icon_width = g_quark_from_static_string("icon_width");
	theme->quark_icon_height = g_quark_from_static_string("icon_height");
	theme->quark_title_width = g_quark_from_static_string("title_width");
	theme->quark_title_height = g_quark_from_static_string("title_height");
	theme->quark_frame_x_center =
		g_quark_from_static_string("frame_x_center");
	theme->quark_frame_y_center =
		g_quark_from_static_string("frame_y_center");
	return theme;
}

void
polo_marco_theme_free(PoloTheme *theme)
{
	int i;

	g_return_if_fail(theme != NULL);

	g_free(theme->name);
	g_free(theme->dirname);
	g_free(theme->filename);
	g_free(theme->readable_name);
	g_free(theme->date);
	g_free(theme->description);
	g_free(theme->author);
	g_free(theme->copyright);

	/* be more careful when destroying the theme hash tables,
	   since they are only constructed as needed, and may be NULL. */
	if (theme->integer_constants)
		g_hash_table_destroy(theme->integer_constants);
	if (theme->float_constants)
		g_hash_table_destroy(theme->float_constants);
	if (theme->color_constants)
		g_hash_table_destroy(theme->color_constants);
	if (theme->images_by_filename)
		g_hash_table_destroy(theme->images_by_filename);
	if (theme->custom_color_names)
		g_hash_table_destroy(theme->custom_color_names);
	if (theme->layouts_by_name)
		g_hash_table_destroy(theme->layouts_by_name);
	if (theme->draw_op_lists_by_name)
		g_hash_table_destroy(theme->draw_op_lists_by_name);
	if (theme->styles_by_name)
		g_hash_table_destroy(theme->styles_by_name);
	if (theme->style_sets_by_name)
		g_hash_table_destroy(theme->style_sets_by_name);

	for (i = 0; i < POLO_FRAME_TYPE_LAST; i++)
		if (theme->style_sets_by_type[i])
			polo_marco_frame_style_set_unref(
				theme->style_sets_by_type[i]);

	DEBUG_FILL_STRUCT(theme);
	g_free(theme);
}

gboolean
polo_marco_theme_validate(PoloTheme *theme, GError **error)
{
	int i;

	g_return_val_if_fail(theme != NULL, FALSE);

	/* FIXME what else should be checked? */

	g_assert(theme->name);

	if (theme->readable_name == NULL) {
		/* Translators: This error means that a necessary XML tag (whose
		 * name is given in angle brackets) was not found in a given
		 * theme (whose name is given second, in quotation marks).
		 */
		g_set_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			    _("No <%s> set for theme \"%s\""), "name",
			    theme->name);
		return FALSE;
	}

	if (theme->author == NULL) {
		g_set_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			    _("No <%s> set for theme \"%s\""), "author",
			    theme->name);
		return FALSE;
	}

	if (theme->date == NULL) {
		g_set_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			    _("No <%s> set for theme \"%s\""), "date",
			    theme->name);
		return FALSE;
	}

	if (theme->description == NULL) {
		g_set_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			    _("No <%s> set for theme \"%s\""), "description",
			    theme->name);
		return FALSE;
	}

	if (theme->copyright == NULL) {
		g_set_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			    _("No <%s> set for theme \"%s\""), "copyright",
			    theme->name);
		return FALSE;
	}

	for (i = 0; i < (int)POLO_FRAME_TYPE_LAST; i++)
		if (i != (int)POLO_FRAME_TYPE_ATTACHED &&
		    theme->style_sets_by_type[i] == NULL) {
			g_set_error(
				error, POLO_THEME_ERROR,
				POLO_THEME_ERROR_FAILED,
				_("No frame style set for window type \"%s\" "
				  "in theme \"%s\", add a <window type=\"%s\" "
				  "style_set=\"whatever\"/> element"),
				polo_marco_frame_type_to_string(i), theme->name,
				polo_marco_frame_type_to_string(i));

			return FALSE;
		}

	return TRUE;
}

GdkPixbuf *
polo_marco_theme_load_image(PoloTheme *theme, const char *filename,
			    guint size_of_theme_icons, GError **error)
{
	GdkPixbuf *pixbuf;
	int scale;

	pixbuf = g_hash_table_lookup(theme->images_by_filename, filename);

	scale = 1;

	if (pixbuf == NULL) {
		if (g_str_has_prefix(filename, "theme:") &&
		    POLO_THEME_ALLOWS(theme,
				      POLO_THEME_IMAGES_FROM_ICON_THEMES)) {
			g_set_error(error, POLO_THEME_ERROR,
				    POLO_THEME_ERROR_FAILED,
				    "Images from the icon theme are not "
				    "supported: %s",
				    filename);
			return NULL;
		} else {
			char *full_path;
			full_path = g_build_filename(theme->dirname, filename,
						     NULL);

			gint width, height;

			if (gdk_pixbuf_get_file_info(full_path, &width,
						     &height) == NULL) {
				g_free(full_path);
				return NULL;
			}

			width *= scale;
			height *= scale;

			pixbuf = gdk_pixbuf_new_from_file_at_size(
				full_path, width, height, error);

			if (pixbuf == NULL) {
				g_free(full_path);
				return NULL;
			}

			g_free(full_path);
		}
		g_hash_table_replace(theme->images_by_filename,
				     g_strdup(filename), pixbuf);
	}

	g_assert(pixbuf);

	g_object_ref(G_OBJECT(pixbuf));

	return pixbuf;
}

PoloFrameStyle *
theme_get_style(PoloTheme *theme, PoloFrameType type, PoloFrameFlags flags)
{
	PoloFrameState state;
	PoloFrameResize resize;
	PoloFrameFocus focus;
	PoloFrameStyle *style;
	PoloFrameStyleSet *style_set;

	style_set = theme->style_sets_by_type[type];

	if (style_set == NULL && type == POLO_FRAME_TYPE_ATTACHED)
		style_set = theme->style_sets_by_type[POLO_FRAME_TYPE_BORDER];

	/* Right now the parser forces a style set for all other types,
	 * but this fallback code is here in case I take that out.
	 */
	if (style_set == NULL)
		style_set = theme->style_sets_by_type[POLO_FRAME_TYPE_NORMAL];
	if (style_set == NULL)
		return NULL;

	switch (flags &
		(POLO_FRAME_MAXIMIZED | POLO_FRAME_SHADED |
		 POLO_FRAME_TILED_LEFT | POLO_FRAME_TILED_RIGHT)) {
	case 0:
		state = POLO_FRAME_STATE_NORMAL;
		break;
	case POLO_FRAME_MAXIMIZED:
		state = POLO_FRAME_STATE_MAXIMIZED;
		break;
	case POLO_FRAME_TILED_LEFT:
	case (POLO_FRAME_MAXIMIZED | POLO_FRAME_TILED_LEFT):
		state = POLO_FRAME_STATE_TILED_LEFT;
		break;
	case POLO_FRAME_TILED_RIGHT:
	case (POLO_FRAME_MAXIMIZED | POLO_FRAME_TILED_RIGHT):
		state = POLO_FRAME_STATE_TILED_RIGHT;
		break;
	case POLO_FRAME_SHADED:
		state = POLO_FRAME_STATE_SHADED;
		break;
	case (POLO_FRAME_MAXIMIZED | POLO_FRAME_SHADED):
		state = POLO_FRAME_STATE_MAXIMIZED_AND_SHADED;
		break;
	case (POLO_FRAME_TILED_LEFT | POLO_FRAME_SHADED):
		state = POLO_FRAME_STATE_TILED_LEFT_AND_SHADED;
		break;
	case (POLO_FRAME_TILED_RIGHT | POLO_FRAME_SHADED):
		state = POLO_FRAME_STATE_TILED_RIGHT_AND_SHADED;
		break;
	default:
		g_assert_not_reached();
		state = POLO_FRAME_STATE_LAST; /* compiler */
		break;
	}

	switch (flags &
		(POLO_FRAME_ALLOWS_VERTICAL_RESIZE |
		 POLO_FRAME_ALLOWS_HORIZONTAL_RESIZE)) {
	case 0:
		resize = POLO_FRAME_RESIZE_NONE;
		break;
	case POLO_FRAME_ALLOWS_VERTICAL_RESIZE:
		resize = POLO_FRAME_RESIZE_VERTICAL;
		break;
	case POLO_FRAME_ALLOWS_HORIZONTAL_RESIZE:
		resize = POLO_FRAME_RESIZE_HORIZONTAL;
		break;
	case (POLO_FRAME_ALLOWS_VERTICAL_RESIZE |
	      POLO_FRAME_ALLOWS_HORIZONTAL_RESIZE):
		resize = POLO_FRAME_RESIZE_BOTH;
		break;
	default:
		g_assert_not_reached();
		resize = POLO_FRAME_RESIZE_LAST; /* compiler */
		break;
	}

	/* re invert the styles used for focus/unfocussed while flashing a frame
	 */
	if (((flags & POLO_FRAME_HAS_FOCUS) &&
	     !(flags & POLO_FRAME_IS_FLASHING)) ||
	    (!(flags & POLO_FRAME_HAS_FOCUS) &&
	     (flags & POLO_FRAME_IS_FLASHING)))
		focus = POLO_FRAME_FOCUS_YES;
	else
		focus = POLO_FRAME_FOCUS_NO;

	style = get_style(style_set, state, resize, focus);

	return style;
}

PoloFrameStyle *
polo_marco_theme_get_frame_style(PoloTheme *theme, PoloFrameType type,
				 PoloFrameFlags flags)
{
	PoloFrameStyle *style;

	g_return_val_if_fail(type < POLO_FRAME_TYPE_LAST, NULL);

	style = theme_get_style(theme, type, flags);

	return style;
}

void
polo_marco_theme_get_frame_borders(PoloTheme *theme, PoloFrameType type,
				   int text_height, PoloFrameFlags flags,
				   PoloThemeBorders *borders)
{
	PoloFrameStyle *style;

	g_return_if_fail(type < POLO_FRAME_TYPE_LAST);

	style = theme_get_style(theme, type, flags);

	polo_marco_theme_borders_clear(borders);

	/* Parser is not supposed to allow this currently */
	if (style == NULL)
		return;

	polo_marco_frame_layout_get_borders(style->layout, text_height, flags,
					    type, borders);
}

void
polo_marco_theme_calc_geometry(PoloTheme *theme, PoloFrameType type,
			       int text_height, PoloFrameFlags flags,
			       int client_width, int client_height,
			       const PoloButtonLayout *button_layout,
			       PoloFrameGeometry *fgeom)
{
	PoloFrameStyle *style;

	g_return_if_fail(type < POLO_FRAME_TYPE_LAST);

	style = theme_get_style(theme, type, flags);

	/* Parser is not supposed to allow this currently */
	if (style == NULL)
		return;

	polo_marco_frame_layout_calc_geometry(style->layout, text_height, flags,
					      client_width, client_height,
					      button_layout, fgeom, theme);
}

PoloFrameLayout *
polo_marco_theme_lookup_layout(PoloTheme *theme, const char *name)
{
	return g_hash_table_lookup(theme->layouts_by_name, name);
}

void
polo_marco_theme_insert_layout(PoloTheme *theme, const char *name,
			       PoloFrameLayout *layout)
{
	polo_marco_frame_layout_ref(layout);
	g_hash_table_replace(theme->layouts_by_name, g_strdup(name), layout);
}

PoloDrawOpList *
polo_marco_theme_lookup_draw_op_list(PoloTheme *theme, const char *name)
{
	return g_hash_table_lookup(theme->draw_op_lists_by_name, name);
}

void
polo_marco_theme_insert_draw_op_list(PoloTheme *theme, const char *name,
				     PoloDrawOpList *op_list)
{
	polo_marco_draw_op_list_ref(op_list);
	g_hash_table_replace(theme->draw_op_lists_by_name, g_strdup(name),
			     op_list);
}

PoloFrameStyle *
polo_marco_theme_lookup_style(PoloTheme *theme, const char *name)
{
	return g_hash_table_lookup(theme->styles_by_name, name);
}

void
polo_marco_theme_insert_style(PoloTheme *theme, const char *name,
			      PoloFrameStyle *style)
{
	polo_marco_frame_style_ref(style);
	g_hash_table_replace(theme->styles_by_name, g_strdup(name), style);
}

PoloFrameStyleSet *
polo_marco_theme_lookup_style_set(PoloTheme *theme, const char *name)
{
	return g_hash_table_lookup(theme->style_sets_by_name, name);
}

void
polo_marco_theme_insert_style_set(PoloTheme *theme, const char *name,
				  PoloFrameStyleSet *style_set)
{
	polo_marco_frame_style_set_ref(style_set);
	g_hash_table_replace(theme->style_sets_by_name, g_strdup(name),
			     style_set);
}

static gboolean
first_uppercase(const char *str)
{
	return g_ascii_isupper(*str);
}

gboolean
polo_marco_theme_define_int_constant(PoloTheme *theme, const char *name,
				     int value, GError **error)
{
	if (theme->integer_constants == NULL)
		theme->integer_constants = g_hash_table_new_full(
			g_str_hash, g_str_equal, g_free, NULL);

	if (!first_uppercase(name)) {
		g_set_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			    _("User-defined constants must begin with a "
			      "capital letter; \"%s\" does not"),
			    name);
		return FALSE;
	}

	if (g_hash_table_lookup_extended(theme->integer_constants, name, NULL,
					 NULL)) {
		g_set_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			    _("Constant \"%s\" has already been defined"),
			    name);

		return FALSE;
	}

	g_hash_table_insert(theme->integer_constants, g_strdup(name),
			    GINT_TO_POINTER(value));

	return TRUE;
}

gboolean
polo_marco_theme_lookup_int_constant(PoloTheme *theme, const char *name,
				     int *value)
{
	gpointer old_value;

	*value = 0;

	if (theme->integer_constants == NULL)
		return FALSE;

	if (g_hash_table_lookup_extended(theme->integer_constants, name, NULL,
					 &old_value)) {
		*value = GPOINTER_TO_INT(old_value);
		return TRUE;
	} else {
		return FALSE;
	}
}

gboolean
polo_marco_theme_define_float_constant(PoloTheme *theme, const char *name,
				       double value, GError **error)
{
	double *d;

	if (theme->float_constants == NULL)
		theme->float_constants = g_hash_table_new_full(
			g_str_hash, g_str_equal, g_free, g_free);

	if (!first_uppercase(name)) {
		g_set_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			    _("User-defined constants must begin with a "
			      "capital letter; \"%s\" does not"),
			    name);
		return FALSE;
	}

	if (g_hash_table_lookup_extended(theme->float_constants, name, NULL,
					 NULL)) {
		g_set_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			    _("Constant \"%s\" has already been defined"),
			    name);

		return FALSE;
	}

	d = g_new(double, 1);
	*d = value;

	g_hash_table_insert(theme->float_constants, g_strdup(name), d);

	return TRUE;
}

gboolean
polo_marco_theme_lookup_float_constant(PoloTheme *theme, const char *name,
				       double *value)
{
	double *d;

	*value = 0.0;

	if (theme->float_constants == NULL)
		return FALSE;

	d = g_hash_table_lookup(theme->float_constants, name);

	if (d) {
		*value = *d;
		return TRUE;
	} else {
		return FALSE;
	}
}

gboolean
polo_marco_theme_define_color_constant(PoloTheme *theme, const char *name,
				       const char *value, GError **error)
{
	if (theme->color_constants == NULL)
		theme->color_constants = g_hash_table_new_full(
			g_str_hash, g_str_equal, g_free, g_free);

	if (!first_uppercase(name)) {
		g_set_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			    _("User-defined constants must begin with a "
			      "capital letter; \"%s\" does not"),
			    name);
		return FALSE;
	}

	if (g_hash_table_lookup_extended(theme->color_constants, name, NULL,
					 NULL)) {
		g_set_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED,
			    _("Constant \"%s\" has already been defined"),
			    name);

		return FALSE;
	}

	g_hash_table_insert(theme->color_constants, g_strdup(name),
			    g_strdup(value));

	return TRUE;
}

/**
 * Looks up a colour constant.
 *
 * \param theme  the theme containing the constant
 * \param name  the name of the constant
 * \param value  [out] the string representation of the colour, or NULL if it
 *               doesn't exist
 * \return  TRUE if it exists, FALSE otherwise
 */
gboolean
polo_marco_theme_lookup_color_constant(PoloTheme *theme, const char *name,
				       char **value)
{
	char *result;

	*value = NULL;

	if (theme->color_constants == NULL)
		return FALSE;

	result = g_hash_table_lookup(theme->color_constants, name);

	if (result) {
		*value = result;
		return TRUE;
	} else {
		return FALSE;
	}
}

/**
 * Returns the height of the letters in a particular font.
 *
 * \param font_desc  the font
 * \param context  the context of the font
 * \return  the height of the letters
 */
int
polo_marco_pango_font_desc_get_text_height(
	const PangoFontDescription *font_desc, PangoContext *context)
{
	PangoFontMetrics *metrics;
	PangoLanguage *lang;
	int retval;

	lang = pango_context_get_language(context);
	metrics = pango_context_get_metrics(context, font_desc, lang);

	retval = PANGO_PIXELS(pango_font_metrics_get_ascent(metrics) +
			      pango_font_metrics_get_descent(metrics));

	pango_font_metrics_unref(metrics);

	return retval;
}

PoloGtkColorComponent
polo_marco_color_component_from_string(const char *str)
{
	if (strcmp("fg", str) == 0)
		return POLO_GTK_COLOR_FG;
	else if (strcmp("bg", str) == 0)
		return POLO_GTK_COLOR_BG;
	else if (strcmp("light", str) == 0)
		return POLO_GTK_COLOR_LIGHT;
	else if (strcmp("dark", str) == 0)
		return POLO_GTK_COLOR_DARK;
	else if (strcmp("mid", str) == 0)
		return POLO_GTK_COLOR_MID;
	else if (strcmp("text", str) == 0)
		return POLO_GTK_COLOR_TEXT;
	else if (strcmp("base", str) == 0)
		return POLO_GTK_COLOR_BASE;
	else if (strcmp("text_aa", str) == 0)
		return POLO_GTK_COLOR_TEXT_AA;
	else
		return POLO_GTK_COLOR_LAST;
}

const char *
polo_marco_color_component_to_string(PoloGtkColorComponent component)
{
	switch (component) {
	case POLO_GTK_COLOR_FG:
		return "fg";
	case POLO_GTK_COLOR_BG:
		return "bg";
	case POLO_GTK_COLOR_LIGHT:
		return "light";
	case POLO_GTK_COLOR_DARK:
		return "dark";
	case POLO_GTK_COLOR_MID:
		return "mid";
	case POLO_GTK_COLOR_TEXT:
		return "text";
	case POLO_GTK_COLOR_BASE:
		return "base";
	case POLO_GTK_COLOR_TEXT_AA:
		return "text_aa";
	case POLO_GTK_COLOR_LAST:
		break;
	}

	return "<unknown>";
}

PoloButtonState
polo_marco_button_state_from_string(const char *str)
{
	if (strcmp("normal", str) == 0)
		return POLO_BUTTON_STATE_NORMAL;
	else if (strcmp("pressed", str) == 0)
		return POLO_BUTTON_STATE_PRESSED;
	else if (strcmp("prelight", str) == 0)
		return POLO_BUTTON_STATE_PRELIGHT;
	else
		return POLO_BUTTON_STATE_LAST;
}

const char *
polo_marco_button_state_to_string(PoloButtonState state)
{
	switch (state) {
	case POLO_BUTTON_STATE_NORMAL:
		return "normal";
	case POLO_BUTTON_STATE_PRESSED:
		return "pressed";
	case POLO_BUTTON_STATE_PRELIGHT:
		return "prelight";
	case POLO_BUTTON_STATE_LAST:
		break;
	}

	return "<unknown>";
}

PoloButtonType
polo_marco_button_type_from_string(const char *str, PoloTheme *theme)
{
	if (POLO_THEME_ALLOWS(theme, POLO_THEME_SHADE_STICK_ABOVE_BUTTONS)) {
		if (strcmp("shade", str) == 0)
			return POLO_BUTTON_TYPE_SHADE;
		else if (strcmp("above", str) == 0)
			return POLO_BUTTON_TYPE_ABOVE;
		else if (strcmp("stick", str) == 0)
			return POLO_BUTTON_TYPE_STICK;
		else if (strcmp("unshade", str) == 0)
			return POLO_BUTTON_TYPE_UNSHADE;
		else if (strcmp("unabove", str) == 0)
			return POLO_BUTTON_TYPE_UNABOVE;
		else if (strcmp("unstick", str) == 0)
			return POLO_BUTTON_TYPE_UNSTICK;
	}

	if (strcmp("close", str) == 0)
		return POLO_BUTTON_TYPE_CLOSE;
	else if (strcmp("maximize", str) == 0)
		return POLO_BUTTON_TYPE_MAXIMIZE;
	else if (strcmp("minimize", str) == 0)
		return POLO_BUTTON_TYPE_MINIMIZE;
	else if (strcmp("menu", str) == 0)
		return POLO_BUTTON_TYPE_MENU;
	else if (strcmp("appmenu", str) == 0)
		return POLO_BUTTON_TYPE_APPMENU;
	else if (strcmp("left_left_background", str) == 0)
		return POLO_BUTTON_TYPE_LEFT_LEFT_BACKGROUND;
	else if (strcmp("left_middle_background", str) == 0)
		return POLO_BUTTON_TYPE_LEFT_MIDDLE_BACKGROUND;
	else if (strcmp("left_right_background", str) == 0)
		return POLO_BUTTON_TYPE_LEFT_RIGHT_BACKGROUND;
	else if (strcmp("left_single_background", str) == 0)
		return POLO_BUTTON_TYPE_LEFT_SINGLE_BACKGROUND;
	else if (strcmp("right_left_background", str) == 0)
		return POLO_BUTTON_TYPE_RIGHT_LEFT_BACKGROUND;
	else if (strcmp("right_middle_background", str) == 0)
		return POLO_BUTTON_TYPE_RIGHT_MIDDLE_BACKGROUND;
	else if (strcmp("right_right_background", str) == 0)
		return POLO_BUTTON_TYPE_RIGHT_RIGHT_BACKGROUND;
	else if (strcmp("right_single_background", str) == 0)
		return POLO_BUTTON_TYPE_RIGHT_SINGLE_BACKGROUND;
	else
		return POLO_BUTTON_TYPE_LAST;
}

const char *
polo_marco_button_type_to_string(PoloButtonType type)
{
	switch (type) {
	case POLO_BUTTON_TYPE_CLOSE:
		return "close";
	case POLO_BUTTON_TYPE_MAXIMIZE:
		return "maximize";
	case POLO_BUTTON_TYPE_MINIMIZE:
		return "minimize";
	case POLO_BUTTON_TYPE_SHADE:
		return "shade";
	case POLO_BUTTON_TYPE_ABOVE:
		return "above";
	case POLO_BUTTON_TYPE_STICK:
		return "stick";
	case POLO_BUTTON_TYPE_UNSHADE:
		return "unshade";
	case POLO_BUTTON_TYPE_UNABOVE:
		return "unabove";
	case POLO_BUTTON_TYPE_UNSTICK:
		return "unstick";
	case POLO_BUTTON_TYPE_MENU:
		return "menu";
	case POLO_BUTTON_TYPE_APPMENU:
		return "appmenu";
	case POLO_BUTTON_TYPE_LEFT_LEFT_BACKGROUND:
		return "left_left_background";
	case POLO_BUTTON_TYPE_LEFT_MIDDLE_BACKGROUND:
		return "left_middle_background";
	case POLO_BUTTON_TYPE_LEFT_RIGHT_BACKGROUND:
		return "left_right_background";
	case POLO_BUTTON_TYPE_LEFT_SINGLE_BACKGROUND:
		return "left_single_background";
	case POLO_BUTTON_TYPE_RIGHT_LEFT_BACKGROUND:
		return "right_left_background";
	case POLO_BUTTON_TYPE_RIGHT_MIDDLE_BACKGROUND:
		return "right_middle_background";
	case POLO_BUTTON_TYPE_RIGHT_RIGHT_BACKGROUND:
		return "right_right_background";
	case POLO_BUTTON_TYPE_RIGHT_SINGLE_BACKGROUND:
		return "right_single_background";
	case POLO_BUTTON_TYPE_LAST:
		break;
	}

	return "<unknown>";
}

PoloFramePiece
polo_marco_frame_piece_from_string(const char *str)
{
	if (strcmp("entire_background", str) == 0)
		return POLO_FRAME_PIECE_ENTIRE_BACKGROUND;
	else if (strcmp("titlebar", str) == 0)
		return POLO_FRAME_PIECE_TITLEBAR;
	else if (strcmp("titlebar_middle", str) == 0)
		return POLO_FRAME_PIECE_TITLEBAR_MIDDLE;
	else if (strcmp("left_titlebar_edge", str) == 0)
		return POLO_FRAME_PIECE_LEFT_TITLEBAR_EDGE;
	else if (strcmp("right_titlebar_edge", str) == 0)
		return POLO_FRAME_PIECE_RIGHT_TITLEBAR_EDGE;
	else if (strcmp("top_titlebar_edge", str) == 0)
		return POLO_FRAME_PIECE_TOP_TITLEBAR_EDGE;
	else if (strcmp("bottom_titlebar_edge", str) == 0)
		return POLO_FRAME_PIECE_BOTTOM_TITLEBAR_EDGE;
	else if (strcmp("title", str) == 0)
		return POLO_FRAME_PIECE_TITLE;
	else if (strcmp("left_edge", str) == 0)
		return POLO_FRAME_PIECE_LEFT_EDGE;
	else if (strcmp("right_edge", str) == 0)
		return POLO_FRAME_PIECE_RIGHT_EDGE;
	else if (strcmp("bottom_edge", str) == 0)
		return POLO_FRAME_PIECE_BOTTOM_EDGE;
	else if (strcmp("overlay", str) == 0)
		return POLO_FRAME_PIECE_OVERLAY;
	else
		return POLO_FRAME_PIECE_LAST;
}

const char *
polo_marco_frame_piece_to_string(PoloFramePiece piece)
{
	switch (piece) {
	case POLO_FRAME_PIECE_ENTIRE_BACKGROUND:
		return "entire_background";
	case POLO_FRAME_PIECE_TITLEBAR:
		return "titlebar";
	case POLO_FRAME_PIECE_TITLEBAR_MIDDLE:
		return "titlebar_middle";
	case POLO_FRAME_PIECE_LEFT_TITLEBAR_EDGE:
		return "left_titlebar_edge";
	case POLO_FRAME_PIECE_RIGHT_TITLEBAR_EDGE:
		return "right_titlebar_edge";
	case POLO_FRAME_PIECE_TOP_TITLEBAR_EDGE:
		return "top_titlebar_edge";
	case POLO_FRAME_PIECE_BOTTOM_TITLEBAR_EDGE:
		return "bottom_titlebar_edge";
	case POLO_FRAME_PIECE_TITLE:
		return "title";
	case POLO_FRAME_PIECE_LEFT_EDGE:
		return "left_edge";
	case POLO_FRAME_PIECE_RIGHT_EDGE:
		return "right_edge";
	case POLO_FRAME_PIECE_BOTTOM_EDGE:
		return "bottom_edge";
	case POLO_FRAME_PIECE_OVERLAY:
		return "overlay";
	case POLO_FRAME_PIECE_LAST:
		break;
	}

	return "<unknown>";
}

PoloFrameState
polo_marco_frame_state_from_string(const char *str)
{
	if (strcmp("normal", str) == 0)
		return POLO_FRAME_STATE_NORMAL;
	else if (strcmp("maximized", str) == 0)
		return POLO_FRAME_STATE_MAXIMIZED;
	else if (strcmp("tiled_left", str) == 0)
		return POLO_FRAME_STATE_TILED_LEFT;
	else if (strcmp("tiled_right", str) == 0)
		return POLO_FRAME_STATE_TILED_RIGHT;
	else if (strcmp("shaded", str) == 0)
		return POLO_FRAME_STATE_SHADED;
	else if (strcmp("maximized_and_shaded", str) == 0)
		return POLO_FRAME_STATE_MAXIMIZED_AND_SHADED;
	else if (strcmp("tiled_left_and_shaded", str) == 0)
		return POLO_FRAME_STATE_TILED_LEFT_AND_SHADED;
	else if (strcmp("tiled_right_and_shaded", str) == 0)
		return POLO_FRAME_STATE_TILED_RIGHT_AND_SHADED;
	else
		return POLO_FRAME_STATE_LAST;
}

const char *
polo_marco_frame_state_to_string(PoloFrameState state)
{
	switch (state) {
	case POLO_FRAME_STATE_NORMAL:
		return "normal";
	case POLO_FRAME_STATE_MAXIMIZED:
		return "maximized";
	case POLO_FRAME_STATE_TILED_LEFT:
		return "tiled_left";
	case POLO_FRAME_STATE_TILED_RIGHT:
		return "tiled_right";
	case POLO_FRAME_STATE_SHADED:
		return "shaded";
	case POLO_FRAME_STATE_MAXIMIZED_AND_SHADED:
		return "maximized_and_shaded";
	case POLO_FRAME_STATE_TILED_LEFT_AND_SHADED:
		return "tiled_left_and_shaded";
	case POLO_FRAME_STATE_TILED_RIGHT_AND_SHADED:
		return "tiled_right_and_shaded";
	case POLO_FRAME_STATE_LAST:
		break;
	}

	return "<unknown>";
}

PoloFrameResize
polo_marco_frame_resize_from_string(const char *str)
{
	if (strcmp("none", str) == 0)
		return POLO_FRAME_RESIZE_NONE;
	else if (strcmp("vertical", str) == 0)
		return POLO_FRAME_RESIZE_VERTICAL;
	else if (strcmp("horizontal", str) == 0)
		return POLO_FRAME_RESIZE_HORIZONTAL;
	else if (strcmp("both", str) == 0)
		return POLO_FRAME_RESIZE_BOTH;
	else
		return POLO_FRAME_RESIZE_LAST;
}

const char *
polo_marco_frame_resize_to_string(PoloFrameResize resize)
{
	switch (resize) {
	case POLO_FRAME_RESIZE_NONE:
		return "none";
	case POLO_FRAME_RESIZE_VERTICAL:
		return "vertical";
	case POLO_FRAME_RESIZE_HORIZONTAL:
		return "horizontal";
	case POLO_FRAME_RESIZE_BOTH:
		return "both";
	case POLO_FRAME_RESIZE_LAST:
		break;
	}

	return "<unknown>";
}

PoloFrameFocus
polo_marco_frame_focus_from_string(const char *str)
{
	if (strcmp("no", str) == 0)
		return POLO_FRAME_FOCUS_NO;
	else if (strcmp("yes", str) == 0)
		return POLO_FRAME_FOCUS_YES;
	else
		return POLO_FRAME_FOCUS_LAST;
}

const char *
polo_marco_frame_focus_to_string(PoloFrameFocus focus)
{
	switch (focus) {
	case POLO_FRAME_FOCUS_NO:
		return "no";
	case POLO_FRAME_FOCUS_YES:
		return "yes";
	case POLO_FRAME_FOCUS_LAST:
		break;
	}

	return "<unknown>";
}

PoloFrameType
polo_marco_frame_type_from_string(const char *str)
{
	if (strcmp("normal", str) == 0)
		return POLO_FRAME_TYPE_NORMAL;
	else if (strcmp("dialog", str) == 0)
		return POLO_FRAME_TYPE_DIALOG;
	else if (strcmp("modal_dialog", str) == 0)
		return POLO_FRAME_TYPE_MODAL_DIALOG;
	else if (strcmp("utility", str) == 0)
		return POLO_FRAME_TYPE_UTILITY;
	else if (strcmp("menu", str) == 0)
		return POLO_FRAME_TYPE_MENU;
	else if (strcmp("border", str) == 0)
		return POLO_FRAME_TYPE_BORDER;
	else if (strcmp("attached", str) == 0)
		return POLO_FRAME_TYPE_ATTACHED;
#if 0
  else if (strcmp ("toolbar", str) == 0)
    return POLO_FRAME_TYPE_TOOLBAR;
#endif
	else
		return POLO_FRAME_TYPE_LAST;
}

const char *
polo_marco_frame_type_to_string(PoloFrameType type)
{
	switch (type) {
	case POLO_FRAME_TYPE_NORMAL:
		return "normal";
	case POLO_FRAME_TYPE_DIALOG:
		return "dialog";
	case POLO_FRAME_TYPE_MODAL_DIALOG:
		return "modal_dialog";
	case POLO_FRAME_TYPE_UTILITY:
		return "utility";
	case POLO_FRAME_TYPE_MENU:
		return "menu";
	case POLO_FRAME_TYPE_BORDER:
		return "border";
	case POLO_FRAME_TYPE_ATTACHED:
		return "attached";
#if 0
    case POLO_FRAME_TYPE_TOOLBAR:
      return "toolbar";
#endif
	case POLO_FRAME_TYPE_LAST:
		break;
	}

	return "<unknown>";
}

PoloGradientType
polo_marco_gradient_type_from_string(const char *str)
{
	if (strcmp("vertical", str) == 0)
		return POLO_GRADIENT_VERTICAL;
	else if (strcmp("horizontal", str) == 0)
		return POLO_GRADIENT_HORIZONTAL;
	else if (strcmp("diagonal", str) == 0)
		return POLO_GRADIENT_DIAGONAL;
	else
		return POLO_GRADIENT_LAST;
}

const char *
polo_marco_gradient_type_to_string(PoloGradientType type)
{
	switch (type) {
	case POLO_GRADIENT_VERTICAL:
		return "vertical";
	case POLO_GRADIENT_HORIZONTAL:
		return "horizontal";
	case POLO_GRADIENT_DIAGONAL:
		return "diagonal";
	case POLO_GRADIENT_LAST:
		break;
	}

	return "<unknown>";
}

PoloStateFlags
polo_marco_gtk_state_from_string(const char *str)
{
	if (g_ascii_strcasecmp("normal", str) == 0)
		return POLO_STATE_FLAG_NORMAL;
	else if (g_ascii_strcasecmp("prelight", str) == 0)
		return POLO_STATE_FLAG_PRELIGHT;
	else if (g_ascii_strcasecmp("active", str) == 0)
		return POLO_STATE_FLAG_ACTIVE;
	else if (g_ascii_strcasecmp("selected", str) == 0)
		return POLO_STATE_FLAG_SELECTED;
	else if (g_ascii_strcasecmp("insensitive", str) == 0)
		return POLO_STATE_FLAG_INSENSITIVE;
	else if (g_ascii_strcasecmp("inconsistent", str) == 0)
		return POLO_STATE_FLAG_INCONSISTENT;
	else if (g_ascii_strcasecmp("focused", str) == 0)
		return POLO_STATE_FLAG_FOCUSED;
	else if (g_ascii_strcasecmp("backdrop", str) == 0)
		return POLO_STATE_FLAG_BACKDROP;
	else
		return -1; /* hack */
}

PoloShadowType
polo_marco_gtk_shadow_from_string(const char *str)
{
	if (strcmp("none", str) == 0)
		return POLO_SHADOW_NONE;
	else if (strcmp("in", str) == 0)
		return POLO_SHADOW_IN;
	else if (strcmp("out", str) == 0)
		return POLO_SHADOW_OUT;
	else if (strcmp("etched_in", str) == 0)
		return POLO_SHADOW_ETCHED_IN;
	else if (strcmp("etched_out", str) == 0)
		return POLO_SHADOW_ETCHED_OUT;
	else
		return -1;
}

const char *
polo_marco_gtk_shadow_to_string(PoloShadowType shadow)
{
	switch (shadow) {
	case POLO_SHADOW_NONE:
		return "none";
	case POLO_SHADOW_IN:
		return "in";
	case POLO_SHADOW_OUT:
		return "out";
	case POLO_SHADOW_ETCHED_IN:
		return "etched_in";
	case POLO_SHADOW_ETCHED_OUT:
		return "etched_out";
	}

	return "<unknown>";
}

PoloArrowType
polo_marco_gtk_arrow_from_string(const char *str)
{
	if (strcmp("up", str) == 0)
		return POLO_ARROW_UP;
	else if (strcmp("down", str) == 0)
		return POLO_ARROW_DOWN;
	else if (strcmp("left", str) == 0)
		return POLO_ARROW_LEFT;
	else if (strcmp("right", str) == 0)
		return POLO_ARROW_RIGHT;
	else if (strcmp("none", str) == 0)
		return POLO_ARROW_NONE;
	else
		return -1;
}

const char *
polo_marco_gtk_arrow_to_string(PoloArrowType arrow)
{
	switch (arrow) {
	case POLO_ARROW_UP:
		return "up";
	case POLO_ARROW_DOWN:
		return "down";
	case POLO_ARROW_LEFT:
		return "left";
	case POLO_ARROW_RIGHT:
		return "right";
	case POLO_ARROW_NONE:
		return "none";
	}

	return "<unknown>";
}

/**
 * Returns a fill_type from a string.  The inverse of
 * polo_marco_image_fill_type_to_string().
 *
 * \param str  a string representing a fill_type
 * \result  the fill_type, or -1 if it represents no fill_type.
 */
PoloImageFillType
polo_marco_image_fill_type_from_string(const char *str)
{
	if (strcmp("tile", str) == 0)
		return POLO_IMAGE_FILL_TILE;
	else if (strcmp("scale", str) == 0)
		return POLO_IMAGE_FILL_SCALE;
	else
		return -1;
}

/**
 * Returns a string representation of a fill_type.  The inverse of
 * polo_marco_image_fill_type_from_string().
 *
 * \param fill_type  the fill type
 * \result  a string representing that type
 */
const char *
polo_marco_image_fill_type_to_string(PoloImageFillType fill_type)
{
	switch (fill_type) {
	case POLO_IMAGE_FILL_TILE:
		return "tile";
	case POLO_IMAGE_FILL_SCALE:
		return "scale";
	}

	return "<unknown>";
}

#if 0

#endif

/**
 * Returns the earliest version of the theme format which required support
 * for a particular button.  (For example, "shade" first appeared in v2, and
 * "close" in v1.)
 *
 * \param type  the button type
 * \return  the number of the theme format
 */
guint
polo_marco_theme_earliest_version_with_button(PoloButtonType type)
{
	switch (type) {
	case POLO_BUTTON_TYPE_CLOSE:
	case POLO_BUTTON_TYPE_MAXIMIZE:
	case POLO_BUTTON_TYPE_MINIMIZE:
	case POLO_BUTTON_TYPE_MENU:
	case POLO_BUTTON_TYPE_LEFT_LEFT_BACKGROUND:
	case POLO_BUTTON_TYPE_LEFT_MIDDLE_BACKGROUND:
	case POLO_BUTTON_TYPE_LEFT_RIGHT_BACKGROUND:
	case POLO_BUTTON_TYPE_RIGHT_LEFT_BACKGROUND:
	case POLO_BUTTON_TYPE_RIGHT_MIDDLE_BACKGROUND:
	case POLO_BUTTON_TYPE_RIGHT_RIGHT_BACKGROUND:
		return 1000;

	case POLO_BUTTON_TYPE_SHADE:
	case POLO_BUTTON_TYPE_ABOVE:
	case POLO_BUTTON_TYPE_STICK:
	case POLO_BUTTON_TYPE_UNSHADE:
	case POLO_BUTTON_TYPE_UNABOVE:
	case POLO_BUTTON_TYPE_UNSTICK:
		return 2000;

	case POLO_BUTTON_TYPE_LEFT_SINGLE_BACKGROUND:
	case POLO_BUTTON_TYPE_RIGHT_SINGLE_BACKGROUND:
		return 3003;

	case POLO_BUTTON_TYPE_APPMENU:
		return 3005;

	default:
		polo_marco_warning("Unknown button %d\n", type);
		return 1000;
	}
}

const char **
polo_marco_theme_get_custom_color_names(PoloTheme *theme)
{
	return (const char **)g_hash_table_get_keys_as_array(
		theme->custom_color_names, NULL);
}
