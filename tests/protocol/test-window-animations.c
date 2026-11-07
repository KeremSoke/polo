/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wayfire's animate plugin in Polo, on GL, with 2 s animations: a window
 * fading, zooming, zapping and spinning in when it opens; a closed window,
 * plain and server-decorated, still drawn while it fades or zooms out after
 * its client destroyed it, and nothing of it left after; minimising zooms
 * into the target. With animations off a window opens at once. With
 * "pixman": opening still fades in, closing happens at once. With
 * "squeezimize": minimising squeezes the window into its target.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "harness.h"
#include "xdg-shell-client-protocol.h"

#define WINDOW_COLOUR 0x3070c0u
#define WIDTH 300
#define HEIGHT 200

struct shot {
	int changed, exact;
	int x1, y1, x2, y2;
	int top_width, bottom_width;
};

static uint32_t *baseline;
static int screen_width, screen_height;

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static struct shot
shoot(struct harness *h)
{
	int width, height;
	uint32_t *pixels = harness_capture(h, &width, &height);
	struct shot s = { .x1 = width, .y1 = height, .x2 = -1, .y2 = -1 };
	int top = -1, bottom = -1;

	CHECK(pixels && width == screen_width && height == screen_height,
	      "cannot capture");
	for (int i = 0; i < width * height; i++) {
		int x = i % width, y = i / width;
		bool exact = (pixels[i] & 0xffffff) == WINDOW_COLOUR;

		if ((pixels[i] & 0xffffff) == (baseline[i] & 0xffffff))
			continue;
		s.changed++;
		s.exact += exact;
		s.x1 = x < s.x1 ? x : s.x1;
		s.y1 = y < s.y1 ? y : s.y1;
		s.x2 = x > s.x2 ? x : s.x2;
		s.y2 = y > s.y2 ? y : s.y2;
		if (exact) {
			if (top < 0)
				top = y;
			bottom = y;
		}
	}
	for (int x = 0; x < width && top >= 0; x++) {
		s.top_width +=
			(pixels[top * width + x] & 0xffffff) == WINDOW_COLOUR;
		s.bottom_width += (pixels[bottom * width + x] & 0xffffff) ==
			WINDOW_COLOUR;
	}
	free(pixels);
	return s;
}

static void
settle(struct harness *h, const char *what)
{
	pause_ms(h, 2300);
	struct shot s = shoot(h);

	CHECK(s.changed == 0, "%s left %d pixels behind", what, s.changed);
}

static struct harness_window *
open_window(struct harness *h, const char *title, bool decorated)
{
	return decorated
		? harness_window_create_decorated(h, WIDTH, HEIGHT, title)
		: harness_window_create(h, WIDTH, HEIGHT, title);
}

