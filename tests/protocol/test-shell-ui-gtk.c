/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * argv[2]: polo-shell-ui. The real helper starts under headless Polo,
 * binds polo_shell_ui_v1, and answers the window menu and dialogs it does
 * not draw yet as dismissed. Alt-Tab shows its popup, with thumbnails;
 * Alt-Space opens the window menu, whose Minimize mnemonic and Escape work.
 */
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "harness.h"

static bool
has_request(struct harness *h, const char *text)
{
	for (int i = 0; i < h->n_shell_ui_requests; i++)
		if (strcmp(h->shell_ui_requests[i], text) == 0)
			return true;
	return false;
}

static void
wait_for(struct harness *h, bool (*done)(struct harness *), const char *what)
{
	struct timespec ts = { 0, 10 * 1000000L };

	for (int i = 0; i < 1000 && !done(h); i++) {
		harness_roundtrip(h);
		nanosleep(&ts, NULL);
	}
	CHECK(done(h), "timed out waiting for %s", what);
}

static bool
bound(struct harness *h)
{
	return h->shell_ui_binds == 1;
}

static bool
answered(struct harness *h)
{
	return has_request(h, "menu_action 7 0 0") &&
		has_request(h, "dialog_response 3 -1 (null)");
}

static bool
answered_none(struct harness *h)
{
	return h->n_shell_ui_requests == 1 &&
		strncmp(h->shell_ui_requests[0], "menu_action ", 12) == 0 &&
		strstr(h->shell_ui_requests[0], " 0 0") != NULL;
}

static bool
answered_minimize(struct harness *h)
{
	return h->n_shell_ui_requests == 1 &&
		strstr(h->shell_ui_requests[0], " 2 0") != NULL;
}

int
main(int argc, char **argv)
{
	struct harness *h;

	CHECK(argc >= 3, "usage: %s POLO POLO-SHELL-UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	h = harness_start(argc, argv);
	wait_for(h, bound, "polo-shell-ui to bind");
	polo_test_v1_shell_ui_samples(h->test);
	wait_for(h, answered, "polo-shell-ui to answer");
	CHECK(h->shell_ui_binds == 1 && h->shell_ui_unbinds == 0 &&
		      h->shell_ui_pid > 0,
	      "polo-shell-ui stayed up");

	struct harness_window *a = harness_window_create(h, 300, 200, "alpha");
	struct harness_window *b = harness_window_create(h, 200, 300, "beta");
	struct timespec ts = { 0, 300 * 1000000L };

	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_TAB, true);
	harness_key(h, KEY_TAB, false);
	nanosleep(&ts, NULL);
	harness_roundtrip(h);
	harness_key(h, KEY_LEFTALT, false);
	CHECK(harness_window_info(a)->focused, "Alt-Tab did not focus alpha");
	nanosleep(&ts, NULL);
	harness_roundtrip(h);
	CHECK(h->shell_ui_binds == 1 && h->shell_ui_unbinds == 0,
	      "polo-shell-ui did not survive the popup");

	h->n_shell_ui_requests = 0;
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_SPACE, true);
	harness_key(h, KEY_SPACE, false);
	harness_key(h, KEY_LEFTALT, false);
	nanosleep(&ts, NULL);
	harness_roundtrip(h);
	CHECK(h->n_shell_ui_requests == 0, "the window menu closed at once: %s",
	      h->shell_ui_requests[0]);
	harness_key(h, KEY_ESC, true);
	harness_key(h, KEY_ESC, false);
	wait_for(h, answered_none, "the window menu to close on Escape");
	CHECK(harness_window_info(a)->focused, "Escape changed the focus");

	h->n_shell_ui_requests = 0;
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_SPACE, true);
	harness_key(h, KEY_SPACE, false);
	harness_key(h, KEY_LEFTALT, false);
	nanosleep(&ts, NULL);
	harness_roundtrip(h);
	harness_key(h, KEY_N, true);
	harness_key(h, KEY_N, false);
	wait_for(h, answered_minimize, "Minimize from the window menu");
	CHECK(harness_window_info(a)->state & (1u << 1),
	      "the window menu did not minimize alpha");
	/* As a person does it: the menu maps while the keys are still down. */
	h->n_shell_ui_requests = 0;
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_SPACE, true);
	nanosleep(&ts, NULL);
	harness_roundtrip(h);
	CHECK(h->n_shell_ui_requests == 0, "menu closed while keys held: %s",
	      h->shell_ui_requests[0]);
	harness_key(h, KEY_SPACE, false);
	harness_key(h, KEY_LEFTALT, false);
	nanosleep(&ts, NULL);
	harness_roundtrip(h);
	CHECK(h->n_shell_ui_requests == 0, "menu closed on release: %s",
	      h->shell_ui_requests[0]);
	harness_key(h, KEY_ESC, true);
	harness_key(h, KEY_ESC, false);
	wait_for(h, answered_none, "held-keys menu to close on Escape");

	/* Space's release went to the menu: the client's next Space must not
	 * lose its own. */
	nanosleep(&ts, NULL);
	harness_roundtrip(h);
	CHECK(harness_window_info(b)->focused,
	      "beta lost the focus to the menu");
	harness_key(h, KEY_SPACE, true);
	harness_key(h, KEY_SPACE, false);
	harness_roundtrip(h);
	CHECK(h->last_key == KEY_SPACE && !h->last_key_pressed,
	      "the client never got Space's release");

	/* A button menu: the release comes after the menu maps. */
	h->n_shell_ui_requests = 0;
	const struct harness_window_info *bi = harness_window_info(b);
	harness_move_pointer(h, bi->x + 20, bi->y + 20);
	harness_wait_repaint(h);
	harness_key(h, KEY_LEFTALT, true);
	harness_button(h, BTN_MIDDLE, true);
	nanosleep(&ts, NULL);
	harness_roundtrip(h);
	CHECK(h->n_shell_ui_requests == 0, "menu closed while button held: %s",
	      h->shell_ui_requests[0]);
	harness_button(h, BTN_MIDDLE, false);
	harness_key(h, KEY_LEFTALT, false);
	nanosleep(&ts, NULL);
	harness_roundtrip(h);
	CHECK(h->n_shell_ui_requests == 0, "menu closed on button release: %s",
	      h->shell_ui_requests[0]);
	harness_key(h, KEY_ESC, true);
	harness_key(h, KEY_ESC, false);
	wait_for(h, answered_none, "button menu to close on Escape");

	harness_window_destroy(a);
	harness_window_destroy(b);
	return harness_stop(h);
}
