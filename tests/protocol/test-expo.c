/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Expo on Pixman, four workspaces in a row: Super+E shows the grid over a
 * backdrop with the window as a small mirror in its cell; arrows and Enter
 * switch workspace, Escape goes back; dragging the window's mirror to
 * another cell moves it to that workspace; a click on a cell switches to it.
 * Then a grid, workspace names, and the blurred wallpaper backdrop.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <linux/input.h>

#include "harness.h"

#define WINDOW_COLOUR 0x3070c0u
#define BACKDROP 0x1a1a1au
#define WORKSPACES 4
#define GAP 16.0

static double scale, left, top, cell_w, cell_h;
static struct wl_subcompositor *subcompositor;

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface, wl_subcompositor_interface.name) == 0)
		subcompositor = wl_registry_bind(
			registry, name, &wl_subcompositor_interface, 1);
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
grid(void)
{
	double w = HARNESS_OUTPUT_WIDTH, h = HARNESS_OUTPUT_HEIGHT;
	double sx = (w - (WORKSPACES + 1) * GAP) / (WORKSPACES * w);
	double sy = (h - 2 * GAP) / h;

	scale = sx < sy ? sx : sy;
	cell_w = w * scale;
	cell_h = h * scale;
	left = (w - (WORKSPACES * cell_w + (WORKSPACES - 1) * GAP)) / 2;
	top = (h - cell_h) / 2;
}

static double
cell_x(int column)
{
	return left + column * (cell_w + GAP);
}

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static int found_x, found_y;

static int
window_pixels(struct harness *h, uint32_t *corner)
{
	int width, height, count = 0;
	uint32_t *pixels = harness_capture(h, &width, &height);

	CHECK(pixels, "cannot capture");
	found_x = width;
	found_y = height;
	for (int i = 0; i < width * height; i++) {
		if ((pixels[i] & 0xffffff) != WINDOW_COLOUR)
			continue;
		count++;
		found_x = i % width < found_x ? i % width : found_x;
		found_y = i / width < found_y ? i / width : found_y;
	}
	if (corner)
		*corner = pixels[2 * width + 2] & 0xffffff;
	free(pixels);
	return count;
}

static int
label_pixels(struct harness *h)
{
	int width, height, count = 0;
	uint32_t *pixels = harness_capture(h, &width, &height);

	CHECK(pixels, "cannot capture");
	for (int i = 0; i < width * height; i++) {
		uint32_t r = pixels[i] >> 16 & 0xff, g = pixels[i] >> 8 & 0xff,
			 b = pixels[i] & 0xff;

		count += r == g && g == b && r >= 0x20 && r <= 0x26;
	}
	free(pixels);
	return count;
}

static int
channel_sum(uint32_t pixel)
{
	return (pixel >> 16 & 0xff) + (pixel >> 8 & 0xff) + (pixel & 0xff);
}

static void
move(struct harness *h, int x, int y)
{
	polo_test_v1_move_pointer(h->test, x, y);
	harness_roundtrip(h);
}

static void
press(struct harness *h, bool pressed)
{
	polo_test_v1_button(h->test, BTN_LEFT,
			    pressed ? WL_POINTER_BUTTON_STATE_PRESSED
				    : WL_POINTER_BUTTON_STATE_RELEASED);
	harness_roundtrip(h);
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_roundtrip(h);
}

