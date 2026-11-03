/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tests that shading rolls a framed window up to its titlebar without resizing
 * or unfocusing its client.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "harness.h"

#define STATE_SHADED (1u << 5)

/* Long enough for a repaint, which makes new frame views pickable. */
static void
settle(struct harness *h)
{
	for (int i = 0; i < 6; i++) {
		harness_roundtrip(h);
		usleep(10000);
	}
}

/* The harness helpers wait for a frame, which a shaded client never gets. */
static void
move_to(struct harness *h, int x, int y)
{
	polo_test_v1_move_pointer(h->test, x, y);
	settle(h);
}

static void
click(struct harness *h, int x, int y)
{
	move_to(h, x, y);
	polo_test_v1_button(h->test, BTN_LEFT,
			    WL_POINTER_BUTTON_STATE_PRESSED);
	settle(h);
	polo_test_v1_button(h->test, BTN_LEFT,
			    WL_POINTER_BUTTON_STATE_RELEASED);
	settle(h);
}

static void
toggle_by_key(struct harness *h)
{
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTSHIFT, true);
	harness_key(h, KEY_S, true);
	harness_key(h, KEY_S, false);
	harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	settle(h);
}

static bool
pointer_on(struct harness *h, struct harness_window *w, int x, int y)
{
	move_to(h, x + 1, y);
	move_to(h, x, y);
	return h->pointer_focus == w->surface;
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w;
	const struct harness_window_info *info;
	int x, y, width, height, configures;

	if (access("/usr/share/themes/Atlanta/metacity-1", F_OK) != 0) {
		printf("Atlanta is not installed\n");
		return 77;
	}
	setenv("POLO_TEST_PREFS",
	       "general/theme='Atlanta';"
	       "general/action-double-click-titlebar='toggle_shade';"
	       "window-keybindings/toggle-shaded='<Control><Shift>s'",
	       1);
	h = harness_start(argc, argv);

	w = harness_window_create_decorated(h, 300, 200, "framed");
	settle(h);
	info = harness_window_info(w);
	x = info->x;
	y = info->y;
	width = info->width;
	height = info->height;
	CHECK(info->focused, "the new window is not focused");
	CHECK(pointer_on(h, w, x + width / 2, y + height / 2),
	      "the client does not take the pointer before shading");
	configures = w->configure_count;

	click(h, x + 40, y - 8);
	click(h, x + 40, y - 8);
	settle(h);
	info = harness_window_info(w);
	CHECK(info->state & STATE_SHADED,
	      "a titlebar double-click did not shade (state 0x%x)",
	      info->state);
	CHECK(info->focused, "the shaded window lost the focus");
	CHECK(info->x == x && info->y == y && info->width == width &&
		      info->height == height,
	      "shading moved the client to %d,%d %dx%d", info->x, info->y,
	      info->width, info->height);
	CHECK(w->width == width && w->height == height &&
		      w->configure_count == configures,
	      "shading resized the client to %dx%d after %d configures",
	      w->width, w->height, w->configure_count - configures);
	CHECK(!pointer_on(h, w, x + width / 2, y + height / 2),
	      "the hidden client still takes the pointer");

	click(h, x + 40, y - 8);
	click(h, x + 40, y - 8);
	settle(h);
	info = harness_window_info(w);
	CHECK(!(info->state & STATE_SHADED),
	      "a second double-click did not unshade (state 0x%x)",
	      info->state);
	CHECK(pointer_on(h, w, x + width / 2, y + height / 2),
	      "the unshaded client does not take the pointer");

	toggle_by_key(h);
	info = harness_window_info(w);
	CHECK(info->state & STATE_SHADED,
	      "toggle-shaded did not shade (state 0x%x)", info->state);
	toggle_by_key(h);
	info = harness_window_info(w);
	CHECK(!(info->state & STATE_SHADED),
	      "toggle-shaded did not unshade (state 0x%x)", info->state);

	harness_window_destroy(w);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
