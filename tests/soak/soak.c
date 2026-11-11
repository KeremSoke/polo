/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A desktop soak on headless Polo with the GL renderer and the real
 * polo-shell-ui: windows opened and closed, workspace switches through the
 * cube, Alt+Tab, minimise with Magic Lamp and restore, wobbly Alt-drags and
 * maximise, in a seeded random order, for a given time. Polo is stopped and
 * started again every session (a logout and login).
 *
 *   soak POLO POLO-SHELL-UI SECONDS [SESSION-SECONDS [SEED]]
 *
 * SOAK_SECONDS, SOAK_SESSION_SECONDS and SOAK_SEED override the arguments
 * (tools/soak.sh runs the meson test with them). For chasing a leak,
 * SOAK_ACTIONS keeps only some actions (digits of act ()'s cases) and
 * SOAK_FIXED_TITLES gives every window the same title, and SOAK_PLAIN leaves
 * the effects off.
 *
 * Fails on a crash or protocol error (the harness), on Polo's RSS at the end
 * of a session more than 5% above its RSS after warming up, and on a Polo that
 * leaves a child process or its socket behind when it stops. Prints a line a
 * minute and a summary.
 */

#include <dirent.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"

#define MAX_WINDOWS 10
#define RSS_GROWTH 0.05

static struct harness_window *windows[MAX_WINDOWS];
static int n_windows;
static unsigned long actions;

static double
now_s(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (double end = now_s() + ms / 1000.0; now_s() < end;) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
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

static int
children_of(int pid)
{
	DIR *proc = opendir("/proc");
	struct dirent *entry;
	int count = 0;

	if (!proc)
		return 0;
	while ((entry = readdir(proc))) {
		char path[300], line[512];
		FILE *file;
		int ppid = 0;

		if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
			continue;
		snprintf(path, sizeof path, "/proc/%s/status", entry->d_name);
		file = fopen(path, "r");
		if (!file)
			continue;
		while (fgets(line, sizeof line, file))
			if (sscanf(line, "PPid: %d", &ppid) == 1)
				break;
		fclose(file);
		count += ppid == pid;
	}
	closedir(proc);
	return count;
}

static void
pointer_to(struct harness *h, int x, int y)
{
	polo_test_v1_move_pointer(h->test, x, y);
	harness_roundtrip(h);
}

static void
button(struct harness *h, bool pressed)
{
	polo_test_v1_button(h->test, BTN_LEFT,
			    pressed ? WL_POINTER_BUTTON_STATE_PRESSED
				    : WL_POINTER_BUTTON_STATE_RELEASED);
	harness_roundtrip(h);
}

static void
key_tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
}

static void
destroy_window(struct harness *h, struct harness_window *w)
{
	for (int i = 0; i < h->n_windows; i++) {
		if (h->windows[i] == w) {
			h->windows[i] = h->windows[--h->n_windows];
			break;
		}
	}
	xdg_toplevel_destroy(w->xdg_toplevel);
	xdg_surface_destroy(w->xdg_surface);
	wl_surface_destroy(w->surface);
	if (w->buffer)
		wl_buffer_destroy(w->buffer);
	if (h->keyboard_focus == w->surface)
		h->keyboard_focus = NULL;
	if (h->pointer_focus == w->surface)
		h->pointer_focus = NULL;
	free(w);
	harness_roundtrip(h);
}

static void
open_window(struct harness *h)
{
	char title[32];

	if (n_windows == MAX_WINDOWS)
		return;
	if (getenv("SOAK_FIXED_TITLES"))
		snprintf(title, sizeof title, "soak");
	else
		snprintf(title, sizeof title, "soak %lu", actions);
	windows[n_windows++] = harness_window_create(h, 200 + rand() % 300,
						     150 + rand() % 200, title);
}

static void
close_window(struct harness *h)
{
	int i;

	if (n_windows <= 2)
		return;
	i = rand() % n_windows;
	destroy_window(h, windows[i]);
	windows[i] = windows[--n_windows];
}

static void
switch_workspace(struct harness *h)
{
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	key_tap(h, rand() % 2 ? KEY_RIGHT : KEY_LEFT);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	pause_ms(h, 250);
}

static void
alt_tab(struct harness *h)
{
	harness_key(h, KEY_LEFTALT, true);
	for (int i = 0, n = 1 + rand() % 3; i < n; i++) {
		key_tap(h, KEY_TAB);
		pause_ms(h, 60);
	}
	harness_key(h, KEY_LEFTALT, false);
	pause_ms(h, 150);
}

static struct harness_window *
any_window(void)
{
	return n_windows ? windows[rand() % n_windows] : NULL;
}

static void
minimise(struct harness *h)
{
	struct harness_window *w = any_window();

	if (!w)
		return;
	xdg_toplevel_set_minimized(w->xdg_toplevel);
	pause_ms(h, 350);
}

static void
maximise(struct harness *h)
{
	struct harness_window *w = any_window();

	if (!w)
		return;
	if (w->xdg_maximized)
		xdg_toplevel_unset_maximized(w->xdg_toplevel);
	else
		xdg_toplevel_set_maximized(w->xdg_toplevel);
	pause_ms(h, 100);
}

static void
wobble(struct harness *h)
{
	struct harness_window *w = any_window();
	const struct harness_window_info *info;
	int x, y;

	if (!w)
		return;
	info = harness_window_info(w);
	x = info->x + info->width / 2;
	y = info->y + info->height / 2;
	pointer_to(h, x, y);
	pause_ms(h, 30);
	if (h->pointer_focus != w->surface)
		return;
	harness_key(h, KEY_LEFTALT, true);
	button(h, true);
	for (int i = 1; i <= 12; i++) {
		pointer_to(h, x + i * (rand() % 21 - 10),
			   y + i * (rand() % 21 - 10));
		pause_ms(h, 16);
	}
	button(h, false);
	harness_key(h, KEY_LEFTALT, false);
	pause_ms(h, 300);
}

static void
act(struct harness *h)
{
	const char *only = getenv("SOAK_ACTIONS");
	int what = rand() % 8;

	while (only && *only && !strchr(only, '0' + what))
		what = rand() % 8;

	if (getenv("SOAK_TRACE"))
		printf("soak: action %lu: %d\n", actions, what);
	switch (what) {
	case 0:
	case 1:
		open_window(h);
		break;
	case 2:
		close_window(h);
		break;
	case 3:
		switch_workspace(h);
		break;
	case 4:
		alt_tab(h);
		break;
	case 5:
		minimise(h);
		break;
	case 6:
		wobble(h);
		break;
	default:
		maximise(h);
		break;
	}
	actions++;
	harness_roundtrip(h);
}

static void
configure(struct harness *h)
{
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	if (getenv("SOAK_PLAIN"))
		goto shell_ui;
	harness_set_setting(h, "org.mate.polo.effects", "cube", "true");
	harness_set_setting(h, "org.mate.polo.effects", "cube-duration-ms",
			    "200");
	harness_set_setting(h, "org.mate.polo.effects", "magic-lamp", "true");
	harness_set_setting(h, "org.mate.polo.effects",
			    "magic-lamp-duration-ms", "250");
	harness_set_setting(h, "org.mate.polo.effects", "wobbly-windows",
			    "true");
shell_ui:
	if (!getenv("POLO_SHELL_UI")[0])
		return;
	for (int i = 0; i < 50 && h->shell_ui_binds == 0; i++)
		pause_ms(h, 100);
	CHECK(h->shell_ui_binds > 0, "polo-shell-ui did not start");
}

static int
by_value(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return x < y ? -1 : x > y;
}

static double
median(const double *values, int n)
{
	double sorted[8];

	memcpy(sorted, values, n * sizeof *values);
	qsort(sorted, n, sizeof *sorted, by_value);
	return n % 2 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
}

static double
checkpoint(struct harness *h, int pid)
{
	while (n_windows)
		destroy_window(h, windows[--n_windows]);
	for (int i = 0; i < 3; i++) {
		char title[32];

		snprintf(title, sizeof title, "checkpoint %d", i);
		windows[n_windows++] =
			harness_window_create(h, 300, 200, title);
	}
	pause_ms(h, 1500);
	polo_test_v1_trim_heap(h->test);
	harness_roundtrip(h);
	return rss_mb(pid);
}

static double
session(int argc, char **argv, double seconds, int index, double soak_start)
{
	double start = now_s(), every = seconds / 10, next_check;
	double checks[128], early, late, peak = 0;
	int n_checks = 0, n;
	struct harness *h = harness_start(argc, argv);
	char socket_path[512];
	int pid = h->pid;

	if (every < 12)
		every = 12;
	if (every > 60)
		every = 60;
	snprintf(socket_path, sizeof socket_path, "%s/%s", h->runtime_dir,
		 h->socket);
	configure(h);
	for (int i = 0; i < 3; i++)
		open_window(h);
	next_check = start + every;
	while (now_s() - start < seconds) {
		double rss;

		act(h);
		rss = rss_mb(pid);
		if (rss > peak)
			peak = rss;
		if (now_s() >= next_check && n_checks < 128) {
			checks[n_checks++] = checkpoint(h, pid);
			printf("soak: %.0f min, login %d: %lu actions, RSS "
			       "%.1f MB "
			       "at checkpoint %d (peak %.1f)\n",
			       (now_s() - soak_start) / 60, index, actions,
			       checks[n_checks - 1], n_checks, peak);
			next_check = now_s() + every;
		}
	}
	while (n_windows)
		destroy_window(h, windows[--n_windows]);
	CHECK(harness_stop(h) == 0, "login %d: Polo did not stop cleanly",
	      index);
	CHECK(children_of(pid) == 0,
	      "login %d: Polo left a child process behind", index);
	CHECK(access(socket_path, F_OK) != 0,
	      "login %d: Polo left its socket %s behind", index, socket_path);
	CHECK(n_checks >= 3, "login %d: only %d checkpoints", index, n_checks);
	n = (n_checks - 1) / 3;
	if (n < 1)
		n = 1;
	if (n > 8)
		n = 8;
	early = median(checks + 1, n);
	late = median(checks + n_checks - n, n);
	printf("soak: login %d ended: RSS %.1f MB early, %.1f late (medians of "
	       "%d checkpoints), peak %.1f\n",
	       index, early, late, n, peak);
	CHECK(late <= early * (1 + RSS_GROWTH),
	      "login %d: RSS at the checkpoints grew from %.1f MB to %.1f MB "
	      "(over %.0f%%)",
	      index, early, late, RSS_GROWTH * 100);
	return (late - early) / early;
}

int
main(int argc, char **argv)
{
	double seconds, session_seconds, start = now_s(), worst = -1;
	unsigned seed;
	int index = 0;

	CHECK(argc >= 4,
	      "usage: %s POLO POLO-SHELL-UI SECONDS "
	      "[SESSION-SECONDS [SEED]]",
	      argv[0]);
	setvbuf(stdout, NULL, _IOLBF, 0);
	seconds =
		atof(getenv("SOAK_SECONDS") ? getenv("SOAK_SECONDS") : argv[3]);
	session_seconds = getenv("SOAK_SESSION_SECONDS")
		? atof(getenv("SOAK_SESSION_SECONDS"))
		: argc > 4 ? atof(argv[4])
			   : 900;
	if (session_seconds > seconds)
		session_seconds = seconds;
	seed = getenv("SOAK_SEED")
		? (unsigned)strtoul(getenv("SOAK_SEED"), NULL, 10)
		: argc > 5 ? (unsigned)strtoul(argv[5], NULL, 10)
			   : (unsigned)time(NULL);
	srand(seed);
	printf("soak: %.0f s in logins of %.0f s, seed %u\n", seconds,
	       session_seconds, seed);
	setenv("POLO_SHELL_UI", argv[2], 1);
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 0);
	while (now_s() - start + session_seconds / 2 < seconds) {
		double growth =
			session(argc, argv, session_seconds, ++index, start);

		if (growth > worst)
			worst = growth;
	}
	printf("soak: done: %.0f min, %d logins, %lu actions, worst RSS growth "
	       "%.1f%%, seed %u\n",
	       (now_s() - start) / 60, index, actions, worst * 100, seed);
	return 0;
}
