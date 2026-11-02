/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "xfce-foreign-toplevel-management-private-v1-client-protocol.h"
#include "xfce-output-private-v1-client-protocol.h"

#define W HARNESS_OUTPUT_WIDTH
#define H HARNESS_OUTPUT_HEIGHT
#define BIT(s) (1u << XFCE_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_##s)

struct toplevel {
	struct zwlr_foreign_toplevel_handle_v1 *wlr;
	struct xfce_foreign_toplevel_handle_v1 *xfce;
	char title[64];
	int dones;
	uint32_t xfce_state;
	int no_icons;
	bool closed;
};

static struct toplevel toplevels[8];
static int n_toplevels;
static uint32_t wlr_name, xfce_toplevel_name, xfce_output_name;

static void
wlr_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	  const char *title)
{
	struct toplevel *t = data;

	snprintf(t->title, sizeof t->title, "%s", title);
}

static void
wlr_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	   const char *app_id)
{
}

static void
wlr_output_enter(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
		 struct wl_output *output)
{
}

static void
wlr_output_leave(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
		 struct wl_output *output)
{
}

static void
wlr_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	  struct wl_array *state)
{
}

static void
wlr_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *h)
{
	struct toplevel *t = data;

	t->dones++;
}

static void
wlr_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *h)
{
	struct toplevel *t = data;

	t->closed = true;
}

static void
wlr_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	   struct zwlr_foreign_toplevel_handle_v1 *parent)
{
}

static const struct zwlr_foreign_toplevel_handle_v1_listener wlr_listener = {
	wlr_title, wlr_app_id, wlr_output_enter, wlr_output_leave,
	wlr_state, wlr_done,   wlr_closed,       wlr_parent,
};

static void
manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *m,
		 struct zwlr_foreign_toplevel_handle_v1 *handle)
{
	struct toplevel *t = &toplevels[n_toplevels++];

	t->wlr = handle;
	zwlr_foreign_toplevel_handle_v1_add_listener(handle, &wlr_listener, t);
}

static void
manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *m)
{
}

static const struct zwlr_foreign_toplevel_manager_v1_listener
	manager_listener = {
		manager_toplevel,
		manager_finished,
	};

static void
xfce_state(void *data, struct xfce_foreign_toplevel_handle_v1 *h,
	   struct wl_array *state)
{
	struct toplevel *t = data;
	uint32_t *s;

	t->xfce_state = 0;
	wl_array_for_each(s, state) t->xfce_state |= 1u << *s;
}

static void
xfce_icon_name(void *data, struct xfce_foreign_toplevel_handle_v1 *h,
	       const char *name)
{
}

static void
xfce_icon_size(void *data, struct xfce_foreign_toplevel_handle_v1 *h,
	       uint32_t size, uint32_t scale)
{
}

static void
xfce_no_icon(void *data, struct xfce_foreign_toplevel_handle_v1 *h)
{
	struct toplevel *t = data;

	t->no_icons++;
}

static void
xfce_workspace_enter(void *data, struct xfce_foreign_toplevel_handle_v1 *h,
		     struct ext_workspace_handle_v1 *workspace)
{
}

static void
xfce_workspace_leave(void *data, struct xfce_foreign_toplevel_handle_v1 *h,
		     struct ext_workspace_handle_v1 *workspace)
{
}

static const struct xfce_foreign_toplevel_handle_v1_listener xfce_listener = {
	xfce_state,   xfce_icon_name,       xfce_icon_size,
	xfce_no_icon, xfce_workspace_enter, xfce_workspace_leave,
};

struct icon_result {
	bool failed;
	uint32_t reason;
};

static void
icon_pixels(void *data, struct xfce_foreign_toplevel_icon_pixels_v1 *p,
	    int32_t fd, uint32_t width, uint32_t height, uint32_t stride)
{
}

static void
icon_failed(void *data, struct xfce_foreign_toplevel_icon_pixels_v1 *p,
	    uint32_t reason)
{
	struct icon_result *r = data;

	r->failed = true;
	r->reason = reason;
}

