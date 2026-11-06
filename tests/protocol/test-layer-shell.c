/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwlr_layer_shell_v1 v5 tests against headless Polo: every anchor
 * combination, exclusive zones on all four edges, margins, keyboard
 * interactivity, popups, output loss and the protocol error paths.
 *
 * The second output (POLO_TEST_TWO_OUTPUTS) is enabled for everything;
 * layer surfaces and windows all use the first one except in the
 * output-loss test.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input.h>

#include "harness.h"

#define LAYER_TOP 2
#define LAYER_OVERLAY 3

#define ANCHOR_TOP 1
#define ANCHOR_BOTTOM 2
#define ANCHOR_LEFT 4
#define ANCHOR_RIGHT 8

/* Polo.WindowState.ACTIVE (src/core/window.vala). */
#define STATE_ACTIVE (1u << 12)

#define KBD_NONE 0
#define KBD_EXCLUSIVE 1
#define KBD_ON_DEMAND 2

/* ---- A layer surface client ---------------------------------------------- */

struct layer_surface {
	struct harness *h;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_buffer *buffer;
	int width, height; /* current buffer size */
	int desired_width, desired_height;
	bool mapped; /* the client's view: a buffer is committed */
	bool closed;
};

static void
layer_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
		uint32_t serial, uint32_t width, uint32_t height)
{
	struct layer_surface *s = data;
	int buffer_width, buffer_height;

	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

	/* Follow the compositor's size like a real client; 0 means keep the
	 * requested size. */
	buffer_width = width ? (int)width : s->desired_width;
	buffer_height = height ? (int)height : s->desired_height;
	if (!s->buffer || !s->mapped || buffer_width != s->width ||
	    buffer_height != s->height) {
		if (s->buffer)
			wl_buffer_destroy(s->buffer);
		s->buffer = harness_buffer_create(s->h->shm, buffer_width,
						  buffer_height);
		s->width = buffer_width;
		s->height = buffer_height;
		wl_surface_attach(s->surface, s->buffer, 0, 0);
		wl_surface_damage_buffer(s->surface, 0, 0, buffer_width,
					 buffer_height);
		s->mapped = true;
	}
	wl_surface_commit(s->surface);
}

static void
layer_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
	struct layer_surface *s = data;

	s->closed = true;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	layer_configure,
	layer_closed,
};

/*
 * Create, configure and map a layer surface. Each parameter that is
 * negative is left at its default; the surface is returned mapped.
 */
static struct layer_surface *
layer_surface_create(struct harness *h, const char *namespace_, uint32_t layer,
		     struct wl_output *output, uint32_t anchor, int zone,
		     uint32_t edge, int margin_top, int margin_right,
		     int margin_bottom, int margin_left, uint32_t kbd,
		     int width, int height)
{
	struct layer_surface *s = calloc(1, sizeof *s);
	int i;

	s->h = h;
	s->surface = wl_compositor_create_surface(h->compositor);
	s->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		h->layer_shell, s->surface, output, layer, namespace_);
	zwlr_layer_surface_v1_add_listener(s->layer_surface, &layer_listener,
					   s);
	if ((int)anchor >= 0)
		zwlr_layer_surface_v1_set_anchor(s->layer_surface, anchor);
	if (zone >= 0)
		zwlr_layer_surface_v1_set_exclusive_zone(s->layer_surface,
							 zone);
	if ((int)edge >= 0)
		zwlr_layer_surface_v1_set_exclusive_edge(s->layer_surface,
							 edge);
	if (margin_top >= 0)
		zwlr_layer_surface_v1_set_margin(s->layer_surface, margin_top,
						 margin_right, margin_bottom,
						 margin_left);
	if ((int)kbd >= 0)
		zwlr_layer_surface_v1_set_keyboard_interactivity(
			s->layer_surface, kbd);
	zwlr_layer_surface_v1_set_size(s->layer_surface, width, height);
	s->desired_width = width;
	s->desired_height = height;
	wl_surface_commit(s->surface);

	for (i = 0; i < 20; i++) {
		harness_roundtrip(h);
		if (harness_layer_info(h, s->surface)->mapped)
			break;
	}
	CHECK(harness_layer_info(h, s->surface)->mapped,
	      "%s: layer surface did not map", namespace_);
	harness_wait_frame(h, s->surface);
	return s;
}

static void
layer_surface_destroy(struct layer_surface *s)
{
	struct harness *h = s->h;

	zwlr_layer_surface_v1_destroy(s->layer_surface);
	wl_surface_destroy(s->surface);
	if (s->buffer)
		wl_buffer_destroy(s->buffer);
	free(s);
	harness_wait_repaint(h);
}

/* ---- Anchor placement----------------------------------------------------- */

