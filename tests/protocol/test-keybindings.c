/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The key binding engine against headless Polo: a bound key runs its action
 * and never reaches the client (press nor release), other keys do, Shift
 * and a second layout still match, and Lock does not get in the way.
 */

#include <stdio.h>
#include <stdlib.h>

#include <linux/input.h>

#include "harness.h"

#define STATE_MAXIMIZED ((1u << 2) | (1u << 3))

/* Press mods, tap key, release mods; returns the last key the client saw
 * after the tap (0 for none since the modifiers went down). */
static uint32_t
tap(struct harness *h, const uint32_t *mods, int n_mods, uint32_t key)
{
	uint32_t seen;
	int i;

	for (i = 0; i < n_mods; i++)
		harness_key(h, mods[i], true);
	h->last_key = 0;
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_roundtrip(h);
	seen = h->last_key;
	for (i = n_mods - 1; i >= 0; i--)
		harness_key(h, mods[i], false);
	harness_roundtrip(h);
	harness_wait_repaint(h);
	return seen;
}

static bool
maximized(struct harness_window *w)
{
	return (harness_window_info(w)->state & STATE_MAXIMIZED) ==
		STATE_MAXIMIZED;
}

int
main(int argc, char **argv)
{
	static const uint32_t ctrl_shift[] = { KEY_LEFTCTRL, KEY_LEFTSHIFT };
	struct harness *h;
	struct harness_window *a;
	int status;

	setenv("XKB_DEFAULT_LAYOUT", "us,ru", 1);
	setenv("XKB_DEFAULT_OPTIONS", "grp:caps_toggle", 1);
	setenv("POLO_TEST_PREFS",
	       "window-keybindings/toggle-maximized='<Control><Shift>m';"
	       "window-keybindings/maximize='<Control><Shift>a'",
	       1);
	h = harness_start(argc, argv);
	a = harness_window_create(h, 400, 300, "a");
	CHECK(h->keyboard_focus == a->surface, "a not focused");

	CHECK(tap(h, NULL, 0, KEY_M) == KEY_M,
	      "an unbound M did not reach the client");
	CHECK(tap(h, ctrl_shift, 2, KEY_N) == KEY_N,
	      "Ctrl+Shift+N did not reach the client");
	CHECK(!maximized(a), "an unbound key maximized");

	/* With Shift the key gives "M": the binding matches lowercased. */
	CHECK(tap(h, ctrl_shift, 2, KEY_M) == 0, "the client saw Ctrl+Shift+M");
	CHECK(maximized(a), "Ctrl+Shift+M did not maximize");
	CHECK(tap(h, ctrl_shift, 2, KEY_M) == 0, "the client saw Ctrl+Shift+M");
	CHECK(!maximized(a), "Ctrl+Shift+M did not unmaximize");

	/* Caps Lock switches to the Russian layout, where the key gives a
	 * Cyrillic letter: the binding matches through the first layout's
	 * symbol, and Lock does not count as a modifier. */
	tap(h, NULL, 0, KEY_CAPSLOCK);
	CHECK(tap(h, ctrl_shift, 2, KEY_M) == 0,
	      "the client saw Ctrl+Shift+M in ru");
	CHECK(maximized(a),
	      "Ctrl+Shift+M in the second layout did not maximize");
	tap(h, ctrl_shift, 2, KEY_M);
	CHECK(!maximized(a),
	      "Ctrl+Shift+M in the second layout did not unmaximize");
	tap(h, NULL, 0, KEY_CAPSLOCK);

	CHECK(tap(h, ctrl_shift, 2, KEY_A) == 0,
	      "Ctrl+Shift+A, bound to maximize, reached the client");
	CHECK(maximized(a), "Ctrl+Shift+A did not maximize");

	harness_window_destroy(a);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
