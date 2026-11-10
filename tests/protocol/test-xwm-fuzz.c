/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Polo's X window manager against an X client that sets malformed properties,
 * loops its transient_for chain, sends client messages made of arbitrary words
 * and makes windows come and go (tests/xwm/x-prop-fuzz.c, several seeds). Polo
 * must stay up and answer, and stop cleanly afterwards; under the sanitizers a
 * bad read or an overflow fails the run.
 *
 * argv[2]: x-prop-fuzz
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"

#define SEEDS 4
#define ROUNDS "2500"

static char log_path[64];

static void
pause_ms(struct harness *h, int msec)
{
	struct timespec tick = { 0, 10 * 1000000L };

	for (int i = 0; i < msec / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&tick, NULL);
	}
}

static bool
log_has(const char *line)
{
	char text[65536];
	FILE *file = fopen(log_path, "r");
	size_t n;

	if (!file)
		return false;
	n = fread(text, 1, sizeof text - 1, file);
	fclose(file);
	text[n] = '\0';
	return strstr(text, line) != NULL;
}

static int
x_display(struct harness *h)
{
	for (int display = 0; display < 64; display++) {
		char path[64];
		int pid = 0;
		FILE *file;

		snprintf(path, sizeof path, "/tmp/.X%d-lock", display);
		file = fopen(path, "r");
		if (!file)
			continue;
		if (fscanf(file, "%d", &pid) != 1)
			pid = 0;
		fclose(file);
		if (pid == h->pid)
			return display;
	}
	return -1;
}

static void
find_xauth(struct harness *h, char *path, size_t size)
{
	char command[512];
	FILE *pipe;

	snprintf(command, sizeof command, "ls %s/polo-xauth-* 2>/dev/null",
		 h->runtime_dir);
	pipe = popen(command, "r");
	CHECK(pipe && fgets(path, size, pipe), "no polo-xauth file in %s",
	      h->runtime_dir);
	pclose(pipe);
	path[strcspn(path, "\n")] = '\0';
}

static pid_t
start_client(int display, const char *path, const char *xauth, int seed)
{
	pid_t client = fork();

	if (client == 0) {
		char name[16], number[16];

		snprintf(name, sizeof name, ":%d", display);
		if (seed == 0)
			snprintf(number, sizeof number, "maps");
		else
			snprintf(number, sizeof number, "%d", seed);
		setenv("DISPLAY", name, 1);
		setenv("HOME", "/nonexistent", 1);
		setenv("XAUTHORITY", xauth, 1);
		execl(path, path, log_path, number, seed == 0 ? "600" : ROUNDS,
		      NULL);
		_exit(127);
	}
	return client;
}

static void
run_seed(int argc, char **argv, int seed)
{
	char xauth[512];
	struct harness *h;
	struct harness_window *window;
	pid_t client;
	int display, status = 0;
	time_t start;

	setenv("POLO_XWAYLAND", "lazy", 1);
	h = harness_start(argc, argv);
	pause_ms(h, 200);
	display = x_display(h);
	CHECK(display >= 0, "Polo did not take an X display");
	find_xauth(h, xauth, sizeof xauth);

	snprintf(log_path, sizeof log_path, "/tmp/polo-xwm-fuzz-XXXXXX");
	close(mkstemp(log_path));
	client = start_client(display, argv[2], xauth, seed);

	start = time(NULL);
	while (!log_has("done") && !log_has("lost")) {
		CHECK(time(NULL) - start < 120,
		      "seed %d: the fuzz client did not finish", seed);
		pause_ms(h, 20);
	}
	CHECK(!log_has("lost"), "seed %d: the X connection went away", seed);
	waitpid(client, &status, 0);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
	      "seed %d: the fuzz client ended with status %d", seed, status);

	pause_ms(h, 300);
	window = harness_window_create(h, 200, 150, "still alive");
	harness_wait_frame(h, window->surface);
	harness_window_destroy(window);
	unlink(log_path);
	CHECK(harness_stop(h) == 0, "seed %d: polo did not stop cleanly", seed);
}

int
main(int argc, char **argv)
{
	CHECK(argc >= 3, "usage: %s POLO X-PROP-FUZZ", argv[0]);
	if (system("command -v Xwayland >/dev/null") != 0)
		return 77;
	/* Seed 0: windows mapped and unmapped faster than Xwayland pairs them
	 * with their Wayland surfaces. */
	run_seed(argc, argv, 0);
	for (int seed = 1; seed <= SEEDS; seed++)
		run_seed(argc, argv, seed);
	return 0;
}
