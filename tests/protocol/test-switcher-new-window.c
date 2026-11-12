/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Alt+Tab pressed while a window has committed its first buffer but has not
 * been painted yet, with the GL renderer: the switcher asks for a thumbnail
 * of a surface the renderer holds no buffer for. Polo must not abort.
 *
 *   test-switcher-new-window POLO POLO-SHELL-UI
 */

#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>

#include "harness.h"

#define WINDOWS 8

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

int
main(int argc, char **argv)
{
	struct harness_window *w[WINDOWS];
	struct harness *h;

	CHECK(argc >= 3, "usage: %s POLO POLO-SHELL-UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
	h = harness_start(argc, argv);
	for (int i = 0; i < 300 && h->shell_ui_binds == 0; i++)
		pause_ms(h, 20);
	CHECK(h->shell_ui_binds == 1, "polo-shell-ui did not start");

	for (int round = 0; round < 5; round++) {
		for (int i = 0; i < WINDOWS; i++) {
			w[i] = harness_window_open(h, 200, 150, "new");
			harness_key(h, KEY_LEFTALT, true);
			harness_key(h, KEY_TAB, true);
			harness_key(h, KEY_TAB, false);
			harness_key(h, KEY_LEFTALT, false);
		}
		pause_ms(h, 300);
		CHECK(kill(h->pid, 0) == 0, "round %d: Polo died", round);
		for (int i = 0; i < WINDOWS; i++)
			harness_window_close(w[i]);
		pause_ms(h, 100);
	}
	h->n_shell_ui_requests = 0;
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
