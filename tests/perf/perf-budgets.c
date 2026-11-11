/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The plan's budgets, measured on headless Polo:
 *   idle wakeups per second and CPU, with the watchdog on as on DRM;
 *   resident memory with 30 windows (pixman: no GPU memory to exclude);
 *   Alt-Tab to the popup on screen (first time and again), real polo-shell-ui;
 *   key press to the frame that shows it, from polo --stats.
 *
 *   perf-budgets POLO POLO-SHELL-UI RESULTS.json
 *
 * Writes the numbers as JSON and fails when an absolute budget is missed.
 */

#include <dirent.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"

#define WAKEUPS_BUDGET 1.0
#define CPU_BUDGET 1.0
#define RSS_BUDGET_MB 120.0
#define ALT_TAB_BUDGET_MS 50.0
#define KEY_FRAME_BUDGET_MS (1000.0 / 60.0 + 7.0)

static double
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void
pause_ms(struct harness *h, int msec)
{
	struct timespec tick = { 0, 10 * 1000000L };

	for (int i = 0; i < msec / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&tick, NULL);
	}
}

static void
sample(int pid, long *switches, long *ticks)
{
	char path[384], line[256];
	DIR *dir;
	struct dirent *d;

	*switches = *ticks = 0;
	snprintf(path, sizeof path, "/proc/%d/task", pid);
	dir = opendir(path);
	if (!dir)
		return;
	while ((d = readdir(dir))) {
		FILE *f;

		if (d->d_name[0] == '.')
			continue;
		snprintf(path, sizeof path, "/proc/%d/task/%s/status", pid,
			 d->d_name);
		if ((f = fopen(path, "r"))) {
			while (fgets(line, sizeof line, f)) {
				long n;

				if (sscanf(line, "voluntary_ctxt_switches: %ld",
					   &n) == 1 ||
				    sscanf(line,
					   "nonvoluntary_ctxt_switches: %ld",
					   &n) == 1)
					*switches += n;
			}
			fclose(f);
		}
		snprintf(path, sizeof path, "/proc/%d/task/%s/stat", pid,
			 d->d_name);
		if ((f = fopen(path, "r"))) {
			char stat[1024];
			size_t n = fread(stat, 1, sizeof stat - 1, f);
			char *p;
			long utime = 0, stime = 0;

			stat[n] = '\0';
			p = strrchr(stat, ')');
			if (p &&
			    sscanf(p + 2,
				   "%*c %*d %*d %*d %*d %*d %*u %*u %*u "
				   "%*u %*u %ld %ld",
				   &utime, &stime) == 2)
				*ticks += utime + stime;
			fclose(f);
		}
	}
	closedir(dir);
}

static double
rss_mb(int pid)
{
	char path[64], line[256];
	FILE *f;
	long kb = 0;

	snprintf(path, sizeof path, "/proc/%d/status", pid);
	if (!(f = fopen(path, "r")))
		return -1;
	while (fgets(line, sizeof line, f))
		if (sscanf(line, "VmRSS: %ld", &kb) == 1)
			break;
	fclose(f);
	return kb / 1024.0;
}

static int
differing(const uint32_t *a, const uint32_t *b, int n)
{
	int count = 0;

	for (int i = 0; i < n; i++)
		if ((a[i] & 0xffffff) != (b[i] & 0xffffff))
			count++;
	return count;
}

static double
alt_tab_ms(struct harness *h)
{
	int width, height, n;
	uint32_t *before = harness_capture(h, &width, &height);
	double start, shown = -1;

	CHECK(before, "cannot capture the output");
	n = width * height;
	harness_key(h, KEY_LEFTALT, true);
	start = now_ms();
	harness_key(h, KEY_TAB, true);
	harness_key(h, KEY_TAB, false);
	while (now_ms() - start < 2000) {
		int w, hgt;
		uint32_t *after = harness_capture(h, &w, &hgt);

		if (after && w == width && hgt == height &&
		    differing(before, after, n) > 2000) {
			shown = now_ms() - start;
			free(after);
			break;
		}
		free(after);
	}
	harness_key(h, KEY_LEFTALT, false);
	pause_ms(h, 400);
	free(before);
	return shown;
}

