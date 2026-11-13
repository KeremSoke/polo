/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Popup constraints against headless Polo: popups of layer surfaces
 * (panel menus, through get_popup) flip and slide against
 * the output; popups of windows stay inside the work area, as GTK3 menus
 * do on X11; a flip that would still be constrained is not
 * done (xdg_positioner); scale 2; a desktop menu shows above windows.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input.h>

#include "harness.h"

#define W HARNESS_OUTPUT_WIDTH
#define H HARNESS_OUTPUT_HEIGHT

#define ANCHOR_TOP 1
#define ANCHOR_BOTTOM 2
#define ANCHOR_LEFT 4
#define ANCHOR_RIGHT 8

#define FLIP_X XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X
#define FLIP_Y XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y
#define SLIDE_X XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X
#define SLIDE_Y XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y
#define RESIZE_Y XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y

/* ---- A layer surface (a panel) ---------------------------------------------
 */

struct panel {
	struct harness *h;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_buffer *buffer;
	int width, height;
	bool mapped;
};

static void
panel_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial,
		uint32_t width, uint32_t height)
{
	struct panel *p = data;

	zwlr_layer_surface_v1_ack_configure(ls, serial);
	if (!p->mapped) {
		p->width = width ? (int)width : p->width;
		p->height = height ? (int)height : p->height;
		p->buffer =
			harness_buffer_create(p->h->shm, p->width, p->height);
		wl_surface_attach(p->surface, p->buffer, 0, 0);
		wl_surface_damage_buffer(p->surface, 0, 0, p->width, p->height);
		p->mapped = true;
	}
	wl_surface_commit(p->surface);
}

static void
panel_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
}

static const struct zwlr_layer_surface_v1_listener panel_listener = {
	panel_configure,
	panel_closed,
};

static struct panel *
layer_create(struct harness *h, uint32_t layer, const char *namespace_,
	     uint32_t anchor, int width, int height, int zone)
{
	struct panel *p = calloc(1, sizeof *p);

	p->h = h;
	p->width = width;
	p->height = height;
	p->surface = wl_compositor_create_surface(h->compositor);
	p->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		h->layer_shell, p->surface, NULL, layer, namespace_);
	zwlr_layer_surface_v1_add_listener(p->layer_surface, &panel_listener,
					   p);
	zwlr_layer_surface_v1_set_anchor(p->layer_surface, anchor);
	zwlr_layer_surface_v1_set_size(p->layer_surface, width, height);
	zwlr_layer_surface_v1_set_exclusive_zone(p->layer_surface, zone);
	wl_surface_commit(p->surface);
	for (int i = 0; i < 20 && !harness_layer_info(h, p->surface)->mapped;
	     i++)
		harness_roundtrip(h);
	CHECK(harness_layer_info(h, p->surface)->mapped, "panel not mapped");
	harness_wait_frame(h, p->surface);
	return p;
}

static struct panel *
panel_create(struct harness *h, uint32_t anchor, int width, int height,
	     int zone)
{
	return layer_create(h, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "panel", anchor,
			    width, height, zone);
}

static void
panel_destroy(struct panel *p)
{
	zwlr_layer_surface_v1_destroy(p->layer_surface);
	wl_surface_destroy(p->surface);
	wl_buffer_destroy(p->buffer);
	harness_roundtrip(p->h);
	free(p);
}

/* ---- Popups -------------------------------------------------------------- */

static uint32_t grab_serial;

