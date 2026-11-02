/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * argv[2]: fake-shell-ui. Marco's window switching: Alt-Tab in MRU order,
 * Shift and arrows, Escape, minimized windows, Alt-Escape cycling, a press
 * outside the popup, and a selection made in the popup.
 */
#include <errno.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "harness.h"

#define STATE_MINIMIZED (1u << 1)

static char log_path[300];

static void
pause_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static void
new_log(void)
{
	int fd;

	snprintf(log_path, sizeof log_path, "%s/polo-switcher-XXXXXX",
		 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	fd = mkstemp(log_path);
	CHECK(fd >= 0, "mkstemp: %s", strerror(errno));
	close(fd);
	setenv("FAKE_SHELL_UI_LOG", log_path, 1);
}

static void
clear_log(void)
{
	FILE *log = fopen(log_path, "w");

	CHECK(log, "cannot truncate %s", log_path);
	fclose(log);
}

static bool
logged(const char *line)
{
	FILE *log = fopen(log_path, "r");
	char buf[512];
	bool found = false;

	CHECK(log, "cannot read %s", log_path);
	while (!found && fgets(buf, sizeof buf, log)) {
		buf[strcspn(buf, "\n")] = '\0';
		found = strcmp(buf, line) == 0;
	}
	fclose(log);
	return found;
}

static void
wait_logged(struct harness *h, const char *line)
{
	for (int i = 0; i < 300 && !logged(line); i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	CHECK(logged(line), "fake-shell-ui never got \"%s\"", line);
}

static void
wait_helper(struct harness *h)
{
	for (int i = 0; i < 300 && h->shell_ui_binds == 0; i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	CHECK(h->shell_ui_binds == 1, "fake-shell-ui did not bind");
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
}

static bool
focused(struct harness_window *w)
{
	return harness_window_info(w)->focused;
}

static struct harness_window *
window(struct harness *h, const char *title)
{
	struct harness_window *w = harness_window_create(h, 200, 100, title);

	CHECK(focused(w), "%s is not focused when mapped", title);
	return w;
}

static void
switcher_runs(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *a, *b, *c;

	new_log();
	h = harness_start(argc, argv);
	wait_helper(h);
	a = window(h, "alpha");
	b = window(h, "beta");
	c = window(h, "gamma");

	/* MRU gamma, beta, alpha: Alt-Tab goes to the second. */
	clear_log();
	harness_key(h, KEY_LEFTALT, true);
	tap(h, KEY_TAB);
	wait_logged(h, "show_switcher output 2 5 0 48");
	CHECK(logged("switcher_entry 1 gamma org.mate.PoloTest 0") &&
		      logged("switcher_entry 2 beta org.mate.PoloTest 0") &&
		      logged("switcher_entry 3 alpha org.mate.PoloTest 0"),
	      "entries are not in MRU order");
	CHECK(logged("switcher_thumbnail 1 67x33"),
	      "no thumbnail scaled as Marco's get_window_surface");
	CHECK(focused(c), "focus moved while the popup shows");
	harness_key(h, KEY_LEFTALT, false);
	wait_logged(h, "hide_switcher");
	CHECK(focused(b), "Alt-Tab did not focus beta");

	/* MRU beta, gamma, alpha: Tab twice reaches alpha. */
	clear_log();
	harness_key(h, KEY_LEFTALT, true);
	tap(h, KEY_TAB);
	tap(h, KEY_TAB);
	wait_logged(h, "update_selected 3");
	harness_key(h, KEY_LEFTALT, false);
	CHECK(focused(a), "Alt-Tab-Tab did not focus alpha");

	/* MRU alpha, beta, gamma: Shift starts from the end of the MRU list. */
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_LEFTSHIFT, true);
	tap(h, KEY_TAB);
	harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, KEY_LEFTALT, false);
	CHECK(focused(c), "Alt-Shift-Tab did not focus gamma");

	/* MRU gamma, alpha, beta: Escape cancels. */
	clear_log();
	harness_key(h, KEY_LEFTALT, true);
	tap(h, KEY_TAB);
	tap(h, KEY_ESC);
	wait_logged(h, "hide_switcher");
	harness_key(h, KEY_LEFTALT, false);
	CHECK(focused(c), "Escape did not cancel");

	/* Right moves on like Tab. */
	harness_key(h, KEY_LEFTALT, true);
	tap(h, KEY_TAB);
	tap(h, KEY_RIGHT);
	harness_key(h, KEY_LEFTALT, false);
	CHECK(focused(b), "Alt-Tab-Right did not focus beta");

	/* A press outside the popup cancels. */
	clear_log();
	harness_key(h, KEY_LEFTALT, true);
	tap(h, KEY_TAB);
	harness_click(h, 1000, 700);
	wait_logged(h, "hide_switcher");
	harness_key(h, KEY_LEFTALT, false);
	CHECK(focused(b), "a press outside the popup did not cancel");

	/* MRU beta, gamma, alpha; alpha minimized goes last, marked hidden. */
	xdg_toplevel_set_minimized(a->xdg_toplevel);
	harness_roundtrip(h);
	CHECK(harness_window_info(a)->state & STATE_MINIMIZED, "not minimized");
	clear_log();
	harness_key(h, KEY_LEFTALT, true);
	tap(h, KEY_TAB);
	tap(h, KEY_TAB);
	wait_logged(h, "switcher_entry 3 alpha org.mate.PoloTest 2");
	harness_key(h, KEY_LEFTALT, false);
	CHECK(!(harness_window_info(a)->state & STATE_MINIMIZED) && focused(a),
	      "the minimized window was not restored and focused");

	/* Alt-Escape raises in place without a popup; another key cancels. */
	clear_log();
	harness_key(h, KEY_LEFTALT, true);
	tap(h, KEY_ESC);
	CHECK(harness_window_info(b)->stack_index >
			      harness_window_info(a)->stack_index &&
		      harness_window_info(b)->stack_index >
			      harness_window_info(c)->stack_index,
	      "Alt-Escape did not raise beta");
	tap(h, KEY_A);
	harness_key(h, KEY_LEFTALT, false);
	CHECK(focused(a) &&
		      harness_window_info(a)->stack_index >
			      harness_window_info(b)->stack_index,
	      "a key did not restore the stacking");
	harness_key(h, KEY_LEFTALT, true);
	tap(h, KEY_ESC);
	harness_key(h, KEY_LEFTALT, false);
	CHECK(focused(b), "Alt-Escape did not focus beta");
	CHECK(!logged("show_switcher output 2 5 0 48") &&
		      !logged("show_switcher output 1 5 0 48"),
	      "Alt-Escape showed the popup");

	harness_window_destroy(a);
	harness_window_destroy(b);
	harness_window_destroy(c);
	CHECK(harness_stop(h) == 0, "polo exit status");
	unlink(log_path);
}

static void
popup_selects(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *a, *b, *c;

	new_log();
	setenv("FAKE_SHELL_UI_SELECT", "3", 1);
	h = harness_start(argc, argv);
	wait_helper(h);
	a = window(h, "alpha");
	b = window(h, "beta");
	c = window(h, "gamma");
	harness_key(h, KEY_LEFTALT, true);
	tap(h, KEY_TAB);
	wait_logged(h, "update_selected 3");
	harness_key(h, KEY_LEFTALT, false);
	CHECK(focused(a), "the entry chosen in the popup was not focused");
	harness_window_destroy(a);
	harness_window_destroy(b);
	harness_window_destroy(c);
	CHECK(harness_stop(h) == 0, "polo exit status");
	unsetenv("FAKE_SHELL_UI_SELECT");
	unlink(log_path);
}

int
main(int argc, char **argv)
{
	CHECK(argc >= 3, "usage: %s POLO FAKE-SHELL-UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	switcher_runs(argc, argv);
	popup_selects(argc, argv);
	return 0;
}
