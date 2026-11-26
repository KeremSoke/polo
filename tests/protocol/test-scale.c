/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Scale on Pixman. "single": Super+W spreads out the workspace's windows
 * over a backdrop; Escape, clicks, Tab, arrows, Enter, middle clicks,
 * minimized windows, every workspace, one application, holding the key
 * and the hot corner action. "screens": a second monitor, with each,
 * summoned and mirrored screens, per-mode overrides, dragging a window to
 * the other monitor, and unplugging it while Scale is open.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

#include "harness.h"

#define BACKDROP 0x1a1a1au
#define RED 0xc03030u
#define GREEN 0x30c030u
#define BLUE 0x3030c0u
#define STATE_MINIMIZED (1u << 1)
#define STATE_ACTIVE (1u << 12)
#define SCALE "org.mate.polo.scale"
#define KEYS "org.mate.polo.keybindings"

static char polo_log[512];

struct spot {
	int count;
	int x, y;
	int left, top, right, bottom;
};

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static int
log_lines(void)
{
	char buffer[2048];
	int lines = 0;
	FILE *file = fopen(polo_log, "r");

	if (!file)
		return 0;
	while (fgets(buffer, sizeof buffer, file))
		lines++;
	fclose(file);
	return lines;
}

static int
logged_since(int since, const char *text)
{
	char buffer[2048];
	int line = 0, found = 0;
	FILE *file = fopen(polo_log, "r");

	if (!file)
		return 0;
	while (fgets(buffer, sizeof buffer, file))
		if (line++ >= since && strstr(buffer, text))
			found++;
	fclose(file);
	return found;
}

static struct harness_window *
coloured(struct harness *h, int width, int height, const char *title,
	 uint32_t colour)
{
	struct harness_window *w =
		harness_window_create(h, width, height, title);

	w->auto_configure = false;
	wl_buffer_destroy(w->buffer);
	w->buffer = harness_buffer_create_colour(h->shm, width, height,
						 0xff000000u | colour);
	wl_surface_attach(w->surface, w->buffer, 0, 0);
	wl_surface_damage_buffer(w->surface, 0, 0, width, height);
	harness_wait_frame(h, w->surface);
	return w;
}

static struct spot
find(struct harness *h, int output, uint32_t colour)
{
	int width, height;
	uint32_t *pixels = output < 0
		? harness_capture(h, &width, &height)
		: harness_capture_output(h, output, &width, &height);
	struct spot s = { 0, -1, -1, width, height, -1, -1 };
	long sx = 0, sy = 0;

	CHECK(pixels, "cannot capture");
	for (int i = 0; i < width * height; i++) {
		int x = i % width, y = i / width;

		if ((pixels[i] & 0xffffff) != colour)
			continue;
		s.count++;
		sx += x;
		sy += y;
		s.left = x < s.left ? x : s.left;
		s.top = y < s.top ? y : s.top;
		s.right = x > s.right ? x : s.right;
		s.bottom = y > s.bottom ? y : s.bottom;
	}
	if (s.count) {
		s.x = sx / s.count;
		s.y = sy / s.count;
	}
	free(pixels);
	return s;
}

static uint32_t
corner(struct harness *h, int output)
{
	int width, height;
	uint32_t *pixels = output < 0
		? harness_capture(h, &width, &height)
		: harness_capture_output(h, output, &width, &height);
	uint32_t pixel;

	CHECK(pixels, "cannot capture");
	pixel = pixels[2 * width + 2] & 0xffffff;
	free(pixels);
	return pixel;
}

static bool
whole(struct spot s)
{
	return s.count > 0 &&
		s.count == (s.right - s.left + 1) * (s.bottom - s.top + 1);
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_roundtrip(h);
}

static void
chord(struct harness *h, bool shift, uint32_t key)
{
	harness_key(h, KEY_LEFTMETA, true);
	if (shift)
		harness_key(h, KEY_LEFTSHIFT, true);
	tap(h, key);
	if (shift)
		harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, KEY_LEFTMETA, false);
	pause_ms(h, 200);
}

static void
escape(struct harness *h)
{
	tap(h, KEY_ESC);
	pause_ms(h, 200);
}