struct popup {
	struct harness *h;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_popup *xdg_popup;
	struct wl_buffer *buffer;
	int x, y, width, height; /* from xdg_popup.configure */
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
popup_configure(void *data, struct xdg_popup *xdg_popup, int32_t x, int32_t y,
		int32_t width, int32_t height)
{
	struct popup *p = data;

	p->x = x;
	p->y = y;
	p->width = width;
	p->height = height;
}

static void
popup_done(void *data, struct xdg_popup *xdg_popup)
{
}

static void
popup_repositioned(void *data, struct xdg_popup *xdg_popup, uint32_t token)
{
}

static const struct xdg_popup_listener popup_listener = {
	popup_configure,
	popup_done,
	popup_repositioned,
};

/*
 * A GTK-style menu: anchored to (ax, ay, aw, ah) in the parent, anchor
 * corner and gravity as given. Parent: an xdg_surface, or a panel through
 * get_popup.
 */
static struct popup *
popup_create(struct harness *h, struct xdg_surface *xdg_parent,
	     struct panel *panel, int ax, int ay, int aw, int ah,
	     enum xdg_positioner_anchor anchor,
	     enum xdg_positioner_gravity gravity, uint32_t adjust, int width,
	     int height)
{
	struct popup *p = calloc(1, sizeof *p);
	struct xdg_positioner *positioner;

	p->h = h;
	p->width = width;
	p->height = height;
	p->surface = wl_compositor_create_surface(h->compositor);
	p->xdg_surface = xdg_wm_base_get_xdg_surface(h->wm_base, p->surface);
	xdg_surface_add_listener(p->xdg_surface, &popup_surface_listener, p);
	positioner = xdg_wm_base_create_positioner(h->wm_base);
	xdg_positioner_set_size(positioner, width, height);
	xdg_positioner_set_anchor_rect(positioner, ax, ay, aw, ah);
	xdg_positioner_set_anchor(positioner, anchor);
	xdg_positioner_set_gravity(positioner, gravity);
	xdg_positioner_set_constraint_adjustment(positioner, adjust);
	p->xdg_popup =
		xdg_surface_get_popup(p->xdg_surface, xdg_parent, positioner);
	xdg_popup_add_listener(p->xdg_popup, &popup_listener, p);
	if (panel)
		zwlr_layer_surface_v1_get_popup(panel->layer_surface,
						p->xdg_popup);
	if (grab_serial)
		xdg_popup_grab(p->xdg_popup, h->seat, grab_serial);
	xdg_positioner_destroy(positioner);
	wl_surface_commit(p->surface);
	for (int i = 0; i < 20 && !p->configured; i++)
		harness_roundtrip(h);
	CHECK(p->configured, "popup not configured");
	/* No frame wait: a popup kept off the screen never repaints. */
	return p;
}

static void
popup_destroy(struct popup *p)
{
	xdg_popup_destroy(p->xdg_popup);
	xdg_surface_destroy(p->xdg_surface);
	wl_surface_destroy(p->surface);
	if (p->buffer)
		wl_buffer_destroy(p->buffer);
	harness_roundtrip(p->h);
	free(p);
}

/* ---- Tests -----------------------------------------------------------------
 */

/* A bottom panel's menu flips up; a menu at the right edge slides left. */
static void
test_panel_menus(struct harness *h)
{
	struct panel *panel = panel_create(
		h, ANCHOR_BOTTOM | ANCHOR_LEFT | ANCHOR_RIGHT, 0, 30, 30);
	struct popup *p;
	int py = H - 30;

	/* A menu button at x 10..60 on the panel; the menu opens below it,
	 * off the screen, so it must flip above the panel. */
	p = popup_create(h, NULL, panel, 10, 0, 50, 30,
			 XDG_POSITIONER_ANCHOR_BOTTOM_LEFT,
			 XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT, FLIP_Y | SLIDE_X,
			 200, 300);
	CHECK(p->x == 10 && p->y == -300,
	      "bottom panel menu at %d,%d, expected 10,-300", p->x, p->y);
	/* And it is really there: the pointer finds it. */
	harness_wait_frame(h, p->surface);
	harness_move_pointer(h, 10 + 100, py - 150);
	CHECK(h->pointer_focus == p->surface, "flipped menu not on screen");
	popup_destroy(p);

	/* A button at the right edge: the menu slides left onto the screen. */
	p = popup_create(h, NULL, panel, W - 40, 0, 40, 30,
			 XDG_POSITIONER_ANCHOR_TOP_LEFT,
			 XDG_POSITIONER_GRAVITY_TOP_RIGHT, FLIP_Y | SLIDE_X,
			 200, 300);
	CHECK(p->x == W - 200 && p->y == -300,
	      "right-edge menu at %d,%d, expected %d,-300", p->x, p->y,
	      W - 200);
	popup_destroy(p);

	/* Too tall for either side: flipping would still be constrained, so
	 * the menu stays where it was (xdg_positioner). */
	p = popup_create(h, NULL, panel, 10, 0, 50, 30,
			 XDG_POSITIONER_ANCHOR_BOTTOM_LEFT,
			 XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT, FLIP_Y, 200,
			 H + 100);
	CHECK(p->y == 30, "over-tall menu flipped to y %d", p->y);
	popup_destroy(p);

	panel_destroy(panel);
}

/* A window's menu does not reach under a bottom panel: it stays inside
 * the work area, so it flips up (GTK3 on X11 does the same). */
static void
test_window_menu_work_area(struct harness *h)
{
	struct panel *panel = panel_create(
		h, ANCHOR_BOTTOM | ANCHOR_LEFT | ANCHOR_RIGHT, 0, 40, 40);
	struct harness_window *w;
	const struct harness_window_info *info;
	struct popup *p;
	int bottom;

	w = harness_window_create(h, 300, 200, "menu-owner");
	info = harness_window_info(w);
	/* Anchor 20 px above the work area's bottom: a 60 px menu below it
	 * would end 40 px into the panel. */
	bottom = H - 40 - info->y - 20;
	p = popup_create(h, w->xdg_surface, NULL, 10, bottom, 50, 1,
			 XDG_POSITIONER_ANCHOR_BOTTOM_LEFT,
			 XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT, FLIP_Y | SLIDE_Y,
			 100, 60);
	CHECK(info->y + p->y + p->height <= H - 40,
	      "window menu reaches y %d, under the panel at %d",
	      info->y + p->y + p->height, H - 40);
	CHECK(p->y == bottom - 60,
	      "window menu at y %d, expected flip "
	      "to %d",
	      p->y, bottom - 60);
	popup_destroy(p);

	/* A panel's own menu may still cover the panel area. */
	p = popup_create(h, NULL, panel, 10, 0, 50, 40,
			 XDG_POSITIONER_ANCHOR_TOP_LEFT,
			 XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT, FLIP_Y, 100, 30);
	CHECK(p->y == 0, "panel menu over the panel moved to y %d", p->y);
	popup_destroy(p);

	harness_window_destroy(w);
	panel_destroy(panel);
}

/* Scale 2: the same rules in logical coordinates. */
static void
test_scale(struct harness *h)
{
	struct panel *panel;
	struct popup *p;

	polo_test_v1_configure_output(h->test, 0, 2, 0, 0, 0);
	for (int i = 0; i < 4; i++)
		harness_roundtrip(h);
	panel = panel_create(h, ANCHOR_BOTTOM | ANCHOR_LEFT | ANCHOR_RIGHT, 0,
			     30, 30);
	p = popup_create(h, NULL, panel, 10, 0, 50, 30,
			 XDG_POSITIONER_ANCHOR_BOTTOM_LEFT,
			 XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT, FLIP_Y | SLIDE_X,
			 200, 150);
	CHECK(p->y == -150, "scale 2: menu at y %d, expected -150", p->y);
	popup_destroy(p);

	/* Logical width W/2: a right-edge menu slides to W/2 - 200. */
	p = popup_create(h, NULL, panel, W / 2 - 40, 0, 40, 30,
			 XDG_POSITIONER_ANCHOR_TOP_LEFT,
			 XDG_POSITIONER_GRAVITY_TOP_RIGHT, FLIP_Y | SLIDE_X,
			 200, 100);
	CHECK(p->x == W / 2 - 200, "scale 2: menu at x %d, expected %d", p->x,
	      W / 2 - 200);
	popup_destroy(p);
	panel_destroy(panel);

	polo_test_v1_configure_output(h->test, 0, 1, 0, 0, 0);
	for (int i = 0; i < 4; i++)
		harness_roundtrip(h);
}

static void
test_menu_returns_focus(struct harness *h)
{
	struct panel *panel = panel_create(
		h, ANCHOR_TOP | ANCHOR_LEFT | ANCHOR_RIGHT, 0, 30, 30);
	struct harness_window *w = harness_window_create(h, 300, 200, "typing");
	struct popup *p;

	CHECK(h->keyboard_focus == w->surface, "window not focused");
	harness_move_pointer(h, 20, 10);
	harness_button(h, BTN_LEFT, true);
	CHECK(h->keyboard_focus == w->surface, "panel click took the keyboard");

	grab_serial = h->last_button_serial;
	p = popup_create(h, NULL, panel, 10, 0, 50, 30,
			 XDG_POSITIONER_ANCHOR_BOTTOM_LEFT,
			 XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT, FLIP_Y, 200, 100);
	grab_serial = 0;
	harness_button(h, BTN_LEFT, false);
	harness_wait_frame(h, p->surface);
	CHECK(h->keyboard_focus == p->surface, "menu has no keyboard");

	popup_destroy(p);
	harness_roundtrip(h);
	CHECK(h->keyboard_focus == w->surface,
	      "keyboard not back on the window after the menu closed");
	harness_key(h, 30, true);
	harness_key(h, 30, false);
	CHECK(h->last_key_surface == w->surface,
	      "key did not reach the window");

	harness_window_destroy(w);
	panel_destroy(panel);
}

/* A desktop menu (caja's, through get_popup on a bottom-layer surface)
 * shows above the windows covering the desktop, not under them. */
static void
test_desktop_menu_above_windows(struct harness *h)
{
	struct panel *desktop = layer_create(
		h, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "desktop",
		ANCHOR_TOP | ANCHOR_BOTTOM | ANCHOR_LEFT | ANCHOR_RIGHT, 0, 0,
		0);
	struct harness_window *w =
		harness_window_create(h, W - 100, H - 100, "covering");
	struct popup *p;

	harness_move_pointer(h, W / 2, H / 2);
	CHECK(h->pointer_focus == w->surface,
	      "the window does not cover the desktop");
	p = popup_create(h, NULL, desktop, W / 2 - 50, H / 2 - 50, 1, 1,
			 XDG_POSITIONER_ANCHOR_TOP_LEFT,
			 XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT, FLIP_Y | SLIDE_X,
			 200, 200);
	harness_wait_frame(h, p->surface);
	harness_move_pointer(h, W / 2, H / 2);
	CHECK(h->pointer_focus == p->surface,
	      "the desktop menu is under the window");
	popup_destroy(p);
	harness_move_pointer(h, W / 2, H / 2);
	CHECK(h->pointer_focus == w->surface,
	      "the window is not back on top after the menu closed");
	harness_window_destroy(w);
	panel_destroy(desktop);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	int status;

	test_panel_menus(h);
	test_window_menu_work_area(h);
	test_scale(h);
	test_menu_returns_focus(h);
	test_desktop_menu_above_windows(h);

	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
