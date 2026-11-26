/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * org.mate.polo.hot-corners end to end. "triggers": every corner and edge,
 * enabled, pressure with its threshold, time window and sliding, hover with
 * its delay and per-corner delays, absolute pointers, cooldown and re-arming,
 * held buttons, click, fullscreen and the magnifier. "screens": the outer,
 * every-screen and whole-area layouts and the all, primary and selected
 * screens on two outputs. "actions": Expo, the cube, show desktop, the
 * window switcher, the run dialog, commands, a failing command, locking,
 * and nothing firing during another grab or while the session is locked.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

#include "ext-session-lock-v1-client-protocol.h"
#include "harness.h"
#include "locker.h"

#define SCHEMA "org.mate.polo.hot-corners"

static struct harness *h;
static char log_path[512];
static char shell_log[512];
static char polo_log[512];

static void
pause_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static void
make_recorder(const char *directory, const char *name)
{
	char path[600];
	FILE *file;

	snprintf(path, sizeof path, "%s/%s", directory, name);
	file = fopen(path, "w");
	CHECK(file != NULL, "fopen %s: %s", path, strerror(errno));
	fputs("#!/bin/sh\nline=$(basename \"$0\")\n"
	      "for arg in \"$@\"; do line=\"$line:$arg\"; done\n"
	      "printf '%s\\n' \"$line\" >> \"$POLO_ACTION_LOG\"\n",
	      file);
	CHECK(fclose(file) == 0, "fclose %s", path);
	CHECK(chmod(path, 0700) == 0, "chmod %s", path);
}

static void
setup(void)
{
	static char directory[] = "/tmp/polo-hot-corners-XXXXXX";
	static char path[2048];
	const char *old_path = getenv("PATH");

	CHECK(mkdtemp(directory) != NULL, "mkdtemp: %s", strerror(errno));
	snprintf(log_path, sizeof log_path, "%s/actions", directory);
	snprintf(shell_log, sizeof shell_log, "%s/shell-ui", directory);
	snprintf(polo_log, sizeof polo_log, "%s/polo", directory);
	make_recorder(directory, "hc");
	make_recorder(directory, "mate-panel");
	make_recorder(directory, "zenity");
	snprintf(path, sizeof path, "%s:%s", directory,
		 old_path ? old_path : "/usr/bin");
	setenv("PATH", path, 1);
	setenv("POLO_ACTION_LOG", log_path, 1);
	setenv("FAKE_SHELL_UI_LOG", shell_log, 1);
	setenv("POLO_TEST_LOG", polo_log, 1);
	setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/polo-bus",
	       1);
}

static int
count_in(const char *path, const char *prefix)
{
	char buffer[1024];
	FILE *file = fopen(path, "r");
	int lines = 0;

	if (!file)
		return 0;
	while (fgets(buffer, sizeof buffer, file)) {
		size_t length = strlen(prefix);

		buffer[strcspn(buffer, "\n")] = '\0';
		if (prefix[length - 1] == ':' ||
				    strncmp(prefix, "show_", 5) == 0 ||
				    strncmp(prefix, "hide_", 5) == 0
			    ? strncmp(buffer, prefix, length) == 0
			    : strcmp(buffer, prefix) == 0)
			lines++;
	}
	fclose(file);
	return lines;
}

static bool
polo_logged(const char *text)
{
	char buffer[1024];
	FILE *file = fopen(polo_log, "r");
	bool found = false;

	if (!file)
		return false;
	while (!found && fgets(buffer, sizeof buffer, file))
		found = strstr(buffer, text) != NULL;
	fclose(file);
	return found;
}

static int
count(const char *line)
{
	return count_in(log_path, line);
}

