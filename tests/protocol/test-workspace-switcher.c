/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * argv[2]: fake-shell-ui. Marco's handle_workspace_switch_or_move () and
 * process_workspace_switch_grab (): each press switches at once, the popup
 * lists the grid while the modifier is held, the wrap style applies, the move
 * keys carry the focused window, and one workspace does nothing.
 */
#include <errno.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "harness.h"

static char log_path[300];

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec ts = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&ts, NULL);
	}
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
	for (int i = 0; i < 300 && !logged(line); i++)
		pause_ms(h, 10);
	CHECK(logged(line), "fake-shell-ui never got \"%s\"", line);
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
}

static struct harness *
start(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);

	for (int i = 0; i < 300 && h->shell_ui_binds == 0; i++)
		pause_ms(h, 10);
	CHECK(h->shell_ui_binds == 1, "fake-shell-ui did not bind");
	return h;
}

static void
hold(struct harness *h, bool pressed, bool shift)
{
	harness_key(h, KEY_LEFTCTRL, pressed);
	harness_key(h, KEY_LEFTALT, pressed);
	if (shift)
		harness_key(h, KEY_LEFTSHIFT, pressed);
}

static void
switching(int argc, char **argv)
{
	struct harness *h = start(argc, argv);
	struct harness_window *a = harness_window_create(h, 300, 200, "alpha");

	harness_move_pointer(h, 1000, 700);
	clear_log();
	hold(h, true, false);
	tap(h, KEY_RIGHT);
	wait_logged(h, "show_workspace_switcher output 2 4 2 1024x768");
	CHECK(logged("workspace_switcher_entry 1 Workspace 1 0") &&
		      logged("workspace_switcher_entry 4 Workspace 4 0"),
	      "the grid is not listed");
	CHECK(logged("workspace_switcher_window 1 300x200 0"),
	      "workspace 1 does not show alpha");
	CHECK(!harness_window_info(a)->focused,
	      "alpha kept the focus on workspace 2");

	tap(h, KEY_RIGHT);
	wait_logged(h, "update_workspace_switcher 3 3");
	tap(h, KEY_LEFT);
	wait_logged(h, "update_workspace_switcher 2 2");

	/* No wrap at the first and the last: Marco's default. */
	tap(h, KEY_LEFT);
	wait_logged(h, "update_workspace_switcher 1 1");
	clear_log();
	tap(h, KEY_LEFT);
	wait_logged(h, "update_workspace_switcher 1 1");
	CHECK(harness_window_info(a)->focused, "alpha is not back in focus");

	/* A key that is not bound ends the grab; so does the release. */
	clear_log();
	tap(h, KEY_RIGHT);
	wait_logged(h, "update_workspace_switcher 2 2");
	clear_log();
	tap(h, KEY_ESC);
	wait_logged(h, "hide_switcher");
	clear_log();
	tap(h, KEY_RIGHT);
	wait_logged(h, "show_workspace_switcher output 3 4 3 1024x768");
	hold(h, false, false);
	wait_logged(h, "hide_switcher");

	/* A press outside the popup ends it too. */
	clear_log();
	hold(h, true, false);
	tap(h, KEY_LEFT);
	wait_logged(h, "show_workspace_switcher output 2 4 2 1024x768");
	polo_test_v1_button(h->test, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
	polo_test_v1_button(h->test, BTN_LEFT,
			    WL_POINTER_BUTTON_STATE_RELEASED);
	wait_logged(h, "hide_switcher");
	hold(h, false, false);

	/* Back to alpha: a window on another workspace gets no frames. */
	clear_log();
	hold(h, true, false);
	tap(h, KEY_LEFT);
	wait_logged(h, "show_workspace_switcher output 1 4 1 1024x768");
	hold(h, false, false);
	wait_logged(h, "hide_switcher");
	harness_window_destroy(a);
	CHECK(harness_stop(h) == 0, "polo exit status");
}

static void
moving(int argc, char **argv)
{
	struct harness *h = start(argc, argv);
	struct harness_window *a = harness_window_create(h, 300, 200, "alpha");
	struct harness_window *b = harness_window_create(h, 300, 200, "beta");

	clear_log();
	hold(h, true, true);
	tap(h, KEY_RIGHT);
	wait_logged(h, "show_workspace_switcher output 2 4 2 1024x768");
	tap(h, KEY_RIGHT);
	wait_logged(h, "update_workspace_switcher 3 3");
	hold(h, false, true);
	wait_logged(h, "hide_switcher");
	CHECK(harness_window_info(b)->workspace == 2 &&
		      harness_window_info(b)->focused,
	      "beta did not travel, focused, to workspace 3");
	CHECK(harness_window_info(a)->workspace == 0, "alpha moved");

	harness_window_destroy(a);
	harness_window_destroy(b);
	CHECK(harness_stop(h) == 0, "polo exit status");
}

static void
toroidal(int argc, char **argv)
{
	struct harness *h;

	setenv("POLO_TEST_PREFS", "general/wrap-style='toroidal'", 1);
	h = start(argc, argv);
	clear_log();
	hold(h, true, false);
	tap(h, KEY_LEFT);
	wait_logged(h, "show_workspace_switcher output 4 4 4 1024x768");
	hold(h, false, false);
	CHECK(harness_stop(h) == 0, "polo exit status");
	unsetenv("POLO_TEST_PREFS");
}

static void
one_workspace(int argc, char **argv)
{
	struct harness *h;

	setenv("POLO_TEST_PREFS", "general/num-workspaces=1", 1);
	h = start(argc, argv);
	clear_log();
	hold(h, true, false);
	tap(h, KEY_RIGHT);
	pause_ms(h, 200);
	hold(h, false, false);
	CHECK(!logged("show_workspace_switcher output 1 1 1 1024x768"),
	      "a popup with one workspace");
	CHECK(harness_stop(h) == 0, "polo exit status");
	unsetenv("POLO_TEST_PREFS");
}

/*
 * A key a grab keeps runs the global bindings: a numbered switch carries the
 * dragged window, a directional one wants a grab of its own and is refused, and
 * reaching the far edge of the outputs switches nothing.
 */
static void
dragging(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *a;

	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	setenv("POLO_TEST_PREFS",
	       "global-keybindings/switch-to-workspace-2='<Control>F2';"
	       "window-keybindings/minimize='<Control>F3'",
	       1);
	h = start(argc, argv);
	a = harness_window_create(h, 300, 200, "alpha");
	const struct harness_window_info *info = harness_window_info(a);

	clear_log();
	harness_move_pointer(h, info->x + 50, info->y + 50);
	harness_key(h, KEY_LEFTALT, true);
	harness_button(h, BTN_LEFT, true);
	harness_key(h, KEY_LEFTALT, false);

	polo_test_v1_move_pointer(h->test, 2040, 300);
	pause_ms(h, 100);
	CHECK(harness_window_info(a)->workspace == 0,
	      "the output edge switched workspace");

	hold(h, true, false);
	tap(h, KEY_RIGHT);
	pause_ms(h, 100);
	hold(h, false, false);
	CHECK(!logged("show_workspace_switcher output 2 4 2 1024x768") &&
		      harness_window_info(a)->workspace == 0,
	      "a directional switch ran during the move");

	harness_key(h, KEY_LEFTCTRL, true);
	tap(h, KEY_F3);
	harness_key(h, KEY_LEFTCTRL, false);
	CHECK(!(harness_window_info(a)->state & (1u << 1)),
	      "a window binding ran during the move");

	harness_key(h, KEY_LEFTCTRL, true);
	tap(h, KEY_F2);
	harness_key(h, KEY_LEFTCTRL, false);
	pause_ms(h, 100);
	CHECK(harness_window_info(a)->workspace == 1,
	      "the dragged window stayed behind");
	polo_test_v1_button(h->test, BTN_LEFT,
			    WL_POINTER_BUTTON_STATE_RELEASED);
	pause_ms(h, 100);

	harness_window_destroy(a);
	CHECK(harness_stop(h) == 0, "polo exit status");
	unsetenv("POLO_TEST_PREFS");
	unsetenv("POLO_TEST_TWO_OUTPUTS");
}

int
main(int argc, char **argv)
{
	int fd;

	CHECK(argc >= 3, "usage: %s POLO FAKE-SHELL-UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	snprintf(log_path, sizeof log_path, "%s/polo-workspace-switcher-XXXXXX",
		 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	fd = mkstemp(log_path);
	CHECK(fd >= 0, "mkstemp: %s", strerror(errno));
	close(fd);
	setenv("FAKE_SHELL_UI_LOG", log_path, 1);
	switching(argc, argv);
	moving(argc, argv);
	toroidal(argc, argv);
	one_workspace(argc, argv);
	dragging(argc, argv);
	unlink(log_path);
	return 0;
}