static void
click_at(struct harness *h, int x, int y, uint32_t button)
{
	polo_test_v1_move_pointer(h->test, x, y);
	harness_roundtrip(h);
	polo_test_v1_button(h->test, button, WL_POINTER_BUTTON_STATE_PRESSED);
	harness_roundtrip(h);
	polo_test_v1_button(h->test, button, WL_POINTER_BUTTON_STATE_RELEASED);
	harness_roundtrip(h);
	pause_ms(h, 200);
}

static void
point(struct harness *h, int x, int y)
{
	polo_test_v1_move_pointer(h->test, x, y);
	harness_roundtrip(h);
}

static bool
focused(struct harness *h, struct harness_window *w)
{
	harness_roundtrip(h);
	return h->keyboard_focus == w->surface &&
		(harness_window_info(w)->state & STATE_ACTIVE);
}

static void
setup(void)
{
	char dir[] = "/tmp/polo-scale-XXXXXX";

	CHECK(mkdtemp(dir), "cannot make a log directory");
	snprintf(polo_log, sizeof polo_log, "%s/polo.log", dir);
	setenv("POLO_TEST_LOG", polo_log, 1);
	setenv("POLO_TEST_DEBUG", "polo", 1);
}

static void
settle(struct harness *h)
{
	harness_set_setting(h, SCALE, "background", "'solid'");
	harness_set_setting(h, SCALE, "window-titles", "false");
	harness_set_setting(h, SCALE, "duration-ms", "uint32 40");
	harness_set_setting(h, SCALE, "layout", "'grid'");
	harness_roundtrip(h);
}

