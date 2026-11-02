/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * argv[2]: fake-shell-ui, which answers each window menu with the op in
 * answer_path. Marco's meta_window_show_menu (): the ops and insensitive
 * items, where Alt-Space, Alt-middle-click and xdg_toplevel.show_window_menu
 * put the menu, and menu_callback () applying the answer.
 */
#include <errno.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "harness.h"

static char log_path[300];
static char answer_path[320];

static void
answer(const char *text)
{
	FILE *file = fopen(answer_path, "w");

	CHECK(file, "cannot write %s", answer_path);
	fprintf(file, "%s\n", text);
	fclose(file);
}

static void
pause_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static bool
logged_prefix(const char *prefix, char *line, size_t size)
{
	FILE *log = fopen(log_path, "r");
	char buf[512];
	bool found = false;

	CHECK(log, "cannot read %s", log_path);
	while (!found && fgets(buf, sizeof buf, log)) {
		buf[strcspn(buf, "\n")] = '\0';
		if (strncmp(buf, prefix, strlen(prefix)) == 0) {
			found = true;
			snprintf(line, size, "%s", buf);
		}
	}
	fclose(log);
	return found;
}

static void
clear_log(void)
{
	FILE *log = fopen(log_path, "w");

	CHECK(log, "cannot truncate %s", log_path);
	fclose(log);
}

/* The menu's line without its id: "output X Y OPS INSENSITIVE ACTIVE N". */
static const char *
next_menu(struct harness *h)
{
	static char line[512];

	for (int i = 0;
	     i < 300 && !logged_prefix("show_window_menu ", line, sizeof line);
	     i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	CHECK(logged_prefix("show_window_menu ", line, sizeof line),
	      "no window menu");
	clear_log();
	return strchr(line + strlen("show_window_menu "), ' ') + 1;
}

static void
expect(const char *got, const char *want)
{
	CHECK(strcmp(got, want) == 0, "menu \"%s\", expected \"%s\"", got,
	      want);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *a;
	char want[256];
	int fd;

	CHECK(argc >= 3, "usage: %s POLO FAKE-SHELL-UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	snprintf(log_path, sizeof log_path, "%s/polo-window-menu-XXXXXX",
		 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	fd = mkstemp(log_path);
	CHECK(fd >= 0, "mkstemp: %s", strerror(errno));
	close(fd);
	setenv("FAKE_SHELL_UI_LOG", log_path, 1);
	snprintf(answer_path, sizeof answer_path, "%s.answer", log_path);
	setenv("FAKE_SHELL_UI_MENU_FILE", answer_path, 1);

	h = harness_start(argc, argv);
	for (int i = 0; i < 300 && h->shell_ui_binds == 0; i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	CHECK(h->shell_ui_binds == 1, "fake-shell-ui did not bind");
	a = harness_window_create(h, 300, 200, "alpha");
	const struct harness_window_info *info = harness_window_info(a);
	int x = info->x, y = info->y;
	clear_log();

	/* Alt-Space: at the client's top left, on workspace 1 of 4. */
	answer("0 0");
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_SPACE, true);
	harness_key(h, KEY_SPACE, false);
	harness_key(h, KEY_LEFTALT, false);
	snprintf(want, sizeof want, "output %d %d 0x4fcb 0x30 0 4", x, y);
	expect(next_menu(h), want);

	/* xdg_toplevel.show_window_menu is surface-local; maximized has
	 * Unmaximize, and Resize insensitive. */
	xdg_toplevel_set_maximized(a->xdg_toplevel);
	harness_roundtrip(h);
	harness_window_draw_pending(a);
	harness_roundtrip(h);
	info = harness_window_info(a);
	CHECK((info->state & (3u << 2)) == (3u << 2), "alpha is not maximized");
	clear_log();
	xdg_toplevel_show_window_menu(a->xdg_toplevel, h->seat, 0, 20, 30);
	harness_roundtrip(h);
	snprintf(want, sizeof want, "output %d %d 0x4fc7 0x430 0 4",
		 info->x + 20, info->y + 30);
	expect(next_menu(h), want);

	/* Alt and the menu button (middle, with resize-with-right-button): at
	 * the pointer. */
	xdg_toplevel_unset_maximized(a->xdg_toplevel);
	harness_roundtrip(h);
	harness_window_draw_pending(a);
	harness_roundtrip(h);
	clear_log();
	info = harness_window_info(a);
	harness_move_pointer(h, info->x + 40, info->y + 50);
	harness_key(h, KEY_LEFTALT, true);
	harness_button(h, BTN_MIDDLE, true);
	harness_button(h, BTN_MIDDLE, false);
	harness_key(h, KEY_LEFTALT, false);
	snprintf(want, sizeof want, "output %d %d 0x4fcb 0x30 0 4",
		 info->x + 40, info->y + 50);
	expect(next_menu(h), want);

	/* menu_callback (): Move to Another Workspace, then Always on Top. */
	answer("256 1");
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_SPACE, true);
	harness_key(h, KEY_SPACE, false);
	harness_key(h, KEY_LEFTALT, false);
	next_menu(h);
	harness_roundtrip(h);
	CHECK(harness_window_info(a)->workspace == 1,
	      "the menu did not move alpha to workspace 2");

	/* Move to Workspace Right goes to the active workspace's neighbour. */
	struct harness_window *b = harness_window_create(h, 300, 200, "beta");

	harness_window_destroy(a);
	clear_log();
	answer("16384 0");
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_SPACE, true);
	harness_key(h, KEY_SPACE, false);
	harness_key(h, KEY_LEFTALT, false);
	next_menu(h);
	harness_roundtrip(h);
	CHECK(harness_window_info(b)->workspace == 1,
	      "Move to Workspace Right did not move beta");

	harness_window_destroy(b);
	unlink(log_path);
	unlink(answer_path);
	return harness_stop(h);
}
