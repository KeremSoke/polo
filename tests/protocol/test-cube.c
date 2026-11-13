/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * End-to-end cube rendering and workspace policy, with one coloured window
 * on each of four workspaces. Captures check the screen, not just the model.
 */
#include <linux/input.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "harness.h"
#include "polo-fx.h"

#define WINDOW_WIDTH 220
#define WINDOW_HEIGHT 150
#define PIXELS (WINDOW_WIDTH * WINDOW_HEIGHT)
#define BACKDROP 0x204060u

static const uint32_t colours[4] = { 0xc03030u, 0x30c030u, 0x3030c0u,
				     0xc0c030u };

struct frame {
	uint32_t *pixels;
	int width, height;
	int count[4], min_x[4], max_x[4];
	int backdrop;
};

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec delay = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&delay, NULL);
	harness_roundtrip(h);
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_roundtrip(h);
}

static void
modifiers(struct harness *h, bool down, bool shift)
{
	if (down) {
		harness_key(h, KEY_LEFTCTRL, true);
		harness_key(h, KEY_LEFTALT, true);
		if (shift)
			harness_key(h, KEY_LEFTSHIFT, true);
	} else {
		if (shift)
			harness_key(h, KEY_LEFTSHIFT, false);
		harness_key(h, KEY_LEFTALT, false);
		harness_key(h, KEY_LEFTCTRL, false);
	}
}

static void
direction(struct harness *h, uint32_t key, bool shift)
{
	modifiers(h, true, shift);
	tap(h, key);
	modifiers(h, false, shift);
}

static void
numbered(struct harness *h, int workspace)
{
	harness_key(h, KEY_LEFTCTRL, true);
	tap(h, KEY_F1 + workspace);
	harness_key(h, KEY_LEFTCTRL, false);
}

static void
open_cube(struct harness *h)
{
	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_C);
	harness_key(h, KEY_LEFTMETA, false);
	pause_ms(h, 700);
}

static void
motion(struct harness *h, int x, int y)
{
	polo_test_v1_move_pointer(h->test, x, y);
	harness_roundtrip(h);
}

static void
wheel(struct harness *h, int amount)
{
	polo_test_v1_pointer_axis(h->test, amount);
	harness_roundtrip(h);
}

static void
button(struct harness *h, uint32_t code, bool pressed)
{
	polo_test_v1_button(h->test, code,
			    pressed ? WL_POINTER_BUTTON_STATE_PRESSED
				    : WL_POINTER_BUTTON_STATE_RELEASED);
	harness_roundtrip(h);
}

static void
paint(struct harness_window *window, uint32_t colour)
{
	struct wl_buffer *old = window->buffer;
	window->buffer = harness_buffer_create_colour(
		window->harness->shm, WINDOW_WIDTH, WINDOW_HEIGHT,
		0xff000000u | colour);
	wl_surface_attach(window->surface, window->buffer, 0, 0);
	wl_surface_damage_buffer(window->surface, 0, 0, WINDOW_WIDTH,
				 WINDOW_HEIGHT);
	harness_wait_frame(window->harness, window->surface);
	if (old)
		wl_buffer_destroy(old);
}

static struct frame
capture(struct harness *h, int output)
{
	struct frame frame = { 0 };
	frame.pixels =
		harness_capture_output(h, output, &frame.width, &frame.height);
	CHECK(frame.pixels, "cannot capture output %d", output);
	for (int i = 0; i < 4; i++)
		frame.min_x[i] = frame.width;
	for (int y = 0; y < frame.height; y++) {
		for (int x = 0; x < frame.width; x++) {
			uint32_t colour =
				frame.pixels[y * frame.width + x] & 0xffffff;
			frame.backdrop += colour == BACKDROP;
			for (int i = 0; i < 4; i++) {
				if (colour != colours[i])
					continue;
				frame.count[i]++;
				if (x < frame.min_x[i])
					frame.min_x[i] = x;
				if (x > frame.max_x[i])
					frame.max_x[i] = x;
			}
		}
	}
	return frame;
}

static void
same_frame(const struct frame *actual, const struct frame *expected,
	   const char *where)
{
	CHECK(actual->width == expected->width &&
		      actual->height == expected->height,
	      "%s: output size changed", where);
	CHECK(memcmp(actual->pixels, expected->pixels,
		     (size_t)actual->width * actual->height * 4) == 0,
	      "%s: cube left pixels after it ended", where);
}

static void
free_frame(struct frame *frame)
{
	free(frame->pixels);
	frame->pixels = NULL;
}

static void
destroy_windows(struct harness *h)
{
	struct harness_window *windows[16];
	int count = h->n_windows;
	memcpy(windows, h->windows, (size_t)count * sizeof windows[0]);
	h->n_windows = 0;
	for (int i = 0; i < count; i++)
		harness_window_destroy(windows[i]);
}