static const struct xfce_foreign_toplevel_icon_pixels_v1_listener
	icon_listener = {
		icon_pixels,
		icon_failed,
	};

struct output {
	struct xfce_output_v1 *xfce;
	int x, y, width, height;
	int dones;
	bool primary;
	bool pointer;
};

static struct output outputs[2];

static void
out_serial(void *data, struct xfce_output_v1 *o, const char *serial)
{
}

static void
out_edid(void *data, struct xfce_output_v1 *o, int32_t fd, uint32_t size)
{
}

static void
out_primary(void *data, struct xfce_output_v1 *o)
{
	struct output *out = data;

	for (int i = 0; i < 2; i++)
		outputs[i].primary = false;
	out->primary = true;
}

static void
out_workarea(void *data, struct xfce_output_v1 *o, int32_t x, int32_t y,
	     uint32_t width, uint32_t height)
{
	struct output *out = data;

	out->x = x;
	out->y = y;
	out->width = width;
	out->height = height;
}

static void
out_done(void *data, struct xfce_output_v1 *o)
{
	struct output *out = data;

	out->dones++;
}

static void
out_pointer_enter(void *data, struct xfce_output_v1 *o, struct wl_seat *seat)
{
	struct output *out = data;

	CHECK(!out->pointer, "pointer_enter twice");
	out->pointer = true;
}

static void
out_pointer_leave(void *data, struct xfce_output_v1 *o, struct wl_seat *seat)
{
	struct output *out = data;

	CHECK(out->pointer, "pointer_leave without enter");
	out->pointer = false;
}

static const struct xfce_output_v1_listener output_listener = {
	out_serial, out_edid,          out_primary,       out_workarea,
	out_done,   out_pointer_enter, out_pointer_leave,
};

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (!strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name))
		wlr_name = name;
	else if (!strcmp(interface,
			 xfce_foreign_toplevel_manager_private_v1_interface
				 .name))
		xfce_toplevel_name = name;
	else if (!strcmp(interface,
			 xfce_output_manager_private_v1_interface.name))
		xfce_output_name = name;
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove,
};

static void
panel_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial,
		uint32_t width, uint32_t height)
{
	zwlr_layer_surface_v1_ack_configure(ls, serial);
}

static void
panel_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
}

static const struct zwlr_layer_surface_v1_listener panel_listener = {
	panel_configure,
	panel_closed,
};

static void
settle(struct harness *h)
{
	for (int i = 0; i < 6; i++)
		harness_roundtrip(h);
}

static struct toplevel *
find(const char *title)
{
	for (int i = 0; i < n_toplevels; i++) {
		if (!toplevels[i].closed && !strcmp(toplevels[i].title, title))
			return &toplevels[i];
	}
	return NULL;
}