static void
test_anchors(struct harness *h)
{
	uint32_t anchor;

	for (anchor = 0; anchor < 16; anchor++) {
		struct layer_surface *s;
		const struct harness_layer_info *info;
		int width = (anchor & (ANCHOR_LEFT | ANCHOR_RIGHT)) ==
				(ANCHOR_LEFT | ANCHOR_RIGHT)
			? HARNESS_OUTPUT_WIDTH
			: 200;
		int height = (anchor & (ANCHOR_TOP | ANCHOR_BOTTOM)) ==
				(ANCHOR_TOP | ANCHOR_BOTTOM)
			? HARNESS_OUTPUT_HEIGHT
			: 50;
		int x = (anchor & ANCHOR_LEFT) ? 0
			: (anchor & ANCHOR_RIGHT)
			? HARNESS_OUTPUT_WIDTH - width
			: (HARNESS_OUTPUT_WIDTH - width) / 2;
		int y = (anchor & ANCHOR_TOP) ? 0
			: (anchor & ANCHOR_BOTTOM)
			? HARNESS_OUTPUT_HEIGHT - height
			: (HARNESS_OUTPUT_HEIGHT - height) / 2;
		char namespace_[32];

		snprintf(namespace_, sizeof namespace_, "anchor-%u", anchor);
		s = layer_surface_create(h, namespace_, LAYER_TOP, NULL, anchor,
					 -1, -1, -1, -1, -1, -1, KBD_NONE, 200,
					 50);
		info = harness_layer_info(h, s->surface);
		CHECK(info->x == x && info->y == y,
		      "anchor %u: placed at %d,%d, expected %d,%d", anchor,
		      info->x, info->y, x, y);
		CHECK(info->width == width && info->height == height,
		      "anchor %u: size %dx%d, expected %dx%d", anchor,
		      info->width, info->height, width, height);
		layer_surface_destroy(s);
	}
}

/* set_layer is double-buffered and applies at the next commit (v2+). */
static void
test_set_layer(struct harness *h)
{
	struct layer_surface *s =
		layer_surface_create(h, "layer-switch", 0, NULL, ANCHOR_TOP, -1,
				     -1, -1, -1, -1, -1, KBD_NONE, 200, 50);
	const struct harness_layer_info *info;

	CHECK(harness_layer_info(h, s->surface)->layer == 0,
	      "layer not background");
	zwlr_layer_surface_v1_set_layer(s->layer_surface, LAYER_OVERLAY);
	harness_wait_frame(h, s->surface);
	info = harness_layer_info(h, s->surface);
	CHECK(info->layer == LAYER_OVERLAY, "set_layer did not apply: %d",
	      info->layer);
	layer_surface_destroy(s);
}

/* Unmap with a NULL buffer. Re-mapping takes a buffer-less commit, a
 * configure and its ack, as on creation; the configure handler attaches
 * the buffer again. */
static void
test_unmap_remap(struct harness *h)
{
	struct layer_surface *s =
		layer_surface_create(h, "unmap", LAYER_TOP, NULL, ANCHOR_TOP,
				     -1, -1, -1, -1, -1, -1, KBD_NONE, 200, 50);
	int i;

	wl_surface_attach(s->surface, NULL, 0, 0);
	wl_surface_commit(s->surface);
	s->mapped = false;
	harness_roundtrip(h);
	CHECK(!harness_layer_info(h, s->surface)->mapped, "still mapped");

	wl_surface_commit(s->surface);
	for (i = 0; i < 20 && !harness_layer_info(h, s->surface)->mapped; i++)
		harness_roundtrip(h);
	CHECK(s->mapped && harness_layer_info(h, s->surface)->mapped,
	      "did not re-map");
	CHECK(harness_layer_info(h, s->surface)->y == 0, "re-mapped at y %d",
	      harness_layer_info(h, s->surface)->y);
	layer_surface_destroy(s);
}

/* A zero size lets the compositor pick that dimension when the surface is
 * anchored to both opposite edges on its axis; mate-panel's top panel does
 * exactly this (anchor top|left|right, size 0x25). */
static void
test_zero_size(struct harness *h)
{
	struct layer_surface *s;
	const struct harness_layer_info *info;

	s = layer_surface_create(h, "zero-width", LAYER_TOP, NULL,
				 ANCHOR_TOP | ANCHOR_LEFT | ANCHOR_RIGHT, 25,
				 -1, -1, -1, -1, -1, KBD_NONE, 0, 25);
	info = harness_layer_info(h, s->surface);
	CHECK(info->x == 0 && info->y == 0 &&
		      info->width == HARNESS_OUTPUT_WIDTH && info->height == 25,
	      "zero width: %d,%d %dx%d", info->x, info->y, info->width,
	      info->height);
	layer_surface_destroy(s);

	s = layer_surface_create(h, "zero-height", LAYER_TOP, NULL,
				 ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_BOTTOM, -1,
				 -1, -1, -1, -1, -1, KBD_NONE, 30, 0);
	info = harness_layer_info(h, s->surface);
	CHECK(info->x == 0 && info->y == 0 && info->width == 30 &&
		      info->height == HARNESS_OUTPUT_HEIGHT,
	      "zero height: %d,%d %dx%d", info->x, info->y, info->width,
	      info->height);
	layer_surface_destroy(s);
}

