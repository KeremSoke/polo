/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An X11 pager sets a 2x2 layout. Down must switch at once with the usual
 * workspace popup, even though the cube is enabled for horizontal rows.
 */
#include <glob.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include "harness.h"

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
numbered(struct harness *h, uint32_t key)
{
	harness_key(h, KEY_LEFTCTRL, true);
	tap(h, key);
	harness_key(h, KEY_LEFTCTRL, false);
}

static int
x_display(struct harness *h)
{
	for (int display = 0; display < 64; display++) {
		char path[64];
		int pid = 0;
		snprintf(path, sizeof path, "/tmp/.X%d-lock", display);
		FILE *file = fopen(path, "r");
		if (!file)
			continue;
		if (fscanf(file, "%d", &pid) != 1)
			pid = 0;
		fclose(file);
		if (pid == h->pid)
			return display;
	}
	return -1;
}

static void
set_layout(struct harness *h)
{
	char pattern[512], name[16];
	glob_t paths = { 0 };
	int display = -1;
	for (int i = 0; i < 100 && display < 0; i++) {
		display = x_display(h);
		if (display < 0)
			pause_ms(h, 20);
	}
	CHECK(display >= 0, "Xwayland did not claim a display");
	snprintf(pattern, sizeof pattern, "%s/polo-xauth-*", h->runtime_dir);
	CHECK(glob(pattern, 0, NULL, &paths) == 0 && paths.gl_pathc > 0,
	      "Xwayland auth file is missing");
	setenv("XAUTHORITY", paths.gl_pathv[0], 1);
	snprintf(name, sizeof name, ":%d", display);
	Display *x11 = XOpenDisplay(name);
	CHECK(x11, "cannot connect to Xwayland %s", name);
	Atom layout = XInternAtom(x11, "_NET_DESKTOP_LAYOUT", False);
	unsigned long values[4] = { 0, 2, 2, 0 };
	XChangeProperty(x11, DefaultRootWindow(x11), layout, XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)values, 4);
	XSync(x11, False);
	pause_ms(h, 100);
	XCloseDisplay(x11);
	globfree(&paths);
}

static void
paint(struct harness_window *window, uint32_t colour)
{
	struct wl_buffer *old = window->buffer;
	window->buffer = harness_buffer_create_colour(
		window->harness->shm, 220, 150, 0xff000000u | colour);
	wl_surface_attach(window->surface, window->buffer, 0, 0);
	wl_surface_damage_buffer(window->surface, 0, 0, 220, 150);
	harness_wait_frame(window->harness, window->surface);
	if (old)
		wl_buffer_destroy(old);
}

static bool
popup_logged(const char *path)
{
	FILE *log = fopen(path, "r");
	char line[512];
	bool found = false;
	CHECK(log, "cannot read fake shell UI log");
	while (fgets(line, sizeof line, log))
		found |=
			strstr(line, "show_workspace_switcher output 3 2 3 ") ==
			line;
	fclose(log);
	return found;
}

int
main(int argc, char **argv)
{
	CHECK(argc >= 3, "usage: %s POLO FAKE-SHELL-UI", argv[0]);
	char log_path[] = "/tmp/polo-cube-layout-XXXXXX";
	int fd = mkstemp(log_path);
	CHECK(fd >= 0, "cannot create fake shell UI log");
	close(fd);
	setenv("FAKE_SHELL_UI_LOG", log_path, 1);
	setenv("POLO_SHELL_UI", argv[2], 1);
	setenv("POLO_XWAYLAND", "eager", 1);
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	setenv("POLO_TEST_PREFS",
	       "global-keybindings/switch-to-workspace-1='<Control>F1';"
	       "global-keybindings/switch-to-workspace-3='<Control>F3'",
	       1);
	struct harness *h = harness_start(argc, argv);
	for (int i = 0; i < 100 && h->shell_ui_binds == 0; i++)
		pause_ms(h, 20);
	CHECK(h->shell_ui_binds == 1, "fake shell UI did not bind");
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	harness_set_setting(h, "org.mate.polo.effects", "cube", "true");
	harness_set_setting(h, "org.mate.polo.effects", "cube-background-color",
			    "'#204060'");
	harness_move_pointer(h, 5, 5);
	struct harness_window *first =
		harness_window_create(h, 220, 150, "cube-grid-first");
	paint(first, 0xc03030);
	numbered(h, KEY_F3);
	struct harness_window *third =
		harness_window_create(h, 220, 150, "cube-grid-third");
	paint(third, 0x3030c0);
	numbered(h, KEY_F1);
	set_layout(h);
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	tap(h, KEY_DOWN);
	CHECK(!harness_window_info(first)->focused &&
		      harness_window_info(third)->focused,
	      "Down did not select the workspace below in the model");
	int width, height, blue = 0, backdrop = 0;
	uint32_t *pixels = harness_capture(h, &width, &height);
	CHECK(pixels, "cannot capture 2x2 workspace switch");
	for (int i = 0; i < width * height; i++) {
		blue += (pixels[i] & 0xffffff) == 0x3030c0;
		backdrop += (pixels[i] & 0xffffff) == 0x204060;
	}
	free(pixels);
	CHECK(blue == 220 * 150 && backdrop == 0,
	      "Down drew a cube frame (%d blue, %d backdrop)", blue, backdrop);
	for (int i = 0; i < 100 && !popup_logged(log_path); i++)
		pause_ms(h, 20);
	CHECK(popup_logged(log_path), "Down did not show workspace popup");
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	h->n_windows = 0;
	harness_window_destroy(first);
	harness_window_destroy(third);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	unlink(log_path);
	return 0;
}