static void
test_single(struct harness *h)
{
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	harness_set_setting(h, "org.mate.Marco.window-keybindings",
			    "move-to-workspace-2", "'<Mod4>F2'");
	settle(h);
	point(h, 500, 400);
	struct harness_window *a = coloured(h, 300, 200, "a", RED);
	struct harness_window *b = coloured(h, 400, 300, "b", GREEN);
	struct harness_window *c = coloured(h, 200, 150, "c", BLUE);

	CHECK(focused(h, c), "the last window is not focused");

	int mark = log_lines();

	chord(h, false, KEY_W);
	CHECK(logged_since(mark, "scale: workspace on 1 screens, 3 windows"),
	      "Super+W did not open Scale on the three windows");
	CHECK(logged_since(mark, "scale: headless0 shows c, b, a") ||
		      logged_since(mark, "shows c, b, a"),
	      "Scale did not list the windows most recent first");
	CHECK(corner(h, -1) == BACKDROP, "no backdrop: %06x", corner(h, -1));
	struct spot sa = find(h, -1, RED), sb = find(h, -1, GREEN),
		    sc = find(h, -1, BLUE);

	CHECK(whole(sa) && whole(sb) && whole(sc),
	      "a thumbnail is covered: %d %d %d", sa.count, sb.count, sc.count);
	escape(h);
	CHECK(corner(h, -1) != BACKDROP, "Escape left the backdrop");
	CHECK(focused(h, c), "Escape moved the focus");

	chord(h, false, KEY_W);
	sa = find(h, -1, RED);
	click_at(h, sa.x, sa.y, BTN_LEFT);
	CHECK(logged_since(mark, "scale: picked a"),
	      "the click picked nothing");
	CHECK(focused(h, a), "the clicked window is not focused");

	chord(h, false, KEY_W);
	tap(h, KEY_TAB);
	tap(h, KEY_ENTER);
	pause_ms(h, 200);
	CHECK(focused(h, c), "Tab and Enter did not pick the next window");

	chord(h, false, KEY_W);
	sa = find(h, -1, RED);
	sb = find(h, -1, GREEN);
	sc = find(h, -1, BLUE);
	double cost_a =
		sa.x > sc.x + 1 ? (sa.x - sc.x) + 2.0 * abs(sa.y - sc.y) : -1.0;
	double cost_b =
		sb.x > sc.x + 1 ? (sb.x - sc.x) + 2.0 * abs(sb.y - sc.y) : -1.0;
	struct harness_window *expected = c;

	if (cost_a >= 0 && (cost_b < 0 || cost_a <= cost_b))
		expected = a;
	else if (cost_b >= 0)
		expected = b;
	tap(h, KEY_RIGHT);
	tap(h, KEY_ENTER);
	pause_ms(h, 200);
	CHECK(focused(h, expected),
	      "Right did not move to the nearest window "
	      "on the right");

	xdg_toplevel_set_minimized(b->xdg_toplevel);
	harness_roundtrip(h);
	pause_ms(h, 100);
	CHECK(harness_window_info(b)->state & STATE_MINIMIZED,
	      "b is not minimized");
	chord(h, false, KEY_W);
	CHECK(find(h, -1, GREEN).count > 0,
	      "the minimized window is not shown");
	escape(h);
	harness_set_setting(h, SCALE, "minimized", "'hide'");
	mark = log_lines();
	chord(h, false, KEY_W);
	CHECK(logged_since(mark, "scale: workspace on 1 screens, 2 windows"),
	      "minimized windows were not left out");
	CHECK(find(h, -1, GREEN).count == 0, "the minimized window shows");
	escape(h);
	harness_set_setting(h, SCALE, "minimized", "'show'");
	chord(h, false, KEY_W);
	sb = find(h, -1, GREEN);
	click_at(h, sb.x, sb.y, BTN_LEFT);
	CHECK(!(harness_window_info(b)->state & STATE_MINIMIZED),
	      "picking the minimized window left it minimized");
	CHECK(focused(h, b), "the minimized window was not focused");

	chord(h, false, KEY_W);
	sa = find(h, -1, RED);
	click_at(h, sa.x, sa.y, BTN_MIDDLE);
	CHECK(a->close_requested, "a middle click did not close the window");
	escape(h);
	a->close_requested = false;

	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_F2);
	harness_key(h, KEY_LEFTMETA, false);
	pause_ms(h, 100);
	CHECK(harness_window_info(b)->workspace == 1,
	      "b did not move to workspace 2: %d",
	      harness_window_info(b)->workspace);
	mark = log_lines();
	chord(h, false, KEY_W);
	CHECK(logged_since(mark, "scale: workspace on 1 screens, 2 windows"),
	      "the workspace mode shows another workspace's window");
	chord(h, true, KEY_W);
	CHECK(logged_since(mark,
			   "scale: all-workspaces on 1 screens, 3 windows"),
	      "Super+Shift+W did not switch to every workspace in place");
	sb = find(h, -1, GREEN);
	CHECK(sb.count > 0, "the other workspace's window is not shown");
	click_at(h, sb.x, sb.y, BTN_LEFT);
	CHECK(focused(h, b), "picking b did not focus it");
	CHECK(harness_window_info(b)->workspace == 1, "b changed workspace");

	harness_set_setting(h, SCALE, "other-workspace", "'bring'");
	chord(h, true, KEY_W);
	sa = find(h, -1, RED);
	click_at(h, sa.x, sa.y, BTN_LEFT);
	CHECK(harness_window_info(a)->workspace == 1,
	      "bring did not move a here");
	CHECK(focused(h, a), "the brought window is not focused");
	harness_set_setting(h, SCALE, "other-workspace", "'switch'");

	xdg_toplevel_set_app_id(c->xdg_toplevel, "org.mate.Other");
	wl_surface_commit(c->surface);
	harness_set_setting(h, KEYS, "scale-application-all-workspaces",
			    "'<Mod4>q'");
	harness_roundtrip(h);
	mark = log_lines();
	chord(h, false, KEY_Q);
	CHECK(logged_since(mark,
			   "scale: application-all-workspaces on 1 "
			   "screens, 2 windows"),
	      "the application mode did not keep to the application");
	CHECK(find(h, -1, BLUE).count == 0,
	      "another application's window shows");
	escape(h);

	harness_set_setting(h, SCALE, "hold", "true");
	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_W);
	pause_ms(h, 100);
	tap(h, KEY_TAB);
	harness_key(h, KEY_LEFTMETA, false);
	pause_ms(h, 200);
	CHECK(focused(h, b), "letting go of Super did not pick the next one");
	harness_set_setting(h, SCALE, "hold", "false");

	harness_set_setting(h, "org.mate.polo.hot-corners", "top-left",
			    "'scale-all-workspaces'");
	harness_set_setting(h, "org.mate.polo.hot-corners", "trigger",
			    "'hover'");
	harness_set_setting(h, "org.mate.polo.hot-corners", "hover-delay",
			    "uint32 0");
	harness_set_setting(h, "org.mate.polo.hot-corners", "enabled", "true");
	harness_roundtrip(h);
	mark = log_lines();
	point(h, 300, 300);
	polo_test_v1_move_pointer(h->test, 0, 0);
	harness_roundtrip(h);
	pause_ms(h, 200);
	CHECK(logged_since(mark, "scale: all-workspaces on 1 screens"),
	      "the hot corner did not open Scale");
	escape(h);
	harness_set_setting(h, "org.mate.polo.hot-corners", "enabled", "false");

	harness_set_setting(h, SCALE, "empty-click", "'show-desktop'");
	chord(h, false, KEY_W);
	click_at(h, 3, 3, BTN_LEFT);
	CHECK(corner(h, -1) != BACKDROP, "an empty click did not close Scale");
	CHECK(find(h, -1, GREEN).count == 0 && find(h, -1, RED).count == 0,
	      "an empty click did not show the desktop");

	harness_set_setting(h, SCALE, "enabled", "false");
	mark = log_lines();
	chord(h, false, KEY_W);
	CHECK(!logged_since(mark, "scale: workspace on"),
	      "Scale opened while disabled");
	CHECK(corner(h, -1) != BACKDROP, "a backdrop shows while disabled");
	harness_set_setting(h, SCALE, "enabled", "true");

	chord(h, false, KEY_W);
	chord(h, false, KEY_E);
	escape(h);
	CHECK(corner(h, -1) != BACKDROP, "Super+E from Scale left a backdrop");

	harness_set_setting(h, SCALE, "empty-click", "'close'");
	chord(h, false, KEY_W);
	sa = find(h, -1, RED);
	click_at(h, sa.x, sa.y, BTN_LEFT);
	CHECK(focused(h, a), "picking after show desktop did not focus a");
	CHECK(harness_window_info(b)->state & STATE_MINIMIZED,
	      "leaving show desktop did not minimize the others");
	chord(h, false, KEY_W);
	sb = find(h, -1, GREEN);
	click_at(h, sb.x, sb.y, BTN_LEFT);
	CHECK(focused(h, b), "b did not come back");

	harness_window_destroy(c);
	harness_window_destroy(a);
	harness_window_destroy(b);
}