static void
test_open_close(struct harness *h)
{
	harness_set_setting(h, "org.mate.polo.effects", "open-animation",
			    "'fade'");
	harness_set_setting(h, "org.mate.polo.effects", "close-animation",
			    "'fade'");
	struct harness_window *window = open_window(h, "fade", false);
	const struct harness_window_info *info = harness_window_info(window);
	int x = info->x, y = info->y;

	pause_ms(h, 400);
	struct shot s = shoot(h);
	CHECK(s.exact == 0 && s.changed > WIDTH * HEIGHT / 2,
	      "fading in: %d exact and %d changed pixels", s.exact, s.changed);
	pause_ms(h, 2000);
	s = shoot(h);
	CHECK(s.exact == WIDTH * HEIGHT, "faded in to %d pixels", s.exact);

	harness_window_destroy(window);
	pause_ms(h, 400);
	s = shoot(h);
	CHECK(s.exact == 0 && s.changed > WIDTH * HEIGHT / 2 && s.x1 >= x &&
		      s.y1 >= y && s.x2 < x + WIDTH && s.y2 < y + HEIGHT,
	      "a closed window is not fading out where it was: %d changed "
	      "at %d,%d-%d,%d",
	      s.changed, s.x1, s.y1, s.x2, s.y2);
	settle(h, "fading out");

	harness_set_setting(h, "org.mate.polo.effects", "open-animation",
			    "'zoom'");
	harness_set_setting(h, "org.mate.polo.effects", "close-animation",
			    "'zoom'");
	window = open_window(h, "zoom", false);
	info = harness_window_info(window);
	x = info->x;
	y = info->y;
	pause_ms(h, 200);
	s = shoot(h);
	CHECK(s.changed > 0 && s.x2 - s.x1 + 1 < WIDTH * 3 / 4 &&
		      abs((s.x1 + s.x2) / 2 - (x + WIDTH / 2)) <= 3 &&
		      abs((s.y1 + s.y2) / 2 - (y + HEIGHT / 2)) <= 3,
	      "zooming in: %d,%d-%d,%d about %d,%d", s.x1, s.y1, s.x2, s.y2,
	      x + WIDTH / 2, y + HEIGHT / 2);
	pause_ms(h, 2200);
	CHECK(shoot(h).exact == WIDTH * HEIGHT, "did not zoom in whole");
	harness_window_destroy(window);
	pause_ms(h, 1600);
	s = shoot(h);
	CHECK(s.changed > 0 && s.x2 - s.x1 + 1 < WIDTH * 3 / 4,
	      "zooming out: %d changed, %d wide", s.changed, s.x2 - s.x1 + 1);
	settle(h, "zooming out");

	harness_set_setting(h, "org.mate.polo.effects", "close-animation",
			    "'fade'");
	window = open_window(h, "decorated", true);
	pause_ms(h, 2300);
	info = harness_window_info(window);
	y = info->y;
	harness_window_destroy(window);
	pause_ms(h, 400);
	s = shoot(h);
	CHECK(s.changed > 0 && s.y1 < y,
	      "the decorations did not fade out with the window: top %d, "
	      "client at %d",
	      s.y1, y);
	settle(h, "a decorated window fading out");
	harness_set_setting(h, "org.mate.polo.effects", "close-animation",
			    "'none'");
}

static void
test_surface_first(struct harness *h)
{
	harness_set_setting(h, "org.mate.polo.effects", "close-animation",
			    "'fade'");
	struct harness_window *window = open_window(h, "surface first", false);
	struct harness *owner = window->harness;

	pause_ms(h, 2300);
	for (int i = 0; i < owner->n_windows; i++) {
		if (owner->windows[i] == window) {
			owner->windows[i] = owner->windows[--owner->n_windows];
			break;
		}
	}
	if (owner->keyboard_focus == window->surface)
		owner->keyboard_focus = NULL;
	if (owner->pointer_focus == window->surface)
		owner->pointer_focus = NULL;
	wl_surface_destroy(window->surface);
	harness_roundtrip(h);
	xdg_toplevel_destroy(window->xdg_toplevel);
	xdg_surface_destroy(window->xdg_surface);
	if (window->buffer)
		wl_buffer_destroy(window->buffer);
	free(window);
	harness_roundtrip(h);
	settle(h, "a window whose surface went first");
	harness_set_setting(h, "org.mate.polo.effects", "close-animation",
			    "'none'");
}

static void
test_zap_spin(struct harness *h)
{
	harness_set_setting(h, "org.mate.polo.effects", "open-animation",
			    "'zap'");
	struct harness_window *window = open_window(h, "zap", false);

	pause_ms(h, 1600);
	struct shot s = shoot(h);
	CHECK(s.exact > 0 && s.x2 - s.x1 + 1 >= WIDTH - 4 &&
		      s.y2 - s.y1 + 1 < HEIGHT * 3 / 4,
	      "zapping: %d exact pixels in %dx%d", s.exact, s.x2 - s.x1 + 1,
	      s.y2 - s.y1 + 1);
	pause_ms(h, 700);
	CHECK(shoot(h).exact == WIDTH * HEIGHT, "did not zap in whole");
	harness_window_destroy(window);
	settle(h, "closing without an animation");

	harness_set_setting(h, "org.mate.polo.effects", "open-animation",
			    "'spin'");
	window = open_window(h, "spin", false);
	pause_ms(h, 1000);
	s = shoot(h);
	CHECK(s.exact == 0 && s.changed > 0 && s.x2 - s.x1 + 1 < WIDTH * 3 / 4,
	      "spinning: %d exact, %d changed, %d wide", s.exact, s.changed,
	      s.x2 - s.x1 + 1);
	pause_ms(h, 1300);
	CHECK(shoot(h).exact == WIDTH * HEIGHT, "did not spin in whole");

	harness_set_setting(h, "org.mate.polo.effects", "open-animation",
			    "'none'");
	harness_set_setting(h, "org.mate.polo.effects", "minimize-animation",
			    "'zoom'");
	const struct harness_window_info *info = harness_window_info(window);
	int centre = info->x + WIDTH / 2;

	xdg_toplevel_set_minimized(window->xdg_toplevel);
	harness_roundtrip(h);
	pause_ms(h, 600);
	s = shoot(h);
	CHECK(s.changed > 0 && s.x2 - s.x1 + 1 < WIDTH &&
		      (s.x1 + s.x2) / 2 < centre,
	      "zooming into the target at the pointer: %d changed, centre %d "
	      "from %d",
	      s.changed, (s.x1 + s.x2) / 2, centre);
	settle(h, "minimising");
	h->n_windows = 0;
	harness_window_destroy(window);
}

