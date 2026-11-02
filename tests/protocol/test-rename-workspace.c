/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * argv[2]: polo-shell-ui. Marco's rename-workspace through the real entry
 * dialog: typing and Return write name-N, Escape and an empty name change
 * nothing, and the binding opens one dialog at a time.
 */
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "harness.h"

static void
settle(struct harness *h, int ms)
{
	struct timespec ts = { 0, 20 * 1000000L };

	for (int i = 0; i < ms / 20; i++) {
		harness_roundtrip(h);
		nanosleep(&ts, NULL);
	}
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
}

static void
binding(struct harness *h)
{
	harness_key(h, KEY_LEFTCTRL, true);
	tap(h, KEY_F9);
	harness_key(h, KEY_LEFTCTRL, false);
	settle(h, 600);
}

static const char *
name_1(struct harness *h)
{
	return harness_get_setting(h, "org.mate.Marco.workspace-names",
				   "name-1");
}

static void
wait_name(struct harness *h, const char *part)
{
	for (int i = 0; i < 100 && !strstr(name_1(h), part); i++)
		settle(h, 40);
	CHECK(strstr(name_1(h), part), "name-1 is %s, not %s", name_1(h), part);
}

int
main(int argc, char **argv)
{
	struct harness *h;

	CHECK(argc >= 3, "usage: %s POLO POLO-SHELL-UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	setenv("POLO_TEST_PREFS",
	       "global-keybindings/rename-workspace='<Control>F9'", 1);
	h = harness_start(argc, argv);
	for (int i = 0; i < 100 && h->shell_ui_binds == 0; i++)
		settle(h, 40);
	CHECK(h->shell_ui_binds == 1, "polo-shell-ui did not bind");

	binding(h);
	tap(h, KEY_P);
	tap(h, KEY_O);
	tap(h, KEY_ENTER);
	wait_name(h, "po");

	/* Escape cancels; the entry starts with the current name. */
	binding(h);
	tap(h, KEY_X);
	tap(h, KEY_ESC);
	settle(h, 500);
	CHECK(strstr(name_1(h), "po") != NULL, "Escape wrote %s", name_1(h));

	/* An empty name changes nothing. */
	binding(h);
	tap(h, KEY_BACKSPACE);
	tap(h, KEY_ENTER);
	settle(h, 500);
	CHECK(strstr(name_1(h), "po") != NULL, "empty name wrote %s",
	      name_1(h));

	/* A second press while one is open opens no second dialog. */
	h->n_shell_ui_requests = 0;
	binding(h);
	binding(h);
	tap(h, KEY_Q);
	tap(h, KEY_ENTER);
	wait_name(h, "q");
	settle(h, 300);
	tap(h, KEY_ENTER);
	settle(h, 500);
	CHECK(h->n_shell_ui_requests == 1, "%d dialogs answered, not one",
	      h->n_shell_ui_requests);

	CHECK(h->shell_ui_unbinds == 0, "polo-shell-ui crashed");
	return harness_stop(h);
}