static void
expect_in(const char *path, const char *line, int wanted, const char *what)
{
	for (int i = 0; i < 100 && count_in(path, line) < wanted; i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	pause_ms(50);
	CHECK(count_in(path, line) == wanted, "%s: wanted %d \"%s\", got %d",
	      what, wanted, line, count_in(path, line));
}

static void
expect(const char *line, int wanted, const char *what)
{
	expect_in(log_path, line, wanted, what);
}

static void
set(const char *key, const char *value)
{
	harness_set_setting(h, SCHEMA, key, value);
}

static void
go(int x, int y)
{
	polo_test_v1_move_pointer(h->test, x, y);
	harness_roundtrip(h);
}

static void
go_absolute(int x, int y)
{
	polo_test_v1_move_pointer_absolute(h->test, x, y);
	harness_roundtrip(h);
}

static void
push(int x, int y, int n)
{
	for (int i = 0; i < n; i++)
		go(x, y);
}

static void
away(void)
{
	go(300, 300);
	pause_ms(400);
}

static void
corner(int x, int y, int beyond_x, int beyond_y, int n)
{
	away();
	go(x, y);
	push(beyond_x, beyond_y, n);
}

static void
click(void)
{
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	harness_roundtrip(h);
}

static void
maximize(struct harness_window *w)
{
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_F10, true);
	harness_key(h, KEY_F10, false);
	harness_key(h, KEY_LEFTALT, false);
	for (int i = 0; i < 20; i++)
		harness_roundtrip(h);
	harness_wait_frame(h, w->surface);
}

static uint32_t *
capture_screen(void)
{
	int width, height;
	uint32_t *pixels = harness_capture(h, &width, &height);

	CHECK(pixels != NULL && width >= 1024, "no capture");
	return pixels;
}

static int
changed_in(const uint32_t *before, int x0, int y0, int x1, int y1)
{
	uint32_t *now = capture_screen();
	int count = 0;

	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			count += (now[y * 1024 + x] & 0xffffff) !=
				(before[y * 1024 + x] & 0xffffff);
	free(now);
	return count;
}

static void
ripples(void)
{
	uint32_t *before;

	away();
	go(0, 0);
	pause_ms(100);
	before = capture_screen();
	push(-100, 0, 1);
	expect("hc:top-left", 14, "a corner with animation off");
	pause_ms(100);
	CHECK(changed_in(before, 30, 0, 90, 30) == 0,
	      "a ripple with animation off");
	free(before);

	set("animation", "true");
	away();
	go(0, 0);
	pause_ms(100);
	before = capture_screen();
	push(-100, 0, 1);
	pause_ms(200);
	CHECK(changed_in(before, 30, 0, 90, 30) > 100,
	      "no ripple in the corner");
	CHECK(changed_in(before, 75, 75, 140, 140) == 0,
	      "the ripple is not a quarter circle around the corner");
	pause_ms(900);
	CHECK(changed_in(before, 0, 0, 200, 200) == 0, "the ripple stayed");
	expect("hc:top-left", 15, "a corner with animation on");
	free(before);

	set("top", "'command:hc top'");
	away();
	go(512, 0);
	pause_ms(100);
	before = capture_screen();
	push(512, -100, 1);
	pause_ms(200);
	CHECK(changed_in(before, 540, 0, 600, 30) > 100,
	      "no ripple on the edge");
	CHECK(changed_in(before, 440, 0, 484, 30) > 100,
	      "the edge ripple is not a half circle");
	CHECK(changed_in(before, 480, 110, 545, 160) == 0,
	      "the edge ripple reaches past its radius");
	pause_ms(900);
	CHECK(changed_in(before, 400, 0, 630, 200) == 0,
	      "the edge ripple stayed");
	expect("hc:top", 2, "the top edge with animation on");
	free(before);
	set("top", "'none'");
	set("animation", "false");
}

