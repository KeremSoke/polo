/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tests moving, resizing, maximizing, and closing through server-side frame
 * pointer input.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "harness.h"
#include "server-decoration-client-protocol.h"

static struct org_kde_kwin_server_decoration_manager *kde_manager;

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface,
		   org_kde_kwin_server_decoration_manager_interface.name) == 0)
		kde_manager = wl_registry_bind(
			registry, name,
			&org_kde_kwin_server_decoration_manager_interface, 1);
}

static void
registry_global_remove(void *data, struct wl_registry *registry,
		       uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove,
};

static void
settle(struct harness *h)
{
	for (int i = 0; i < 6; i++)
		harness_roundtrip(h);
}

static void
drag(struct harness *h, int x, int y, int dx, int dy)
{
	harness_move_pointer(h, x, y);
	harness_button(h, BTN_LEFT, true);
	for (int i = 1; i <= 4; i++)
		harness_move_pointer(h, x + dx * i / 4, y + dy * i / 4);
	harness_button(h, BTN_LEFT, false);
	settle(h);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w;
	const struct harness_window_info *info;
	int x, y, width, height;

	if (access("/usr/share/themes/Atlanta/metacity-1", F_OK) != 0) {
		printf("Atlanta is not installed\n");
		return 77;
	}
	setenv("POLO_TEST_PREFS",
	       "general/theme='Atlanta';general/button-layout=':close'", 1);
	h = harness_start(argc, argv);

	w = harness_window_create_decorated(h, 300, 200, "framed");
	settle(h);
	info = harness_window_info(w);
	x = info->x;
	y = info->y;
	CHECK(y > 10, "the client is at %d,%d, under no titlebar", x, y);

	drag(h, x + 40, y - 8, 120, 60);
	info = harness_window_info(w);
	CHECK(info->x == x + 120 && info->y == y + 60,
	      "a titlebar drag from %d,%d moved the window to %d,%d", x, y,
	      info->x, info->y);
	x = info->x;
	y = info->y;
	width = info->width;
	height = info->height;

	drag(h, x + width + 4, y + height + 4, 50, 30);
	info = harness_window_info(w);
	CHECK(info->x == x && info->y == y && w->width == width + 50 &&
		      w->height == height + 30,
	      "a corner drag made %d,%d %dx%d of %d,%d %dx%d", info->x,
	      info->y, w->width, w->height, x, y, width, height);

	harness_click(h, x + 40, y - 8);
	harness_click(h, x + 40, y - 8);
	settle(h);
	info = harness_window_info(w);
	CHECK(info->width > width + 50 && info->y < y,
	      "a titlebar double-click left the window at %d,%d %dx%d",
	      info->x, info->y, info->width, info->height);
	harness_click(h, 40, info->y - 8);
	harness_click(h, 40, info->y - 8);
	settle(h);
	info = harness_window_info(w);
	CHECK(info->x == x && info->y == y,
	      "a second double-click left the window at %d,%d, not %d,%d",
	      info->x, info->y, x, y);

	CHECK(!w->close_requested, "the window was asked to close early");
	harness_click(h, x + info->width - 8, y - 8);
	settle(h);
	CHECK(w->close_requested, "the close button did not close the window");

	harness_window_destroy(w);
	harness_roundtrip(h);

	/* GTK 3 creates its KDE decoration on wl_surface, then destroys only the
	 * toplevel. Its frame must still disappear. */
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(kde_manager != NULL, "no org_kde_kwin_server_decoration_manager");
	struct harness_window *below =
		harness_window_create(h, 700, 500, "below");
	settle(h);
	w = harness_window_create(h, 300, 200, "kde");
	struct org_kde_kwin_server_decoration *kde =
		org_kde_kwin_server_decoration_manager_create(kde_manager,
							       w->surface);
	org_kde_kwin_server_decoration_request_mode(
		kde, ORG_KDE_KWIN_SERVER_DECORATION_MODE_SERVER);
	settle(h);
	harness_window_draw_pending(w);
	settle(h);
	info = harness_window_info(w);
	x = info->x;
	y = info->y;
	harness_move_pointer(h, x + 40, y - 8);
	CHECK(h->pointer_focus != below->surface,
	      "the titlebar does not cover the window below");

	/* GTK retains wl_surface for a later remap. */
	CHECK(h->windows[h->n_windows - 1] == w, "the KDE window is not last");
	h->n_windows--;
	xdg_toplevel_destroy(w->xdg_toplevel);
	xdg_surface_destroy(w->xdg_surface);
	settle(h);
	usleep(50000);
	harness_move_pointer(h, x + 41, y - 8);
	harness_move_pointer(h, x + 40, y - 8);
	CHECK(h->pointer_focus == below->surface,
	      "the frame of a destroyed window still takes the pointer");
	org_kde_kwin_server_decoration_release(kde);
	wl_surface_destroy(w->surface);
	wl_buffer_destroy(w->buffer);
	free(w);
	org_kde_kwin_server_decoration_manager_destroy(kde_manager);
	wl_registry_destroy(registry);
	harness_window_destroy(below);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