/* ---- Margins --------------------------------------------------------------
 */

static void
test_margins(struct harness *h)
{
	struct layer_surface *s;
	const struct harness_layer_info *info;

	/* Corner anchors pin the surface to the margins. */
	s = layer_surface_create(h, "margins", LAYER_TOP, NULL,
				 ANCHOR_TOP | ANCHOR_LEFT, -1, -1, 10, 5, 3, 20,
				 KBD_NONE, 200, 50);
	info = harness_layer_info(h, s->surface);
	CHECK(info->x == 20 && info->y == 10, "margins: placed at %d,%d",
	      info->x, info->y);
	layer_surface_destroy(s);

	/* Anchoring to both opposite edges spans the work area minus the
	 * margins. */
	s = layer_surface_create(h, "margins-span", LAYER_TOP, NULL,
				 ANCHOR_TOP | ANCHOR_BOTTOM, -1, -1, 10, 0, 5,
				 0, KBD_NONE, 200, 50);
	info = harness_layer_info(h, s->surface);
	CHECK(info->height == HARNESS_OUTPUT_HEIGHT - 15,
	      "span height %d, expected %d", info->height,
	      HARNESS_OUTPUT_HEIGHT - 15);
	layer_surface_destroy(s);
}

/* ---- Exclusive zones------------------------------------------------------ */

static void
test_exclusive_zone(struct harness *h, uint32_t edge, int thickness,
		    int expected_x, int expected_y, int expected_width,
		    int expected_height)
{
	uint32_t anchor = edge == ANCHOR_TOP || edge == ANCHOR_BOTTOM
		? edge | ANCHOR_LEFT | ANCHOR_RIGHT
		: edge | ANCHOR_TOP | ANCHOR_BOTTOM;
	int surface_width =
		edge == ANCHOR_TOP || edge == ANCHOR_BOTTOM ? 10 : thickness;
	int surface_height =
		edge == ANCHOR_TOP || edge == ANCHOR_BOTTOM ? thickness : 10;
	struct layer_surface *s;
	struct harness_window *w;
	const struct harness_window_info *winfo;
	char namespace_[32];

	snprintf(namespace_, sizeof namespace_, "zone-%u", edge);
	s = layer_surface_create(h, namespace_, LAYER_TOP, NULL, anchor,
				 thickness, edge, 0, 0, 0, 0, KBD_NONE,
				 surface_width, surface_height);
	/* The surface sits at the output edge, outside its own strut. */
	{
		const struct harness_layer_info *info =
			harness_layer_info(h, s->surface);
		int x = edge == ANCHOR_RIGHT ? HARNESS_OUTPUT_WIDTH - thickness
					     : 0;
		int y = edge == ANCHOR_BOTTOM
			? HARNESS_OUTPUT_HEIGHT - thickness
			: 0;

		CHECK(info->x == x && info->y == y,
		      "edge %u: surface at %d,%d, expected %d,%d", edge,
		      info->x, info->y, x, y);
	}

	w = harness_window_create(h, 400, 300, "zone");
	xdg_toplevel_set_maximized(w->xdg_toplevel);
	for (int i = 0; i < 20; i++)
		harness_roundtrip(h);
	harness_wait_repaint(h);

	winfo = harness_window_info(w);
	CHECK(winfo->x == expected_x && winfo->y == expected_y &&
		      winfo->width == expected_width &&
		      winfo->height == expected_height,
	      "edge %u: work area %d,%d %dx%d, expected %d,%d %dx%d", edge,
	      winfo->x, winfo->y, winfo->width, winfo->height, expected_x,
	      expected_y, expected_width, expected_height);

	harness_window_destroy(w);
	layer_surface_destroy(s);
}

