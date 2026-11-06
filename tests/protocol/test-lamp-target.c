/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Magic Lamp targets (D13): a task bar's set_rectangle on its panel
 * surface becomes the target in global coordinates, funnelling toward the
 * panel's edge; without it the pointer is the target, moved to the nearest
 * window border when it is inside the window, and the funnel leaves toward
 * the panel under it or the nearest output edge.
 */

#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define PANEL_HEIGHT 30

enum { BOTTOM, TOP, LEFT, RIGHT };
enum { PANEL, ICON_GEOMETRY, POINTER };

static struct zwlr_foreign_toplevel_manager_v1 *manager;
static struct zwlr_foreign_toplevel_handle_v1 *handles[16];
static char *titles[16];
static int n_handles;
static uint32_t panel_width, panel_height;

static void
handle_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
	     const char *title)
{
	for (int i = 0; i < n_handles; i++) {
		if (handles[i] == handle) {
			free(titles[i]);
			titles[i] = strdup(title);
		}
	}
}

static void
handle_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
	      const char *app_id)
{
}

static void
handle_output(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
	      struct wl_output *output)
{
}

static void
handle_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
	     struct wl_array *state)
{
}

static void
handle_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle)
{
}

static void
handle_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle)
{
}

static void
handle_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
	      struct zwlr_foreign_toplevel_handle_v1 *parent)
{
}

static const struct zwlr_foreign_toplevel_handle_v1_listener handle_listener = {
	handle_title,  handle_app_id, handle_output, handle_output,
	handle_state,  handle_done,   handle_closed, handle_parent,
};

static void
manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *m,
		 struct zwlr_foreign_toplevel_handle_v1 *handle)
{
	CHECK(n_handles < 16, "too many toplevels");
	handles[n_handles++] = handle;
	zwlr_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener,
						     NULL);
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
panel_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
		uint32_t serial, uint32_t width, uint32_t height)
{
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
	panel_width = width;
	panel_height = height;
}

static void
panel_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
}

static const struct zwlr_layer_surface_v1_listener panel_listener = {
	panel_configure,
	panel_closed,
};

static struct zwlr_foreign_toplevel_handle_v1 *
handle_for(struct harness *h, const char *title)
{
	for (int attempt = 0; attempt < 20; attempt++) {
		for (int i = 0; i < n_handles; i++) {
			if (titles[i] && strcmp(titles[i], title) == 0)
				return handles[i];
		}
		harness_roundtrip(h);
	}
	return NULL;
}

static void
expect(const struct harness_lamp_target *t, int x, int y, int width,
       int height, uint32_t edge, uint32_t source, const char *what)
{
	CHECK(t->known, "%s: no lamp target", what);
	CHECK(t->x == x && t->y == y && t->width == width &&
		      t->height == height && t->edge == edge &&
		      t->source == source,
	      "%s: %d,%d %dx%d edge %u source %u", what, t->x, t->y, t->width,
	      t->height, t->edge, t->source);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);

	harness_move_pointer(h, 512, 300);
	struct harness_window *window =
		harness_window_create(h, 400, 300, "lamp");
	const struct harness_window_info *info = harness_window_info(window);

	CHECK(info->known, "no window geometry");
	int wx = info->x, wy = info->y, ww = info->width, wh = info->height;

	struct wl_surface *panel = wl_compositor_create_surface(h->compositor);
	struct zwlr_layer_surface_v1 *panel_layer =
		zwlr_layer_shell_v1_get_layer_surface(
			h->layer_shell, panel, h->outputs[0],
			ZWLR_LAYER_SHELL_V1_LAYER_TOP, "panel");

	zwlr_layer_surface_v1_add_listener(panel_layer, &panel_listener, NULL);
	zwlr_layer_surface_v1_set_anchor(panel_layer,
					 ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
						 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
						 ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(panel_layer, 0, PANEL_HEIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(panel_layer, PANEL_HEIGHT);
	wl_surface_commit(panel);
	harness_roundtrip(h);
	CHECK(panel_width > 0, "the panel was not configured");
	struct wl_buffer *buffer =
		harness_buffer_create(h->shm, (int)panel_width, PANEL_HEIGHT);

	wl_surface_attach(panel, buffer, 0, 0);
	wl_surface_damage_buffer(panel, 0, 0, (int)panel_width, PANEL_HEIGHT);
	harness_wait_frame(h, panel);
	const struct harness_layer_info *layer = harness_layer_info(h, panel);

	CHECK(layer->mapped, "the panel did not map");
	int px = layer->x, py = layer->y;

	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(manager, "no zwlr_foreign_toplevel_manager_v1");
	struct zwlr_foreign_toplevel_handle_v1 *handle = handle_for(h, "lamp");

	CHECK(handle, "the window has no task-bar handle");

	zwlr_foreign_toplevel_handle_v1_set_rectangle(handle, panel, 100, 4,
						      160, 22);
	harness_roundtrip(h);
	expect(harness_lamp_target(window), px + 100, py + 4, 160, 22, BOTTOM,
	       PANEL, "the task-bar button");

	zwlr_foreign_toplevel_handle_v1_set_rectangle(handle, panel, 0, 0, 0,
						      0);
	harness_move_pointer(h, wx + 5, wy + wh / 2);
	expect(harness_lamp_target(window), wx, wy + wh / 2, 0, 0, LEFT,
	       POINTER, "the pointer inside the window");

	harness_move_pointer(h, 900, py + 10);
	expect(harness_lamp_target(window), 900, py + 10, 0, 0, BOTTOM,
	       POINTER, "the pointer over the panel");

	CHECK(wx + ww < 1000, "the window reaches the right edge");
	harness_move_pointer(h, 1015, 300);
	expect(harness_lamp_target(window), 1015, 300, 0, 0, RIGHT, POINTER,
	       "the pointer near the right edge");

	for (int i = 0; i < n_handles; i++) {
		zwlr_foreign_toplevel_handle_v1_destroy(handles[i]);
		free(titles[i]);
	}
	zwlr_foreign_toplevel_manager_v1_destroy(manager);
	wl_registry_destroy(registry);
	zwlr_layer_surface_v1_destroy(panel_layer);
	wl_surface_destroy(panel);
	wl_buffer_destroy(buffer);
	harness_window_destroy(window);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
