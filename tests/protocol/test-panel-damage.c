/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A panel that redraws itself (a clock ticking) without changing its size or
 * its reserved strip leaves the windows alone: the repaint covers the panel,
 * not the screen behind it. Measured with polo --stats.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define PANEL_HEIGHT 30

static char log_path[64];
static uint32_t panel_serial;
static bool panel_configured;

static void
layer_configure(void *data, struct zwlr_layer_surface_v1 *surface,
		uint32_t serial, uint32_t width, uint32_t height)
{
	panel_serial = serial;
	panel_configured = true;
}

static void
layer_closed(void *data, struct zwlr_layer_surface_v1 *surface)
{
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	layer_configure,
	layer_closed,
};

static void
pause_ms(struct harness *h, int msec)
{
	struct timespec tick = { 0, 10 * 1000000L };

	for (int i = 0; i < msec / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&tick, NULL);
	}
}

static long
log_size(void)
{
	struct stat st;

	return stat(log_path, &st) == 0 ? (long)st.st_size : 0;
}

static int
max_damage_since(long from, int *lines)
{
	char line[2048];
	FILE *file = fopen(log_path, "r");
	int max = -1;

	*lines = 0;
	if (!file)
		return -1;
	fseek(file, from, SEEK_SET);
	while (fgets(line, sizeof line, file)) {
		const char *at = strstr(line, "stats headless0:");
		const char *damage = at ? strstr(at, "; damage ") : NULL;
		int share;

		if (damage && sscanf(damage, "; damage %d%%", &share) == 1) {
			(*lines)++;
			if (share > max)
				max = share;
		}
	}
	fclose(file);
	return max;
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *panel;
	struct wl_buffer *buffer;
	int lines, before_x, before_y, before_width, before_height, configures;
	long from;

	snprintf(log_path, sizeof log_path, "/tmp/polo-panel-damage-XXXXXX");
	close(mkstemp(log_path));
	setenv("POLO_TEST_LOG", log_path, 1);
	setenv("POLO_STATS_INTERVAL_MS", "250", 1);
	h = harness_start(argc, argv);
	CHECK(h->layer_shell, "no zwlr_layer_shell_v1");

	surface = wl_compositor_create_surface(h->compositor);
	panel = zwlr_layer_shell_v1_get_layer_surface(
		h->layer_shell, surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		"panel");
	zwlr_layer_surface_v1_add_listener(panel, &layer_listener, NULL);
	zwlr_layer_surface_v1_set_anchor(
		panel,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(panel, 0, PANEL_HEIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(panel, PANEL_HEIGHT);
	wl_surface_commit(surface);
	for (int i = 0; i < 50 && !panel_configured; i++)
		harness_roundtrip(h);
	CHECK(panel_configured, "the panel was not configured");
	zwlr_layer_surface_v1_ack_configure(panel, panel_serial);
	buffer = harness_buffer_create(h->shm, HARNESS_OUTPUT_WIDTH,
				       PANEL_HEIGHT);
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);
	harness_wait_frame(h, surface);

	w = harness_window_create(h, 400, 300, "behind the panel");
	xdg_toplevel_set_maximized(w->xdg_toplevel);
	pause_ms(h, 800);
	before_x = harness_window_info(w)->x;
	before_y = harness_window_info(w)->y;
	before_width = harness_window_info(w)->width;
	before_height = harness_window_info(w)->height;
	CHECK(before_y == PANEL_HEIGHT, "the window is not below the panel");
	configures = w->configure_count;

	from = log_size();
	for (int i = 0; i < 10; i++) {
		wl_surface_attach(surface, buffer, 0, 0);
		wl_surface_damage_buffer(surface, 0, 0, 60, PANEL_HEIGHT);
		harness_wait_frame(h, surface);
		pause_ms(h, 100);
	}
	pause_ms(h, 600);

	int max = max_damage_since(from, &lines);

	CHECK(lines > 0, "no stats while the panel redrew");
	CHECK(max >= 0 && max < 20,
	      "a panel redraw repainted %d%% of the screen: the windows were "
	      "placed again although nothing they depend on changed",
	      max);
	CHECK(w->configure_count == configures,
	      "the window got %d configures from a panel redraw",
	      w->configure_count - configures);
	CHECK(harness_window_info(w)->x == before_x &&
		      harness_window_info(w)->y == before_y &&
		      harness_window_info(w)->width == before_width &&
		      harness_window_info(w)->height == before_height,
	      "the window moved");

	zwlr_layer_surface_v1_set_exclusive_zone(panel, PANEL_HEIGHT + 20);
	wl_surface_commit(surface);
	pause_ms(h, 300);
	CHECK(harness_window_info(w)->y == PANEL_HEIGHT + 20,
	      "a new exclusive zone did not move the window (y %d)",
	      harness_window_info(w)->y);

	harness_window_destroy(w);
	zwlr_layer_surface_v1_destroy(panel);
	wl_surface_destroy(surface);
	wl_buffer_destroy(buffer);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	unlink(log_path);
	return 0;
}