static void
test_exclusive_zones(struct harness *h)
{
	int w = HARNESS_OUTPUT_WIDTH, hgt = HARNESS_OUTPUT_HEIGHT;

	test_exclusive_zone(h, ANCHOR_TOP, 30, 0, 30, w, hgt - 30);
	test_exclusive_zone(h, ANCHOR_BOTTOM, 30, 0, 0, w, hgt - 30);
	test_exclusive_zone(h, ANCHOR_LEFT, 40, 40, 0, w - 40, hgt);
	test_exclusive_zone(h, ANCHOR_RIGHT, 40, 0, 0, w - 40, hgt);

	/* The exclusive zone is the strut thickness; the margin extends the
	 * strut (Marco's strut rect covers zone + margin). */
	struct layer_surface *s;
	struct harness_window *win;
	const struct harness_window_info *info;

	s = layer_surface_create(h, "zone-margin", LAYER_TOP, NULL,
				 ANCHOR_TOP | ANCHOR_LEFT | ANCHOR_RIGHT, 20,
				 ANCHOR_TOP, 5, 0, 0, 0, KBD_NONE, 10, 20);
	win = harness_window_create(h, 400, 300, "zone-margin");
	xdg_toplevel_set_maximized(win->xdg_toplevel);
	for (int i = 0; i < 20; i++)
		harness_roundtrip(h);
	harness_wait_repaint(h);
	info = harness_window_info(win);
	CHECK(info->y == 25, "zone+margin work area starts at %d", info->y);
	harness_window_destroy(win);
	layer_surface_destroy(s);

	/* A zone on the second output leaves the first one alone: struts
	 * apply only to the output they touch. */
	s = layer_surface_create(h, "zone-other-output", LAYER_TOP,
				 h->outputs[1],
				 ANCHOR_TOP | ANCHOR_LEFT | ANCHOR_RIGHT, 40,
				 ANCHOR_TOP, 0, 0, 0, 0, KBD_NONE, 10, 40);
	win = harness_window_create(h, 400, 300, "zone-other-output");
	xdg_toplevel_set_maximized(win->xdg_toplevel);
	for (int i = 0; i < 20; i++)
		harness_roundtrip(h);
	harness_wait_repaint(h);
	info = harness_window_info(win);
	CHECK(info->x == 0 && info->y == 0 && info->width == w &&
		      info->height == hgt,
	      "zone on output 2 changed output 1's work area: %d,%d %dx%d",
	      info->x, info->y, info->width, info->height);
	harness_window_destroy(win);
	layer_surface_destroy(s);
}

/* ---- Keyboard interactivity----------------------------------------------- */

static void
test_keyboard_modes(struct harness *h)
{
	struct harness_window *w;
	struct layer_surface *s;

	w = harness_window_create(h, 400, 300, "kbd");
	CHECK(h->keyboard_focus == w->surface, "window not focused");

	/* none: clicking the layer surface leaves focus alone. */
	s = layer_surface_create(h, "kbd-none", LAYER_TOP, NULL,
				 ANCHOR_TOP | ANCHOR_LEFT, -1, -1, -1, -1, -1,
				 -1, KBD_NONE, 200, 50);
	harness_click(h, 100, 25);
	CHECK(h->keyboard_focus == w->surface, "kbd none took focus");
	layer_surface_destroy(s);

	/* on_demand: focus on map in the top layer, and a click keeps it. */
	s = layer_surface_create(h, "kbd-on-demand", LAYER_TOP, NULL,
				 ANCHOR_TOP | ANCHOR_LEFT, -1, -1, -1, -1, -1,
				 -1, KBD_ON_DEMAND, 200, 50);
	harness_roundtrip(h);
	CHECK(h->keyboard_focus == s->surface,
	      "on_demand surface did not take focus on map");
	harness_click(h, 100, 25);
	CHECK(h->keyboard_focus == s->surface, "click did not focus the panel");
	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	CHECK(h->last_key_surface == s->surface, "key did not reach the panel");
	/* As with Marco's desktop and docks: no window is focused meanwhile. */
	CHECK(!(harness_window_info(w)->state & STATE_ACTIVE),
	      "window still active while the panel has the keyboard");

	/* Clicking the window again gives it focus back (dock behaviour). */
	{
		const struct harness_window_info *winfo =
			harness_window_info(w);

		harness_click(h, winfo->x + winfo->width / 2,
			      winfo->y + winfo->height / 2);
	}
	CHECK(h->keyboard_focus == w->surface,
	      "window click did not take focus back");
	CHECK(harness_window_info(w)->state & STATE_ACTIVE,
	      "window not active again after its click");
	layer_surface_destroy(s);

	/* exclusive: focus on map, restored to the previous window on
	 * destroy. */
	s = layer_surface_create(h, "kbd-exclusive", LAYER_TOP, NULL,
				 ANCHOR_TOP | ANCHOR_LEFT, -1, -1, -1, -1, -1,
				 -1, KBD_EXCLUSIVE, 200, 50);
	CHECK(h->keyboard_focus == s->surface,
	      "exclusive surface did not take focus on map");
	CHECK(!(harness_window_info(w)->state & STATE_ACTIVE),
	      "window still active under an exclusive surface");
	layer_surface_destroy(s);
	harness_roundtrip(h);
	CHECK(h->keyboard_focus == w->surface,
	      "focus did not return to the window");
	CHECK(harness_window_info(w)->state & STATE_ACTIVE,
	      "window not active again after the exclusive surface");

	harness_window_destroy(w);
}

