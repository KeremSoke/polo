/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stands in for Weston's shared/cairo-util.h. The XWM runs with
 * no_xwm_decorations, so no frame exists and these are never reached with one.
 */

#ifndef POLO_XWM_FRAME_STUB_H
#define POLO_XWM_FRAME_STUB_H

#include <stdint.h>

struct frame;

struct theme {
	int margin;
	int width;
	int titlebar_height;
};

enum theme_location {
	THEME_LOCATION_INTERIOR = 0,
	THEME_LOCATION_RESIZING_TOP = 1,
	THEME_LOCATION_RESIZING_BOTTOM = 2,
	THEME_LOCATION_RESIZING_LEFT = 4,
	THEME_LOCATION_RESIZING_TOP_LEFT = 5,
	THEME_LOCATION_RESIZING_BOTTOM_LEFT = 6,
	THEME_LOCATION_RESIZING_RIGHT = 8,
	THEME_LOCATION_RESIZING_TOP_RIGHT = 9,
	THEME_LOCATION_RESIZING_BOTTOM_RIGHT = 10,
	THEME_LOCATION_RESIZING_MASK = 15,
	THEME_LOCATION_EXTERIOR = 16,
	THEME_LOCATION_TITLEBAR = 17,
	THEME_LOCATION_CLIENT_AREA = 18,
};

enum frame_status {
	FRAME_STATUS_NONE = 0,
	FRAME_STATUS_REPAINT = 0x1,
	FRAME_STATUS_MINIMIZE = 0x2,
	FRAME_STATUS_MAXIMIZE = 0x4,
	FRAME_STATUS_CLOSE = 0x8,
	FRAME_STATUS_MENU = 0x10,
	FRAME_STATUS_RESIZE = 0x20,
	FRAME_STATUS_MOVE = 0x40,
	FRAME_STATUS_ALL = 0x7f
};

enum frame_flag {
	FRAME_FLAG_ACTIVE = 0x1,
	FRAME_FLAG_MAXIMIZED = 0x2,
	FRAME_FLAG_NO_SHADOW = 0x4
};

enum {
	FRAME_BUTTON_NONE = 0,
	FRAME_BUTTON_CLOSE = 0x1,
	FRAME_BUTTON_MAXIMIZE = 0x2,
	FRAME_BUTTON_MINIMIZE = 0x4,
	FRAME_BUTTON_ALL = 0x7
};

static inline struct theme *
theme_create(void)
{
	static struct theme none;

	return &none;
}

static inline void
theme_destroy(struct theme *t)
{
}

static inline struct frame *
frame_create(struct theme *t, int32_t width, int32_t height, uint32_t buttons,
	     const char *title, void *icon)
{
	return 0;
}

static inline void
frame_destroy(struct frame *frame)
{
}

static inline void
frame_set_flag(struct frame *frame, enum frame_flag flag)
{
}

static inline void
frame_unset_flag(struct frame *frame, enum frame_flag flag)
{
}

static inline void
frame_resize_inside(struct frame *frame, int32_t width, int32_t height)
{
}

static inline int32_t
frame_width(struct frame *frame)
{
	return 0;
}

static inline int32_t
frame_height(struct frame *frame)
{
	return 0;
}

static inline void
frame_interior(struct frame *frame, int32_t *x, int32_t *y, int32_t *width,
	       int32_t *height)
{
}

static inline void
frame_input_rect(struct frame *frame, int32_t *x, int32_t *y, int32_t *width,
		 int32_t *height)
{
}

static inline void
frame_decoration_sizes(struct frame *frame, int32_t *top, int32_t *bottom,
		       int32_t *left, int32_t *right)
{
}

static inline uint32_t
frame_status(struct frame *frame)
{
	return 0;
}

static inline void
frame_status_clear(struct frame *frame, enum frame_status status)
{
}

static inline enum theme_location
frame_pointer_enter(struct frame *frame, void *pointer, int x, int y)
{
	return THEME_LOCATION_EXTERIOR;
}

static inline enum theme_location
frame_pointer_motion(struct frame *frame, void *pointer, int x, int y)
{
	return THEME_LOCATION_EXTERIOR;
}

static inline void
frame_pointer_leave(struct frame *frame, void *pointer)
{
}

static inline enum theme_location
frame_pointer_button(struct frame *frame, void *pointer, uint32_t button,
		     int state)
{
	return THEME_LOCATION_EXTERIOR;
}

static inline enum theme_location
frame_double_click(struct frame *frame, void *pointer, uint32_t button,
		   int state)
{
	return THEME_LOCATION_EXTERIOR;
}

#endif