static void
test_already_bound(struct harness *h)
{
	struct wl_display *display = wl_display_connect(h->socket);
	struct wl_registry *registry = wl_display_get_registry(display);
	struct zwlr_foreign_toplevel_manager_v1 *wlr;
	struct xfce_foreign_toplevel_manager_private_v1 *xfce;
	struct toplevel *first;
	struct xfce_foreign_toplevel_handle_v1 *x1, *x2;
	int i, before = n_toplevels;

	CHECK(display != NULL, "connect: %s", strerror(errno));
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	wlr = wl_registry_bind(registry, wlr_name,
			       &zwlr_foreign_toplevel_manager_v1_interface, 3);
	zwlr_foreign_toplevel_manager_v1_add_listener(wlr, &manager_listener,
						      NULL);
	xfce = wl_registry_bind(
		registry, xfce_toplevel_name,
		&xfce_foreign_toplevel_manager_private_v1_interface, 1);
	wl_display_roundtrip(display);
	CHECK(n_toplevels > before, "second client sees no toplevel");
	first = &toplevels[before];
	x1 = xfce_foreign_toplevel_manager_private_v1_get_xfce_foreign_toplevel_handle(
		xfce, first->wlr);
	x2 = xfce_foreign_toplevel_manager_private_v1_get_xfce_foreign_toplevel_handle(
		xfce, first->wlr);
	for (i = 0; i < 10 && wl_display_get_error(display) == 0; i++)
		if (wl_display_roundtrip(display) < 0)
			break;
	CHECK(wl_display_get_error(display) == EPROTO,
	      "second xfce handle for one toplevel not rejected");
	wl_proxy_destroy((struct wl_proxy *)x1);
	wl_proxy_destroy((struct wl_proxy *)x2);
	for (i = before; i < n_toplevels; i++)
		wl_proxy_destroy((struct wl_proxy *)toplevels[i].wlr);
	wl_proxy_destroy((struct wl_proxy *)xfce);
	wl_proxy_destroy((struct wl_proxy *)wlr);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	memset(&toplevels[before], 0,
	       (n_toplevels - before) * sizeof toplevels[0]);
	n_toplevels = before;
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct wl_registry *registry;
	struct zwlr_foreign_toplevel_manager_v1 *wlr;
	struct xfce_foreign_toplevel_manager_private_v1 *xfce_toplevels;
	struct xfce_output_manager_private_v1 *xfce_outputs;
	struct harness_window *w;
	struct toplevel *t;
	struct icon_result icon = { 0 };
	struct xfce_foreign_toplevel_icon_pixels_v1 *pixels;
	int dones, status;

	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	h = harness_start(argc, argv);
	registry = wl_display_get_registry(h->display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(wlr_name && xfce_toplevel_name && xfce_output_name,
	      "missing globals");
	wlr = wl_registry_bind(registry, wlr_name,
			       &zwlr_foreign_toplevel_manager_v1_interface, 3);
	zwlr_foreign_toplevel_manager_v1_add_listener(wlr, &manager_listener,
						      NULL);
	xfce_toplevels = wl_registry_bind(
		registry, xfce_toplevel_name,
		&xfce_foreign_toplevel_manager_private_v1_interface, 1);
	xfce_outputs =
		wl_registry_bind(registry, xfce_output_name,
				 &xfce_output_manager_private_v1_interface, 1);

	w = harness_window_create(h, 300, 200, "xfce");
	settle(h);
	t = find("xfce");
	CHECK(t != NULL, "window not listed");
	dones = t->dones;
	t->xfce =
		xfce_foreign_toplevel_manager_private_v1_get_xfce_foreign_toplevel_handle(
			xfce_toplevels, t->wlr);
	xfce_foreign_toplevel_handle_v1_add_listener(t->xfce, &xfce_listener,
						     t);
	settle(h);
	CHECK(t->dones == dones + 1, "no second done after the xfce state");
	CHECK(t->xfce_state == 0 && t->no_icons == 1,
	      "initial xfce state 0x%x, no_icon %d", t->xfce_state,
	      t->no_icons);

	xfce_foreign_toplevel_handle_v1_set_sticky(t->xfce);
	settle(h);
	CHECK(t->xfce_state == BIT(STICKY), "sticky: 0x%x", t->xfce_state);
	CHECK(harness_window_info(w)->workspace == -1, "window not sticky");
	xfce_foreign_toplevel_handle_v1_unset_sticky(t->xfce);
	settle(h);
	CHECK(t->xfce_state == 0, "unsticky: 0x%x", t->xfce_state);

	xfce_foreign_toplevel_handle_v1_set_above(t->xfce);
	settle(h);
	CHECK(t->xfce_state == BIT(ABOVE), "above: 0x%x", t->xfce_state);
	dones = t->dones;
	xfce_foreign_toplevel_handle_v1_set_below(t->xfce);
	settle(h);
	CHECK(t->xfce_state == BIT(BELOW), "below did not replace above: 0x%x",
	      t->xfce_state);
	CHECK(t->dones > dones, "state change without done");
	xfce_foreign_toplevel_handle_v1_unset_below(t->xfce);
	settle(h);
	CHECK(t->xfce_state == 0, "unset below: 0x%x", t->xfce_state);

	pixels =
		xfce_foreign_toplevel_handle_v1_get_icon_pixels(t->xfce, 32, 1);
	xfce_foreign_toplevel_icon_pixels_v1_add_listener(
		pixels, &icon_listener, &icon);
	settle(h);
	CHECK(icon.failed &&
		      icon.reason ==
			      XFCE_FOREIGN_TOPLEVEL_ICON_PIXELS_V1_FAILURE_REASON_INVALID_ARGS,
	      "get_icon_pixels did not fail with invalid_args");
	xfce_foreign_toplevel_icon_pixels_v1_destroy(pixels);

	for (int i = 0; i < 2; i++) {
		outputs[i].xfce =
			xfce_output_manager_private_v1_get_xfce_output(
				xfce_outputs, h->outputs[i]);
		xfce_output_v1_add_listener(outputs[i].xfce, &output_listener,
					    &outputs[i]);
	}
	harness_move_pointer(h, 100, 100);
	settle(h);
	CHECK(outputs[0].dones == 1 && outputs[1].dones == 1,
	      "outputs not done: %d %d", outputs[0].dones, outputs[1].dones);
	CHECK(outputs[0].x == 0 && outputs[0].y == 0 && outputs[0].width == W &&
		      outputs[0].height == H,
	      "workarea %d,%d %dx%d", outputs[0].x, outputs[0].y,
	      outputs[0].width, outputs[0].height);
	CHECK(outputs[0].primary != outputs[1].primary, "no single primary");
	CHECK(outputs[0].pointer && !outputs[1].pointer,
	      "pointer not reported on output 0");

	harness_move_pointer(h, W + 100, 100);
	settle(h);
	CHECK(!outputs[0].pointer && outputs[1].pointer,
	      "pointer did not move to output 1");

	{
		struct wl_surface *surface =
			wl_compositor_create_surface(h->compositor);
		struct zwlr_layer_surface_v1 *ls =
			zwlr_layer_shell_v1_get_layer_surface(
				h->layer_shell, surface, h->outputs[0], 2,
				"panel");
		struct wl_buffer *buffer;

		zwlr_layer_surface_v1_add_listener(ls, &panel_listener, NULL);
		zwlr_layer_surface_v1_set_anchor(ls, 1 | 4 | 8);
		zwlr_layer_surface_v1_set_size(ls, 0, 30);
		zwlr_layer_surface_v1_set_exclusive_zone(ls, 30);
		wl_surface_commit(surface);
		settle(h);
		buffer = harness_buffer_create(h->shm, W, 30);
		wl_surface_attach(surface, buffer, 0, 0);
		wl_surface_commit(surface);
		settle(h);
		CHECK(outputs[0].y == 30 && outputs[0].height == H - 30,
		      "workarea after a panel: %d,%d %dx%d", outputs[0].x,
		      outputs[0].y, outputs[0].width, outputs[0].height);
		CHECK(outputs[1].y == 0 && outputs[1].height == H,
		      "other output's workarea changed");
		zwlr_layer_surface_v1_destroy(ls);
		wl_surface_destroy(surface);
		wl_buffer_destroy(buffer);
		settle(h);
	}

	test_already_bound(h);

	harness_window_destroy(w);
	settle(h);
	CHECK(t->closed, "toplevel not closed");
	xfce_foreign_toplevel_handle_v1_destroy(t->xfce);
	for (int i = 0; i < n_toplevels; i++)
		zwlr_foreign_toplevel_handle_v1_destroy(toplevels[i].wlr);
	for (int i = 0; i < 2; i++)
		xfce_output_v1_destroy(outputs[i].xfce);
	xfce_output_manager_private_v1_release(xfce_outputs);
	xfce_foreign_toplevel_manager_private_v1_destroy(xfce_toplevels);
	zwlr_foreign_toplevel_manager_v1_destroy(wlr);
	wl_registry_destroy(registry);

	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
