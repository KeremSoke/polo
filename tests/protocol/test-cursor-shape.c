/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A cursor shape draws the theme's image, and follows the rules of
 * wl_pointer.set_cursor. Skips without an Xcursor theme.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xcursor/Xcursor.h>
#include <linux/input.h>

#include "cursor-shape-v1-client-protocol.h"
#include "harness.h"
#include "weston-output-capture-client-protocol.h"

/* The last check kills this client's connection with its proxies live. Polo is
 * its own process and is still checked. */
int
__lsan_is_turned_off(void);

int
__lsan_is_turned_off(void)
{
	return 1;
}

#define BOX 48

static struct wp_cursor_shape_manager_v1 *manager;
static struct weston_capture_v1 *capture;
static int cap_width, cap_height, cap_done;
static int pointer_x, pointer_y;

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0)
		manager = wl_registry_bind(
			registry, name, &wp_cursor_shape_manager_v1_interface, 2);
	else if (strcmp(interface, weston_capture_v1_interface.name) == 0)
		capture = wl_registry_bind(registry, name,
					   &weston_capture_v1_interface, 1);
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
source_size(void *data, struct weston_capture_source_v1 *s, int32_t w, int32_t h)
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

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec tick = { 0, 20 * 1000000L };

	for (int i = 0; i < ms / 20; i++) {
		harness_roundtrip(h);
		nanosleep(&tick, NULL);
	}
}

/* The box at the pointer, from the cursor layer up. */
static uint32_t *
shot(struct harness *h)
{
	struct weston_capture_source_v1 *source = weston_capture_v1_create(
		capture, h->outputs[0], WESTON_CAPTURE_V1_SOURCE_FRAMEBUFFER);

	cap_done = 0;
	weston_capture_source_v1_add_listener(source, &source_listener, NULL);
	harness_roundtrip(h);
	CHECK(cap_width > 0, "no capture size");
	size_t size = (size_t)cap_width * cap_height * 4;
	int fd = memfd_create("capture", MFD_CLOEXEC);

	CHECK(fd >= 0 && ftruncate(fd, size) == 0, "capture buffer");
	uint32_t *pixels =
		mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	struct wl_shm_pool *pool = wl_shm_create_pool(h->shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(
		pool, 0, cap_width, cap_height, cap_width * 4,
		WL_SHM_FORMAT_XRGB8888);

	for (int attempt = 0; attempt < 20 && cap_done != 1; attempt++) {
		cap_done = 0;
		weston_capture_source_v1_capture(source, buffer);
		for (int i = 0; i < 200 && !cap_done; i++)
			pause_ms(h, 20);
	}
	CHECK(cap_done == 1, "capture ended with %d", cap_done);
	wl_buffer_destroy(buffer);
	wl_shm_pool_destroy(pool);
	weston_capture_source_v1_destroy(source);
	close(fd);

	uint32_t *box = malloc(BOX * BOX * 4);

	for (int y = 0; y < BOX; y++)
		memcpy(box + y * BOX, pixels + (pointer_y + y - 8) * cap_width +
					      pointer_x - 8,
		       BOX * 4);
	munmap(pixels, size);
	return box;
}

static int
differ(const uint32_t *a, const uint32_t *b)
{
	int n = 0;

	for (int i = 0; i < BOX * BOX; i++)
		n += (a[i] & 0xffffff) != (b[i] & 0xffffff);
	return n;
}

/* The next shot that settles; a shape loads a moment after it is asked for. */
static uint32_t *
settled(struct harness *h)
{
	pause_ms(h, 300);
	return shot(h);
}

struct other_client {
	struct wl_display *display;
	struct wl_seat *seat;
	struct wp_cursor_shape_manager_v1 *manager;
};

static void
other_global(void *data, struct wl_registry *registry, uint32_t name,
	     const char *interface, uint32_t version)
{
	struct other_client *other = data;

	if (strcmp(interface, wl_seat_interface.name) == 0 && !other->seat)
		other->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
	else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0)
		other->manager = wl_registry_bind(
			registry, name, &wp_cursor_shape_manager_v1_interface, 1);
}