static void
triggers(void)
{
	set("top-left", "'command:hc top-left'");
	corner(0, 0, -10, 0, 20);
	expect("hc:top-left", 0, "disabled");

	set("enabled", "true");
	corner(0, 0, -10, 0, 9);
	expect("hc:top-left", 0, "nine pushes of 10 px");
	push(-10, 0, 1);
	expect("hc:top-left", 1, "ten pushes of 10 px");
	push(-10, 0, 15);
	push(0, -10, 15);
	expect("hc:top-left", 1, "fired again before leaving");
	corner(0, 0, 0, -10, 10);
	expect("hc:top-left", 2, "upward pushes after leaving");
	corner(0, 0, -60, -60, 1);
	expect("hc:top-left", 3, "one push over the threshold");

	set("top-right", "'command:hc top-right'");
	set("bottom-left", "'command:hc bottom-left'");
	set("bottom-right", "'command:hc bottom-right'");
	set("top", "'command:hc top'");
	set("bottom", "'command:hc bottom'");
	set("left", "'command:hc left'");
	set("right", "'command:hc right'");
	corner(1023, 0, 1033, 0, 12);
	expect("hc:top-right", 1, "top-right");
	corner(0, 767, 0, 777, 12);
	expect("hc:bottom-left", 1, "bottom-left");
	corner(1023, 767, 1033, 777, 12);
	expect("hc:bottom-right", 1, "bottom-right");
	corner(512, 0, 512, -10, 10);
	expect("hc:top", 1, "top edge");
	corner(512, 767, 512, 777, 12);
	expect("hc:bottom", 1, "bottom edge");
	corner(0, 384, -10, 384, 10);
	expect("hc:left", 1, "left edge");
	corner(1023, 384, 1033, 384, 12);
	expect("hc:right", 1, "right edge");
	corner(512, 0, 600, 0, 10);
	expect("hc:top", 1, "motion along the top edge");
	corner(5, 0, 5, 0, 0);
	for (int i = 0; i < 30; i++)
		go(i % 2 ? 0 : 9, -5);
	expect("hc:top-left", 3, "sliding along the edge into the corner");
	expect("hc:top", 1, "sliding along the edge");
	set("top", "'none'");
	corner(512, 0, 512, -10, 20);
	expect("hc:top", 1, "an edge set to none");
	set("bottom", "''");
	corner(512, 767, 512, 777, 20);
	expect("hc:bottom", 1, "an edge set to an empty string");
	set("left", "'none'");
	set("right", "'none'");

	set("pressure-threshold", "uint32 30");
	corner(0, 0, -10, 0, 2);
	expect("hc:top-left", 3, "20 px against a threshold of 30");
	push(-10, 0, 1);
	expect("hc:top-left", 4, "30 px against a threshold of 30");
	set("pressure-threshold", "uint32 100");

	set("pressure-time", "uint32 100");
	away();
	go(0, 0);
	for (int i = 0; i < 15; i++) {
		push(-10, 0, 1);
		pause_ms(60);
	}
	expect("hc:top-left", 4, "pushes too slow for pressure-time");
	set("pressure-time", "uint32 1000");
	push(-10, 0, 10);
	expect("hc:top-left", 5, "the same pushes within pressure-time");

	away();
	go_absolute(0, 0);
	expect("hc:top-left", 6, "an absolute pointer resting in the corner");
	away();
	go(0, 0);
	pause_ms(500);
	expect("hc:top-left", 6,
	       "a mouse resting in the corner in pressure mode");

	set("trigger", "'hover'");
	set("hover-delay", "uint32 400");
	away();
	go(1, 1);
	pause_ms(200);
	CHECK(count("hc:top-left") == 6, "hover fired before hover-delay");
	expect("hc:top-left", 7, "hover after hover-delay");
	away();
	go(0, 0);
	pause_ms(150);
	away();
	expect("hc:top-left", 7, "hover left before hover-delay");
	corner(0, 0, -100, 0, 3);
	pause_ms(150);
	CHECK(count("hc:top-left") == 7, "a push fired in hover mode at once");
	expect("hc:top-left", 8, "a push in hover mode, after hover-delay");
	away();
	go(0, 0);
	pause_ms(100);
	set("enabled", "false");
	pause_ms(600);
	expect("hc:top-left", 8,
	       "hover pending while hot corners were turned off");
	set("enabled", "true");

	set("hover-delay", "uint32 2000");
	set("corner-delays", "{'top-right': uint32 0}");
	away();
	go(1023, 0);
	pause_ms(150);
	CHECK(count("hc:top-right") == 2,
	      "corner-delays 0 did not fire at once");
	away();
	go(0, 0);
	pause_ms(300);
	CHECK(count("hc:top-left") == 8, "top-left ignored hover-delay");
	set("corner-delays", "@a{su} {}");
	set("hover-delay", "uint32 150");
	set("trigger", "'pressure'");

	set("cooldown", "uint32 3000");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 9, "first push with a long cooldown");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 9, "a push within cooldown");
	set("cooldown", "uint32 0");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 10, "a push with no cooldown");
	set("cooldown", "uint32 350");

	away();
	go(0, 0);
	harness_button(h, BTN_LEFT, true);
	push(-10, 0, 20);
	harness_button(h, BTN_LEFT, false);
	expect("hc:top-left", 10, "pushes with a button held");

	struct harness_window *w = harness_window_create(h, 300, 200, "under");
	maximize(w);
	CHECK(harness_window_info(w)->x == 0 && harness_window_info(w)->y == 0,
	      "the window does not cover the corner");
	set("click", "true");
	away();
	go(0, 0);
	int presses = h->button_presses;
	click();
	expect("hc:top-left", 11, "a click on the corner");
	CHECK(h->button_presses == presses,
	      "the window got the corner's click");
	away();
	go(500, 300);
	presses = h->button_presses;
	click();
	CHECK(h->button_presses == presses + 1, "a click elsewhere was taken");
	expect("hc:top-left", 11, "a click away from the corner");
	set("click", "false");
	away();
	go(0, 0);
	presses = h->button_presses;
	click();
	CHECK(h->button_presses == presses + 1, "click off took the click");
	expect("hc:top-left", 11, "a click with click off");

	xdg_toplevel_set_fullscreen(w->xdg_toplevel, NULL);
	for (int i = 0; i < 20; i++)
		harness_roundtrip(h);
	harness_wait_frame(h, w->surface);
	CHECK(harness_window_info(w)->width == 1024, "not fullscreen");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 11, "a fullscreen window over the corner");
	set("click", "true");
	away();
	go(0, 0);
	presses = h->button_presses;
	click();
	CHECK(h->button_presses == presses + 1,
	      "a blocked corner took the fullscreen window's click");
	expect("hc:top-left", 11, "a click over a fullscreen window");
	set("fullscreen", "true");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 12, "fullscreen allowed");
	set("fullscreen", "false");
	set("click", "false");
	harness_window_destroy(w);

	harness_set_setting(h, "org.gnome.desktop.a11y.applications",
			    "screen-magnifier-enabled", "true");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 12, "the magnifier on");
	harness_set_setting(h, "org.gnome.desktop.a11y.applications",
			    "screen-magnifier-enabled", "false");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 13, "the magnifier off again");

	ripples();
	set("top-left", "'none'");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 15, "top-left set to none");
	set("top-left", "'command:hc top-left'");
	set("enabled", "false");
	corner(0, 0, -100, 0, 1);
	corner(1023, 0, 1200, 0, 1);
	expect("hc:top-left", 15, "enabled off");
	expect("hc:top-right", 2, "enabled off");
}

