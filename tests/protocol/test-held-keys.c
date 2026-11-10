/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A key Polo takes for a shortcut is not a key the client holds. When the
 * keyboard focus moves while such a key is still down, the window that gets
 * the focus must not be told it is down (Xwayland turns that list into key
 * presses, and with the release swallowed the key repeats for ever), while
 * the modifiers held with it are still listed.
 */

#include <stdlib.h>

#include <linux/input.h>

#include "harness.h"

static bool
lists(struct harness *h, uint32_t key)
{
	for (int i = 0; i < h->n_enter_keys; i++)
		if (h->enter_keys[i] == key)
			return true;
	return false;
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *a, *b;

	setenv("POLO_TEST_PREFS",
	       "window-keybindings/toggle-maximized='<Control><Shift>m'", 1);
	h = harness_start(argc, argv);
	a = harness_window_create(h, 400, 300, "a");
	CHECK(h->keyboard_focus == a->surface, "a not focused");

	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTSHIFT, true);
	h->last_key = 0;
	harness_key(h, KEY_M, true);
	harness_roundtrip(h);
	CHECK(h->last_key != KEY_M, "the shortcut key reached the client");

	b = harness_window_create(h, 300, 200, "b");
	CHECK(h->keyboard_focus == b->surface, "b not focused");
	CHECK(!lists(h, KEY_M),
	      "the new window is told the shortcut key is held");
	CHECK(lists(h, KEY_LEFTCTRL) && lists(h, KEY_LEFTSHIFT),
	      "the modifiers held are not listed");

	h->last_key = 0;
	harness_key(h, KEY_M, false);
	harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	harness_roundtrip(h);
	CHECK(h->last_key != KEY_M, "the shortcut key's release reached b");

	harness_window_destroy(b);
	harness_window_destroy(a);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
