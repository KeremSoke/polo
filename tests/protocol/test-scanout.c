/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Direct scanout eligibility, through polo --stats: a fullscreen window
 * counts as a scanout candidate and nothing of Polo's is composited above it,
 * decorated or not; an overlay surface above it is named as the reason it
 * stays off a plane; a window that is not fullscreen is no candidate at all.
 * The headless backend has no planes, so "not tried" is the best result here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static char log_path[64];

static char *
read_log(void)
{
	static char text[1 << 20];
	FILE *file = fopen(log_path, "r");
	size_t n = 0;

	if (file) {
		n = fread(text, 1, sizeof text - 1, file);
		fclose(file);
	}
	text[n] = '\0';
	return text;
}

static const char *
summaries_while_drawing(struct harness *h, struct wl_surface *surface)
{
	static char lines[1 << 16];
	size_t from = strlen(read_log());
	const char *text, *at;

	for (int i = 0; i < 70; i++) {
		harness_wait_frame(h, surface);
		usleep(16 * 1000);
	}
	text = read_log() + from;
	lines[0] = '\0';
	at = strstr(text, "stats headless0:");
	for (at = at ? strstr(at + 1, "stats headless0:") : NULL; at;
	     at = strstr(at + 1, "stats headless0:")) {
		const char *end = strchr(at, '\n');
		size_t len = end ? (size_t)(end - at) : strlen(at);

		if (strlen(lines) + len + 2 < sizeof lines) {
			strncat(lines, at, len);
			strcat(lines, "\n");
		}
	}
	CHECK(lines[0], "no output summary while drawing");
	return lines;
}

static unsigned
candidates(const char *lines)
{
	unsigned total = 0, on, of;

	for (const char *at = strstr(lines, "top view on a plane "); at;
	     at = strstr(at + 1, "top view on a plane ")) {
		CHECK(sscanf(at, "top view on a plane %u of %u", &on, &of) == 2,
		      "unreadable: %.80s", at);
		total += of;
	}
	return total;
}

static void
make_fullscreen(struct harness *h, struct harness_window *w)
{
	xdg_toplevel_set_fullscreen(w->xdg_toplevel, NULL);
	wl_surface_commit(w->surface);
	for (int i = 0; i < 100 && w->width != HARNESS_OUTPUT_WIDTH; i++) {
		harness_roundtrip(h);
		usleep(10 * 1000);
	}
	CHECK(w->width == HARNESS_OUTPUT_WIDTH &&
		      w->height == HARNESS_OUTPUT_HEIGHT,
	      "the window is %dx%d, not fullscreen", w->width, w->height);
	harness_wait_frame(h, w->surface);
}

static void
expect_clear(struct harness *h, struct harness_window *w, const char *what)
{
	const char *lines = summaries_while_drawing(h, w->surface);

	CHECK(candidates(lines) > 0, "%s is not a scanout candidate:\n%s", what,
	      lines);
	CHECK(!strstr(lines, "under "), "something is composited above %s:\n%s",
	      what, lines);
	CHECK(strstr(lines, "off it: not tried "),
	      "%s has no reason to be off a plane:\n%s", what, lines);
}

static void
layer_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial,
		uint32_t width, uint32_t height)
{
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	*(bool *)data = true;
}

static void
layer_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	layer_configure,
	layer_closed,
};

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w;
	struct wl_surface *osd;
	struct zwlr_layer_surface_v1 *osd_layer;
	struct wl_buffer *osd_buffer;
	bool configured = false;
	const char *lines;

	snprintf(log_path, sizeof log_path, "/tmp/polo-scanout-XXXXXX");
	close(mkstemp(log_path));
	setenv("POLO_TEST_LOG", log_path, 1);
	setenv("POLO_STATS_INTERVAL_MS", "300", 1);
	h = harness_start(argc, argv);
	CHECK(h->layer_shell, "no layer shell");
	harness_move_pointer(h, -2000, -2000);

	w = harness_window_create(h, 400, 300, "windowed");
	lines = summaries_while_drawing(h, w->surface);
	CHECK(candidates(lines) == 0,
	      "a window that is not fullscreen was a scanout candidate:\n%s",
	      lines);

	make_fullscreen(h, w);
	expect_clear(h, w, "a fullscreen window");

	osd = wl_compositor_create_surface(h->compositor);
	osd_layer = zwlr_layer_shell_v1_get_layer_surface(
		h->layer_shell, osd, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		"osd");
	zwlr_layer_surface_v1_add_listener(osd_layer, &layer_listener,
					   &configured);
	zwlr_layer_surface_v1_set_size(osd_layer, 200, 60);
	wl_surface_commit(osd);
	for (int i = 0; i < 50 && !configured; i++)
		harness_roundtrip(h);
	CHECK(configured, "the overlay surface was not configured");
	osd_buffer = harness_buffer_create(h->shm, 200, 60);
	wl_surface_attach(osd, osd_buffer, 0, 0);
	wl_surface_damage_buffer(osd, 0, 0, 200, 60);
	harness_wait_frame(h, osd);
	lines = summaries_while_drawing(h, w->surface);
	CHECK(strstr(lines, "under test-scanout (overlay)"),
	      "an overlay above the fullscreen window was not named:\n%s",
	      lines);
	zwlr_layer_surface_v1_destroy(osd_layer);
	wl_surface_destroy(osd);
	wl_buffer_destroy(osd_buffer);
	harness_roundtrip(h);
	expect_clear(h, w, "a fullscreen window once the overlay went");
	harness_window_destroy(w);

	w = harness_window_create_decorated(h, 400, 300, "decorated");
	make_fullscreen(h, w);
	expect_clear(h, w, "a fullscreen window with Polo's decorations");
	harness_window_destroy(w);

	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	unlink(log_path);
	return 0;
}
