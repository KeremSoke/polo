/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>

#include <linux/input.h>

#include "harness.h"

#define STATE_MINIMIZED (1u << 1)
#define STATE_MAXIMIZED_H (1u << 2)
#define STATE_MAXIMIZED_V (1u << 3)
#define STATE_FULLSCREEN (1u << 4)
#define STATE_ABOVE (1u << 6)
#define STATE_STICKY (1u << 8)

static uint32_t
tap(struct harness *h, uint32_t key)
{
	uint32_t seen;

	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTSHIFT, true);
	h->last_key = 0;
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_roundtrip(h);
	seen = h->last_key;
	harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	harness_roundtrip(h);
	return seen;
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *a, *b;
	struct harness_window_info info;
	int status;

	setenv("POLO_TEST_PREFS",
	       "window-keybindings/toggle-fullscreen='<Control><Shift>f';"
	       "window-keybindings/toggle-above='<Control><Shift>b';"
	       "window-keybindings/maximize='<Control><Shift>x';"
	       "window-keybindings/unmaximize='<Control><Shift>u';"
	       "window-keybindings/minimize='<Control><Shift>n';"
	       "window-keybindings/close='<Control><Shift>q';"
	       "window-keybindings/raise='<Control><Shift>r';"
	       "window-keybindings/lower='<Control><Shift>l';"
	       "window-keybindings/raise-or-lower='<Control><Shift>o';"
	       "window-keybindings/maximize-vertically='<Control><Shift>v';"
	       "window-keybindings/maximize-horizontally='<Control><Shift>h';"
	       "window-keybindings/"
	       "toggle-on-all-workspaces='<Control><Shift>k';"
	       "window-keybindings/move-to-center='<Control><Shift>c';"
	       "window-keybindings/move-to-corner-nw='<Control><Shift>j';"
	       "window-keybindings/move-to-side-e='<Control><Shift>e';"
	       "window-keybindings/move-to-workspace-2='<Control><Shift>3';"
	       "window-keybindings/move-to-workspace-1='<Control><Shift>4';"
	       "global-keybindings/switch-to-workspace-1='<Control><Shift>1';"
	       "global-keybindings/switch-to-workspace-2='<Control><Shift>2';"
	       "global-keybindings/"
	       "switch-to-workspace-prev='<Control><Shift>p'",
	       1);
	h = harness_start(argc, argv);
	a = harness_window_create(h, 300, 200, "a");
	CHECK(h->keyboard_focus == a->surface, "a not focused");

	CHECK(tap(h, KEY_C) == 0, "move-to-center reached the client");
	info = *harness_window_info(a);
	CHECK(info.x == (HARNESS_OUTPUT_WIDTH - info.width) / 2 &&
		      info.y == (HARNESS_OUTPUT_HEIGHT - info.height) / 2,
	      "move-to-center put window at %d,%d", info.x, info.y);
	CHECK(tap(h, KEY_J) == 0, "move-to-corner reached the client");
	CHECK(harness_window_info(a)->x == 0 && harness_window_info(a)->y == 0,
	      "move-to-corner did not reach top left");
	CHECK(tap(h, KEY_E) == 0, "move-to-side reached the client");
	info = *harness_window_info(a);
	CHECK(info.x + info.width == HARNESS_OUTPUT_WIDTH,
	      "move-to-side did not reach right edge");

	CHECK(tap(h, KEY_B) == 0, "toggle-above reached the client");
	CHECK(harness_window_info(a)->state & STATE_ABOVE, "above not set");
	tap(h, KEY_B);
	CHECK(!(harness_window_info(a)->state & STATE_ABOVE),
	      "above not cleared");
	CHECK(tap(h, KEY_K) == 0, "toggle-sticky reached the client");
	CHECK(harness_window_info(a)->state & STATE_STICKY, "sticky not set");
	tap(h, KEY_K);
	CHECK(!(harness_window_info(a)->state & STATE_STICKY),
	      "sticky not cleared");

	CHECK(tap(h, KEY_V) == 0, "vertical maximize reached the client");
	CHECK(harness_window_info(a)->state & STATE_MAXIMIZED_V,
	      "vertical maximize failed");
	CHECK(tap(h, KEY_H) == 0, "horizontal maximize reached the client");
	CHECK(harness_window_info(a)->state & STATE_MAXIMIZED_H,
	      "horizontal maximize failed");
	CHECK(tap(h, KEY_U) == 0, "unmaximize reached the client");
	CHECK(!(harness_window_info(a)->state &
		(STATE_MAXIMIZED_H | STATE_MAXIMIZED_V)),
	      "unmaximize failed");
	tap(h, KEY_V);
	CHECK(harness_window_info(a)->state & STATE_MAXIMIZED_V,
	      "vertical maximize did not turn on");
	tap(h, KEY_V);
	CHECK(!(harness_window_info(a)->state & STATE_MAXIMIZED_V),
	      "vertical maximize did not turn off");
	tap(h, KEY_H);
	CHECK(harness_window_info(a)->state & STATE_MAXIMIZED_H,
	      "horizontal maximize did not turn on");
	tap(h, KEY_H);
	CHECK(!(harness_window_info(a)->state & STATE_MAXIMIZED_H),
	      "horizontal maximize did not turn off");
	CHECK(tap(h, KEY_X) == 0, "maximize reached the client");
	CHECK((harness_window_info(a)->state &
	       (STATE_MAXIMIZED_H | STATE_MAXIMIZED_V)) ==
		      (STATE_MAXIMIZED_H | STATE_MAXIMIZED_V),
	      "maximize failed");
	tap(h, KEY_U);
	CHECK(tap(h, KEY_F) == 0, "fullscreen reached the client");
	CHECK(harness_window_info(a)->state & STATE_FULLSCREEN,
	      "fullscreen failed");
	tap(h, KEY_F);
	CHECK(!(harness_window_info(a)->state & STATE_FULLSCREEN),
	      "unfullscreen failed");

	CHECK(tap(h, KEY_2) == 0, "workspace switch reached the client");
	CHECK(h->keyboard_focus == NULL, "focus remained on hidden workspace");
	CHECK(tap(h, KEY_P) == 0, "previous workspace reached the client");
	CHECK(h->keyboard_focus == a->surface,
	      "previous workspace did not restore focus");
	CHECK(tap(h, KEY_3) == 0, "move-to-workspace reached the client");
	CHECK(harness_window_info(a)->workspace == 1,
	      "window did not move to workspace 2");
	CHECK(tap(h, KEY_2) == 0, "workspace switch reached the client");
	CHECK(h->keyboard_focus == a->surface,
	      "moved window did not regain focus");
	CHECK(tap(h, KEY_4) == 0, "move-to-workspace-1 reached the client");
	CHECK(harness_window_info(a)->workspace == 0,
	      "window did not move to workspace 1");
	CHECK(tap(h, KEY_1) == 0, "workspace switch reached the client");
	CHECK(h->keyboard_focus == a->surface,
	      "workspace 1 did not restore focus");

	b = harness_window_create(h, 300, 200, "b");
	CHECK(h->keyboard_focus == b->surface, "b not focused");
	CHECK(tap(h, KEY_L) == 0, "lower reached the client");
	CHECK(harness_window_info(b)->stack_index <
		      harness_window_info(a)->stack_index,
	      "lower failed");
	CHECK(tap(h, KEY_R) == 0, "raise reached the client");
	CHECK(harness_window_info(b)->stack_index >
		      harness_window_info(a)->stack_index,
	      "raise failed");
	CHECK(tap(h, KEY_O) == 0, "raise-or-lower reached the client");
	CHECK(harness_window_info(b)->stack_index <
		      harness_window_info(a)->stack_index,
	      "raise-or-lower did not lower unobscured window");
	CHECK(tap(h, KEY_Q) == 0, "close reached the client");
	CHECK(b->close_requested, "close request was not delivered");
	CHECK(tap(h, KEY_N) == 0, "minimize reached the client");
	CHECK(harness_window_info(b)->state & STATE_MINIMIZED,
	      "minimize failed");

	harness_window_destroy(b);
	harness_window_destroy(a);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
