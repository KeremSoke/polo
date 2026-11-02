/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's keyboard move and resize (keybindings.c process_keyboard_move_grab
 * and process_keyboard_resize_grab) against headless Polo: the grab warps
 * the pointer and holds it, arrows and hjkl move by 10, Ctrl by 1, Shift
 * snaps to the next edge (edge-resistance.c), the keypad diagonals move both
 * ways, the first arrow of a resize only picks
 * the edge, Escape restores, any other key or a click ends it, and nothing
 * of it reaches the client.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <linux/input.h>

#include "harness.h"

static void
tap(struct harness *h, uint32_t key)
{
	h->last_key = 0;
	harness_key(h, key, true);
	harness_key(h, key, false);
	CHECK(h->last_key == 0, "key %u reached the client", key);
}

static void
tap_with(struct harness *h, uint32_t mod, uint32_t key)
{
	harness_key(h, mod, true);
	tap(h, key);
	harness_key(h, mod, false);
}

static void
begin(struct harness *h, uint32_t key)
{
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTSHIFT, true);
	tap(h, key);
	harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, KEY_LEFTCTRL, false);
}

static struct harness_window_info
info(struct harness_window *w)
{
	return *harness_window_info(w);
}

static void
test_default_shortcuts(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w;
	struct harness_window_info start;
	int motions, status;

	unsetenv("POLO_TEST_PREFS");
	h = harness_start(argc, argv);
	w = harness_window_create(h, 300, 200, "default shortcuts");
	harness_move_pointer(h, 5, 5);
	start = info(w);

	tap_with(h, KEY_LEFTALT, KEY_F7);
	CHECK(h->pointer_focus == NULL,
	      "the client kept the pointer during the move");
	motions = h->motions;
	harness_move_pointer(h, start.x + start.width / 2 + 24,
			     start.y + start.height / 2 + 16);
	CHECK(info(w).x == start.x + 24 && info(w).y == start.y + 16,
	      "Alt+F7 then the pointer moved to %d,%d", info(w).x, info(w).y);
	CHECK(h->motions == motions,
	      "the client saw pointer motion during the move");
	tap(h, KEY_ENTER);
	CHECK(info(w).x == start.x + 24, "Return did not keep the move");

	start = info(w);
	tap_with(h, KEY_LEFTALT, KEY_F8);
	harness_move_pointer(h, start.x + start.width / 2 + 18,
			     start.y + start.height / 2 + 12);
	CHECK(info(w).x == start.x && info(w).y == start.y &&
		      info(w).width == start.width + 18 &&
		      info(w).height == start.height + 12,
	      "Alt+F8 then the pointer gave %d,%d %dx%d", info(w).x, info(w).y,
	      info(w).width, info(w).height);
	tap(h, KEY_ENTER);

	harness_window_destroy(w);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
}