static const struct wl_registry_listener other_listener = { other_global,
							    global_remove };

int
main(int argc, char **argv)
{
	XcursorImage *theme = XcursorLibraryLoadImage("left_ptr", "default", 24);

	if (!theme) {
		printf("no cursor theme to test with\n");
		return 77;
	}
	XcursorImageDestroy(theme);

	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(manager, "no wp_cursor_shape_manager_v1");
	CHECK(capture, "no weston_capture_v1");

	struct harness_window *w = harness_window_create(h, 400, 300, "shapes");
	const struct harness_window_info *info = harness_window_info(w);

	pointer_x = info->x + 100;
	pointer_y = info->y + 100;
	harness_move_pointer(h, pointer_x - 1, pointer_y);
	harness_move_pointer(h, pointer_x, pointer_y);
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	harness_roundtrip(h);
	CHECK(h->pointer_focus == w->surface, "the pointer is not on the window");
	uint32_t serial = h->pointer_enter_serial;

	struct wp_cursor_shape_device_v1 *device =
		wp_cursor_shape_manager_v1_get_pointer(manager, h->pointer);

	wl_pointer_set_cursor(h->pointer, serial, NULL, 0, 0);
	uint32_t *hidden = settled(h);

	wp_cursor_shape_device_v1_set_shape(
		device, serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
	uint32_t *arrow = settled(h);

	CHECK(differ(hidden, arrow) > 0, "the default shape draws nothing");

	wp_cursor_shape_device_v1_set_shape(device, serial,
					    WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
	uint32_t *text = settled(h);

	CHECK(differ(arrow, text) > 0, "the text shape looks like the arrow");
	CHECK(differ(hidden, text) > 0, "the text shape draws nothing");

	/* From the cache, at once. */
	wp_cursor_shape_device_v1_set_shape(
		device, serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
	CHECK(differ(arrow, settled(h)) == 0, "the arrow did not come back");

	/* wl_pointer.set_cursor takes over. */
	wl_pointer_set_cursor(h->pointer, serial, NULL, 0, 0);
	CHECK(differ(hidden, settled(h)) == 0, "set_cursor did not replace the shape");

	/* A serial from the future changes nothing. */
	wp_cursor_shape_device_v1_set_shape(
		device, serial + 100000, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
	CHECK(differ(hidden, settled(h)) == 0, "a future serial was accepted");


	/* Another client's request changes nothing. */
	struct other_client other = { 0 };

	other.display = wl_display_connect(h->socket);
	CHECK(other.display != NULL, "second connection");
	wl_registry_add_listener(wl_display_get_registry(other.display),
				 &other_listener, &other);
	wl_display_roundtrip(other.display);
	CHECK(other.manager && other.seat, "no globals on the second connection");
	struct wl_pointer *other_pointer = wl_seat_get_pointer(other.seat);
	struct wp_cursor_shape_device_v1 *other_device =
		wp_cursor_shape_manager_v1_get_pointer(other.manager, other_pointer);

	wp_cursor_shape_device_v1_set_shape(other_device, 0,
					    WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
	wl_display_roundtrip(other.display);
	CHECK(differ(hidden, settled(h)) == 0, "another client changed the cursor");
	wl_display_disconnect(other.display);

	/* The shape goes with focus, and the shape needs focus to be set. */
	wp_cursor_shape_device_v1_set_shape(device, serial,
					    WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
	CHECK(differ(text, settled(h)) == 0, "the text shape did not come back");
	int inside_x = pointer_x, inside_y = pointer_y;

	pointer_x = info->x > 60 ? info->x - 40 : info->x + 440;
	pointer_y = inside_y;
	harness_move_pointer(h, pointer_x, pointer_y);
	harness_roundtrip(h);
	CHECK(h->pointer_focus != w->surface, "the pointer did not leave");
	uint32_t *outside = settled(h);

	wp_cursor_shape_device_v1_set_shape(device, serial,
					    WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
	CHECK(differ(outside, settled(h)) == 0,
	      "a shape set without focus changed the cursor");
	pointer_x = inside_x;
	harness_move_pointer(h, pointer_x, pointer_y);
	harness_roundtrip(h);
	CHECK(h->pointer_focus == w->surface, "the pointer did not come back");

	serial = h->pointer_enter_serial;
	wl_pointer_set_cursor(h->pointer, serial, NULL, 0, 0);
	CHECK(differ(hidden, settled(h)) == 0, "the cursor did not hide again");

	/* Focus moves while a shape loads: it does not land on the new focus. */
	struct harness_window *second = harness_window_create(h, 200, 150, "second");
	const struct harness_window_info *second_info = harness_window_info(second);
	int second_x = second_info->x + 100, second_y = second_info->y + 75;

	int first_x = info->x + 12, first_y = info->y + 12;

	for (int corner = 0; corner < 4; corner++) {
		first_x = info->x + (corner & 1 ? 388 : 12);
		first_y = info->y + (corner & 2 ? 288 : 12);
		harness_move_pointer(h, first_x, first_y);
		harness_roundtrip(h);
		if (h->pointer_focus == w->surface)
			break;
	}
	CHECK(h->pointer_focus == w->surface, "no corner of the first window is free");
	pointer_x = second_x;
	pointer_y = second_y;
	harness_move_pointer(h, second_x, second_y);
	harness_roundtrip(h);
	CHECK(h->pointer_focus == second->surface, "the pointer is not on the second window");
	wl_pointer_set_cursor(h->pointer, h->pointer_enter_serial, NULL, 0, 0);
	uint32_t *second_hidden = settled(h);

	harness_move_pointer(h, first_x, first_y);
	harness_roundtrip(h);
	CHECK(h->pointer_focus == w->surface, "the pointer is not back on the first");
	serial = h->pointer_enter_serial;
	wp_cursor_shape_device_v1_set_shape(device, serial,
					    WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR);
	polo_test_v1_move_pointer(h->test, second_x, second_y);
	harness_roundtrip(h);
	CHECK(h->pointer_focus == second->surface, "the pointer did not move in time");
	CHECK(differ(second_hidden, settled(h)) == 0,
	      "a shape still loading landed on the window focus moved to");
	harness_window_destroy(second);
	pointer_x = inside_x;
	pointer_y = inside_y;
	harness_move_pointer(h, pointer_x, pointer_y);
	harness_roundtrip(h);
	CHECK(h->pointer_focus == w->surface, "the pointer did not return");
	serial = h->pointer_enter_serial;
	wl_pointer_set_cursor(h->pointer, serial, NULL, 0, 0);
	CHECK(differ(hidden, settled(h)) == 0, "the cursor did not hide after that");

	/* Every shape draws something, from the theme or the fallback. */
	for (uint32_t shape = 1; shape <= 36; shape++) {
		wp_cursor_shape_device_v1_set_shape(device, serial, shape);
		pause_ms(h, 60);
		CHECK(differ(hidden, shot(h)) > 0, "shape %u draws nothing", shape);
		wl_pointer_set_cursor(h->pointer, serial, NULL, 0, 0);
		harness_roundtrip(h);
		pause_ms(h, 40);
	}

	/* A shape that does not exist is an error. */
	wp_cursor_shape_device_v1_set_shape(device, serial, 999);
	for (int i = 0; i < 10 && !wl_display_get_error(h->display); i++)
		wl_display_roundtrip(h->display);
	CHECK(wl_display_get_error(h->display) == EPROTO,
	      "no error for a shape that does not exist");
	return harness_stop(h) == 0 ? 0 : 1;
}