static void
screens(void)
{
	set("enabled", "true");
	set("top-left", "'command:hc top-left'");
	set("top-right", "'command:hc top-right'");

	corner(1023, 0, 1023, -10, 20);
	pause_ms(300);
	expect("hc:top-right", 0, "outer: the left screen's top-right");
	corner(1024, 0, 1024, -10, 20);
	pause_ms(300);
	expect("hc:top-left", 0, "outer: the right screen's top-left");
	corner(2047, 0, 2200, 0, 1);
	expect("hc:top-right", 1, "outer: the outer top-right");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 1, "outer: the outer top-left");

	set("layout", "'every-screen'");
	corner(1023, 0, 1023, 0, 0);
	expect("hc:top-right", 2,
	       "every-screen: resting in a corner between screens");
	corner(1024, 0, 1024, -100, 1);
	expect("hc:top-left", 2,
	       "every-screen: pushing in a corner between screens");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 3, "every-screen: the outer top-left");

	set("layout", "'whole-area'");
	corner(1023, 0, 1023, -100, 1);
	pause_ms(300);
	corner(1024, 0, 1024, -100, 1);
	pause_ms(300);
	expect("hc:top-right", 2, "whole-area: between screens");
	expect("hc:top-left", 3, "whole-area: between screens");
	corner(2047, 0, 2200, 0, 1);
	expect("hc:top-right", 3, "whole-area: the top-right");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 4, "whole-area: the top-left");

	set("layout", "'every-screen'");
	set("screens", "'primary'");
	corner(1024, 0, 1024, 0, 0);
	pause_ms(400);
	expect("hc:top-left", 4, "primary: the other screen");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 5, "primary: the primary screen");

	set("screens", "'selected'");
	set("selected-screens", "['weston-headless@headless1']");
	corner(1024, 0, 1024, 0, 0);
	expect("hc:top-left", 6, "selected: the selected screen");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 6, "selected: another screen");
	set("selected-screens", "@as []");
	corner(1024, 0, 1024, 0, 0);
	pause_ms(400);
	expect("hc:top-left", 6, "selected: no screen");

	set("screens", "'all'");
	corner(1024, 0, 1024, 0, 0);
	expect("hc:top-left", 7, "all: the right screen");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 8, "all: the left screen");

	const char *own = "org.mate.polo.hot-corners.screen:"
			  "/org/mate/polo/hot-corners/screens/"
			  "weston-headless@headless1/";

	harness_set_setting(h, own, "top-left", "'command:hc own-left'");
	corner(1024, 0, 1024, 0, 0);
	expect("hc:top-left", 9, "own corners while use-shared is on");
	expect("hc:own-left", 0, "own corners while use-shared is on");
	harness_set_setting(h, own, "use-shared", "false");
	corner(1024, 0, 1024, 0, 0);
	expect("hc:own-left", 1, "the right screen's own top-left");
	expect("hc:top-left", 9, "the right screen's own top-left");
	corner(0, 0, -100, 0, 1);
	expect("hc:top-left", 10, "the left screen keeps the shared corners");
	harness_set_setting(h, own, "top-left", "'none'");
	corner(1024, 0, 1024, 0, 0);
	pause_ms(400);
	expect("hc:own-left", 1, "the right screen's own top-left set to none");
	expect("hc:top-left", 10,
	       "the right screen's own top-left set to none");
	harness_set_setting(h, own, "top-right", "'command:hc own-right'");
	corner(2047, 0, 2200, 0, 1);
	expect("hc:own-right", 1, "the right screen's own top-right");
	expect("hc:top-right", 3, "the right screen's own top-right");
	corner(1023, 0, 1023, 0, 0);
	expect("hc:top-right", 4, "the left screen's shared top-right");
	harness_set_setting(h, own, "use-shared", "true");
	corner(1024, 0, 1024, 0, 0);
	expect("hc:top-left", 11, "back to the shared corners");
	expect("hc:own-left", 1, "back to the shared corners");
}