static void
run_a(int argc, char **argv, FILE *out, int *missed)
{
	struct harness *h;
	struct harness_window *windows[30];
	long s0, t0, s1, t1;
	double t, wakeups, cpu, rss, first, again;

	setenv("POLO_SHELL_UI", argv[2], 1);
	setenv("POLO_WATCHDOG_SECONDS", "15", 1);
	h = harness_start(argc, argv);
	for (int i = 0; i < 1000 && h->shell_ui_binds == 0; i++)
		pause_ms(h, 10);
	CHECK(h->shell_ui_binds == 1, "polo-shell-ui did not bind");

	pause_ms(h, 2000);
	sample(h->pid, &s0, &t0);
	t = now_ms();
	sleep(5);
	sample(h->pid, &s1, &t1);
	t = (now_ms() - t) / 1000.0;
	wakeups = (s1 - s0) / t;
	cpu = 100.0 * (t1 - t0) / sysconf(_SC_CLK_TCK) / t;

	for (int i = 0; i < 30; i++)
		windows[i] = harness_window_create(h, 400, 300, "window");
	pause_ms(h, 500);
	rss = rss_mb(h->pid);

	first = alt_tab_ms(h);
	again = alt_tab_ms(h);

	fprintf(out,
		"  \"idle_wakeups_per_second\": %.2f,\n"
		"  \"idle_cpu_percent\": %.2f,\n"
		"  \"rss_30_windows_mb\": %.1f,\n"
		"  \"alt_tab_first_ms\": %.1f,\n"
		"  \"alt_tab_ms\": %.1f,\n",
		wakeups, cpu, rss, first, again);
	fprintf(stderr,
		"idle: %.2f wakeups/s, %.2f%% CPU; 30 windows: %.1f MB; "
		"Alt-Tab "
		"%.1f ms first, %.1f ms again\n",
		wakeups, cpu, rss, first, again);
	if (wakeups > WAKEUPS_BUDGET) {
		fprintf(stderr, "over budget: idle wakeups\n");
		(*missed)++;
	}
	if (cpu > CPU_BUDGET) {
		fprintf(stderr, "over budget: idle CPU\n");
		(*missed)++;
	}
	if (rss > RSS_BUDGET_MB) {
		fprintf(stderr, "over budget: memory\n");
		(*missed)++;
	}
	if (again < 0 || again > ALT_TAB_BUDGET_MS) {
		fprintf(stderr, "over budget: Alt-Tab popup\n");
		(*missed)++;
	}

	for (int i = 0; i < 30; i++)
		harness_window_destroy(windows[i]);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	unsetenv("POLO_SHELL_UI");
}

static void
run_b(int argc, char **argv, FILE *out, int *missed)
{
	char log_path[64], line[4096];
	struct harness *h;
	struct harness_window *w;
	double p50 = -1, p95 = -1;
	FILE *f;

	snprintf(log_path, sizeof log_path, "/tmp/polo-perf-XXXXXX");
	close(mkstemp(log_path));
	setenv("POLO_TEST_LOG", log_path, 1);
	setenv("POLO_STATS_INTERVAL_MS", "20000", 1);
	setenv("POLO_SHELL_UI", "", 1);
	h = harness_start(argc, argv);
	w = harness_window_create(h, 400, 300, "typing");
	pause_ms(h, 300);
	for (int i = 0; i < 40; i++) {
		harness_key(h, KEY_A, true);
		harness_wait_frame(h, w->surface);
		harness_key(h, KEY_A, false);
		pause_ms(h, 70);
	}
	pause_ms(h, 21000 - 40 * 80);
	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");

	f = fopen(log_path, "r");
	while (f && fgets(line, sizeof line, f)) {
		const char *at = strstr(line, "input to frame p50 ");
		double a, b;

		if (at &&
		    sscanf(at, "input to frame p50 %lf p95 %lf", &a, &b) == 2) {
			p50 = a > p50 ? a : p50;
			p95 = b > p95 ? b : p95;
		}
	}
	if (f)
		fclose(f);
	unlink(log_path);
	unsetenv("POLO_TEST_LOG");
	unsetenv("POLO_STATS_INTERVAL_MS");

	fprintf(out,
		"  \"key_to_frame_p50_ms\": %.1f,\n"
		"  \"key_to_frame_p95_ms\": %.1f\n",
		p50, p95);
	fprintf(stderr, "key to frame: p50 %.1f ms, p95 %.1f ms\n", p50, p95);
	if (p95 < 0 || p95 > KEY_FRAME_BUDGET_MS) {
		fprintf(stderr, "over budget: key to frame\n");
		(*missed)++;
	}
}

int
main(int argc, char **argv)
{
	FILE *out;
	int missed = 0;

	CHECK(argc >= 4, "usage: %s POLO POLO-SHELL-UI RESULTS.json", argv[0]);
	out = fopen(argv[3], "w");
	CHECK(out, "cannot write %s", argv[3]);
	fprintf(out, "{\n");
	run_a(argc, argv, out, &missed);
	run_b(argc, argv, out, &missed);
	fprintf(out, "}\n");
	fclose(out);
	return missed ? 1 : 0;
}
