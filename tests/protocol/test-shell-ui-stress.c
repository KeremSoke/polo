/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * polo-shell-ui under a seeded random storm of what Polo sends it: windows
 * with hostile titles (long, markup, format directives, right-to-left, emoji,
 * control characters) in Alt+Tab, dialogs of every kind with random and
 * malformed payloads and reused ids, every event at once, OSDs and named
 * workspaces. It must stay the same process, bound and answering, with its
 * memory bounded. When Polo dies in the middle of a storm, it must exit too.
 *
 *   test-shell-ui-stress POLO POLO-SHELL-UI [SEED [ROUNDS]]
 */

#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"

#define MAX_WINDOWS 20

#if defined(__SANITIZE_ADDRESS__)
#define SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SANITIZED 1
#endif
#endif

static const char *const PIECES[] = {
	"a",    "Ünïcödé",  "日本語",
	"שלום", "‏",      "<b>",
	"</i>", "&amp;",    "&",
	"%s",   "%n",       "%999999d",
	"\t",   "\\n",      "🙂",
	"́",     "' OR 1=1", "\"",
	"{}",   "(",        "{'dialog': <'logout'>}",
};

static struct harness_window *windows[MAX_WINDOWS];
static int n_windows;

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static bool
exited(int pid)
{
	char path[64], stat[512];
	const char *close_paren;
	FILE *file;
	size_t n;

	if (kill(pid, 0) != 0)
		return true;
	snprintf(path, sizeof path, "/proc/%d/stat", pid);
	file = fopen(path, "r");
	if (!file)
		return true;
	n = fread(stat, 1, sizeof stat - 1, file);
	fclose(file);
	stat[n] = '\0';
	close_paren = strrchr(stat, ')');
	return close_paren && (close_paren[2] == 'Z' || close_paren[2] == 'X');
}

static double
rss_mb(int pid)
{
	char path[64], line[256];
	long kb = 0;
	FILE *file;

	snprintf(path, sizeof path, "/proc/%d/status", pid);
	file = fopen(path, "r");
	if (!file)
		return 0;
	while (fgets(line, sizeof line, file))
		if (sscanf(line, "VmRSS: %ld", &kb) == 1)
			break;
	fclose(file);
	return kb / 1024.0;
}

static void
random_text(char *out, size_t size)
{
	size_t n = 0, want = rand() % 4 == 0 ? size - 1 : (size_t)(rand() % 80);

	out[0] = '\0';
	while (n < want) {
		const char *piece =
			PIECES[rand() % (sizeof PIECES / sizeof PIECES[0])];
		size_t len = strlen(piece);

		if (n + len >= size)
			break;
		memcpy(out + n, piece, len);
		n += len;
		out[n] = '\0';
	}
}

static void
end_session_payload(char *out, size_t size)
{
	static const char *const FORMS[] = {
		"{'dialog': <'logout'>, 'seconds': <%d>}",
		"{'dialog': <'shutdown'>, 'seconds': <%d>, 'reboot': <true>}",
		"{'dialog': <'inhibit'>, 'inhibitors': <[('app', 'reason %d', "
		"'icon')]>}",
		"{'dialog': <'inhibit'>, 'inhibitors': <%d>}",
		"{'dialog': <%d>}",
		"{'seconds': <%d>}",
		"[%d]",
		"{'dialog': <'logout'",
	};

	snprintf(out, size, FORMS[rand() % (sizeof FORMS / sizeof FORMS[0])],
		 rand() % 3 == 0 ? -rand() : rand() % 100000);
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
}

static void
act(struct harness *h)
{
	char text[2000];
	const char *only = getenv("STRESS_ONLY");
	int what = rand() % 8;

	if (only && !strchr(only, '0' + what))
		what = 7;

	if (getenv("STRESS_TRACE"))
		fprintf(stderr, "stress: action %d\n", what);
	switch (what) {
	case 0:
		polo_test_v1_shell_ui_samples(h->test);
		break;
	case 1: {
		uint32_t kind = rand() % 6;

		if (kind == 2)
			end_session_payload(text, sizeof text);
		else if (kind == 4)
			snprintf(text, sizeof text, "%d",
				 rand() % 2 ? rand() : -rand());
		else
			random_text(text, sizeof text);
		polo_test_v1_shell_ui_dialog(h->test, 1 + rand() % 6, kind,
					     text);
		break;
	}
	case 2:
		if (n_windows < MAX_WINDOWS) {
			random_text(text, sizeof text);
			windows[n_windows++] =
				harness_window_open(h, 150 + rand() % 200,
						    100 + rand() % 150, text);
		}
		break;
	case 3:
		harness_key(h, KEY_LEFTALT, true);
		for (int i = 0, n = rand() % 6; i < n; i++)
			tap(h, KEY_TAB);
		if (rand() % 3 == 0)
			tap(h, KEY_ESC);
		harness_key(h, KEY_LEFTALT, false);
		break;
	case 4:
		tap(h, rand() % 2 ? KEY_TOUCHPAD_ON : KEY_TOUCHPAD_OFF);
		break;
	case 5:
		harness_key(h, KEY_LEFTCTRL, true);
		harness_key(h, KEY_LEFTALT, true);
		tap(h, rand() % 2 ? KEY_RIGHT : KEY_LEFT);
		harness_key(h, KEY_LEFTALT, false);
		harness_key(h, KEY_LEFTCTRL, false);
		break;
	case 6: {
		static const char *const NAMES[] = {
			"'Renamed'",
			"'Ünïcödé'",
			"'日本語'",
			"''",
			"'a very long workspace name that goes on and on'",
		};

		harness_set_setting(h, "org.mate.Marco.workspace-names",
				    rand() % 2 ? "name-1" : "name-2",
				    NAMES[rand() % 5]);
		break;
	}
	default:
		if (n_windows > 3) {
			int i = rand() % n_windows;

			harness_window_close(windows[i]);
			windows[i] = windows[--n_windows];
		}
		break;
	}
	harness_roundtrip(h);
	h->n_shell_ui_requests = 0;
}

