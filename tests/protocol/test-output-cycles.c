/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Output robustness: a monitor at another refresh rate docked and undocked
 * 30 times (every fifth time its connector goes too, as with a dock), 30
 * suspend and resume cycles with the monitor changing while asleep, and
 * monitor, scale and rotation changes while the cube and expo animate. After
 * each step every window is on a monitor that is on, every work area matches
 * its monitor and frames still come.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <linux/input.h>

#include "harness.h"

#define WL_OUTPUT_TRANSFORM_NORMAL 0
#define WL_OUTPUT_TRANSFORM_90 1

static const int laptop[][3] = { { 1920, 1080, 60000 } };
static const int fast[][3] = { { 2560, 1440, 144000 }, { 1920, 1080, 60000 } };

static struct harness_window *windows[3];

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_roundtrip(h);
}

static bool
inside(const struct harness_output_state *o, int x, int y)
{
	return x >= o->x && y >= o->y && x < o->x + o->width &&
		y < o->y + o->height;
}

/* Wait for the layout to settle, then hold every check. Frames come only
 * from windows on the workspace shown, so not while the cube turns. */
static void
check_sane(struct harness *h, const char *when, int monitors, bool frames)
{
	for (int i = 0; i < 100; i++) {
		harness_output_state(h, "");
		if (h->monitors == (unsigned)monitors)
			break;
		pause_ms(h, 10);
	}
	pause_ms(h, 50);
	harness_output_state(h, "");
	CHECK(h->monitors == (unsigned)monitors, "%s: %u monitors, not %d",
	      when, h->monitors, monitors);

	for (int i = 0; i < h->n_output_states; i++) {
		const struct harness_output_state *o = &h->output_states[i];

		if (!o->enabled)
			continue;
		CHECK(o->area_width > 0 && o->area_height > 0 &&
			      o->area_x >= o->x && o->area_y >= o->y &&
			      o->area_x + o->area_width <= o->x + o->width &&
			      o->area_y + o->area_height <= o->y + o->height,
		      "%s: %s at %d,%d %dx%d has a work area of %d,%d %dx%d",
		      when, o->name, o->x, o->y, o->width, o->height, o->area_x,
		      o->area_y, o->area_width, o->area_height);
	}

	for (int w = 0; w < 3; w++) {
		const struct harness_window_info *info =
			harness_window_info(windows[w]);
		int cx = info->x + info->width / 2,
		    cy = info->y + info->height / 2;
		bool shown = false;

		CHECK(info->known, "%s: Polo forgot window %d", when, w);
		for (int i = 0; i < h->n_output_states && !shown; i++)
			shown = h->output_states[i].enabled &&
				inside(&h->output_states[i], cx, cy);
		CHECK(shown, "%s: window %d at %d,%d %dx%d is on no monitor",
		      when, w, info->x, info->y, info->width, info->height);
		if (frames)
			harness_wait_frame(h, windows[w]->surface);
	}
}

static void
expect_sane(struct harness *h, const char *when, int monitors)
{
	check_sane(h, when, monitors, true);
}

static void
cube_step(struct harness *h, uint32_t direction)
{
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	tap(h, direction);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	harness_roundtrip(h);
}

/* Turn back to the first workspace once the cube is done, and look again. */
static void
cube_back(struct harness *h, const char *when, int monitors)
{
	char back[80];

	pause_ms(h, 1700);
	cube_step(h, KEY_LEFT);
	pause_ms(h, 1700);
	snprintf(back, sizeof back, "back from %s", when);
	expect_sane(h, back, monitors);
}

static int
index_of(struct harness *h, const char *name)
{
	harness_output_state(h, "");
	for (int i = 0; i < h->n_output_states; i++)
		if (strcmp(h->output_states[i].name, name) == 0)
			return i;
	CHECK(false, "no output %s", name);
	return -1;
}

