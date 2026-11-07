/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Monitors that come and go, on fake heads with an EDID and a mode list:
 * a hot-plugged monitor extends the desktop at its preferred mode right of
 * the others, unplugging leaves the rest where they were, the same monitor
 * comes back where it was, another monitor on the same connector is a new
 * one, and a layout saved in monitors.xml comes back after a restart.
 * argv[2]: fake-shell-ui, for the display-mode popup that saves one.
 */
#include <errno.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "harness.h"

static const int laptop[][3] = { { 1920, 1080, 60000 }, { 1280, 720, 60000 } };
static const int dell[][3] = { { 2560, 1440, 59951 },
			       { 1920, 1080, 60000 },
			       { 1280, 1024, 75025 } };
static const int benq[][3] = { { 1920, 1200, 59950 }, { 1024, 768, 60004 } };

static void
wait_helper(struct harness *h)
{
	struct timespec ten_ms = { 0, 10000000L };

	for (int i = 0; i < 300 && h->shell_ui_binds == 0; i++) {
		harness_roundtrip(h);
		nanosleep(&ten_ms, NULL);
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
       uint32_t refresh)
{
	const struct harness_output_state *o = harness_output_state(h, name);

	if (!o || !o->enabled || o->x != x || o->y != y || o->width != width ||
	    o->height != height || (refresh && o->refresh != refresh))
		dump(h);
	CHECK(o && o->enabled, "%s is not on", name);
	CHECK(o->x == x && o->y == y && o->width == width &&
		      o->height == height,
	      "%s is at %d,%d %dx%d, not %d,%d %dx%d", name, o->x, o->y,
	      o->width, o->height, x, y, width, height);
	CHECK(!refresh || o->refresh == refresh, "%s runs at %u mHz, not %u",
	      name, o->refresh, refresh);
}

static void
expect_off(struct harness *h, const char *name)
{
	const struct harness_output_state *o = harness_output_state(h, name);

	if (o && o->enabled)
		dump(h);
	CHECK(!o || !o->enabled, "%s is still on", name);
}

static const char *
primary(struct harness *h)
{
	static char name[32];
	int primaries = 0;

	harness_output_state(h, "");
	name[0] = '\0';
	for (int i = 0; i < h->n_output_states; i++) {
		if (h->output_states[i].enabled &&
		    h->output_states[i].primary) {
			primaries++;
			snprintf(name, sizeof name, "%s",
				 h->output_states[i].name);
		}
	}
	CHECK(primaries == 1, "%d primary outputs", primaries);
	return name;
}

static void
plug_and_unplug(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	char first[32];

	CHECK(harness_output_count(h) == 1, "not one output at start");
	snprintf(first, sizeof first, "%s", h->output_states[0].name);
	expect(h, first, 0, 0, HARNESS_OUTPUT_WIDTH, HARNESS_OUTPUT_HEIGHT, 0);

	harness_add_output(h, "eDP-1", "BOE", 0x0a1b, 0, "Laptop", true, 344,
			   194, laptop, 2);
	expect(h, first, 0, 0, HARNESS_OUTPUT_WIDTH, HARNESS_OUTPUT_HEIGHT, 0);
	expect(h, "eDP-1", HARNESS_OUTPUT_WIDTH, 0, 1920, 1080, 60000);

	harness_add_output(h, "HDMI-A-1", "DEL", 0x4321, 12345, "U2715H", false,
			   597, 336, dell, 3);
	expect(h, "HDMI-A-1", HARNESS_OUTPUT_WIDTH + 1920, 0, 2560, 1440,
	       59951);
	CHECK(strcmp(primary(h), first) == 0, "hotplug moved the primary");

	harness_connect_output(h, "HDMI-A-1", false);
	expect_off(h, "HDMI-A-1");
	expect(h, first, 0, 0, HARNESS_OUTPUT_WIDTH, HARNESS_OUTPUT_HEIGHT, 0);
	expect(h, "eDP-1", HARNESS_OUTPUT_WIDTH, 0, 1920, 1080, 60000);

	harness_connect_output(h, "HDMI-A-1", true);
	expect(h, "HDMI-A-1", HARNESS_OUTPUT_WIDTH + 1920, 0, 2560, 1440,
	       59951);

	/* The laptop's lid: the external monitor moves over the gap, and the
	 * panel comes back right of it. */
	harness_connect_output(h, "eDP-1", false);
	expect_off(h, "eDP-1");
	expect(h, first, 0, 0, HARNESS_OUTPUT_WIDTH, HARNESS_OUTPUT_HEIGHT, 0);
	expect(h, "HDMI-A-1", HARNESS_OUTPUT_WIDTH, 0, 2560, 1440, 59951);
	harness_connect_output(h, "eDP-1", true);
	expect(h, "HDMI-A-1", HARNESS_OUTPUT_WIDTH, 0, 2560, 1440, 59951);
	expect(h, "eDP-1", HARNESS_OUTPUT_WIDTH + 2560, 0, 1920, 1080, 60000);

	/* Another monitor on the same connector is new: its own preferred mode.
	 */
	harness_remove_output(h, "HDMI-A-1");
	CHECK(!harness_output_state(h, "HDMI-A-1"), "the connector stayed");
	expect(h, "eDP-1", HARNESS_OUTPUT_WIDTH, 0, 1920, 1080, 60000);
	harness_add_output(h, "HDMI-A-1", "BNQ", 0x7f10, 777, "GW2470", false,
			   521, 293, benq, 2);
	expect(h, "HDMI-A-1", HARNESS_OUTPUT_WIDTH + 1920, 0, 1920, 1200,
	       59950);
	CHECK(harness_output_count(h) == 3, "not three outputs");

	/* A monitor without an EDID still comes up. */
	harness_add_output(h, "DP-2", NULL, 0, 0, NULL, false, 0, 0, benq + 1,
			   1);
	expect(h, "DP-2", HARNESS_OUTPUT_WIDTH + 1920 + 1920, 0, 1024, 768,
	       60004);
	CHECK(strcmp(primary(h), first) == 0, "the primary moved");

	CHECK(harness_stop(h) == 0, "polo did not exit cleanly");
}

static void
make_config_home(char *dir, size_t size)
{
	snprintf(dir, size, "%s/polo-hotplug-XXXXXX",
		 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	CHECK(mkdtemp(dir), "mkdtemp: %s", strerror(errno));
	setenv("POLO_TEST_CONFIG_HOME", dir, 1);
}

static void
remove_config_home(const char *dir)
{
	char command[400];

	snprintf(command, sizeof command, "rm -rf '%s'", dir);
	CHECK(system(command) == 0, "cannot remove %s", dir);
	unsetenv("POLO_TEST_CONFIG_HOME");
}

static void
saved_layout_returns(int argc, char **argv)
{
	char dir[300], path[400];
	struct harness *h;

	make_config_home(dir, sizeof dir);
	h = harness_start(argc, argv);
	harness_add_output(h, "eDP-1", "BOE", 0x0a1b, 0, "Laptop", true, 344,
			   194, laptop, 2);
	harness_add_output(h, "HDMI-A-1", "DEL", 0x4321, 12345, "U2715H", false,
			   597, 336, dell, 3);
	/* Only monitors with an EDID are remembered. */
	harness_connect_output(h, "headless0", false);
	expect(h, "eDP-1", 0, 0, 1920, 1080, 60000);
	expect(h, "HDMI-A-1", 1920, 0, 2560, 1440, 59951);
	/* No common mode for mirroring: secondary only, then primary only. */
	wait_helper(h);
	tap(h, KEY_SWITCHVIDEOMODE);
	tap(h, KEY_SWITCHVIDEOMODE);
	tap(h, KEY_ENTER);
	expect(h, "eDP-1", 0, 0, 1920, 1080, 60000);
	expect_off(h, "HDMI-A-1");
	snprintf(path, sizeof path, "%s/mate/polo/monitors.xml", dir);
	CHECK(access(path, R_OK) == 0, "the layout was not saved in %s", path);
	CHECK(harness_stop(h) == 0, "polo did not exit cleanly");

	h = harness_start(argc, argv);
	harness_add_output(h, "eDP-1", "BOE", 0x0a1b, 0, "Laptop", true, 344,
			   194, laptop, 2);
	harness_connect_output(h, "headless0", false);
	harness_add_output(h, "HDMI-A-1", "DEL", 0x4321, 12345, "U2715H", false,
			   597, 336, dell, 3);
	expect(h, "eDP-1", 0, 0, 1920, 1080, 60000);
	expect_off(h, "HDMI-A-1");

	harness_set_setting(h, "org.mate.polo.effects", "open-animation",
			    "'zoom'");
	struct harness_window *window =
		harness_window_create(h, 200, 150, "animated");

	for (int i = 0; i < 30; i++) {
		harness_roundtrip(h);
		nanosleep(&(struct timespec){ 0, 10000000L }, NULL);
	}
	harness_window_destroy(window);

	/* Turning it on afterwards animates there too. */
	tap(h, KEY_SWITCHVIDEOMODE);
	tap(h, KEY_ENTER);
	expect(h, "HDMI-A-1", 0, 0, 1920, 1080, 60000);
	window = harness_window_create(h, 200, 150, "animated again");
	for (int i = 0; i < 30; i++) {
		harness_roundtrip(h);
		nanosleep(&(struct timespec){ 0, 10000000L }, NULL);
	}
	harness_window_destroy(window);
	CHECK(harness_stop(h) == 0, "polo did not exit cleanly");
	remove_config_home(dir);
}

int
main(int argc, char **argv)
{
	CHECK(argc >= 3, "usage: %s POLO FAKE-SHELL-UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	setenv("POLO_TEST_MANAGED_OUTPUTS", "1", 1);
	plug_and_unplug(argc, argv);
	saved_layout_returns(argc, argv);
	return 0;
}