static void
open_expo(struct harness *h)
{
	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_E);
	harness_key(h, KEY_LEFTMETA, false);
	pause_ms(h, 500);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	uint32_t corner;

	grid();
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	harness_set_setting(h, "org.mate.polo.effects", "expo-workspace-names",
			    "false");
	harness_set_setting(h, "org.mate.polo.effects", "expo-background",
			    "'solid'");
	harness_move_pointer(h, 1000, 740);
	struct harness_window *window =
		harness_window_create(h, 300, 200, "expo");
	const struct harness_window_info *info = harness_window_info(window);
	int wx = info->x, wy = info->y;

	CHECK(info->workspace == 0, "the window is on workspace %d",
	      info->workspace);

	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(subcompositor, "no wl_subcompositor");
	struct wl_surface *part = wl_compositor_create_surface(h->compositor);
	struct wl_subsurface *sub = wl_subcompositor_get_subsurface(
		subcompositor, part, window->surface);
	struct wl_buffer *part_buffer = harness_buffer_create(h->shm, 50, 50);

	wl_subsurface_set_position(sub, 280, 180);
	wl_subsurface_set_desync(sub);
	wl_surface_attach(part, part_buffer, 0, 0);
	wl_surface_damage_buffer(part, 0, 0, 50, 50);
	wl_surface_commit(part);
	harness_wait_frame(h, window->surface);
	const int whole = window_pixels(h, NULL);

	CHECK(whole == 300 * 200 + 50 * 50 - 20 * 20,
	      "the window and its subsurface show %d pixels", whole);

	open_expo(h);
	int small = window_pixels(h, &corner);

	CHECK(corner == BACKDROP, "no backdrop: the corner is 0x%06x", corner);
	CHECK(abs(found_x - (int)lround(cell_x(0) + wx * scale)) <= 2 &&
		      abs(found_y - (int)lround(top + wy * scale)) <= 2,
	      "the window's mirror is at %d,%d, not %ld,%ld", found_x, found_y,
	      lround(cell_x(0) + wx * scale), lround(top + wy * scale));
	CHECK(small > 0 && small < whole / 4,
	      "the window shows %d pixels in Expo", small);

	tap(h, KEY_RIGHT);
	tap(h, KEY_ENTER);
	pause_ms(h, 500);
	CHECK(window_pixels(h, &corner) == 0,
	      "the window shows on the second workspace");
	CHECK(corner != BACKDROP, "Expo did not close");

	open_expo(h);
	tap(h, KEY_ESC);
	pause_ms(h, 500);
	CHECK(window_pixels(h, NULL) == 0,
	      "Escape did not stay on workspace 2");

	open_expo(h);
	tap(h, KEY_LEFT);
	tap(h, KEY_ENTER);
	pause_ms(h, 500);
	CHECK(window_pixels(h, NULL) == whole,
	      "Left and Enter did not go back to the first workspace");
	open_expo(h);
	double px = cell_x(0) + (wx + 150) * scale;
	double py = top + (wy + 100) * scale;

	move(h, (int)px, (int)py);
	press(h, true);
	for (int step = 1; step <= 10; step++) {
		move(h, (int)(px + step * 2 * (cell_w + GAP) / 10), (int)py);
		if (step == 5) {
			int over = window_pixels(h, NULL);

			CHECK(over >= small * 4 / 5,
			      "over the next workspace the dragged window "
			      "shows "
			      "%d of %d pixels",
			      over, small);
		}
	}
	press(h, false);
	pause_ms(h, 100);
	info = harness_window_info(window);
	CHECK(info->workspace == 2,
	      "the dragged window is on workspace %d, not 3", info->workspace);
	CHECK(abs(info->x - wx) <= 8 && abs(info->y - wy) <= 8,
	      "the dropped window moved to %d,%d from %d,%d", info->x, info->y,
	      wx, wy);

	move(h, (int)(cell_x(2) + cell_w / 2), (int)(top + cell_h - 4));
	press(h, true);
	press(h, false);
	pause_ms(h, 500);
	CHECK(window_pixels(h, &corner) == whole,
	      "clicking the third workspace did not show the window whole");

	harness_set_setting(h, "org.mate.polo.effects", "expo-layout",
			    "'grid'");
	open_expo(h);
	CHECK(window_pixels(h, NULL) > 0 &&
		      found_x < HARNESS_OUTPUT_WIDTH / 2 &&
		      found_y >= HARNESS_OUTPUT_HEIGHT / 2,
	      "in a grid the third workspace's window is at %d,%d", found_x,
	      found_y);
	int plain = label_pixels(h);

	tap(h, KEY_ESC);
	pause_ms(h, 500);
	harness_set_setting(h, "org.mate.polo.effects", "expo-workspace-names",
			    "true");
	open_expo(h);
	int named = label_pixels(h);

	CHECK(named > plain + 1000,
	      "no workspace names: %d label pixels, %d without", named, plain);
	tap(h, KEY_ESC);
	pause_ms(h, 500);

	uint32_t wallpaper;

	window_pixels(h, &wallpaper);
	harness_set_setting(h, "org.mate.polo.effects", "expo-background",
			    "'blurred-wallpaper'");
	harness_set_setting(h, "org.mate.polo.effects", "expo-background-dim",
			    "0.5");
	open_expo(h);
	window_pixels(h, &corner);
	CHECK(corner != BACKDROP && channel_sum(corner) > 0 &&
		      abs(2 * channel_sum(corner) - channel_sum(wallpaper)) <=
			      12,
	      "the blurred backdrop's corner is 0x%06x over a 0x%06x wallpaper",
	      corner, wallpaper);
	tap(h, KEY_ESC);
	pause_ms(h, 500);

	wl_subsurface_destroy(sub);
	wl_surface_destroy(part);
	wl_buffer_destroy(part_buffer);
	wl_subcompositor_destroy(subcompositor);
	wl_registry_destroy(registry);
	harness_window_destroy(window);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