int
main(int argc, char **argv)
{
	struct harness *h;
	const struct harness_output_state *o;
	char when[64];

	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	h = harness_start(argc, argv);
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	harness_set_setting(h, "org.mate.polo.effects", "cube", "true");
	harness_set_setting(h, "org.mate.polo.effects", "cube-duration-ms",
			    "1500");

	harness_add_output(h, "eDP-1", "BOE", 0x0a1b, 0, "Laptop", true, 344,
			   194, laptop, 1);
	harness_connect_output(h, "headless0", false);
	harness_move_pointer_absolute(h, 100, 100);
	for (int w = 0; w < 3; w++)
		windows[w] =
			harness_window_create(h, 400 + 50 * w, 300, "cycle");
	expect_sane(h, "at start", 1);

	harness_add_output(h, "DP-1", "AOC", 0x2701, 4242, "Q27G2", false, 597,
			   336, fast, 2);
	expect_sane(h, "with the monitor added", 2);
	o = harness_output_state(h, "DP-1");
	CHECK(o && o->refresh == 144000, "the monitor runs at %u mHz",
	      o ? o->refresh : 0);
	o = harness_output_state(h, "eDP-1");
	CHECK(o && o->refresh == 60000, "the laptop runs at %u mHz",
	      o ? o->refresh : 0);

	for (int cycle = 0; cycle < 30; cycle++) {
		bool unplug_dock = cycle % 5 == 4;

		snprintf(when, sizeof when, "undock %d", cycle);
		if (unplug_dock)
			harness_remove_output(h, "DP-1");
		else
			harness_connect_output(h, "DP-1", false);
		expect_sane(h, when, 1);
		snprintf(when, sizeof when, "dock %d", cycle);
		if (unplug_dock)
			harness_add_output(h, "DP-1", "AOC", 0x2701, 4242,
					   "Q27G2", false, 597, 336, fast, 2);
		else
			harness_connect_output(h, "DP-1", true);
		expect_sane(h, when, 2);
	}

	for (int cycle = 0; cycle < 30; cycle++) {
		bool docked = cycle % 2 == 0;

		harness_set_session_active(h, false);
		pause_ms(h, 20);
		harness_connect_output(h, "DP-1", !docked);
		pause_ms(h, 20);
		harness_set_session_active(h, true);
		snprintf(when, sizeof when, "resume %d", cycle);
		expect_sane(h, when, docked ? 1 : 2);
	}
	harness_connect_output(h, "DP-1", true);
	expect_sane(h, "docked again", 2);

	cube_step(h, KEY_RIGHT);
	pause_ms(h, 300);
	harness_connect_output(h, "DP-1", false);
	check_sane(h, "undocked during the cube", 1, false);
	cube_back(h, "undocked during the cube", 1);
	cube_step(h, KEY_RIGHT);
	pause_ms(h, 300);
	harness_connect_output(h, "DP-1", true);
	check_sane(h, "docked during the cube", 2, false);
	cube_back(h, "docked during the cube", 2);

	cube_step(h, KEY_RIGHT);
	pause_ms(h, 300);
	polo_test_v1_configure_output(h->test, index_of(h, "eDP-1"), 2,
				      WL_OUTPUT_TRANSFORM_NORMAL, 0, 0);
	check_sane(h, "scale 2 during the cube", 2, false);
	cube_back(h, "scale 2 during the cube", 2);
	cube_step(h, KEY_RIGHT);
	pause_ms(h, 300);
	polo_test_v1_configure_output(h->test, index_of(h, "eDP-1"), 1,
				      WL_OUTPUT_TRANSFORM_90, 0, 0);
	check_sane(h, "rotated during the cube", 2, false);
	o = harness_output_state(h, "eDP-1");
	CHECK(o && o->width == 1080 && o->height == 1920,
	      "the rotated laptop is %dx%d", o ? o->width : 0,
	      o ? o->height : 0);
	cube_back(h, "rotated during the cube", 2);

	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_E);
	harness_key(h, KEY_LEFTMETA, false);
	pause_ms(h, 200);
	harness_connect_output(h, "DP-1", false);
	check_sane(h, "undocked during expo", 1, false);
	polo_test_v1_configure_output(h->test, index_of(h, "eDP-1"), 1,
				      WL_OUTPUT_TRANSFORM_NORMAL, 0, 0);
	check_sane(h, "unrotated during expo", 1, false);
	harness_connect_output(h, "DP-1", true);
	check_sane(h, "docked during expo", 2, false);
	tap(h, KEY_ESC);
	pause_ms(h, 500);
	expect_sane(h, "at the end", 2);

	for (int w = 0; w < 3; w++)
		harness_window_destroy(windows[w]);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
