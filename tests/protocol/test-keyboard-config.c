/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The MATE keyboard settings msd applies under X, through Polo: repeat
 * rate and delay as wl_keyboard.repeat_info, layouts and options as the
 * keymap, libxklavier's layout group per window, and the remembered
 * NumLock state.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input.h>

#include "harness.h"

#define KEYBOARD "org.mate.peripherals-keyboard"
#define KBD "org.mate.peripherals-keyboard-xkb.kbd"
#define GENERAL "org.mate.peripherals-keyboard-xkb.general"
#define MOD2_MASK (1u << 4)

static bool
inside(const struct harness_window_info *r, int x, int y)
{
	return x >= r->x && x < r->x + r->width && y >= r->y &&
		y < r->y + r->height;
}

/* Clicks w where other does not cover it. */
static void
click(struct harness *h, struct harness_window *w, struct harness_window *other)
{
	struct harness_window_info info = *harness_window_info(w);
	struct harness_window_info above = *harness_window_info(other);
	int xs[] = { info.x + 5, info.x + info.width - 5 };
	int ys[] = { info.y + 5, info.y + info.height - 5 };
	int i;

	for (i = 0; i < 4; i++) {
		int x = xs[i % 2], y = ys[i / 2];

		if (!inside(&above, x, y)) {
			harness_move_pointer(h, x, y);
			harness_button(h, BTN_LEFT, true);
			harness_button(h, BTN_LEFT, false);
			harness_roundtrip(h);
			CHECK(h->keyboard_focus == w->surface,
			      "the click at %d,%d did not focus", x, y);
			return;
		}
	}
	CHECK(false, "no visible part of %d,%d %dx%d under %d,%d %dx%d", info.x,
	      info.y, info.width, info.height, above.x, above.y, above.width,
	      above.height);
}

static void
switch_group(struct harness *h)
{
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_LEFTSHIFT, true);
	harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_roundtrip(h);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *a, *b;
	int keymaps, infos, status;

	setenv("POLO_TEST_SETTINGS",
	       KEYBOARD "/remember-numlock-state=true;" KEYBOARD
			"/numlock-state='on'",
	       1);
	h = harness_start(argc, argv);
	a = harness_window_create(h, 300, 200, "a");
	CHECK(h->keyboard_focus == a->surface, "a not focused");

	CHECK(h->repeat_rate == 30 && h->repeat_delay == 500,
	      "default repeat %d/s after %d ms", h->repeat_rate,
	      h->repeat_delay);
	infos = h->repeat_infos;
	harness_set_setting(h, KEYBOARD, "rate", "50");
	harness_set_setting(h, KEYBOARD, "delay", "250");
	CHECK(h->repeat_infos > infos && h->repeat_rate == 50 &&
		      h->repeat_delay == 250,
	      "repeat changed to %d/s after %d ms", h->repeat_rate,
	      h->repeat_delay);
	harness_set_setting(h, KEYBOARD, "repeat", "false");
	CHECK(h->repeat_rate == 0, "repeat off gave rate %d", h->repeat_rate);
	harness_set_setting(h, KEYBOARD, "repeat", "true");
	CHECK(h->repeat_rate == 50, "repeat on gave rate %d", h->repeat_rate);

	CHECK(h->mods_locked & MOD2_MASK, "the remembered NumLock is off");
	harness_key(h, KEY_NUMLOCK, true);
	harness_key(h, KEY_NUMLOCK, false);
	harness_roundtrip(h);
	CHECK(!(h->mods_locked & MOD2_MASK), "NumLock did not toggle");
	CHECK(strcmp(harness_get_setting(h, KEYBOARD, "numlock-state"),
		     "'off'") == 0,
	      "numlock-state saved as %s", h->setting);

	keymaps = h->keymaps;
	harness_set_setting(h, KBD, "options",
			    "['grp\\tgrp:alt_shift_toggle']");
	harness_set_setting(h, KBD, "layouts", "['us', 'ru\\tphonetic']");
	CHECK(h->keymaps > keymaps, "no new keymap");
	CHECK(h->group == 0, "group %u after the new keymap", h->group);
	switch_group(h);
	CHECK(h->group == 1, "Alt+Shift gave group %u", h->group);

	keymaps = h->keymaps;
	harness_set_setting(h, KBD, "model", "'pc105'");
	harness_set_setting(h, KBD, "model", "''");
	CHECK(h->keymaps == keymaps + 2, "model changes gave %d keymaps",
	      h->keymaps - keymaps);
	keymaps = h->keymaps;
	harness_set_setting(h, GENERAL, "disable-indicator", "true");
	CHECK(h->keymaps == keymaps, "an unrelated key recompiled the keymap");
	switch_group(h);
	CHECK(h->group == 1, "group %u", h->group);

	harness_set_setting(h, GENERAL, "default-group", "0");
	b = harness_window_create(h, 400, 80, "b");
	CHECK(h->keyboard_focus == b->surface, "b not focused");
	CHECK(h->group == 0, "the new window got group %u, not default-group",
	      h->group);
	click(h, a, b);
	CHECK(h->group == 1, "a's group %u was not restored", h->group);
	switch_group(h);
	CHECK(h->group == 0, "group %u", h->group);
	switch_group(h);
	click(h, b, a);
	CHECK(h->group == 0, "b's group %u was not restored", h->group);

	harness_set_setting(h, GENERAL, "group-per-window", "false");
	click(h, a, b);
	CHECK(h->group == 0, "a restored group %u with group-per-window off",
	      h->group);

	keymaps = h->keymaps;
	harness_set_setting(h, KBD, "layouts", "['us', 'nosuchlayout']");
	CHECK(h->keymaps == keymaps + 1,
	      "an unknown layout was not filtered out");

	harness_window_destroy(b);
	harness_window_destroy(a);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
