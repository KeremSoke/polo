/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * argv[2]: fake-shell-ui. Marco's titlebar clicks with their default actions
 * (frames.c meta_frame_titlebar_event ()): the right button opens the window
 * menu, a double click maximizes, the middle button lowers the window. And the
 * mini icon TraditionalOk draws in its menu button, looked up as
 * meta_read_icons () does: the desktop file, then the default icon.
 */
#include <errno.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "harness.h"

#define STATE_MAXIMIZED ((1u << 2) | (1u << 3))

static char log_path[300];
static char answer_path[320];

static void
pause_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static void
settle(struct harness *h)
{
	for (int i = 0; i < 10; i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
}

static bool
logged(const char *text)
{
	FILE *log = fopen(log_path, "r");
	char buf[512];
	bool found = false;

	CHECK(log, "cannot read %s", log_path);
	while (!found && fgets(buf, sizeof buf, log)) {
		buf[strcspn(buf, "\n")] = '\0';
		found = strncmp(buf, text, strlen(text)) == 0;
	}
	fclose(log);
	return found;
}

static void
wait_logged(struct harness *h, const char *text)
{
	for (int i = 0; i < 300 && !logged(text); i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	CHECK(logged(text), "fake-shell-ui never got \"%s\"", text);
}

static void
clear_log(void)
{
	FILE *log = fopen(log_path, "w");

	CHECK(log, "cannot truncate %s", log_path);
	fclose(log);
}

static int
corner_pixels(struct harness *h, int x, int y, uint32_t color)
{
	int width, height, count = 0;
	uint32_t *pixels;

	settle(h);
	pixels = harness_capture(h, &width, &height);
	CHECK(pixels, "no capture");
	for (int row = y; row < y + 30 && row < height; row++)
		for (int col = x; col < x + 40 && col < width; col++)
			if (row >= 0 && col >= 0 &&
			    (pixels[row * width + col] & 0xffffff) ==
				    (color & 0xffffff))
				count++;
	free(pixels);
	return count;
}

static void
click(struct harness *h, uint32_t button, int x, int y)
{
	harness_move_pointer(h, x, y);
	harness_button(h, button, true);
	harness_button(h, button, false);
	settle(h);
}

int
main(int argc, char **argv)
{
	int fd;

	CHECK(argc >= 3, "usage: %s POLO FAKE-SHELL-UI", argv[0]);
	if (access("/usr/share/themes/TraditionalOk/metacity-1", F_OK) != 0) {
		printf("TraditionalOk is not installed\n");
		return 77;
	}
	setenv("POLO_SHELL_UI", argv[2], 1);
	snprintf(log_path, sizeof log_path, "%s/polo-frame-actions-XXXXXX",
		 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	fd = mkstemp(log_path);
	CHECK(fd >= 0, "mkstemp: %s", strerror(errno));
	close(fd);
	setenv("FAKE_SHELL_UI_LOG", log_path, 1);
	snprintf(answer_path, sizeof answer_path, "%s.answer", log_path);
	FILE *answer = fopen(answer_path, "w");

	CHECK(answer, "cannot write %s", answer_path);
	fprintf(answer, "0 0\n");
	fclose(answer);
	setenv("FAKE_SHELL_UI_MENU_FILE", answer_path, 1);
	setenv("FAKE_SHELL_UI_ICON", "polo-known.desktop", 1);
	setenv("POLO_TEST_PREFS",
	       "general/theme='TraditionalOk';"
	       "general/titlebar-uses-system-font=false;"
	       "general/titlebar-font='Sans Bold 10';"
	       "general/button-layout='menu:minimize,maximize,close'",
	       1);

	struct harness *h = harness_start(argc, argv);

	for (int i = 0; i < 300 && h->shell_ui_binds == 0; i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	CHECK(h->shell_ui_binds == 1, "fake-shell-ui did not bind");

	struct harness_window *below =
		harness_window_create_decorated(h, 300, 200, "below");
	struct harness_window *w =
		harness_window_create_decorated(h, 300, 200, "frame");
	const struct harness_window_info *info = harness_window_info(w);
	int x = info->x, y = info->y;

	/* The harness's app id has no desktop file or theme icon: the default
	 * icon, red from the fake helper. */
	wait_logged(h, "lookup_icon (null) org.mate.PoloTest.desktop 16 48 1 0");
	wait_logged(h, "lookup_icon org.mate.PoloTest (null) 16 48 1 1");
	CHECK(corner_pixels(h, x - 10, y - 30, 0xffff0000) > 50,
	      "the default mini icon is not in the menu button");

	/* The desktop file's icon, green, replaces it. */
	clear_log();
	xdg_toplevel_set_app_id(w->xdg_toplevel, "polo-known");
	wl_surface_commit(w->surface);
	wait_logged(h, "lookup_icon (null) polo-known.desktop 16 48 1 0");
	CHECK(corner_pixels(h, x - 10, y - 30, 0xff00ff00) > 50,
	      "the desktop file's mini icon is not drawn");
	CHECK(corner_pixels(h, x - 10, y - 30, 0xffff0000) == 0,
	      "the default mini icon is still drawn");

	/* An app id with no desktop file: the theme's icon of that name, then
	 * the default. */
	clear_log();
	xdg_toplevel_set_app_id(w->xdg_toplevel, "polo-unknown");
	wl_surface_commit(w->surface);
	wait_logged(h, "lookup_icon (null) polo-unknown.desktop 16 48 1 0");
	wait_logged(h, "lookup_icon polo-unknown (null) 16 48 1 1");
	CHECK(corner_pixels(h, x - 10, y - 30, 0xffff0000) > 50,
	      "no default mini icon for an unknown app id");

	clear_log();
	click(h, BTN_RIGHT, x + 150, y - 8);
	wait_logged(h, "show_window_menu ");
	settle(h);

	harness_click(h, x + 150, y - 8);
	harness_click(h, x + 150, y - 8);
	settle(h);
	CHECK((harness_window_info(w)->state & STATE_MAXIMIZED) ==
		      STATE_MAXIMIZED,
	      "a double click on the titlebar did not maximize");

	CHECK(harness_window_info(w)->stack_index >
		      harness_window_info(below)->stack_index,
	      "the frame window is not on top");
	click(h, BTN_MIDDLE, x + 150, harness_window_info(w)->y - 8);
	CHECK(harness_window_info(w)->stack_index <
		      harness_window_info(below)->stack_index,
	      "a middle click on the titlebar did not lower the window");


	harness_window_destroy(w);
	harness_window_destroy(below);
	harness_roundtrip(h);
	unlink(log_path);
	unlink(answer_path);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
