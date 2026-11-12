/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * polo-shell-ui's memory across hundreds of window menus and Alt+Tab
 * popups: GTK 3.24 on Wayland keeps every native window it destroys (a few
 * KB each) unless the shell UI lets them go. After a warm-up that fills
 * GTK's caches, 400 of each must not grow its proportional set by 1 MB.
 *
 *   test-shell-ui-popup-memory POLO POLO-SHELL-UI
 */

#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "harness.h"

#if defined(__SANITIZE_ADDRESS__)
#define SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SANITIZED 1
#endif
#endif

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static double
pss_mb(int pid)
{
	char path[64], line[256];
	long kb = 0;
	FILE *file;

	snprintf(path, sizeof path, "/proc/%d/smaps_rollup", pid);
	if (!(file = fopen(path, "r")))
		return 0;
	while (fgets(line, sizeof line, file))
		if (sscanf(line, "Pss: %ld", &kb) == 1)
			break;
	fclose(file);
	return kb / 1024.0;
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
}

static void
popups(struct harness *h, int n)
{
	for (int i = 0; i < n; i++) {
		harness_key(h, KEY_LEFTALT, true);
		tap(h, KEY_SPACE);
		harness_key(h, KEY_LEFTALT, false);
		pause_ms(h, 100);
		tap(h, KEY_ESC);
		pause_ms(h, 40);
		harness_key(h, KEY_LEFTALT, true);
		tap(h, KEY_TAB);
		pause_ms(h, 40);
		harness_key(h, KEY_LEFTALT, false);
		pause_ms(h, 40);
	}
	h->n_shell_ui_requests = 0;
}

int
main(int argc, char **argv)
{
	struct harness_window *w[2];
	struct harness *h;
	double before, after;
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

	popups(h, 80);
	pause_ms(h, 4000);
	before = pss_mb(shell_ui);
	popups(h, 400);
	pause_ms(h, 4000);
	after = pss_mb(shell_ui);
	printf("shell UI PSS %.2f MB after warming up, %.2f MB after 400 "
	       "window "
	       "menus and Alt+Tab popups\n",
	       before, after);
	CHECK(h->shell_ui_unbinds == 0 && kill(shell_ui, 0) == 0,
	      "polo-shell-ui went away");
#ifndef SANITIZED
	/* The sanitizer keeps freed memory in quarantine. */
	CHECK(after - before < 1.0,
	      "polo-shell-ui grew by %.2f MB over 400 window menus and Alt+Tab "
	      "popups",
	      after - before);
#endif

	harness_window_destroy(w[1]);
	harness_window_destroy(w[0]);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
