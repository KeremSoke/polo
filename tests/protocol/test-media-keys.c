/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * argv[2]: fake-shell-ui. msd's XF86Display key, one of acme.h's hard-coded
 * keys: Polo takes it before the focused window and shows msd's OSD on
 * the output under the pointer. Other keys still reach the window.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

#include "harness.h"

static char log_path[300];

static void
pause_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static bool
logged(const char *text)
{
	FILE *log = fopen(log_path, "r");
	char line[512];
	bool found = false;

	if (!log)
		return false;
	while (!found && fgets(line, sizeof line, log)) {
		line[strcspn(line, "\n")] = '\0';
		found = strcmp(line, text) == 0;
	}
	fclose(log);
	return found;
}

static bool
wait_logged(struct harness *h, const char *text)
{
	for (int i = 0; i < 300 && !logged(text); i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	return logged(text);
}

int
main(int argc, char **argv)
{
	int fd;

	snprintf(log_path, sizeof log_path, "%s/polo-media-keys-XXXXXX",
		 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	fd = mkstemp(log_path);
	CHECK(fd >= 0, "mkstemp: %s", strerror(errno));
	close(fd);
	setenv("FAKE_SHELL_UI_LOG", log_path, 1);
	setenv("POLO_SHELL_UI", argv[2], 1);
	setenv("LANGUAGE", "C", 1);

	struct harness *h = harness_start(argc, argv);

	for (int i = 0; i < 300 && h->shell_ui_binds == 0; i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	CHECK(h->shell_ui_binds == 1, "fake-shell-ui did not bind");

	struct harness_window *w = harness_window_create(h, 300, 200, "keys");

	CHECK(h->keyboard_focus == w->surface, "the window is not focused");
	h->last_key_surface = NULL;
	harness_key(h, KEY_SWITCHVIDEOMODE, true);
	harness_key(h, KEY_SWITCHVIDEOMODE, false);
	CHECK(wait_logged(h,
			  "show_osd 3 output video-single-display-symbolic "
			  "No External Display -1"),
	      "XF86Display showed no OSD");
	CHECK(h->last_key_surface != w->surface,
	      "XF86Display reached the focused window");

	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	harness_roundtrip(h);
	CHECK(h->last_key_surface == w->surface,
	      "an ordinary key did not reach the window");

	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	unlink(log_path);
	return 0;
}
