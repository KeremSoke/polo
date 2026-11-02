/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Runs a headless Polo with eager Xwayland for replay-x.py and smoke.py, and gives them
 * Polo's real input path, which XTEST inside Xwayland never reaches. Prints
 * "DISPLAY XAUTHORITY", then reads lines: "move X Y", "pinch PHASE SCALE%", "back X Y" (10 right and straight back, with no round trip between), "button CODE 0|1", "key
 * CODE 0|1", and answers each with "ok". Stops Polo at end of input.
 * Exits 77 without Xwayland.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"

static int
x_display(struct harness *h)
{
	for (int display = 0; display < 64; display++) {
		char path[64];
		int pid = 0;

		snprintf(path, sizeof path, "/tmp/.X%d-lock", display);
		FILE *file = fopen(path, "r");

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

int
main(int argc, char **argv)
{
	struct timespec tick = { 0, 20 * 1000000L };
	char line[512], command[32], xauth[512] = "";
	int display = -1, a, b;

	if (system("command -v Xwayland >/dev/null") != 0)
		return 77;
	setenv("POLO_XWAYLAND", "eager", 1);
	FILE *out = fdopen(dup(STDOUT_FILENO), "w");

	dup2(STDERR_FILENO, STDOUT_FILENO);
	struct harness *h = harness_start(argc, argv);

	for (int i = 0; i < 400 && display < 0; i++) {
		harness_roundtrip(h);
		nanosleep(&tick, NULL);
		display = x_display(h);
	}
	CHECK(display >= 0, "Xwayland did not take a display");
	char list[320];

	snprintf(list, sizeof list, "ls %s/polo-xauth-* 2>/dev/null",
		 h->runtime_dir);
	FILE *pipe = popen(list, "r");

	CHECK(pipe && fgets(xauth, sizeof xauth, pipe), "no polo-xauth file");
	pclose(pipe);
	xauth[strcspn(xauth, "\n")] = '\0';
	fprintf(out, ":%d %s\n", display, xauth);
	fflush(out);

	while (fgets(line, sizeof line, stdin)) {
		if (sscanf(line, "%31s %d %d", command, &a, &b) != 3)
			break;
		if (strcmp(command, "move") == 0)
			polo_test_v1_move_pointer(h->test, a, b);
		else if (strcmp(command, "pinch") == 0)
			polo_test_v1_gesture(h->test, 1, a, 2, 0, 0,
					     wl_fixed_from_double(b / 100.0), 0, 0);
		else if (strcmp(command, "back") == 0) {
			polo_test_v1_move_pointer(h->test, a + 10, b);
			polo_test_v1_move_pointer(h->test, a, b);
		} else if (strcmp(command, "button") == 0)
			polo_test_v1_button(
				h->test, a,
				b ? WL_POINTER_BUTTON_STATE_PRESSED
				  : WL_POINTER_BUTTON_STATE_RELEASED);
		else if (strcmp(command, "key") == 0)
			polo_test_v1_key(h->test, a,
					 b ? WL_KEYBOARD_KEY_STATE_PRESSED
					   : WL_KEYBOARD_KEY_STATE_RELEASED);
		harness_roundtrip(h);
		fprintf(out, "ok\n");
		fflush(out);
	}
	return harness_stop(h) == 0 ? 0 : 1;
}
