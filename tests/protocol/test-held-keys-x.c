/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A shortcut key held while an X window takes the keyboard focus: Xwayland
 * must not be told it is down, or it presses it in the X window and repeats it
 * for ever, since Polo keeps its release.
 *
 * argv[2]: x-test-client
 */

#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"

#define X_KEY_M (KEY_M + 8)

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

static int
count_key(unsigned code)
{
	char line[128], want[32];
	int n = 0;
	FILE *file = fopen(log_path, "r");

	if (!file)
		return 0;
	snprintf(want, sizeof want, "key %u\n", code);
	while (fgets(line, sizeof line, file))
		if (strcmp(line, want) == 0)
			n++;
	fclose(file);
	return n;
}

int
main(int argc, char **argv)
{
	char xauth[512];
	struct harness *h;
	struct harness_window *a;
	pid_t client;
	int display, held, after;

	CHECK(argc >= 3, "usage: %s POLO X-TEST-CLIENT", argv[0]);
	if (system("command -v Xwayland >/dev/null") != 0)
		return 77;
	setenv("POLO_XWAYLAND", "lazy", 1);
	setenv("X_TEST_NO_HINTS", "1", 1);
	setenv("POLO_TEST_PREFS",
	       "window-keybindings/toggle-maximized='<Control><Shift>m'", 1);
	h = harness_start(argc, argv);
	a = harness_window_create(h, 400, 300, "a");
	CHECK(h->keyboard_focus == a->surface, "a not focused");
	pause_ms(h, 200);
	display = x_display(h);
	CHECK(display >= 0, "Polo did not take an X display");
	find_xauth(h, xauth, sizeof xauth);
	snprintf(log_path, sizeof log_path, "/tmp/polo-held-x-XXXXXX");
	close(mkstemp(log_path));

	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTSHIFT, true);
	harness_key(h, KEY_M, true);
	harness_roundtrip(h);

	client = fork();
	if (client == 0) {
		char name[16];

		snprintf(name, sizeof name, ":%d", display);
		setenv("DISPLAY", name, 1);
		setenv("XAUTHORITY", xauth, 1);
		execl(argv[2], argv[2], log_path, NULL);
		_exit(127);
	}
	for (int i = 0; i < 500 && !log_has("focus"); i++)
		pause_ms(h, 20);
	CHECK(log_has("focus"), "the X window never got the focus");

	pause_ms(h, 1500);
	held = count_key(X_KEY_M);
	harness_key(h, KEY_M, false);
	harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	harness_roundtrip(h);
	pause_ms(h, 1500);
	after = count_key(X_KEY_M) - held;
	fprintf(stderr,
		"M presses seen by the X window: %d while held, %d after "
		"release\n",
		held, after);
	CHECK(held == 0, "the X window got the shortcut key %d times", held);
	CHECK(after == 0,
	      "the X window kept repeating the key after its release (%d)",
	      after);

	kill(client, SIGTERM);
	waitpid(client, NULL, 0);
	unlink(log_path);
	harness_window_destroy(a);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