static uint32_t
pixel(int x, int y)
{
	int width, height;
	uint32_t *pixels = harness_capture(h, &width, &height);
	uint32_t value;

	CHECK(pixels != NULL, "no capture");
	value = pixels[y * width + x] & 0xffffff;
	free(pixels);
	return value;
}

static uint32_t
settled_pixel(int x, int y)
{
	pause_ms(500);
	harness_roundtrip(h);
	return pixel(x, y);
}

static void
actions(void)
{
	struct harness_window *a = harness_window_create(h, 200, 150, "a");
	struct harness_window *b = harness_window_create(h, 200, 150, "b");
	const uint32_t backdrop = 0x1a1a1a;

	CHECK(h->keyboard_focus == b->surface, "b is not focused");
	set("enabled", "true");
	harness_set_setting(h, "org.mate.polo.effects", "expo-background",
			    "'solid'");
	uint32_t desk = pixel(5, 700);

	CHECK(desk != backdrop, "the desktop is the backdrop's colour");
	set("top-left", "'expo'");
	corner(0, 0, -100, 0, 1);
	CHECK(settled_pixel(5, 700) == backdrop, "expo did not open");
	corner(0, 0, -100, 0, 1);
	CHECK(settled_pixel(5, 700) == desk,
	      "a second push did not close expo");

	set("top-left", "'cube'");
	corner(0, 0, -100, 0, 1);
	CHECK(settled_pixel(5, 700) == backdrop,
	      "cube without the cube did not fall back to expo");
	corner(0, 0, -100, 0, 1);
	CHECK(settled_pixel(5, 700) == desk, "the fallback did not close");

	const struct harness_window_info *info = harness_window_info(b);
	int cx = info->x + info->width / 2, cy = info->y + info->height / 2;
	uint32_t window = pixel(cx, cy);

	CHECK(window == 0x3070c0, "b's colour is %06x", window);
	set("top-left", "'show-desktop'");
	corner(0, 0, -100, 0, 1);
	CHECK(settled_pixel(cx, cy) != window, "show-desktop left b shown");
	corner(0, 0, -100, 0, 1);
	CHECK(settled_pixel(cx, cy) == window, "show-desktop did not end");

	set("top-left", "'window-switcher'");
	set("top-right", "'command:hc top-right'");
	corner(0, 0, -100, 0, 1);
	expect_in(shell_log, "show_switcher", 1, "the window switcher");
	corner(1023, 0, 1200, 0, 1);
	expect("hc:top-right", 0, "another corner while the switcher shows");
	corner(0, 0, -100, 0, 1);
	expect_in(shell_log, "hide_switcher", 1, "a second push");
	CHECK(h->keyboard_focus == b->surface, "closing changed the focus");
	corner(0, 0, -100, 0, 1);
	expect_in(shell_log, "show_switcher", 2, "the window switcher again");
	harness_key(h, KEY_ENTER, true);
	harness_key(h, KEY_ENTER, false);
	expect_in(shell_log, "hide_switcher", 2, "Enter");
	CHECK(h->keyboard_focus == a->surface, "Enter did not switch to a");
	corner(0, 0, -100, 0, 1);
	expect_in(shell_log, "show_switcher", 3, "the window switcher, third");
	harness_key(h, KEY_ESC, true);
	harness_key(h, KEY_ESC, false);
	expect_in(shell_log, "hide_switcher", 3, "Escape");
	CHECK(h->keyboard_focus == a->surface, "Escape switched windows");
	corner(0, 0, -100, 0, 1);
	expect_in(shell_log, "show_switcher", 4, "the switcher, then a window");
	struct harness_window *c = harness_window_create(h, 200, 150, "c");

	harness_key(h, KEY_ESC, true);
	harness_key(h, KEY_ESC, false);
	expect_in(shell_log, "hide_switcher", 4, "Escape after a new window");
	CHECK(!polo_logged("somehow changed"),
	      "cancelling reset a stack whose windows had changed");
	harness_window_destroy(c);

	set("top-left", "'run-dialog'");
	corner(0, 0, -100, 0, 1);
	expect("mate-panel:--run-dialog", 1, "the run dialog");

	set("top-left", "'command:/nonexistent/polo-hot-corner'");
	corner(0, 0, -100, 0, 1);
	expect("zenity:", 1, "a command that cannot run");

	set("top-left", "'command:   '");
	corner(0, 0, -100, 0, 1);
	pause_ms(200);
	expect("zenity:", 1, "an empty command");
	set("top-left", "'no-such-action'");
	corner(0, 0, -100, 0, 1);
	pause_ms(200);
	expect("zenity:", 1, "an unknown action");

	struct locker *l = locker_new(h);

	locker_lock(l);
	CHECK(wait_locked(h, l), "no locked event");
	corner(1023, 0, 1200, 0, 1);
	expect("hc:top-right", 0, "a corner while locked");
	unlock(h, l);
	corner(1023, 0, 1200, 0, 1);
	expect("hc:top-right", 1, "the corner after unlocking");

	harness_set_setting(h, "org.mate.polo.session", "locker-command",
			    "'hc locker'");
	set("top-left", "'lock-screen'");
	corner(0, 0, -100, 0, 1);
	expect("hc:locker", 1, "lock-screen");
	harness_window_destroy(a);
	harness_window_destroy(b);
}

int
main(int argc, char **argv)
{
	const char *mode = argc > 2 ? argv[2] : "triggers";

	setup();
	if (strcmp(mode, "screens") == 0)
		setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	if (strcmp(mode, "actions") == 0)
		setenv("POLO_SHELL_UI", argv[3], 1);
	h = harness_start(argc, argv);
	if (strcmp(mode, "screens") == 0)
		screens();
	else if (strcmp(mode, "actions") == 0)
		actions();
	else
		triggers();
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
