/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * argv[2]: fake-shell-ui. Polo starts it over its trusted socket pair,
 * offers polo_shell_ui_v1 to it alone, carries events and requests, and
 * restarts it at most five times a minute.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "harness.h"

static char log_path[300];

static void
pause_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

#define WAIT_FOR(h, cond)                                                      \
	do {                                                                   \
		for (int _i = 0; _i < 400 && !(cond); _i++) {                  \
			harness_roundtrip(h);                                  \
			if (!(cond))                                           \
				pause_ms(10);                                  \
		}                                                              \
		CHECK(cond, "timed out waiting for %s", #cond);                \
	} while (0)

static void
new_log(void)
{
	int fd;

	snprintf(log_path, sizeof log_path, "%s/polo-fake-shell-ui-XXXXXX",
		 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	fd = mkstemp(log_path);
	CHECK(fd >= 0, "mkstemp: %s", strerror(errno));
	close(fd);
	setenv("FAKE_SHELL_UI_LOG", log_path, 1);
}

static int
count_lines(const char *prefix)
{
	FILE *log = fopen(log_path, "r");
	char line[512];
	int n = 0;

	CHECK(log, "cannot read %s", log_path);
	while (fgets(line, sizeof line, log))
		if (strncmp(line, prefix, strlen(prefix)) == 0)
			n++;
	fclose(log);
	return n;
}

static bool
has_request(struct harness *h, const char *text)
{
	for (int i = 0; i < h->n_shell_ui_requests; i++)
		if (strcmp(h->shell_ui_requests[i], text) == 0)
			return true;
	return false;
}

static const char *const expected_events[] = {
	"switcher_entry 1 One one 0",
	"switcher_entry 2 Two (null) 2",
	"show_switcher output 2 5 0 48",
	"update_selected 1",
	"hide_switcher",
	"window_menu_workspace 0 Workspace 1",
	"window_menu_workspace 1 Workspace 2",
	"show_window_menu 7 output 10 20 0x3ffff 0x2 1 2",
	"hide_window_menu 7",
	"show_osd 0 output (null) Workspace 2 -1",
	"show_dialog 3 0 output Hung",
	"hide_dialog 3",
};

static void
check_events(void)
{
	FILE *log = fopen(log_path, "r");
	char line[512];
	size_t next = 0;

	CHECK(log, "cannot read %s", log_path);
	while (fgets(line, sizeof line, log) &&
	       next < sizeof expected_events / sizeof expected_events[0]) {
		line[strcspn(line, "\n")] = '\0';
		if (strncmp(line, "start", 5) == 0 ||
		    strcmp(line, "bound") == 0)
			continue;
		CHECK(strcmp(line, expected_events[next]) == 0,
		      "event %zu: \"%s\", expected \"%s\"", next, line,
		      expected_events[next]);
		next++;
	}
	fclose(log);
	CHECK(next == sizeof expected_events / sizeof expected_events[0],
	      "only %zu events arrived", next);
}

static void
helper_runs(int argc, char **argv)
{
	struct harness *h;
	int first;

	new_log();
	h = harness_start(argc, argv);
	WAIT_FOR(h, h->shell_ui_binds == 1);
	first = h->shell_ui_pid;
	CHECK(first > 0 && count_lines("start") == 1, "one helper");
	CHECK(!h->saw_shell_ui_global,
	      "polo_shell_ui_v1 offered to an untrusted client");

	polo_test_v1_shell_ui_samples(h->test);
	WAIT_FOR(h, has_request(h, "color_table 0.25 0.75"));
	CHECK(has_request(h, "menu_action 7 256 1"), "menu_action");
	CHECK(has_request(h, "dialog_response 3 -5 yes"), "dialog_response");
	check_events();

	CHECK(kill(first, SIGKILL) == 0, "kill helper");
	WAIT_FOR(h, h->shell_ui_binds == 2);
	CHECK(h->shell_ui_unbinds == 1 && h->shell_ui_pid != first,
	      "restarted helper");
	CHECK(harness_stop(h) == 0, "polo exit status");
	unlink(log_path);
}

static void
helper_keeps_exiting(int argc, char **argv)
{
	struct harness *h;

	new_log();
	setenv("FAKE_SHELL_UI_EXIT", "1", 1);
	h = harness_start(argc, argv);
	WAIT_FOR(h, count_lines("bound") == 6);
	for (int i = 0; i < 50; i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	CHECK(count_lines("start") == 6, "%d helpers started, not 6",
	      count_lines("start"));
	CHECK(h->shell_ui_pid == 0, "a helper is still bound");
	CHECK(harness_stop(h) == 0, "polo exit status");
	unsetenv("FAKE_SHELL_UI_EXIT");
	unlink(log_path);
}

static void
helper_off(int argc, char **argv)
{
	struct harness *h;

	setenv("POLO_SHELL_UI", "", 1);
	h = harness_start(argc, argv);
	for (int i = 0; i < 30; i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	CHECK(h->shell_ui_binds == 0, "helper started with POLO_SHELL_UI=\"\"");
	CHECK(harness_stop(h) == 0, "polo exit status");
}

int
main(int argc, char **argv)
{
	CHECK(argc >= 3, "usage: %s POLO FAKE-SHELL-UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	helper_runs(argc, argv);
	helper_keeps_exiting(argc, argv);
	helper_off(argc, argv);
	return 0;
}
