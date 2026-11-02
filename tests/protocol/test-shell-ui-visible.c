/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * argv[2]: polo-shell-ui. Answering is not showing: this reads the output's
 * pixels with weston_capture_v1 and checks that the window menu, from each
 * of its triggers, and the Alt-Tab popup really reach the screen.
 * POLO_TEST_CAPTURE_DIR keeps the pictures as .ppm files.
 */
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "harness.h"
#include "weston-output-capture-client-protocol.h"
#include "xdg-toplevel-icon-v1-client-protocol.h"

static struct weston_capture_v1 *capture;
static struct xdg_toplevel_icon_manager_v1 *icon_manager;
static int cap_width, cap_height;
static int cap_done;

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface, weston_capture_v1_interface.name) == 0)
		capture = wl_registry_bind(registry, name,
					   &weston_capture_v1_interface, 1);
	if (strcmp(interface, xdg_toplevel_icon_manager_v1_interface.name) == 0)
		icon_manager = wl_registry_bind(
			registry, name, &xdg_toplevel_icon_manager_v1_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
source_format(void *data, struct weston_capture_source_v1 *s, uint32_t format)
{
}

static void
source_size(void *data, struct weston_capture_source_v1 *s, int32_t w,
	    int32_t h)
{
	cap_width = w;
	cap_height = h;
}

static void
source_complete(void *data, struct weston_capture_source_v1 *s)
{
	cap_done = 1;
}

static void
source_retry(void *data, struct weston_capture_source_v1 *s)
{
	cap_done = 2;
}

static void
source_failed(void *data, struct weston_capture_source_v1 *s, const char *msg)
{
	cap_done = 3;
}

static const struct weston_capture_source_v1_listener source_listener = {
	.format = source_format,
	.size = source_size,
	.complete = source_complete,
	.retry = source_retry,
	.failed = source_failed,
};

static uint32_t *
screenshot(struct harness *h, const char *name)
{
	struct weston_capture_source_v1 *source = weston_capture_v1_create(
		capture, h->outputs[0], WESTON_CAPTURE_V1_SOURCE_FRAMEBUFFER);
	uint32_t *pixels = NULL;

	weston_capture_source_v1_add_listener(source, &source_listener, NULL);
	harness_roundtrip(h);
	CHECK(cap_width > 0, "no capture size");
	size_t size = (size_t)cap_width * cap_height * 4;
	int fd = memfd_create("capture", MFD_CLOEXEC);

	CHECK(fd >= 0 && ftruncate(fd, size) == 0, "capture buffer");
	pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	struct wl_shm_pool *pool = wl_shm_create_pool(h->shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(
		pool, 0, cap_width, cap_height, cap_width * 4,
		WL_SHM_FORMAT_XRGB8888);

	for (int attempt = 0; attempt < 20; attempt++) {
		cap_done = 0;
		weston_capture_source_v1_capture(source, buffer);
		for (int i = 0; i < 200 && !cap_done; i++) {
			struct timespec ts = { 0, 5 * 1000000L };

			harness_roundtrip(h);
			nanosleep(&ts, NULL);
		}
		if (cap_done == 1)
			break;
	}
	CHECK(cap_done == 1, "capture ended with %d", cap_done);
	wl_buffer_destroy(buffer);
	wl_shm_pool_destroy(pool);
	weston_capture_source_v1_destroy(source);
	close(fd);

	const char *dir = getenv("POLO_TEST_CAPTURE_DIR");

	if (dir) {
		char path[512];

		snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
		FILE *f = fopen(path, "w");

		CHECK(f, "cannot write %s", path);
		fprintf(f, "P6\n%d %d\n255\n", cap_width, cap_height);
		for (int i = 0; i < cap_width * cap_height; i++) {
			fputc((pixels[i] >> 16) & 0xff, f);
			fputc((pixels[i] >> 8) & 0xff, f);
			fputc(pixels[i] & 0xff, f);
		}
		fclose(f);
	}
	return pixels;
}

/* How many pixels of the box differ between two pictures. */
static int
changed(const uint32_t *a, const uint32_t *b, int x, int y, int w, int height)
{
	int n = 0;

	for (int row = y; row < y + height && row < cap_height; row++)
		for (int col = x; col < x + w && col < cap_width; col++)
			if ((a[row * cap_width + col] & 0xffffff) !=
			    (b[row * cap_width + col] & 0xffffff))
				n++;
	return n;
}

static void
settle(struct harness *h, int ms)
{
	struct timespec ts = { 0, 20 * 1000000L };

	for (int i = 0; i < ms / 20; i++) {
		harness_roundtrip(h);
		nanosleep(&ts, NULL);
	}
	harness_wait_repaint(h);
}

/* Without a frame: a window on another workspace gets none. */
static void
pause_ms(struct harness *h, int ms)
{
	struct timespec ts = { 0, 20 * 1000000L };

	for (int i = 0; i < ms / 20; i++) {
		harness_roundtrip(h);
		nanosleep(&ts, NULL);
	}
}

static void
escape(struct harness *h)
{
	harness_key(h, KEY_ESC, true);
	harness_key(h, KEY_ESC, false);
	settle(h, 300);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *a;
	uint32_t *before, *after;

	CHECK(argc >= 3, "usage: %s POLO POLO-SHELL-UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	setenv("POLO_TEST_PREFS", "general/allow-tiling=true", 1);
	h = harness_start(argc, argv);
	for (int i = 0; i < 200 && h->shell_ui_binds == 0; i++)
		settle(h, 20);
	CHECK(h->shell_ui_binds == 1, "polo-shell-ui did not bind");
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(capture, "no weston_capture_v1");

	a = harness_window_create(h, 500, 400, "alpha");
	settle(h, 300);
	const struct harness_window_info *info = harness_window_info(a);
	int x = info->x, y = info->y;

	before = screenshot(h, "before");

	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_SPACE, true);
	harness_key(h, KEY_SPACE, false);
	harness_key(h, KEY_LEFTALT, false);
	after = screenshot(h, "alt-space");
	for (int i = 0; i < 100 && changed(before, after, x, y, 150, 200) <= 5000;
	     i++) {
		settle(h, 100);
		after = screenshot(h, "alt-space");
	}
	CHECK(changed(before, after, x, y, 150, 200) > 5000,
	      "Alt-Space: the menu is not on screen (%d pixels changed)",
	      changed(before, after, x, y, 150, 200));
	escape(h);

	after = screenshot(h, "closed");
	CHECK(changed(before, after, x, y, 150, 200) == 0,
	      "the menu stayed on screen after Escape");

	harness_move_pointer(h, x + 100, y + 100);
	harness_wait_repaint(h);
	harness_key(h, KEY_LEFTALT, true);
	harness_button(h, BTN_MIDDLE, true);
	harness_button(h, BTN_MIDDLE, false);
	harness_key(h, KEY_LEFTALT, false);
	settle(h, 500);
	after = screenshot(h, "alt-middle");
	CHECK(changed(before, after, x + 100, y + 100, 150, 200) > 5000,
	      "Alt-middle: the menu is not on screen");
	escape(h);

	xdg_toplevel_show_window_menu(a->xdg_toplevel, h->seat, 0, 200, 150);
	harness_roundtrip(h);
	settle(h, 500);
	after = screenshot(h, "show-window-menu");
	CHECK(changed(before, after, x + 200, y + 150, 150, 200) > 5000,
	      "xdg_toplevel.show_window_menu: the menu is not on screen");
	escape(h);

	/* Show desktop takes the window off the screen and puts it back. */
	before = screenshot(h, "before-show-desktop");
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_D, true);
	harness_key(h, KEY_D, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	pause_ms(h, 200);
	after = screenshot(h, "show-desktop");
	CHECK(changed(before, after, x, y, 500, 400) > 100000,
	      "show desktop left the window on screen");
	CHECK(!(harness_window_info(a)->state & (1u << 1)),
	      "show desktop minimized the window");
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_D, true);
	harness_key(h, KEY_D, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	pause_ms(h, 200);
	after = screenshot(h, "after-show-desktop");
	CHECK(changed(before, after, x, y, 500, 400) == 0,
	      "the window did not come back");

	/* Dragging to the left edge shows the tile preview beneath the window.
	 */
	harness_move_pointer(h, x + 100, y + 100);
	settle(h, 100);
	uint32_t *clean = screenshot(h, "tile-clean");

	harness_key(h, KEY_LEFTALT, true);
	harness_button(h, BTN_LEFT, true);
	harness_move_pointer(h, 700, 700);
	settle(h, 300);
	const struct harness_window_info *dragged = harness_window_info(a);
	int inside = (dragged->y + 50) * cap_width + dragged->x + 50;

	before = screenshot(h, "tile-no-preview");
	uint32_t window_pixel = before[inside];

	harness_move_pointer(h, 1, 400);
	settle(h, 400);
	dragged = harness_window_info(a);
	CHECK(dragged->x + 400 > 100 && dragged->y < 400,
	      "the window should cover 100,400, it is at %d,%d", dragged->x,
	      dragged->y);
	after = screenshot(h, "tile-preview");
	CHECK(changed(before, after, 10, 10, 490, 270) == 490 * 270,
	      "the left half is not tinted (%d pixels changed)",
	      changed(before, after, 10, 10, 490, 270));
	CHECK(changed(before, after, 530, 10, 450, 270) == 0,
	      "the preview reached the right half");
	CHECK((after[400 * cap_width + 100] & 0xffffff) ==
		      (window_pixel & 0xffffff),
	      "the preview is above the dragged window");

	harness_key(h, KEY_ESC, true);
	harness_key(h, KEY_ESC, false);
	harness_button(h, BTN_LEFT, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_move_pointer(h, x + 100, y + 100);
	settle(h, 200);
	uint32_t *cancelled = screenshot(h, "tile-cancelled");

	CHECK(changed(clean, cancelled, 0, 0, cap_width, cap_height) == 0,
	      "Escape left the preview on screen (%d pixels)",
	      changed(clean, cancelled, 0, 0, cap_width, cap_height));

	harness_key(h, KEY_LEFTALT, true);
	harness_button(h, BTN_LEFT, true);
	harness_move_pointer(h, 1, 400);
	settle(h, 100);
	harness_button(h, BTN_LEFT, false);
	harness_key(h, KEY_LEFTALT, false);
	settle(h, 300);
	CHECK(harness_window_info(a)->x == 0 &&
		      harness_window_info(a)->width == cap_width / 2,
	      "the release did not tile: %d %d", a->info.x, a->info.width);
	before = screenshot(h, "tile-released");
	CHECK((before[100 * cap_width + 700] & 0xffffff) ==
		      (after[100 * cap_width + 700] & 0xffffff),
	      "the right half changed");
	CHECK((before[20 * cap_width + 100] & 0xffffff) ==
		      (window_pixel & 0xffffff),
	      "the tiled window does not cover its half");

	struct harness_window *b = harness_window_create(h, 300, 200, "beta");

	settle(h, 300);
	before = screenshot(h, "two-windows");
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_TAB, true);
	harness_key(h, KEY_TAB, false);
	settle(h, 500);
	after = screenshot(h, "alt-tab");
	CHECK(changed(before, after, 0, 0, cap_width, cap_height) > 5000,
	      "Alt-Tab: the popup is not on screen");
	harness_key(h, KEY_LEFTALT, false);
	settle(h, 300);
	before = screenshot(h, "after-alt-tab");
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_TAB, true);
	harness_key(h, KEY_TAB, false);
	settle(h, 500);
	after = screenshot(h, "alt-tab-again");
	CHECK(changed(before, after, 0, 0, cap_width, cap_height) > 5000,
	      "the second Alt-Tab popup is not on screen");
	harness_key(h, KEY_LEFTALT, false);
	settle(h, 1500);
	CHECK(h->shell_ui_binds == 1 && h->shell_ui_unbinds == 0,
	      "polo-shell-ui crashed (%d binds)", h->shell_ui_binds);
	before = screenshot(h, "before-workspaces");
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_RIGHT, true);
	harness_key(h, KEY_RIGHT, false);
	pause_ms(h, 500);
	after = screenshot(h, "workspace-popup");
	harness_key(h, KEY_RIGHT, true);
	harness_key(h, KEY_RIGHT, false);
	pause_ms(h, 300);
	after = screenshot(h, "workspace-popup-2");
	CHECK(changed(before, after, 0, 0, cap_width, cap_height) > 5000,
	      "the workspace popup is not on screen");
	harness_key(h, KEY_LEFT, true);
	harness_key(h, KEY_LEFT, false);
	harness_key(h, KEY_LEFT, true);
	harness_key(h, KEY_LEFT, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	pause_ms(h, 300);
	/* A window's own icon, in a colour nothing else on screen has. */
	CHECK(icon_manager, "no xdg_toplevel_icon_manager_v1");
	const size_t icon_bytes = 64 * 64 * 4;
	int icon_fd = memfd_create("icon", MFD_CLOEXEC);

	CHECK(icon_fd >= 0 && ftruncate(icon_fd, icon_bytes) == 0, "icon file");
	uint32_t *icon_pixels = mmap(NULL, icon_bytes, PROT_READ | PROT_WRITE,
				     MAP_SHARED, icon_fd, 0);

	CHECK(icon_pixels != MAP_FAILED, "icon mmap");
	for (int i = 0; i < 64 * 64; i++)
		icon_pixels[i] = 0xffff00ff;
	munmap(icon_pixels, icon_bytes);
	struct wl_shm_pool *icon_pool = wl_shm_create_pool(h->shm, icon_fd, icon_bytes);
	struct wl_buffer *icon_buffer = wl_shm_pool_create_buffer(
		icon_pool, 0, 64, 64, 64 * 4, WL_SHM_FORMAT_ARGB8888);

	wl_shm_pool_destroy(icon_pool);
	close(icon_fd);
	struct xdg_toplevel_icon_v1 *icon =
		xdg_toplevel_icon_manager_v1_create_icon(icon_manager);

	xdg_toplevel_icon_v1_add_buffer(icon, icon_buffer, 1);
	xdg_toplevel_icon_manager_v1_set_icon(icon_manager, b->xdg_toplevel, icon);
	wl_surface_commit(b->surface);
	harness_roundtrip(h);
	xdg_toplevel_icon_v1_destroy(icon);
	wl_buffer_destroy(icon_buffer);

	int magenta = 0;

	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_TAB, true);
	harness_key(h, KEY_TAB, false);
	for (int attempt = 0; attempt < 100 && magenta < 200; attempt++) {
		settle(h, 100);
		after = screenshot(h, "icon");
		magenta = 0;
		for (int i = 0; i < cap_width * cap_height; i++)
			magenta += (after[i] & 0xffffff) == 0xff00ff;
	}
	CHECK(magenta >= 200, "the window's icon is not in the popup (%d pixels)",
	      magenta);
	harness_key(h, KEY_ESC, true);
	harness_key(h, KEY_ESC, false);
	harness_key(h, KEY_LEFTALT, false);
	settle(h, 300);

	harness_window_destroy(b);
	harness_window_destroy(a);
	xdg_toplevel_icon_manager_v1_destroy(icon_manager);
	icon_manager = NULL;
	weston_capture_v1_destroy(capture);
	wl_registry_destroy(registry);
	return harness_stop(h);
}
