/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * argv[2]: fake-shell-ui. The display-mode popup on fake monitors: Super+P
 * opens it on the mode after the current one, P again moves on, releasing
 * Super applies; the display key, arrows, Enter, Escape, a press outside
 * and a selection in the popup; mirroring switches the laptop's mode so
 * both screens show the same picture, expo included; mirror is greyed out
 * and skipped without a common mode; and unplugging the only lit screen
 * lights another.
 */
#include <errno.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "harness.h"

static const int laptop[][3] = { { 1920, 1080, 60000 }, { 1024, 768, 60000 } };
static const int wide[][3] = { { 2560, 1440, 59951 } };

static char log_path[300];

static void
pause_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static void
new_log(void)
{
	int fd;

	snprintf(log_path, sizeof log_path, "%s/polo-display-switcher-XXXXXX",
		 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	fd = mkstemp(log_path);
	CHECK(fd >= 0, "mkstemp: %s", strerror(errno));
	close(fd);
	setenv("FAKE_SHELL_UI_LOG", log_path, 1);
}

static void
clear_log(void)
{
	FILE *log = fopen(log_path, "w");

	CHECK(log, "cannot truncate %s", log_path);
	fclose(log);
}

static bool
logged(const char *line)
{
	FILE *log = fopen(log_path, "r");
	char buf[512];
	bool found = false;

	CHECK(log, "cannot read %s", log_path);
	while (!found && fgets(buf, sizeof buf, log)) {
		buf[strcspn(buf, "\n")] = '\0';
		found = strcmp(buf, line) == 0;
	}
	fclose(log);
	return found;
}

static void
wait_logged(struct harness *h, const char *line)
{
	for (int i = 0; i < 300 && !logged(line); i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	CHECK(logged(line), "fake-shell-ui never got \"%s\"", line);
}

static void
settle(struct harness *h)
{
	for (int i = 0; i < 10; i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
}

static void
wait_helper(struct harness *h)
{
	for (int i = 0; i < 300 && h->shell_ui_binds == 0; i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	CHECK(h->shell_ui_binds == 1, "fake-shell-ui did not bind");
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_roundtrip(h);
}

static void
dump(struct harness *h)
{
	harness_output_state(h, "");
	for (int i = 0; i < h->n_output_states; i++) {
		const struct harness_output_state *o = &h->output_states[i];

		fprintf(stderr,
			"  %s enabled %d %d,%d %dx%d scale %d primary %d %u "
			"mHz\n",
			o->name, o->enabled, o->x, o->y, o->width, o->height,
			o->scale, o->primary, o->refresh);
	}
}

static void
expect(struct harness *h, const char *name, int x, int y, int width, int height,
       bool primary)
{
	const struct harness_output_state *o = harness_output_state(h, name);

	if (!o || !o->enabled || o->x != x || o->y != y || o->width != width ||
	    o->height != height || o->primary != primary)
		dump(h);
	CHECK(o && o->enabled, "%s is not on", name);
	CHECK(o->x == x && o->y == y && o->width == width &&
		      o->height == height,
	      "%s is at %d,%d %dx%d, not %d,%d %dx%d", name, o->x, o->y,
	      o->width, o->height, x, y, width, height);
	CHECK(o->primary == primary, "%s is %sprimary", name,
	      o->primary ? "" : "not ");
}

static void
expect_off(struct harness *h, const char *name)
{
	const struct harness_output_state *o = harness_output_state(h, name);

	if (o && (o->enabled || o->primary))
		dump(h);
	CHECK(!o || !o->enabled, "%s is still on", name);
	CHECK(!o || !o->primary, "%s is off but primary", name);
}

static void
expect_extended(struct harness *h)
{
	expect(h, "eDP-1", 0, 0, 1920, 1080, true);
	expect(h, "headless0", 1920, 0, HARNESS_OUTPUT_WIDTH,
	       HARNESS_OUTPUT_HEIGHT, false);
}

static void
expect_mirrored(struct harness *h)
{
	const struct harness_output_state *o;

	expect(h, "eDP-1", 0, 0, 1024, 768, true);
	expect(h, "headless0", 0, 0, 1024, 768, true);
	o = harness_output_state(h, "eDP-1");
	CHECK(o->refresh == 60000, "the laptop runs at %u mHz", o->refresh);
}

static void
super_p(struct harness *h)
{
	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_P);
}

/* Both screens' pixels, which must be the same; the caller frees them. The
 * screens repaint on their own timers, so give them time to catch up. */
static uint32_t *
same_picture(struct harness *h)
{
	size_t differ = 0;

	for (int attempt = 0; attempt < 100; attempt++) {
		int w0, h0, w1, h1;
		uint32_t *a = harness_capture_output(h, 0, &w0, &h0);
		uint32_t *b = harness_capture_output(h, 1, &w1, &h1);

		CHECK(a && b, "cannot capture both screens");
		CHECK(w0 == w1 && h0 == h1, "the screens are %dx%d and %dx%d",
		      w0, h0, w1, h1);
		differ = 0;
		for (int i = 0; i < w0 * h0; i++)
			differ += a[i] != b[i];
		free(b);
		if (differ == 0)
			return a;
		free(a);
		harness_roundtrip(h);
		pause_ms(20);
	}
	CHECK(false, "the mirrored screens differ in %zu pixels", differ);
	return NULL;
}

/* Expo draws once for the one logical monitor, on both screens. */
static void
expo_mirrored(struct harness *h)
{
	uint32_t *desktop, *expo;
	size_t changed = 0;

	harness_wait_repaint(h);
	desktop = same_picture(h);
	harness_key(h, KEY_LEFTMETA, true);
	tap(h, KEY_E);
	harness_key(h, KEY_LEFTMETA, false);
	for (int i = 0; i < 50; i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	expo = same_picture(h);
	for (int i = 0; i < 1024 * 768; i++)
		changed += desktop[i] != expo[i];
	CHECK(changed > 1024 * 768 / 10,
	      "expo did not show (%zu pixels changed)", changed);
	tap(h, KEY_ESC);
	for (int i = 0; i < 50; i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	free(desktop);
	free(expo);
}

static void
modes_cycle(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w;

	new_log();
	h = harness_start(argc, argv);
	wait_helper(h);

	/* One screen: nothing to switch. */
	super_p(h);
	harness_key(h, KEY_LEFTMETA, false);
	settle(h);
	CHECK(!logged("hide_switcher") &&
		      !logged("switcher_entry 1 Primary only (null) 0"),
	      "the popup showed with one screen");

	harness_add_output(h, "eDP-1", "BOE", 0x0a1b, 0, "Laptop", true, 344,
			   194, laptop, 2);
	expect(h, "eDP-1", HARNESS_OUTPUT_WIDTH, 0, 1920, 1080, false);
	w = harness_window_create(h, 300, 200, "picture");

	/* Extend now: the popup opens on secondary only; releasing Super
	 * applies. */
	clear_log();
	super_p(h);
	wait_logged(h, "show_switcher output 4 4 0 96");
	CHECK(logged("switcher_entry 1 Primary only (null) 0") &&
		      logged("switcher_entry 2 Mirror (null) 0") &&
		      logged("switcher_entry 3 Extend (null) 0") &&
		      logged("switcher_entry 4 Secondary only (null) 0"),
	      "not the four modes");
	CHECK(logged("switcher_icon_name 1 "
		     "polo-display-primary-only-symbolic") &&
		      logged("switcher_icon_name 2 "
			     "polo-display-mirror-symbolic") &&
		      logged("switcher_icon_name 3 "
			     "polo-display-extend-symbolic") &&
		      logged("switcher_icon_name 4 "
			     "polo-display-secondary-only-symbolic"),
	      "not the display icons");
	expect(h, "eDP-1", HARNESS_OUTPUT_WIDTH, 0, 1920, 1080, false);
	harness_key(h, KEY_LEFTMETA, false);
	wait_logged(h, "hide_switcher");
	expect_off(h, "eDP-1");
	expect(h, "headless0", 0, 0, HARNESS_OUTPUT_WIDTH,
	       HARNESS_OUTPUT_HEIGHT, true);

	/* Then primary only, the laptop's panel. */
	clear_log();
	super_p(h);
	wait_logged(h, "show_switcher output 1 4 0 96");
	harness_key(h, KEY_LEFTMETA, false);
	wait_logged(h, "hide_switcher");
	expect(h, "eDP-1", 0, 0, 1920, 1080, true);
	expect_off(h, "headless0");

	/* Mirror is next; P again moves on and back round to it. The laptop
	 * drops to the common mode. */
	clear_log();
	super_p(h);
	wait_logged(h, "show_switcher output 2 4 0 96");
	tap(h, KEY_P);
	wait_logged(h, "update_selected 3");
	tap(h, KEY_P);
	tap(h, KEY_P);
	tap(h, KEY_P);
	wait_logged(h, "update_selected 2");
	CHECK(logged("update_selected 4") && logged("update_selected 1"),
	      "P did not step through every mode");
	harness_key(h, KEY_LEFTMETA, false);
	wait_logged(h, "hide_switcher");
	expect_mirrored(h);
	expo_mirrored(h);

	/* Escape leaves the screens as they are, and so does releasing Super.
	 */
	clear_log();
	super_p(h);
	wait_logged(h, "show_switcher output 3 4 0 96");
	tap(h, KEY_ESC);
	wait_logged(h, "hide_switcher");
	harness_key(h, KEY_LEFTMETA, false);
	settle(h);
	expect_mirrored(h);

	/* The display key: no modifier held, so it stays open for Enter. */
	clear_log();
	tap(h, KEY_SWITCHVIDEOMODE);
	wait_logged(h, "show_switcher output 3 4 0 96");
	tap(h, KEY_SWITCHVIDEOMODE);
	wait_logged(h, "update_selected 4");
	tap(h, KEY_LEFT);
	wait_logged(h, "update_selected 3");
	settle(h);
	CHECK(!logged("hide_switcher"), "the popup closed by itself");
	tap(h, KEY_ENTER);
	wait_logged(h, "hide_switcher");
	expect_extended(h);

	/* A press outside the popup cancels. */
	clear_log();
	tap(h, KEY_SWITCHVIDEOMODE);
	wait_logged(h, "show_switcher output 4 4 0 96");
	harness_click(h, 500, 500);
	wait_logged(h, "hide_switcher");
	expect_extended(h);

	/* Secondary only, then the lit screen goes: the laptop lights up. */
	clear_log();
	super_p(h);
	harness_key(h, KEY_LEFTMETA, false);
	wait_logged(h, "hide_switcher");
	expect_off(h, "eDP-1");
	harness_connect_output(h, "headless0", false);
	expect(h, "eDP-1", 0, 0, 1920, 1080, true);
	harness_connect_output(h, "headless0", true);

	/* No common mode with a third screen: mirror is greyed out and skipped.
	 */
	harness_add_output(h, "DP-1", "GSM", 0x5b7f, 99, "27GL850", false, 597,
			   336, wide, 1);
	clear_log();
	tap(h, KEY_SWITCHVIDEOMODE);
	wait_logged(h, "switcher_entry 2 Mirror (null) 2");
	for (int i = 0; i < 4; i++)
		tap(h, KEY_RIGHT);
	settle(h);
	CHECK(!logged("update_selected 2") &&
		      !logged("show_switcher output 2 4 0 96"),
	      "mirror was selected without a common mode");
	tap(h, KEY_ESC);
	wait_logged(h, "hide_switcher");

	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not exit cleanly");
	unlink(log_path);
}

static void
popup_selects(int argc, char **argv)
{
	struct harness *h;

	new_log();
	setenv("FAKE_SHELL_UI_SELECT", "1", 1);
	h = harness_start(argc, argv);
	wait_helper(h);
	harness_add_output(h, "eDP-1", "BOE", 0x0a1b, 0, "Laptop", true, 344,
			   194, laptop, 2);
	tap(h, KEY_SWITCHVIDEOMODE);
	wait_logged(h, "hide_switcher");
	expect(h, "eDP-1", 0, 0, 1920, 1080, true);
	expect_off(h, "headless0");
	CHECK(harness_stop(h) == 0, "polo did not exit cleanly");
	unsetenv("FAKE_SHELL_UI_SELECT");
	unlink(log_path);
}

int
main(int argc, char **argv)
{
	CHECK(argc >= 3, "usage: %s POLO FAKE-SHELL-UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	setenv("POLO_TEST_MANAGED_OUTPUTS", "1", 1);
	modes_cycle(argc, argv);
	popup_selects(argc, argv);
	/* Mode switches remake the renderer's output state; GL too. */
	setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	modes_cycle(argc, argv);
	return 0;
}
