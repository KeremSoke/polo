/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Screens plugged in and out while polo-shell-ui has shown and closed
 * popups and window menus: GTK 3 leaves every closed popup's window connected
 * to the screen's
 * monitors-changed signal, and a monitor change then runs into the freed
 * window. The shell UI must stay the same process, bound, through every
 * change.
 *
 *   test-shell-ui-hotplug POLO POLO-SHELL-UI
 */

#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "harness.h"

static const int modes[][3] = { { 1280, 720, 60000 } };

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static void
popups(struct harness *h)
{
	for (int i = 0; i < 2; i++) {
		harness_key(h, KEY_LEFTALT, true);
		harness_key(h, KEY_SPACE, true);
		harness_key(h, KEY_SPACE, false);
		harness_key(h, KEY_LEFTALT, false);
		pause_ms(h, 250);
		harness_key(h, KEY_ESC, true);
		harness_key(h, KEY_ESC, false);
		pause_ms(h, 100);
	}
	for (int i = 0; i < 4; i++) {
		polo_test_v1_shell_ui_samples(h->test);
		harness_key(h, KEY_LEFTALT, true);
		harness_key(h, KEY_TAB, true);
		harness_key(h, KEY_TAB, false);
		pause_ms(h, 30);
		harness_key(h, KEY_LEFTALT, false);
		pause_ms(h, 30);
	}
	h->n_shell_ui_requests = 0;
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w[2];
	int shell_ui;

	CHECK(argc >= 3, "usage: %s POLO POLO-SHELL-UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	h = harness_start(argc, argv);
	for (int i = 0; i < 300 && h->shell_ui_binds == 0; i++)
		pause_ms(h, 20);
	CHECK(h->shell_ui_binds == 1, "polo-shell-ui did not start");
	shell_ui = h->shell_ui_pid;
	w[0] = harness_window_create(h, 300, 200, "one");
	w[1] = harness_window_create(h, 300, 200, "two");

	for (int round = 0; round < 8; round++) {
		popups(h);
		pause_ms(h, 1300);
		harness_add_output(h, "HDMI-A-1", "DEL", 0x4321, 7, "Plugged",
				   false, 527, 296, modes, 1);
		pause_ms(h, 150);
		popups(h);
		pause_ms(h, 1300);
		harness_remove_output(h, "HDMI-A-1");
		pause_ms(h, 150);
		CHECK(h->shell_ui_binds == 1 && h->shell_ui_unbinds == 0 &&
			      h->shell_ui_pid == shell_ui &&
			      kill(shell_ui, 0) == 0,
		      "round %d: polo-shell-ui went away (binds %d, unbinds "
		      "%d)",
		      round, h->shell_ui_binds, h->shell_ui_unbinds);
	}

	harness_window_destroy(w[1]);
	harness_window_destroy(w[0]);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