static void
test_animations_off(struct harness *h)
{
	harness_set_setting(h, "org.mate.polo.effects", "open-animation",
			    "'fade'");
	harness_set_setting(h, "org.mate.interface", "enable-animations",
			    "false");
	struct harness_window *window = open_window(h, "off", false);

	CHECK(shoot(h).exact == WIDTH * HEIGHT,
	      "with animations off a window did not open at once");
	h->n_windows = 0;
	harness_window_destroy(window);
}

static void
test_pixman(struct harness *h)
{
	harness_set_setting(h, "org.mate.polo.effects", "open-animation",
			    "'fade'");
	harness_set_setting(h, "org.mate.polo.effects", "close-animation",
			    "'fade'");
	struct harness_window *window = open_window(h, "pixman", false);

	pause_ms(h, 400);
	struct shot s = shoot(h);
	CHECK(s.exact == 0 && s.changed > WIDTH * HEIGHT / 2,
	      "fading in on Pixman: %d exact and %d changed pixels", s.exact,
	      s.changed);
	pause_ms(h, 2000);
	CHECK(shoot(h).exact == WIDTH * HEIGHT, "did not fade in whole");
	h->n_windows = 0;
	harness_window_destroy(window);
	pause_ms(h, 100);
	s = shoot(h);
	CHECK(s.changed == 0, "on Pixman a closed window lingered: %d pixels",
	      s.changed);
}

static void
test_squeezimize(struct harness *h)
{
	harness_set_setting(h, "org.mate.polo.effects", "minimize-animation",
			    "'squeezimize'");
	harness_set_setting(h, "org.mate.polo.effects",
			    "squeezimize-duration-ms", "2000");
	struct harness_window *window = open_window(h, "squeeze", false);

	pause_ms(h, 300);
	CHECK(shoot(h).exact == WIDTH * HEIGHT, "the window is not whole");
	xdg_toplevel_set_minimized(window->xdg_toplevel);
	harness_roundtrip(h);
	pause_ms(h, 900);
	struct shot s = shoot(h);
	CHECK(s.exact > 0 && s.exact < WIDTH * HEIGHT &&
		      abs(s.top_width - s.bottom_width) > 20,
	      "squeezing: %d exact pixels, rows %d and %d wide", s.exact,
	      s.top_width, s.bottom_width);
	settle(h, "squeezing");
	h->n_windows = 0;
	harness_window_destroy(window);
}

int
main(int argc, char **argv)
{
	bool squeeze = argc > 2 && strcmp(argv[2], "squeezimize") == 0;
	bool pixman = argc > 2 && strcmp(argv[2], "pixman") == 0;

	if (!pixman)
		setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	struct harness *h = harness_start(argc, argv);

	harness_set_setting(h, "org.mate.polo.effects", "animation-duration-ms",
			    "2000");
	harness_set_setting(h, "org.mate.polo.effects", "zap-duration-ms",
			    "2000");
	harness_set_setting(h, "org.mate.polo.effects", "spin-duration-ms",
			    "2000");
	harness_move_pointer(h, 5, 5);
	pause_ms(h, 100);
	baseline = harness_capture(h, &screen_width, &screen_height);
	CHECK(baseline, "cannot capture the empty screen");
	if (squeeze) {
		test_squeezimize(h);
	} else if (pixman) {
		test_pixman(h);
	} else {
		test_open_close(h);
		test_surface_first(h);
		test_zap_spin(h);
		test_animations_off(h);
	}
	free(baseline);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