static const int screen_mode[][3] = { { 1024, 768, 60000 } };

static void
test_screens(struct harness *h)
{
	settle(h);
	harness_add_output(h, "HDMI-A-1", "DEL", 0x4321, 12345, "U2715H", false,
			   597, 336, screen_mode, 1);
	pause_ms(h, 200);
	CHECK(harness_output_count(h) == 2, "the second monitor is missing");
	const struct harness_output_state *second =
		harness_output_state(h, "HDMI-A-1");

	CHECK(second && second->x == HARNESS_OUTPUT_WIDTH,
	      "the second monitor is not on the right");
	point(h, 300, 300);
	struct harness_window *a = coloured(h, 300, 200, "a", RED);

	point(h, HARNESS_OUTPUT_WIDTH + 300, 300);
	pause_ms(h, 50);
	struct harness_window *b = coloured(h, 300, 200, "b", GREEN);

	CHECK(harness_window_info(a)->x < HARNESS_OUTPUT_WIDTH,
	      "a is not on the first monitor");
	CHECK(harness_window_info(b)->x >= HARNESS_OUTPUT_WIDTH,
	      "b is not on the second monitor: %d", harness_window_info(b)->x);

	int mark = log_lines();

	chord(h, false, KEY_W);
	CHECK(logged_since(mark, "scale: workspace on 2 screens, 2 windows"),
	      "Scale did not open on two screens");
	CHECK(logged_since(mark, "scale: headless0 shows a") &&
		      logged_since(mark, "scale: HDMI-A-1 shows b"),
	      "each screen does not show its own window");
	CHECK(find(h, 0, RED).count > 0 && find(h, 0, GREEN).count == 0,
	      "the first screen shows the wrong windows");
	CHECK(find(h, 1, GREEN).count > 0 && find(h, 1, RED).count == 0,
	      "the second screen shows the wrong windows");
	CHECK(corner(h, 0) == BACKDROP && corner(h, 1) == BACKDROP,
	      "a screen has no backdrop");

	struct spot sa = find(h, -1, RED);

	polo_test_v1_move_pointer(h->test, sa.x, sa.y);
	harness_roundtrip(h);
	polo_test_v1_button(h->test, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
	harness_roundtrip(h);
	for (int x = sa.x; x < HARNESS_OUTPUT_WIDTH + 500; x += 40) {
		polo_test_v1_move_pointer(h->test, x, sa.y);
		harness_roundtrip(h);
	}
	polo_test_v1_button(h->test, BTN_LEFT,
			    WL_POINTER_BUTTON_STATE_RELEASED);
	harness_roundtrip(h);
	pause_ms(h, 200);
	CHECK(harness_window_info(a)->x >= HARNESS_OUTPUT_WIDTH,
	      "dragging a to the second screen did not move it: %d",
	      harness_window_info(a)->x);
	escape(h);

	harness_set_setting(h, SCALE, "screens", "'summoned'");
	point(h, 200, 200);
	mark = log_lines();
	chord(h, false, KEY_W);
	CHECK(logged_since(mark, "scale: headless0 shows nothing") &&
		      !logged_since(mark, "scale: HDMI-A-1 shows"),
	      "summoned did not keep to the screen under the pointer");
	CHECK(corner(h, 0) == BACKDROP && corner(h, 1) != BACKDROP,
	      "summoned put a backdrop on the other screen");
	escape(h);

	harness_set_setting(h, SCALE, "summoned-windows", "'every-screen'");
	mark = log_lines();
	chord(h, false, KEY_W);
	CHECK(logged_since(mark, "scale: headless0 shows a, b") ||
		      logged_since(mark, "scale: headless0 shows b, a"),
	      "every-screen did not gather the windows");
	CHECK(find(h, 0, RED).count > 0 && find(h, 0, GREEN).count > 0,
	      "the summoned screen does not show both windows");
	escape(h);

	harness_set_setting(h, SCALE, "screens", "'mirrored'");
	mark = log_lines();
	chord(h, false, KEY_W);
	CHECK(logged_since(mark, "scale: headless0 shows") &&
		      logged_since(mark, "scale: HDMI-A-1 shows"),
	      "mirrored did not use both screens");
	for (int o = 0; o < 2; o++)
		CHECK(find(h, o, RED).count > 0 && find(h, o, GREEN).count > 0,
		      "mirrored screen %d misses a window", o);
	escape(h);

	harness_set_setting(h, SCALE, "screens", "'each'");
	harness_set_setting(h, SCALE, "mode-screens",
			    "{'workspace': 'summoned'}");
	harness_set_setting(h, SCALE, "summoned-windows", "'this-screen'");
	point(h, HARNESS_OUTPUT_WIDTH + 200, 200);
	mark = log_lines();
	chord(h, false, KEY_W);
	CHECK(logged_since(mark, "scale: HDMI-A-1 shows") &&
		      !logged_since(mark, "scale: headless0 shows"),
	      "the per-mode screens were not used");
	escape(h);
	mark = log_lines();
	chord(h, true, KEY_W);
	CHECK(logged_since(mark, "scale: HDMI-A-1 shows") &&
		      logged_since(mark, "scale: headless0 shows"),
	      "another mode took the per-mode screens");
	escape(h);
	harness_set_setting(h, SCALE, "mode-screens", "@a{ss} {}");

	mark = log_lines();
	chord(h, false, KEY_W);
	harness_remove_output(h, "HDMI-A-1");
	pause_ms(h, 300);
	CHECK(logged_since(mark, "scale: workspace on 1 screens"),
	      "Scale did not follow the unplugged monitor");
	CHECK(corner(h, 0) == BACKDROP, "the backdrop went with the monitor");
	escape(h);
	CHECK(corner(h, 0) != BACKDROP, "Scale did not close");
	CHECK(harness_window_info(a)->x < HARNESS_OUTPUT_WIDTH,
	      "a stayed off screen");

	harness_window_destroy(a);
	harness_window_destroy(b);
}

int
main(int argc, char **argv)
{
	const char *mode = argc > 2 ? argv[2] : "single";

	setup();
	struct harness *h = harness_start(argc, argv);

	if (strcmp(mode, "screens") == 0)
		test_screens(h);
	else
		test_single(h);
	CHECK(harness_stop(h) == 0, "polo did not exit cleanly");
	unlink(polo_log);
	return 0;
}
