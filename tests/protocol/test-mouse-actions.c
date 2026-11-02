/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>

#include <linux/input.h>

#include "harness.h"

static void
test_binding(struct harness *h, struct harness_window *w, uint32_t modifier,
	     uint32_t resize_button, uint32_t menu_button)
{
	struct harness_window_info start;
	int presses, releases, i;

	start = *harness_window_info(w);
	harness_move_pointer(h, start.x + 100, start.y + 100);
	presses = h->button_presses;
	releases = h->button_releases;
	harness_key(h, modifier, true);
	harness_button(h, BTN_LEFT, true);
	harness_move_pointer(h, start.x + 135, start.y + 125);
	CHECK(harness_window_info(w)->x == start.x + 35 &&
		      harness_window_info(w)->y == start.y + 25,
	      "modifier-drag moved from %d,%d to %d,%d", start.x, start.y,
	      w->info.x, w->info.y);
	harness_button(h, BTN_LEFT, false);
	harness_key(h, modifier, false);
	CHECK(h->button_presses == presses && h->button_releases == releases,
	      "move binding leaked a button event to the client");

	start = *harness_window_info(w);
	harness_move_pointer(h, start.x + start.width - 3,
			     start.y + start.height - 3);
	harness_key(h, modifier, true);
	harness_button(h, resize_button, true);
	harness_move_pointer(h, start.x + start.width + 27,
			     start.y + start.height + 17);
	for (i = 0; i < 4; i++)
		harness_roundtrip(h);
	harness_button(h, resize_button, false);
	harness_key(h, modifier, false);
	CHECK(harness_window_info(w)->width == start.width + 30 &&
		      harness_window_info(w)->height == start.height + 20,
	      "modifier-resize produced %dx%d", w->info.width, w->info.height);
	CHECK(h->button_presses == presses && h->button_releases == releases,
	      "resize binding leaked a button event to the client");

	start = *harness_window_info(w);
	harness_move_pointer(h, start.x + 80, start.y + 80);
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	CHECK(h->button_presses == presses + 1 &&
		      h->button_releases == releases + 1,
	      "plain click did not reach the client");

	/* Marco grabs the menu button with the modifier too. */
	harness_key(h, modifier, true);
	harness_button(h, menu_button, true);
	harness_button(h, menu_button, false);
	harness_key(h, modifier, false);
	CHECK(h->button_presses == presses + 1 &&
		      h->button_releases == releases + 1,
	      "the modifier and menu button reached the client");

	/* display.c: thirds of the client area pick the operation; the middle
	 * third does nothing but is still eaten. */
	start = *harness_window_info(w);
	harness_move_pointer(h, start.x + start.width / 2,
			     start.y + start.height / 2);
	harness_key(h, modifier, true);
	harness_button(h, resize_button, true);
	harness_move_pointer(h, start.x + start.width / 2 + 40,
			     start.y + start.height / 2 + 30);
	harness_button(h, resize_button, false);
	harness_key(h, modifier, false);
	CHECK(harness_window_info(w)->width == start.width &&
		      harness_window_info(w)->height == start.height &&
		      harness_window_info(w)->x == start.x,
	      "a press in the middle third changed the window");
	CHECK(h->button_presses == presses + 1 &&
		      h->button_releases == releases + 1,
	      "the middle-third press reached the client");

	start = *harness_window_info(w);
	harness_move_pointer(h, start.x + start.width - 5,
			     start.y + start.height / 2);
	harness_key(h, modifier, true);
	harness_button(h, resize_button, true);
	harness_move_pointer(h, start.x + start.width + 15,
			     start.y + start.height / 2 + 40);
	harness_button(h, resize_button, false);
	harness_key(h, modifier, false);
	CHECK(harness_window_info(w)->width == start.width + 20 &&
		      harness_window_info(w)->height == start.height &&
		      harness_window_info(w)->x == start.x,
	      "the east third gave %d,%d %dx%d", w->info.x, w->info.y,
	      w->info.width, w->info.height);

	start = *harness_window_info(w);
	harness_move_pointer(h, start.x + 5, start.y + 5);
	harness_key(h, modifier, true);
	harness_button(h, resize_button, true);
	harness_move_pointer(h, start.x - 15, start.y - 5);
	harness_button(h, resize_button, false);
	harness_key(h, modifier, false);
	CHECK(harness_window_info(w)->x == start.x - 20 &&
		      harness_window_info(w)->y == start.y - 10 &&
		      harness_window_info(w)->width == start.width + 20 &&
		      harness_window_info(w)->height == start.height + 10,
	      "the north-west third gave %d,%d %dx%d", w->info.x, w->info.y,
	      w->info.width, w->info.height);

	/* Escape cancels a modifier-drag and restores the window; the client
	 * sees no motion while it lasts. */
	{
		int motions;

		start = *harness_window_info(w);
		harness_move_pointer(h, start.x + 100, start.y + 100);
		harness_key(h, modifier, true);
		harness_button(h, BTN_LEFT, true);
		motions = h->motions;
		harness_move_pointer(h, start.x + 160, start.y + 140);
		CHECK(harness_window_info(w)->x == start.x + 60,
		      "the drag did not move");
		CHECK(h->motions == motions,
		      "the client saw motion during the drag");
		h->last_key = 0;
		harness_key(h, KEY_ESC, true);
		harness_key(h, KEY_ESC, false);
		CHECK(h->last_key != KEY_ESC,
		      "Escape reached the client during the drag");
		CHECK(harness_window_info(w)->x == start.x &&
			      harness_window_info(w)->y == start.y,
		      "Escape did not restore the window");
		harness_move_pointer(h, start.x + 200, start.y + 150);
		CHECK(harness_window_info(w)->x == start.x,
		      "the drag outlived Escape");
		harness_button(h, BTN_LEFT, false);
		harness_key(h, modifier, false);
	}

	/* Marco grabs the exact modifier, and the modifier with Shift for
	 * button 1 only: a snap move, to the nearest edges. */
	presses = h->button_presses;
	start = *harness_window_info(w);
	harness_move_pointer(h, start.x + 100, start.y + 100);
	harness_key(h, modifier, true);
	harness_key(h, KEY_LEFTCTRL, true);
	harness_button(h, BTN_LEFT, true);
	harness_move_pointer(h, start.x + 130, start.y + 120);
	harness_button(h, BTN_LEFT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	harness_key(h, modifier, false);
	CHECK(h->button_presses == presses + 1,
	      "the modifier with Ctrl was grabbed");
	CHECK(harness_window_info(w)->x == start.x,
	      "the modifier with Ctrl moved the window");

	harness_move_pointer(h, start.x + 100, start.y + 100);
	harness_key(h, modifier, true);
	harness_key(h, KEY_LEFTSHIFT, true);
	harness_button(h, BTN_LEFT, true);
	harness_move_pointer(h, start.x + 130, start.y + 120);
	harness_button(h, BTN_LEFT, false);
	harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, modifier, false);
	CHECK(h->button_presses == presses + 1,
	      "the modifier with Shift reached the client");
	harness_window_info(w);
	CHECK((w->info.x == 0 ||
	       w->info.x + w->info.width == HARNESS_OUTPUT_WIDTH) &&
		      (w->info.y == 0 ||
		       w->info.y + w->info.height == HARNESS_OUTPUT_HEIGHT),
	      "the snap move left the window at %d,%d %dx%d", w->info.x,
	      w->info.y, w->info.width, w->info.height);
}

static void
run_case(int argc, char **argv, const char *prefs, uint32_t modifier,
	 uint32_t resize_button, uint32_t menu_button)
{
	struct harness *h;
	struct harness_window *w;
	int status;

	setenv("POLO_TEST_PREFS", prefs, 1);
	h = harness_start(argc, argv);
	w = harness_window_create(h, 300, 200, "mouse-actions");
	test_binding(h, w, modifier, resize_button, menu_button);
	harness_window_destroy(w);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
}

int
main(int argc, char **argv)
{
	run_case(argc, argv, "general/resize-with-right-button=false",
		 KEY_LEFTALT, BTN_MIDDLE, BTN_RIGHT);
	run_case(argc, argv,
		 "general/mouse-button-modifier='<Super>';"
		 "general/resize-with-right-button=true",
		 KEY_LEFTMETA, BTN_RIGHT, BTN_MIDDLE);
	printf("ok\n");
	return 0;
}