/* ---- Popups (get_popup)--------------------------------------------------- */

struct popup {
	struct harness *h;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_popup *xdg_popup;
	struct wl_buffer *buffer;
	int width, height;
	int configure_x, configure_y;
	bool configured;
};

static void
popup_surface_configure(void *data, struct xdg_surface *xdg_surface,
			uint32_t serial)
{
	struct popup *p = data;

	xdg_surface_ack_configure(xdg_surface, serial);
	if (!p->buffer) {
		p->buffer =
			harness_buffer_create(p->h->shm, p->width, p->height);
		wl_surface_attach(p->surface, p->buffer, 0, 0);
		wl_surface_damage_buffer(p->surface, 0, 0, p->width, p->height);
	}
	wl_surface_commit(p->surface);
	p->configured = true;
}

static const struct xdg_surface_listener popup_surface_listener = {
	popup_surface_configure,
};

static void
popup_popup_configure(void *data, struct xdg_popup *xdg_popup, int32_t x,
		      int32_t y, int32_t width, int32_t height)
{
	struct popup *p = data;

	p->configure_x = x;
	p->configure_y = y;
}

static const struct xdg_popup_listener popup_popup_listener = {
	popup_popup_configure,
	NULL, /* popup_done */
	NULL, /* repositioned (v3) */
};

/* A popup attached to a layer surface through get_popup (NULL xdg parent):
 * the positioner geometry is an offset from the layer surface's view. */
static struct popup *
popup_create(struct harness *h, struct layer_surface *parent, int x, int y,
	     int width, int height)
{
	struct popup *p = calloc(1, sizeof *p);
	struct xdg_positioner *positioner;
	int i;

	p->h = h;
	p->width = width;
	p->height = height;
	p->surface = wl_compositor_create_surface(h->compositor);
	p->xdg_surface = xdg_wm_base_get_xdg_surface(h->wm_base, p->surface);
	positioner = xdg_wm_base_create_positioner(h->wm_base);
	xdg_positioner_set_size(positioner, width, height);
	xdg_positioner_set_anchor_rect(positioner, 0, 0, 1, 1);
	xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_TOP_LEFT);
	xdg_positioner_set_gravity(positioner,
				   XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
	xdg_positioner_set_offset(positioner, x, y);
	p->xdg_popup = xdg_surface_get_popup(p->xdg_surface, NULL, positioner);
	xdg_surface_add_listener(p->xdg_surface, &popup_surface_listener, p);
	xdg_popup_add_listener(p->xdg_popup, &popup_popup_listener, p);
	zwlr_layer_surface_v1_get_popup(parent->layer_surface, p->xdg_popup);
	wl_surface_commit(p->surface);

	for (i = 0; i < 20 && !p->configured; i++)
		harness_roundtrip(h);
	CHECK(p->configured, "popup: no xdg configure");
	xdg_positioner_destroy(positioner);
	harness_wait_frame(h, p->surface);
	return p;
}

static void
popup_destroy(struct popup *p)
{
	xdg_popup_destroy(p->xdg_popup);
	xdg_surface_destroy(p->xdg_surface);
	wl_surface_destroy(p->surface);
	wl_buffer_destroy(p->buffer);
	free(p);
}

static void
check_released(struct harness *h, struct harness_window *w, const char *what)
{
	harness_roundtrip(h);
	CHECK(h->keyboard_focus == w->surface,
	      "%s did not give focus back to the window", what);
	CHECK(harness_window_info(w)->state & STATE_ACTIVE,
	      "window not active again after %s", what);
}