static void
outlive_polo(int argc, char **argv, unsigned seed)
{
	struct harness *h = harness_start(argc, argv);
	int shell_ui;
	bool gone = false;

	for (int i = 0; i < 600 && h->shell_ui_binds == 0; i++)
		pause_ms(h, 50);
	CHECK(h->shell_ui_binds == 1, "polo-shell-ui did not start again");
	shell_ui = h->shell_ui_pid;
	n_windows = 0;
	for (int i = 0; i < 6; i++)
		windows[n_windows++] = harness_window_open(h, 200, 150, "w");
	harness_roundtrip(h);
	for (int i = 0; i < 40; i++) {
		polo_test_v1_shell_ui_samples(h->test);
		harness_key(h, KEY_LEFTALT, true);
		tap(h, KEY_TAB);
		harness_key(h, KEY_LEFTALT, false);
	}
	wl_display_flush(h->display);
	kill(h->pid, SIGKILL);
	waitpid(h->pid, NULL, 0);
	h->pid = 0;
	for (int i = 0; i < 100 && !gone; i++) {
		struct timespec step = { 0, 100 * 1000000L };

		gone = exited(shell_ui);
		nanosleep(&step, NULL);
	}
	if (!gone)
		kill(shell_ui, SIGKILL);
	while (n_windows)
		harness_window_close(windows[--n_windows]);
	harness_stop(h);
	CHECK(gone, "seed %u: polo-shell-ui outlived Polo by 10 s", seed);
}

int
main(int argc, char **argv)
{
	unsigned seed = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 10) : 1;
	int rounds = argc > 4 ? atoi(argv[4]) : 600;
	struct harness *h;
	int shell_ui;
	double early = 0, late;

	CHECK(argc >= 3, "usage: %s POLO POLO-SHELL-UI [SEED [ROUNDS]]",
	      argv[0]);
	srand(seed);
	setenv("POLO_SHELL_UI", argv[2], 1);
	setenv("POLO_TEST_PREFS", "general/num-workspaces=4", 1);
	h = harness_start(argc, argv);
	for (int i = 0; i < 600 && h->shell_ui_binds == 0; i++)
		pause_ms(h, 50);
	CHECK(h->shell_ui_binds == 1, "polo-shell-ui did not start");
	shell_ui = h->shell_ui_pid;
	for (int i = 0; i < 3; i++)
		windows[n_windows++] = harness_window_open(h, 200, 150, "w");

	for (int round = 1; round <= rounds; round++) {
		act(h);
		if (round % 25 == 0) {
			pause_ms(h, 50);
			CHECK(h->shell_ui_binds == 1 &&
				      h->shell_ui_unbinds == 0 &&
				      h->shell_ui_pid == shell_ui,
			      "seed %u round %d: polo-shell-ui went away "
			      "(binds %d, unbinds %d)",
			      seed, round, h->shell_ui_binds,
			      h->shell_ui_unbinds);
			CHECK(kill(shell_ui, 0) == 0,
			      "seed %u round %d: polo-shell-ui died", seed,
			      round);
		}
		if (round == rounds / 4)
			early = rss_mb(shell_ui);
	}
	pause_ms(h, 500);
	late = rss_mb(shell_ui);
	printf("shell-ui stress: seed %u, %d rounds, polo-shell-ui RSS %.1f MB "
	       "after a quarter, %.1f at the end\n",
	       seed, rounds, early, late);
#ifndef SANITIZED
	CHECK(late < early * 1.5 + 20,
	      "seed %u: polo-shell-ui grew from %.1f MB to %.1f MB", seed,
	      early, late);
#endif

	while (n_windows)
		harness_window_close(windows[--n_windows]);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	for (int i = 0; i < 3; i++)
		outlive_polo(argc, argv, seed);
	return 0;
}
