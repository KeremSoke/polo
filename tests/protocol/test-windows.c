/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Window basics against headless Polo: placement, click-to-focus and
 * raise, typing, focus after close, idle CPU and clean shutdown.
 */

#include <stdio.h>
#include <time.h>

#include <linux/input.h>

#include <unistd.h>

#include "harness.h"

/* Polo.WindowState bits (src/core/window.vala). */
#define STATE_MAPPED (1u << 0)
#define STATE_ACTIVE (1u << 12)

#define LAYER_NORMAL 3

static void
test_map_centered(struct harness *h, struct harness_window *a)
{
	const struct harness_window_info *info = harness_window_info(a);

	CHECK(info->width == 400 && info->height == 300, "size %dx%d",
	      info->width, info->height);
	CHECK(info->x == (HARNESS_OUTPUT_WIDTH - 400) / 2 &&
		      info->y == (HARNESS_OUTPUT_HEIGHT - 300) / 2,
	      "placed at %d,%d", info->x, info->y);
	CHECK(info->layer == LAYER_NORMAL, "layer %d", info->layer);
	CHECK((info->state & STATE_MAPPED) && (info->state & STATE_ACTIVE),
	      "state 0x%x", info->state);
	CHECK(info->focused, "new window not focused");
	CHECK(h->keyboard_focus == a->surface, "no wl_keyboard.enter");
}

static void
test_click_to_focus(struct harness *h, struct harness_window *a,
		    struct harness_window *b)
{
	/* b maps on top of a, both centred; b is smaller. */
	CHECK(h->keyboard_focus == b->surface, "b not focused after mapping");
	CHECK(harness_window_info(b)->stack_index == 1, "b not on top");
	CHECK(harness_window_info(a)->stack_index == 0, "a not below");

	/* A corner of a that b does not cover. */
	harness_click(h, harness_window_info(a)->x + 10,
		      harness_window_info(a)->y + 10);
	CHECK(h->keyboard_focus == a->surface, "click did not focus a");
	CHECK(harness_window_info(a)->focused, "model: a not focused");
	CHECK(!harness_window_info(b)->focused, "model: b still focused");
	CHECK(harness_window_info(a)->stack_index == 1,
	      "click did not raise a");
	CHECK(!(harness_window_info(b)->state & STATE_ACTIVE),
	      "b still active");

	/* The centre is covered by a now; clicking it keeps a. */
	harness_click(h, HARNESS_OUTPUT_WIDTH / 2, HARNESS_OUTPUT_HEIGHT / 2);
	CHECK(h->keyboard_focus == a->surface, "focus moved through a");

	/* Clicking the background changes nothing. */
	harness_click(h, 5, 5);
	CHECK(h->keyboard_focus == a->surface, "background click took focus");
}

static void
test_typing(struct harness *h, struct harness_window *a)
{
	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	CHECK(h->last_key == KEY_A && h->last_key_surface == a->surface,
	      "key went to %p", (void *)h->last_key_surface);
}

static void
test_idle_cpu(struct harness *h)
{
	struct timespec second = { 1, 0 };
	long before = harness_cpu_ticks(h);
	long used;

	nanosleep(&second, NULL);
	used = harness_cpu_ticks(h) - before;
	/* No busy loop: well under 5% of one CPU while nothing happens. */
	CHECK(used <= 5, "%ld ticks of CPU in one idle second", used);
}

static int early_w, early_h;

static void
early_surface_configure(void *data, struct xdg_surface *xdg, uint32_t serial)
{
	*(uint32_t *)data = serial;
}

static const struct xdg_surface_listener early_listener = {
	early_surface_configure,
};

static void
early_toplevel_configure(void *data, struct xdg_toplevel *top, int32_t width,
			 int32_t height, struct wl_array *states)
{
	if (width > 0) {
		early_w = width;
		early_h = height;
	}
}

static void
early_toplevel_close(void *data, struct xdg_toplevel *top)
{
}

static const struct xdg_toplevel_listener early_toplevel_listener = {
	.configure = early_toplevel_configure,
	.close = early_toplevel_close,
};

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct harness_window *a, *b;
	int status;

	a = harness_window_create(h, 400, 300, "a");
	test_map_centered(h, a);

	b = harness_window_create(h, 200, 150, "b");
	test_click_to_focus(h, a, b);
	test_typing(h, a);

	/* Closing the focused window hands focus to the next one down. */
	harness_window_destroy(a);
	CHECK(h->keyboard_focus == b->surface, "focus did not return to b");
	CHECK(harness_window_info(b)->focused, "model: b not focused");

	test_idle_cpu(h);

	/* Maximized at the size it already has, as VS Code is: the client must still
	 * be told the size, or it picks its own and is disconnected for it. */
	struct harness_window *full = harness_window_create(h, 1024, 768, "full");

	full->pending_width = full->pending_height = 0;
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_F10, true);
	harness_key(h, KEY_F10, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_roundtrip(h);
	harness_roundtrip(h);
	CHECK(full->xdg_maximized, "Alt+F10 did not maximize");
	CHECK(full->pending_width == 1024 && full->pending_height == 768,
	      "maximized with a configure of %dx%d", full->pending_width,
	      full->pending_height);
	harness_window_destroy(full);

	/* Asked for before the first frame, as Electron does: the first configure can
	 * carry the state with no size yet, and any size is valid for it. */
	for (int fullscreen = 0; fullscreen < 2; fullscreen++) {
		struct wl_surface *early = wl_compositor_create_surface(h->compositor);
		struct xdg_surface *xdg =
			xdg_wm_base_get_xdg_surface(h->wm_base, early);
		struct xdg_toplevel *top = xdg_surface_get_toplevel(xdg);
		uint32_t serial = 0;

		xdg_surface_add_listener(xdg, &early_listener, &serial);
		xdg_toplevel_add_listener(top, &early_toplevel_listener, NULL);
		if (fullscreen)
			xdg_toplevel_set_fullscreen(top, NULL);
		else
			xdg_toplevel_set_maximized(top);
		wl_surface_commit(early);
		harness_roundtrip(h);
		CHECK(serial != 0, "no first configure");
		xdg_surface_ack_configure(xdg, serial);
		struct wl_buffer *early_buffer = harness_buffer_create(h->shm, 432, 312);

		wl_surface_attach(early, early_buffer, 0, 0);
		wl_surface_commit(early);
		for (int i = 0; i < 100 && early_w == 0; i++) {
			harness_roundtrip(h);
			usleep(10000);
		}
		CHECK(early_w == 1024 && early_h == 768,
		      "the window asked to be %s was offered %dx%d",
		      fullscreen ? "fullscreen" : "maximized", early_w, early_h);
		early_w = early_h = 0;
		xdg_toplevel_destroy(top);
		xdg_surface_destroy(xdg);
		wl_surface_destroy(early);
		wl_buffer_destroy(early_buffer);
		harness_roundtrip(h);
	}

	/* Focus left and came back, so the unmap has no state to change. It still
	 * gets the configure it must wait for. */
	struct harness_window *c = harness_window_create(h, 100, 100, "c");
	struct harness_window *cover = harness_window_create(h, 100, 100, "cover");

	harness_window_destroy(cover);
	harness_roundtrip(h);
	harness_window_unmap(c);
	harness_window_remap(c, 100, 100);
	harness_window_destroy(c);

	harness_window_destroy(b);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