/* An applet popup: on_demand, focused on map, closed by losing focus. */
static void
test_on_demand_release(struct harness *h)
{
	struct harness_window *w = harness_window_create(h, 300, 200, "rel");
	const struct harness_window_info *winfo = harness_window_info(w);
	struct layer_surface *panel, *s;
	struct popup *menu;
	int empty_x = HARNESS_OUTPUT_WIDTH - 5;
	int empty_y = HARNESS_OUTPUT_HEIGHT - 5;

	CHECK(empty_x < winfo->x || empty_x >= winfo->x + winfo->width ||
		      empty_y < winfo->y || empty_y >= winfo->y + winfo->height,
	      "the window covers the empty corner");
	panel = layer_surface_create(h, "rel-panel", LAYER_TOP, NULL,
				     ANCHOR_BOTTOM | ANCHOR_LEFT, -1, -1, -1,
				     -1, -1, -1, KBD_NONE, 200, 50);
	CHECK(h->keyboard_focus == w->surface, "a none panel took focus");

	s = layer_surface_create(h, "rel-popup", LAYER_TOP, NULL,
				 ANCHOR_TOP | ANCHOR_LEFT, -1, -1, -1, -1, -1,
				 -1, KBD_ON_DEMAND, 200, 100);
	harness_roundtrip(h);
	CHECK(h->keyboard_focus == s->surface,
	      "the popup was not focused on map");
	CHECK(!(harness_window_info(w)->state & STATE_ACTIVE),
	      "window still active under the focused popup");

	menu = popup_create(h, s, 50, 25, 120, 60);
	harness_click(h, 50 + 60, 25 + 30);
	CHECK(h->keyboard_focus == s->surface,
	      "a click in the popup's own menu released focus");
	popup_destroy(menu);
	harness_roundtrip(h);
	CHECK(h->keyboard_focus == s->surface,
	      "focus left the popup when its menu went away");

	harness_click(h, 100, HARNESS_OUTPUT_HEIGHT - 25);
	check_released(h, w, "a click on the panel");
	layer_surface_destroy(s);

	s = layer_surface_create(h, "rel-popup", LAYER_TOP, NULL,
				 ANCHOR_TOP | ANCHOR_LEFT, -1, -1, -1, -1, -1,
				 -1, KBD_ON_DEMAND, 200, 100);
	harness_roundtrip(h);
	CHECK(h->keyboard_focus == s->surface,
	      "the popup was not focused again");
	harness_click(h, empty_x, empty_y);
	check_released(h, w, "a click on empty space");
	layer_surface_destroy(s);

	s = layer_surface_create(h, "rel-bottom", 1, NULL,
				 ANCHOR_TOP | ANCHOR_LEFT, -1, -1, -1, -1, -1,
				 -1, KBD_ON_DEMAND, 200, 100);
	harness_roundtrip(h);
	CHECK(h->keyboard_focus == w->surface,
	      "a bottom on_demand surface took focus on map");
	layer_surface_destroy(s);

	layer_surface_destroy(panel);
	harness_window_destroy(w);
}

static void
test_popups(struct harness *h)
{
	struct layer_surface *s = layer_surface_create(
		h, "popup-parent", LAYER_TOP, NULL, ANCHOR_TOP | ANCHOR_LEFT,
		-1, -1, -1, -1, -1, -1, KBD_NONE, 200, 100);
	struct popup *p = popup_create(h, s, 50, 25, 120, 60);

	/* The popup's configure carries the raw positioner geometry; the
	 * shim moves the view to the layer surface + that offset. The
	 * pointer proves where the view actually is. */
	CHECK(p->configure_x == 50 && p->configure_y == 25,
	      "popup configure at %d,%d", p->configure_x, p->configure_y);
	harness_move_pointer(h, 50 + 60, 25 + 30);
	CHECK(h->pointer_focus == p->surface,
	      "popup not at layer surface + positioner offset");

	popup_destroy(p);
	layer_surface_destroy(s);
}

/* ---- Output loss---------------------------------------------------------- */

static void
test_output_loss(struct harness *h)
{
	struct layer_surface *s;

	CHECK(h->n_outputs == 2, "expected two outputs, got %d", h->n_outputs);
	s = layer_surface_create(h, "output-two", LAYER_TOP, h->outputs[1],
				 ANCHOR_TOP | ANCHOR_LEFT, -1, -1, -1, -1, -1,
				 -1, KBD_NONE, 200, 50);
	/* Output 1 is placed to the right of output 0. */
	CHECK(harness_layer_info(h, s->surface)->x == HARNESS_OUTPUT_WIDTH,
	      "surface on output 2 at x %d",
	      harness_layer_info(h, s->surface)->x);

	polo_test_v1_output_connected(h->test, 1, 0);
	for (int i = 0; i < 20 && !s->closed; i++)
		harness_roundtrip(h);
	CHECK(s->closed, "surface did not receive closed on output removal");

	zwlr_layer_surface_v1_destroy(s->layer_surface);
	wl_surface_destroy(s->surface);
	if (s->buffer)
		wl_buffer_destroy(s->buffer);
	free(s);

	/* Bring the output back so the server shuts down in a sane state. */
	polo_test_v1_output_connected(h->test, 1, 1);
	harness_roundtrip(h);
	harness_roundtrip(h);
}

/* ---- Protocol errors (each on a fresh connection) --------------------------
 */

struct raw_conn {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct zwlr_layer_shell_v1 *layer_shell;
	/* Every other proxy, destroyed by raw_close(). */
	void *proxies[8];
	int n_proxies;
};