/* A client that rounds the size must not stop later sizes from reaching it. */
static void
test_rounded_sizes(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w;
	struct harness_window_info start;
	int corner_x, corner_y;

	unsetenv("POLO_TEST_PREFS");
	h = harness_start(argc, argv);
	w = harness_window_create(h, 300, 200, "rounded");
	w->size_inc = 20;
	for (int i = 0; i < 4; i++)
		harness_wait_frame(h, w->surface);

	for (int round = 0; round < 2; round++) {
		start = info(w);
		tap_with(h, KEY_LEFTALT, KEY_F8);
		harness_move_pointer(h, start.x + start.width / 2 + 2,
				     start.y + start.height / 2 + 2);
		corner_x = start.x + start.width - 1;
		corner_y = start.y + start.height - 1;
		for (int step = 1; step <= 20; step++) {
			struct timespec nap = { 0, 15 * 1000000L };

			harness_move_pointer(h, corner_x + step,
					     corner_y + step);
			nanosleep(&nap, NULL);
			harness_roundtrip(h);
		}
		for (int i = 0; i < 10; i++) {
			struct timespec nap = { 0, 20 * 1000000L };

			nanosleep(&nap, NULL);
			harness_roundtrip(h);
		}
		CHECK(info(w).width == start.width + 20 &&
			      info(w).height == start.height + 20,
		      "round %d: %dx%d after 20 pixels from %dx%d", round,
		      info(w).width, info(w).height, start.width, start.height);
		CHECK(w->width == info(w).width && w->height == info(w).height,
		      "round %d: the client draws %dx%d", round, w->width,
		      w->height);
		tap(h, KEY_ENTER);
	}
	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *a;
	struct harness_window_info start, now;
	int presses, releases, status;

	test_default_shortcuts(argc, argv);
	test_rounded_sizes(argc, argv);
	setenv("POLO_TEST_PREFS",
	       "window-keybindings/begin-move='<Control><Shift>m';"
	       "window-keybindings/begin-resize='<Control><Shift>r'",
	       1);
	h = harness_start(argc, argv);
	a = harness_window_create(h, 300, 200, "keyboard move resize");
	CHECK(h->keyboard_focus == a->surface, "window not focused");
	start = info(a);

	/* Move: 10 px, 1 px with Ctrl, hjkl, keypad diagonals. */
	begin(h, KEY_M);
	tap(h, KEY_RIGHT);
	tap_with(h, KEY_LEFTCTRL, KEY_DOWN);
	tap(h, KEY_L);
	tap_with(h, KEY_LEFTCTRL, KEY_LEFT);
	now = info(a);
	CHECK(now.x == start.x + 19 && now.y == start.y + 1,
	      "keyboard move reached %d,%d from %d,%d", now.x, now.y, start.x,
	      start.y);
	tap(h, KEY_KP7);
	now = info(a);
	CHECK(now.x == start.x + 9 && now.y == start.y - 9,
	      "KP_Home moved to %d,%d", now.x, now.y);
	tap(h, KEY_LEFTSHIFT);
	tap_with(h, KEY_LEFTSHIFT, KEY_LEFT);
	CHECK(info(a).x == 0, "Shift+Left snapped to x=%d", info(a).x);
	tap_with(h, KEY_LEFTSHIFT, KEY_UP);
	CHECK(info(a).y == 0, "Shift+Up snapped to y=%d", info(a).y);
	tap(h, KEY_ESC);
	now = info(a);
	CHECK(now.x == start.x && now.y == start.y,
	      "Escape did not restore the move");
	harness_move_pointer(h, now.x + 20, now.y + 20);
	CHECK(h->pointer_focus != NULL,
	      "the pointer did not come back to the client");

	/* Any key that does nothing ends the move, and is eaten. */
	begin(h, KEY_M);
	tap(h, KEY_LEFT);
	tap(h, KEY_A);
	now = info(a);
	CHECK(now.x == start.x - 10, "the move was not kept");
	h->last_key = 0;
	harness_key(h, KEY_RIGHT, true);
	harness_key(h, KEY_RIGHT, false);
	CHECK(h->last_key == KEY_RIGHT, "the move did not end on A");
	CHECK(info(a).x == now.x, "the window moved after the move ended");

	/* Resize: the first arrow picks the edge, later ones resize. */
	start = info(a);
	begin(h, KEY_R);
	tap(h, KEY_RIGHT);
	now = info(a);
	CHECK(now.width == start.width && now.height == start.height,
	      "the first arrow resized to %dx%d", now.width, now.height);
	tap(h, KEY_RIGHT);
	now = info(a);
	CHECK(now.x == start.x && now.width == start.width + 10,
	      "right edge: x=%d width=%d", now.x, now.width);
	tap_with(h, KEY_LEFTCTRL, KEY_LEFT);
	now = info(a);
	CHECK(now.x == start.x && now.width == start.width + 9,
	      "fine left on the right edge: width=%d", now.width);
	tap(h, KEY_UP);
	now = info(a);
	CHECK(now.height == start.height, "switching to the top edge resized");
	tap(h, KEY_UP);
	now = info(a);
	CHECK(now.y == start.y - 10 && now.height == start.height + 10,
	      "top edge: y=%d height=%d", now.y, now.height);
	tap(h, KEY_ESC);
	now = info(a);
	CHECK(now.x == start.x && now.y == start.y &&
		      now.width == start.width && now.height == start.height,
	      "Escape did not restore the resize");

	/* A click ends the operation; neither its press nor its release reach
	 * the client. */
	start = info(a);
	begin(h, KEY_M);
	harness_move_pointer(h, start.x + start.width / 2 + 35,
			     start.y + start.height / 2 + 25);
	now = info(a);
	CHECK(now.x == start.x + 35 && now.y == start.y + 25,
	      "the pointer did not move the window");
	presses = h->button_presses;
	releases = h->button_releases;
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	CHECK(h->button_presses == presses && h->button_releases == releases,
	      "the ending click reached the client");
	harness_move_pointer(h, now.x + now.width / 2 + 50,
			     now.y + now.height / 2);
	CHECK(info(a).x == now.x, "the window moved after the click");

	/* The window going away ends it. */
	begin(h, KEY_M);
	harness_window_destroy(a);
	a = harness_window_create(h, 200, 100, "after");
	h->last_key = 0;
	harness_key(h, KEY_LEFT, true);
	harness_key(h, KEY_LEFT, false);
	CHECK(h->last_key == KEY_LEFT, "the move outlived its window");

	harness_window_destroy(a);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
