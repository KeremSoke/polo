/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * polo-shell-ui's memory across hundreds of window menus and Alt+Tab
 * popups: GTK 3.24 on Wayland keeps every native window it destroys (a few
 * KB each) unless the shell UI lets them go. After a warm-up that fills
 * GTK's caches, 400 of each must not grow its private memory by 1 MB. The
 * proportional set is printed beside it; it also moves when other processes
 * map or unmap the libraries it shares.
 *
 *   test-shell-ui-popup-memory POLO POLO-SHELL-UI
 */

#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

struct memory {
	long rss, pss, private_clean, private_dirty, anonymous, pss_anon,
		pss_file, pss_shmem;
	long rss_anon, rss_file, rss_shmem, threads;
};

static void
field(const char *line, const char *name, long *value)
{
	size_t n = strlen(name);

	if (strncmp(line, name, n) == 0 && line[n] == ':')
		*value = strtol(line + n + 1, NULL, 10);
}

static struct memory
memory_of(int pid)
{
	struct memory m = { 0 };
	char path[64], line[256];
	FILE *file;

	snprintf(path, sizeof path, "/proc/%d/smaps_rollup", pid);
	if ((file = fopen(path, "r"))) {
		while (fgets(line, sizeof line, file)) {
			field(line, "Rss", &m.rss);
			field(line, "Pss", &m.pss);
			field(line, "Pss_Anon", &m.pss_anon);
			field(line, "Pss_File", &m.pss_file);
			field(line, "Pss_Shmem", &m.pss_shmem);
			field(line, "Private_Clean", &m.private_clean);
			field(line, "Private_Dirty", &m.private_dirty);
			field(line, "Anonymous", &m.anonymous);
		}
		fclose(file);
	}
	snprintf(path, sizeof path, "/proc/%d/status", pid);
	if ((file = fopen(path, "r"))) {
		while (fgets(line, sizeof line, file)) {
			field(line, "RssAnon", &m.rss_anon);
			field(line, "RssFile", &m.rss_file);
			field(line, "RssShmem", &m.rss_shmem);
			field(line, "Threads", &m.threads);
		}
		fclose(file);
	}
	return m;
}

static double
private_mb(const struct memory *m)
{
	return (m->private_clean + m->private_dirty) / 1024.0;
}

static void
print_memory(const char *when, const struct memory *m)
{
	printf("shell UI %s: private %.2f MB (clean %ld kB, dirty %ld kB), PSS "
	       "%.2f MB (anon %ld, file %ld, shmem %ld kB), RSS %ld kB (anon "
	       "%ld, file %ld, shmem %ld kB), anonymous %ld kB, %ld threads\n",
	       when, private_mb(m), m->private_clean, m->private_dirty,
	       m->pss / 1024.0, m->pss_anon, m->pss_file, m->pss_shmem, m->rss,
	       m->rss_anon, m->rss_file, m->rss_shmem, m->anonymous,
	       m->threads);
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
	struct memory before, after;
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
	before = memory_of(shell_ui);
	print_memory("after warming up", &before);
	popups(h, 400);
	pause_ms(h, 4000);
	after = memory_of(shell_ui);
	print_memory("after 400 window menus and Alt+Tab popups", &after);
	CHECK(h->shell_ui_unbinds == 0 && kill(shell_ui, 0) == 0,
	      "polo-shell-ui went away");
#ifndef SANITIZED
	/* The sanitizer keeps freed memory in quarantine. */
	CHECK(private_mb(&after) - private_mb(&before) < 1.0,
	      "polo-shell-ui's private memory grew by %.2f MB over 400 window "
	      "menus and Alt+Tab popups (PSS by %.2f MB)",
	      private_mb(&after) - private_mb(&before),
	      (after.pss - before.pss) / 1024.0);
#endif

	harness_window_destroy(w[1]);
	harness_window_destroy(w[0]);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