/* Remember a proxy for raw_close() and return it. */
static void *
raw_track(struct raw_conn *c, void *proxy)
{
	CHECK(c->n_proxies < 8, "raw client: too many proxies");
	c->proxies[c->n_proxies++] = proxy;
	return proxy;
}

static void
raw_registry_global(void *data, struct wl_registry *registry, uint32_t name,
		    const char *interface, uint32_t version)
{
	struct raw_conn *c = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
		c->compositor = wl_registry_bind(registry, name,
						 &wl_compositor_interface, 4);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		c->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
		c->wm_base =
			wl_registry_bind(registry, name, &xdg_wm_base_interface,
					 version < 3 ? version : 3);
	else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
		c->layer_shell = wl_registry_bind(
			registry, name, &zwlr_layer_shell_v1_interface,
			version < 5 ? version : 5);
}

static void
raw_registry_global_remove(void *data, struct wl_registry *registry,
			   uint32_t name)
{
}

static const struct wl_registry_listener raw_registry_listener = {
	raw_registry_global,
	raw_registry_global_remove,
};

/* A fresh client connection to the running Polo. */
static struct raw_conn *
raw_connect(struct harness *h)
{
	struct raw_conn *c = calloc(1, sizeof *c);
	struct wl_registry *registry;

	c->display = wl_display_connect(h->socket);
	CHECK(c->display != NULL, "raw connect: %s", strerror(errno));
	registry = wl_display_get_registry(c->display);
	wl_registry_add_listener(registry, &raw_registry_listener, c);
	wl_display_roundtrip(c->display);
	wl_display_roundtrip(c->display);
	wl_registry_destroy(registry);
	CHECK(c->compositor && c->shm && c->wm_base && c->layer_shell,
	      "raw client: missing globals");
	return c;
}

/* Dispatch until the server kills the client for the protocol error.
 * Returns the wl_display error code (EPROTO). */
static int
raw_expect_error(struct raw_conn *c)
{
	int i;

	for (i = 0; i < 20 && wl_display_get_error(c->display) == 0; i++) {
		if (wl_display_dispatch(c->display) < 0)
			break;
	}
	return wl_display_get_error(c->display);
}

static void
raw_close(struct raw_conn *c)
{
	int i;

	for (i = c->n_proxies - 1; i >= 0; i--)
		wl_proxy_destroy(c->proxies[i]);
	wl_proxy_destroy((struct wl_proxy *)c->layer_shell);
	wl_proxy_destroy((struct wl_proxy *)c->wm_base);
	wl_proxy_destroy((struct wl_proxy *)c->shm);
	wl_proxy_destroy((struct wl_proxy *)c->compositor);
	wl_display_disconnect(c->display);
	free(c);
}

