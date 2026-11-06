/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Magic Lamp on Pixman, which cannot deform views: minimising fades the
 * window out over 150 ms, with focus moved at once; restoring fades it back
 * in; with animations disabled it just goes.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "harness.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define WINDOW_COLOUR 0x3070c0u

static struct zwlr_foreign_toplevel_manager_v1 *manager;
static struct zwlr_foreign_toplevel_handle_v1 *handle;

static void
handle_noop(void *data, struct zwlr_foreign_toplevel_handle_v1 *h)
{
}

static void
handle_string(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	      const char *value)
{
}

static void
handle_output(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	      struct wl_output *output)
{
}

static void
handle_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	     struct wl_array *state)
{
}

static void
handle_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
	      struct zwlr_foreign_toplevel_handle_v1 *parent)
{
}

static const struct zwlr_foreign_toplevel_handle_v1_listener handle_listener = {
	handle_string, handle_string, handle_output, handle_output,
	handle_state,  handle_noop,   handle_noop,   handle_parent,
};

static void
manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *m,
		 struct zwlr_foreign_toplevel_handle_v1 *h)
{
	CHECK(!handle, "more than one toplevel");
	handle = h;
	zwlr_foreign_toplevel_handle_v1_add_listener(h, &handle_listener, NULL);
}

static void
manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *m)
{
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
	manager_toplevel,
	manager_finished,
};

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) ==
	    0) {
		manager = wl_registry_bind(
			registry, name, &zwlr_foreign_toplevel_manager_v1_interface,
			3);
		zwlr_foreign_toplevel_manager_v1_add_listener(
			manager, &manager_listener, NULL);
	}
}

static void
registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_remove,
};

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static uint32_t
pixel_at(struct harness *h, int x, int y)
{
	int width, height;
	uint32_t *pixels = harness_capture(h, &width, &height);
	uint32_t pixel;

	CHECK(pixels, "cannot capture");
	pixel = pixels[y * width + x] & 0xffffff;
	free(pixels);
	return pixel;
}

static bool
between(uint32_t pixel, uint32_t a, uint32_t b)
{
	bool blended = false;

	for (int shift = 0; shift < 24; shift += 8) {
		int p = (pixel >> shift) & 0xff, ca = (a >> shift) & 0xff,
		    cb = (b >> shift) & 0xff;
		int lo = ca < cb ? ca : cb, hi = ca < cb ? cb : ca;

		if (lo == hi)
			continue;
		if (p <= lo || p >= hi)
			return false;
		blended = true;
	}
	return blended;
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);

	harness_set_setting(h, "org.mate.polo.effects", "magic-lamp", "true");
	harness_move_pointer(h, 1000, 740);
	struct harness_window *window =
		harness_window_create(h, 300, 200, "fade");
	const struct harness_window_info *info = harness_window_info(window);
	int cx = info->x + info->width / 2, cy = info->y + info->height / 2;

	CHECK(pixel_at(h, cx, cy) == WINDOW_COLOUR, "the window is not drawn");

	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	harness_roundtrip(h);
	CHECK(manager && handle, "no task-bar handle for the window");

	xdg_toplevel_set_minimized(window->xdg_toplevel);
	harness_roundtrip(h);
	CHECK(h->keyboard_focus != window->surface,
	      "focus stayed on the fading window");
	pause_ms(h, 400);
	uint32_t background = pixel_at(h, cx, cy);

	CHECK(background != WINDOW_COLOUR, "the minimised window is still drawn");

	zwlr_foreign_toplevel_handle_v1_unset_minimized(handle);
	harness_roundtrip(h);
	pause_ms(h, 400);
	CHECK(pixel_at(h, cx, cy) == WINDOW_COLOUR,
	      "the restored window is not opaque");

	xdg_toplevel_set_minimized(window->xdg_toplevel);
	harness_roundtrip(h);
	pause_ms(h, 60);
	uint32_t fading = pixel_at(h, cx, cy);

	CHECK(between(fading, WINDOW_COLOUR, background),
	      "60 ms in the window shows 0x%06x, not between 0x%06x and 0x%06x",
	      fading, WINDOW_COLOUR, background);
	pause_ms(h, 400);
	CHECK(pixel_at(h, cx, cy) == background, "the faded window is still drawn");

	harness_set_setting(h, "org.mate.interface", "enable-animations", "false");
	zwlr_foreign_toplevel_handle_v1_unset_minimized(handle);
	harness_roundtrip(h);
	pause_ms(h, 30);
	CHECK(pixel_at(h, cx, cy) == WINDOW_COLOUR,
	      "without animations the window did not come back at once");
	xdg_toplevel_set_minimized(window->xdg_toplevel);
	harness_roundtrip(h);
	pause_ms(h, 30);
	CHECK(pixel_at(h, cx, cy) == background,
	      "without animations the window did not go at once");

	zwlr_foreign_toplevel_handle_v1_unset_minimized(handle);
	pause_ms(h, 100);
	zwlr_foreign_toplevel_handle_v1_destroy(handle);
	zwlr_foreign_toplevel_manager_v1_destroy(manager);
	wl_registry_destroy(registry);
	harness_window_destroy(window);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
