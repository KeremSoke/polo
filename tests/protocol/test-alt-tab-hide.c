/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Exercises rapid Alt-Tab dismissal. Constructing the popup dispatches
 * Wayland events, so hide can arrive before the popup exists. The windows
 * are maximized and look alike, so focus changes do not change a pixel.
 * POLO_TEST_CAPTURE_DIR saves captures as .ppm files.
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

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec ts = { 0, 20 * 1000000L };

	for (int i = 0; i < ms / 20; i++) {
		harness_roundtrip(h);
		nanosleep(&ts, NULL);
	}
}



static int failures;

static void
expect_clean(struct harness *h, uint32_t *base, const char *what)
{
	settle(h, 600);
	uint32_t *now = screenshot(h, what);
	int n = changed(base, now, 0, 0, cap_width, cap_height);

	if (n > 200) {
		fprintf(stderr, "%s: the popup is still on screen (%d pixels)\n",
			what, n);
		failures++;
	}
}

static void
key(struct harness *h, uint32_t k, bool down)
{
	harness_key(h, k, down);
}

int
main(int argc, char **argv)
{
	struct harness *h;

	CHECK(argc >= 3, "usage: %s POLO POLO-SHELL-UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	struct harness_window *a = harness_window_create(h, 300, 200, "alpha");
	struct harness_window *b = harness_window_create(h, 300, 200, "beta");
	struct harness_window *c = harness_window_create(h, 300, 200, "gamma");
	xdg_toplevel_set_maximized(a->xdg_toplevel);
	xdg_toplevel_set_maximized(b->xdg_toplevel);
	xdg_toplevel_set_maximized(c->xdg_toplevel);
	settle(h, 500);
	harness_window_draw_pending(a);
	harness_window_draw_pending(b);
	harness_window_draw_pending(c);
	harness_move_pointer(h, 1000, 740);
	settle(h, 1500);

	{
		uint32_t *base = screenshot(h, "base");

		key(h, KEY_LEFTALT, true);
		key(h, KEY_TAB, true);
		key(h, KEY_TAB, false);
		key(h, KEY_LEFTALT, false);
		expect_clean(h, base, "instant");
		base = screenshot(h, "base");

		key(h, KEY_LEFTALT, true);
		key(h, KEY_TAB, true);
		key(h, KEY_LEFTALT, false);
		key(h, KEY_TAB, false);
		expect_clean(h, base, "alt-up-first");
		base = screenshot(h, "base");

		key(h, KEY_LEFTALT, true);
		key(h, KEY_TAB, true);
		key(h, KEY_TAB, false);
		pause_ms(h, 400);
		key(h, KEY_TAB, true);
		key(h, KEY_TAB, false);
		pause_ms(h, 200);
		key(h, KEY_LEFTALT, false);
		expect_clean(h, base, "shown-twice");
		base = screenshot(h, "base");

		key(h, KEY_LEFTALT, true);
		key(h, KEY_TAB, true);
		key(h, KEY_TAB, false);
		pause_ms(h, 400);
		key(h, KEY_ESC, true);
		key(h, KEY_ESC, false);
		key(h, KEY_LEFTALT, false);
		expect_clean(h, base, "escape");
		base = screenshot(h, "base");

		for (int i = 0; i < 10; i++) {
			key(h, KEY_LEFTALT, true);
			key(h, KEY_TAB, true);
			key(h, KEY_TAB, false);
			pause_ms(h, 20 * (i % 4));
			key(h, KEY_LEFTALT, false);
		}
		expect_clean(h, base, "rapid");
		base = screenshot(h, "base");

		key(h, KEY_LEFTALT, true);
		key(h, KEY_LEFTSHIFT, true);
		key(h, KEY_TAB, true);
		key(h, KEY_TAB, false);
		pause_ms(h, 400);
		key(h, KEY_LEFTSHIFT, false);
		pause_ms(h, 100);
		key(h, KEY_LEFTALT, false);
		expect_clean(h, base, "shift");
		base = screenshot(h, "base");
	}

	CHECK(failures == 0, "%d sequences left the popup up", failures);
	harness_window_destroy(a);
	harness_window_destroy(b);
	harness_window_destroy(c);
	weston_capture_v1_destroy(capture);
	if (icon_manager)
		xdg_toplevel_icon_manager_v1_destroy(icon_manager);
	wl_registry_destroy(registry);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