static void
test_errors(struct harness *h)
{
	struct raw_conn *c;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *ls;
	struct wl_buffer *buffer;

	/* get_layer_surface with an invalid layer. */
	c = raw_connect(h);
	surface = raw_track(c, wl_compositor_create_surface(c->compositor));
	raw_track(c,
		  zwlr_layer_shell_v1_get_layer_surface(c->layer_shell, surface,
							NULL, 4, "bad-layer"));
	CHECK(raw_expect_error(c) == EPROTO, "invalid layer not rejected");
	raw_close(c);

	/* A surface with another role. */
	c = raw_connect(h);
	surface = raw_track(c, wl_compositor_create_surface(c->compositor));
	raw_track(
		c,
		xdg_surface_get_toplevel(raw_track(
			c, xdg_wm_base_get_xdg_surface(c->wm_base, surface))));
	raw_track(c,
		  zwlr_layer_shell_v1_get_layer_surface(c->layer_shell, surface,
							NULL, 0, "role"));
	CHECK(raw_expect_error(c) == EPROTO, "role conflict not rejected");
	raw_close(c);

	/* A surface that already has a buffer. */
	c = raw_connect(h);
	surface = raw_track(c, wl_compositor_create_surface(c->compositor));
	buffer = raw_track(c, harness_buffer_create(c->shm, 16, 16));
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);
	wl_display_roundtrip(c->display);
	raw_track(c,
		  zwlr_layer_shell_v1_get_layer_surface(c->layer_shell, surface,
							NULL, 0, "buffer"));
	CHECK(raw_expect_error(c) == EPROTO, "buffer not rejected");
	raw_close(c);

	/* get_layer_surface twice on one surface. */
	c = raw_connect(h);
	surface = raw_track(c, wl_compositor_create_surface(c->compositor));
	raw_track(c,
		  zwlr_layer_shell_v1_get_layer_surface(c->layer_shell, surface,
							NULL, 0, "first"));
	raw_track(c,
		  zwlr_layer_shell_v1_get_layer_surface(c->layer_shell, surface,
							NULL, 0, "second"));
	CHECK(raw_expect_error(c) == EPROTO, "second role not rejected");
	raw_close(c);

	/* Invalid anchor bits. */
	c = raw_connect(h);
	surface = raw_track(c, wl_compositor_create_surface(c->compositor));
	ls = raw_track(c,
		       zwlr_layer_shell_v1_get_layer_surface(
			       c->layer_shell, surface, NULL, 0, "anchor"));
	zwlr_layer_surface_v1_set_anchor(ls, 16);
	CHECK(raw_expect_error(c) == EPROTO, "invalid anchor not rejected");
	raw_close(c);

	/* Invalid keyboard interactivity. */
	c = raw_connect(h);
	surface = raw_track(c, wl_compositor_create_surface(c->compositor));
	ls = raw_track(c,
		       zwlr_layer_shell_v1_get_layer_surface(
			       c->layer_shell, surface, NULL, 0, "kbd"));
	zwlr_layer_surface_v1_set_keyboard_interactivity(ls, 3);
	CHECK(raw_expect_error(c) == EPROTO,
	      "invalid keyboard interactivity not rejected");
	raw_close(c);

	/* Invalid exclusive edge (not a single anchor bit). */
	c = raw_connect(h);
	surface = raw_track(c, wl_compositor_create_surface(c->compositor));
	ls = raw_track(c,
		       zwlr_layer_shell_v1_get_layer_surface(
			       c->layer_shell, surface, NULL, 0, "edge"));
	zwlr_layer_surface_v1_set_exclusive_edge(ls, ANCHOR_TOP | ANCHOR_LEFT);
	CHECK(raw_expect_error(c) == EPROTO, "invalid edge not rejected");
	raw_close(c);

	/* A zero size without both opposite edges anchored. */
	c = raw_connect(h);
	surface = raw_track(c, wl_compositor_create_surface(c->compositor));
	ls = raw_track(c,
		       zwlr_layer_shell_v1_get_layer_surface(
			       c->layer_shell, surface, NULL, 0, "size"));
	zwlr_layer_surface_v1_set_size(ls, 0, 0);
	wl_surface_commit(surface);
	CHECK(raw_expect_error(c) == EPROTO, "invalid size not rejected");
	raw_close(c);

	/* A zero width anchored top and bottom only (the axes must not be
	 * mixed up). */
	c = raw_connect(h);
	surface = raw_track(c, wl_compositor_create_surface(c->compositor));
	ls = raw_track(c,
		       zwlr_layer_shell_v1_get_layer_surface(
			       c->layer_shell, surface, NULL, 0, "size-axis"));
	zwlr_layer_surface_v1_set_anchor(ls, ANCHOR_TOP | ANCHOR_BOTTOM);
	zwlr_layer_surface_v1_set_size(ls, 0, 50);
	wl_surface_commit(surface);
	CHECK(raw_expect_error(c) == EPROTO, "zero width on the wrong axis");
	raw_close(c);

	/* An exclusive edge that is not part of the anchor. */
	c = raw_connect(h);
	surface = raw_track(c, wl_compositor_create_surface(c->compositor));
	ls = raw_track(c,
		       zwlr_layer_shell_v1_get_layer_surface(c->layer_shell,
							     surface, NULL, 0,
							     "edge-anchor"));
	zwlr_layer_surface_v1_set_exclusive_edge(ls, ANCHOR_TOP);
	wl_surface_commit(surface);
	CHECK(raw_expect_error(c) == EPROTO,
	      "exclusive edge without anchor not rejected");
	raw_close(c);

	/* A buffer commit before acking the configure. */
	c = raw_connect(h);
	surface = raw_track(c, wl_compositor_create_surface(c->compositor));
	ls = raw_track(c,
		       zwlr_layer_shell_v1_get_layer_surface(
			       c->layer_shell, surface, NULL, 0, "ack"));
	zwlr_layer_surface_v1_set_size(ls, 200, 50);
	wl_surface_commit(surface);
	wl_display_roundtrip(c->display); /* configures arrive, unacked */
	buffer = raw_track(c, harness_buffer_create(c->shm, 200, 50));
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);
	CHECK(raw_expect_error(c) == EPROTO, "buffer before ack not rejected");
	raw_close(c);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	int status;

	/* The output-loss test needs the second output from the start. */
	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	h = harness_start(argc, argv);

	test_anchors(h);
	test_set_layer(h);
	test_unmap_remap(h);
	test_zero_size(h);
	test_margins(h);
	test_exclusive_zones(h);
	test_keyboard_modes(h);
	test_popups(h);
	test_on_demand_release(h);
	test_errors(h);
	test_output_loss(h);

	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