static void
test_gl(int argc, char **argv)
{
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	setenv("POLO_TEST_WIDTH", "--width=640", 1);
	setenv("POLO_TEST_HEIGHT", "--height=480", 1);
	setenv("POLO_TEST_PREFS",
	       "global-keybindings/switch-to-workspace-1='<Control>F1';"
	       "global-keybindings/switch-to-workspace-2='<Control>F2';"
	       "global-keybindings/switch-to-workspace-3='<Control>F3';"
	       "global-keybindings/switch-to-workspace-4='<Control>F4'",
	       1);
	struct harness *h = harness_start(argc, argv);
	struct harness_window *windows[4];
	struct frame baseline[4];
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	harness_set_setting(h, "org.mate.polo.effects", "cube-light", "false");
	harness_set_setting(h, "org.mate.polo.effects", "cube-duration-ms",
			    "800");
	harness_set_setting(h, "org.mate.polo.effects", "cube-background-color",
			    "'#204060'");
	harness_move_pointer(h, 5, 5);
	for (int i = 0; i < 4; i++) {
		if (i) {
			numbered(h, i);
			pause_ms(h, 100);
		}
		windows[i] = harness_window_create(
			h, WINDOW_WIDTH, WINDOW_HEIGHT, "cube-colour");
		paint(windows[i], colours[i]);
		CHECK(harness_window_info(windows[i])->workspace == i,
		      "window %d landed on the wrong workspace", i);
		baseline[i] = capture(h, 0);
		CHECK(baseline[i].count[i] == PIXELS,
		      "workspace %d has %d coloured pixels", i,
		      baseline[i].count[i]);
	}
	numbered(h, 0);
	pause_ms(h, 100);
	harness_set_setting(h, "org.mate.polo.effects", "cube", "true");

	modifiers(h, true, false);
	tap(h, KEY_RIGHT);
	CHECK(!harness_window_info(windows[0])->focused &&
		      harness_window_info(windows[1])->focused,
	      "model did not select the next workspace immediately");
	struct frame turning = capture(h, 0);
	bool ordered = false;

	for (int i = 0; i < 40; i++) {
		ordered = turning.count[0] > 1000 &&
			turning.count[0] < PIXELS && turning.count[1] > 1000 &&
			turning.count[1] < PIXELS &&
			turning.max_x[0] < turning.min_x[1];
		if (ordered)
			break;
		free_frame(&turning);
		pause_ms(h, 20);
		turning = capture(h, 0);
	}
	CHECK(ordered,
	      "two sides are not narrow and ordered: %d at %d, %d at %d",
	      turning.count[0], turning.max_x[0], turning.count[1],
	      turning.min_x[1]);
	free_frame(&turning);
	modifiers(h, false, false);
	pause_ms(h, 1000);
	turning = capture(h, 0);
	same_frame(&turning, &baseline[1], "one turn");
	free_frame(&turning);

	modifiers(h, true, false);
	tap(h, KEY_RIGHT);
	tap(h, KEY_RIGHT);
	modifiers(h, false, false);
	pause_ms(h, 1000);
	turning = capture(h, 0);
	same_frame(&turning, &baseline[3], "two turns");
	free_frame(&turning);
	numbered(h, 0);
	pause_ms(h, 1000);
	direction(h, KEY_LEFT, false);
	pause_ms(h, 1000);
	turning = capture(h, 0);
	same_frame(&turning, &baseline[3], "left wrap");
	free_frame(&turning);
	numbered(h, 2);
	CHECK(harness_window_info(windows[2])->focused,
	      "numbered key did not change model immediately");
	pause_ms(h, 1000);
	turning = capture(h, 0);
	same_frame(&turning, &baseline[2], "numbered from fourth");
	free_frame(&turning);
	numbered(h, 0);
	pause_ms(h, 1000);
	numbered(h, 2);
	pause_ms(h, 1000);
	turning = capture(h, 0);
	same_frame(&turning, &baseline[2], "numbered from first");
	free_frame(&turning);
	numbered(h, 0);
	pause_ms(h, 1000);

	open_cube(h);
	turning = capture(h, 0);
	CHECK(turning.count[0] > 1000 && turning.count[0] < PIXELS &&
		      turning.backdrop > 1000 &&
		      (turning.pixels[0] & 0xffffff) == BACKDROP,
	      "interactive cube did not zoom out over its backdrop");
	int zoomed = turning.count[0];
	free_frame(&turning);
	wheel(h, 10);
	pause_ms(h, 200);
	turning = capture(h, 0);
	CHECK(turning.count[0] > 0 && turning.count[0] < zoomed,
	      "wheel did not shrink the front side");
	free_frame(&turning);
	tap(h, KEY_ESC);
	pause_ms(h, 1000);
	turning = capture(h, 0);
	same_frame(&turning, &baseline[0], "Escape");
	free_frame(&turning);

	open_cube(h);
	int px, py;
	harness_pointer_position(h, &px, &py);
	motion(h, px - 80, py);
	pause_ms(h, 850);
	tap(h, KEY_ENTER);
	pause_ms(h, 1000);
	turning = capture(h, 0);
	same_frame(&turning, &baseline[1], "pointer turn and Enter");
	free_frame(&turning);

	modifiers(h, true, false);
	button(h, BTN_LEFT, true);
	motion(h, px - 80, py);
	pause_ms(h, 850);
	button(h, BTN_LEFT, false);
	modifiers(h, false, false);
	pause_ms(h, 1000);
	turning = capture(h, 0);
	same_frame(&turning, &baseline[2], "button drag");
	free_frame(&turning);

	numbered(h, 0);
	pause_ms(h, 1000);
	modifiers(h, true, true);
	tap(h, KEY_RIGHT);
	pause_ms(h, 200);
	turning = capture(h, 0);
	CHECK(turning.count[0] == PIXELS,
	      "carried window was deformed: %d pixels", turning.count[0]);
	free_frame(&turning);
	modifiers(h, false, true);
	pause_ms(h, 1000);
	CHECK(harness_window_info(windows[0])->workspace == 1 &&
		      harness_window_info(windows[0])->focused,
	      "carried window did not land focused on the second workspace");
	for (int i = 0; i < 4; i++)
		free_frame(&baseline[i]);
	destroy_windows(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

static void
test_fallback(int argc, char **argv, bool pixman)
{
	setenv("POLO_TEST_RENDERER",
	       pixman ? "--renderer=pixman" : "--renderer=gl", 1);
	setenv("POLO_TEST_WIDTH", "--width=640", 1);
	setenv("POLO_TEST_HEIGHT", "--height=480", 1);
	struct harness *h = harness_start(argc, argv);
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	harness_set_setting(h, "org.mate.polo.effects", "cube", "true");
	harness_set_setting(h, "org.mate.polo.effects", "cube-background-color",
			    "'#204060'");
	harness_set_setting(h, "org.mate.polo.effects", "cube-duration-ms",
			    "800");
	if (!pixman)
		harness_set_setting(h, "org.mate.interface",
				    "enable-animations", "false");
	harness_move_pointer(h, 5, 5);
	struct harness_window *window = harness_window_create(
		h, WINDOW_WIDTH, WINDOW_HEIGHT, "cube-fallback");
	paint(window, colours[0]);
	struct frame initial = capture(h, 0);
	CHECK(initial.count[0] == PIXELS,
	      "fallback fixture has %d window pixels", initial.count[0]);

	direction(h, KEY_RIGHT, false);
	CHECK(!harness_window_info(window)->focused,
	      "fallback did not switch workspace immediately");
	struct frame switched = capture(h, 0);
	CHECK(switched.count[0] == 0 && switched.backdrop == 0,
	      "fallback rendered a cube frame (%d window, %d backdrop)",
	      switched.count[0], switched.backdrop);
	free_frame(&switched);
	open_cube(h);
	switched = capture(h, 0);
	CHECK(switched.count[0] == 0 && switched.backdrop == 0,
	      "cube key rendered in fallback mode");
	free_frame(&switched);
	direction(h, KEY_LEFT, false);
	switched = capture(h, 0);
	same_frame(&switched, &initial, "fallback switch back");
	free_frame(&switched);
	free_frame(&initial);
	destroy_windows(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

static void
move_window_east(struct harness *h)
{
	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_RIGHT);
	harness_key(h, KEY_LEFTMETA, false);
}

static void
test_outputs(int argc, char **argv)
{
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	setenv("POLO_TEST_PREFS",
	       "window-keybindings/move-to-monitor-e='<Super>Right'", 1);
	struct harness *h = harness_start(argc, argv);
	CHECK(h->n_outputs == 2, "expected two outputs, got %d", h->n_outputs);
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	harness_set_setting(h, "org.mate.polo.effects", "cube-light", "false");
	harness_set_setting(h, "org.mate.polo.effects", "cube-duration-ms",
			    "4000");
	harness_move_pointer(h, 5, 5);
	struct frame baseline[2][2];
	for (int workspace = 0; workspace < 2; workspace++) {
		if (workspace)
			direction(h, KEY_RIGHT, false);
		struct harness_window *west = harness_window_create(
			h, WINDOW_WIDTH, WINDOW_HEIGHT, "cube-west");
		paint(west, colours[workspace * 2]);
		struct harness_window *east = harness_window_create(
			h, WINDOW_WIDTH, WINDOW_HEIGHT, "cube-east");
		paint(east, colours[workspace * 2 + 1]);
		move_window_east(h);
		CHECK(harness_window_info(east)->x >= 1024,
		      "east window did not move to second output");
		for (int output = 0; output < 2; output++) {
			baseline[workspace][output] = capture(h, output);
			CHECK(baseline[workspace][output]
					      .count[workspace * 2 + output] ==
				      PIXELS,
			      "workspace %d output %d has no own window",
			      workspace, output);
		}
	}
	direction(h, KEY_LEFT, false);
	harness_set_setting(h, "org.mate.polo.effects", "cube", "true");
	modifiers(h, true, false);
	tap(h, KEY_RIGHT);
	modifiers(h, false, false);
	pause_ms(h, 300);
	for (int output = 0; output < 2; output++) {
		struct frame turning = capture(h, output);
		CHECK(turning.count[output] > 1000 &&
			      turning.count[output + 2] > 1000,
		      "output %d did not draw both own workspace sides",
		      output);
		CHECK(turning.count[1 - output] == 0 &&
			      turning.count[3 - output] == 0,
		      "output %d drew the other output's windows", output);
		free_frame(&turning);
	}
	pause_ms(h, 4200);
	for (int output = 0; output < 2; output++) {
		struct frame landed = capture(h, output);
		same_frame(&landed, &baseline[1][output], "two-output turn");
		free_frame(&landed);
	}
	for (int workspace = 0; workspace < 2; workspace++)
		for (int output = 0; output < 2; output++)
			free_frame(&baseline[workspace][output]);
	destroy_windows(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

static int
left_edge(const struct frame *frame, int y, uint32_t colour)
{
	for (int x = 0; x < frame->width; x++)
		if ((frame->pixels[y * frame->width + x] & 0xffffff) == colour)
			return x;
	return -1;
}

static void
test_deform(int argc, char **argv)
{
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	setenv("POLO_TEST_WIDTH", "--width=640", 1);
	setenv("POLO_TEST_HEIGHT", "--height=480", 1);
	struct harness *h = harness_start(argc, argv);
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	harness_set_setting(h, "org.mate.polo.effects", "cube", "true");
	harness_set_setting(h, "org.mate.polo.effects", "cube-light", "false");
	harness_set_setting(h, "org.mate.polo.effects", "cube-duration-ms",
			    "300");
	harness_move_pointer(h, 5, 5);
	struct harness_window *window = harness_window_create(
		h, WINDOW_WIDTH, WINDOW_HEIGHT, "cube-deform");
	paint(window, colours[0]);
	struct frame baseline = capture(h, 0);
	int middle = baseline.height / 2;
	int source_x = left_edge(&baseline, middle, colours[0]);
	CHECK(source_x >= 0, "deformation fixture missing at midline");
	for (int mode = 0; mode < 2; mode++) {
		harness_set_setting(h, "org.mate.polo.effects", "cube-deform",
				    mode ? "'star'" : "'cylinder'");
		open_cube(h);
		PoloFxCube cube = {
			.sides = 4,
			.offset_z = polo_fx_cube_rest_offset_z(4) + 0.1,
			.zoom = 1,
			.ease = 1,
			.deform = mode ? POLO_FX_CUBE_DEFORM_STAR
				       : POLO_FX_CUBE_DEFORM_CYLINDER,
			.size = { 640, 480 },
		};
		PoloFxVec2 projected;
		CHECK(polo_fx_cube_point(&cube, 0,
					 (PoloFxVec2){ source_x, middle },
					 &projected),
		      "deformed pixel is behind camera");
		struct frame actual = capture(h, 0);
		int y = (int)lround(projected.y);
		CHECK(y >= 0 && y < actual.height,
		      "deformed pixel moved outside output");
		int observed = left_edge(&actual, y, colours[0]);
		CHECK(observed >= 0 &&
			      abs(observed - (int)lround(projected.x)) <= 8,
		      "%s moved known edge to %d, Fx.Cube.point predicts %.1f",
		      mode ? "star" : "cylinder", observed, projected.x);
		free_frame(&actual);
		tap(h, KEY_ESC);
		pause_ms(h, 400);
		actual = capture(h, 0);
		same_frame(&actual, &baseline, "deformation exit");
		free_frame(&actual);
	}
	free_frame(&baseline);
	destroy_windows(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

int
main(int argc, char **argv)
{
	if (argc >= 3 && !strcmp(argv[2], "pixman"))
		test_fallback(argc, argv, true);
	else if (argc >= 3 && !strcmp(argv[2], "animations-off"))
		test_fallback(argc, argv, false);
	else if (argc >= 3 && !strcmp(argv[2], "outputs"))
		test_outputs(argc, argv);
	else if (argc >= 3 && !strcmp(argv[2], "deform"))
		test_deform(argc, argv);
	else
		test_gl(argc, argv);
	return 0;
}
