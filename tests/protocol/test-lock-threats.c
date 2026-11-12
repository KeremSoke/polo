/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ways around the lock that must not work: a window holding a keyboard
 * shortcuts inhibitor, a VT switch away and back, a monitor plugged in while
 * locked (curtained before any locker draws on it), and an X override-redirect
 * window mapped while locked (hidden, no input). A crashed or killed locker is
 * covered by test-session-lock.
 *
 * argv[2], optional: x-test-client
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"
#include "keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h"
#include "locker.h"

#define WINDOW_RGB 0x3070c0u
#define LOCK_COLOUR 0xff00a000u
#define WHITE 0xffffffu

static const int dell[][3] = { { 2560, 1440, 59951 }, { 1920, 1080, 60000 } };
static struct zwp_keyboard_shortcuts_inhibit_manager_v1 *inhibit_manager;
static char log_path[64];

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface,
		   zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name) ==
	    0)
		inhibit_manager = wl_registry_bind(
			registry, name,
			&zwp_keyboard_shortcuts_inhibit_manager_v1_interface,
			1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
pause_ms(struct harness *h, int msec)
{
	struct timespec tick = { 0, 10 * 1000000L };

	for (int i = 0; i < msec / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&tick, NULL);
	}
}

static bool
log_has(const char *line)
{
	char text[65536];
	FILE *file = fopen(log_path, "r");
	size_t n;

	if (!file)
		return false;
	n = fread(text, 1, sizeof text - 1, file);
	fclose(file);
	text[n] = '\0';
	return strstr(text, line) != NULL;
}

static int
x_display(struct harness *h)
{
	for (int display = 0; display < 64; display++) {
		char path[64];
		int pid = 0;
		FILE *file;

		snprintf(path, sizeof path, "/tmp/.X%d-lock", display);
		file = fopen(path, "r");
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
find_xauth(struct harness *h, char *path, size_t size)
{
	char command[512];
	FILE *pipe;

	snprintf(command, sizeof command, "ls %s/polo-xauth-* 2>/dev/null",
		 h->runtime_dir);
	pipe = popen(command, "r");
	CHECK(pipe && fgets(path, size, pipe), "no polo-xauth file in %s",
	      h->runtime_dir);
	pclose(pipe);
	path[strcspn(path, "\n")] = '\0';
}

static void
count(struct harness *h, int index, uint32_t rgb, int *matching, int *lit)
{
	int width, height;
	uint32_t *pixels = harness_capture_output(h, index, &width, &height);

	*matching = *lit = 0;
	CHECK(pixels, "cannot capture output %d", index);
	for (int i = 0; i < width * height; i++) {
		if ((pixels[i] & 0xffffff) == rgb)
			(*matching)++;
		if ((pixels[i] & 0xffffff) != 0)
			(*lit)++;
	}
	free(pixels);
}

static void
cover(struct locker *l, struct wl_surface **surface,
      struct ext_session_lock_surface_v1 **lock_surface)
{
	*surface = wl_compositor_create_surface(l->compositor);
	*lock_surface = ext_session_lock_v1_get_lock_surface(l->lock, *surface,
							     l->output);
	ext_session_lock_surface_v1_add_listener(*lock_surface,
						 &surface_listener, l);
	wl_display_roundtrip(l->display);
	CHECK(l->configured, "the lock surface was not configured");
	ext_session_lock_surface_v1_ack_configure(*lock_surface, l->serial);
	struct wl_buffer *buffer = harness_buffer_create_colour(
		l->shm, l->width, l->height, LOCK_COLOUR);

	own(l, buffer);
	wl_surface_attach(*surface, buffer, 0, 0);
	wl_surface_commit(*surface);
	wl_display_roundtrip(l->display);
}

static pid_t
start_override_client(struct harness *h, const char *path)
{
	char xauth[512];
	int display = x_display(h);
	pid_t client;

	CHECK(display >= 0, "Polo did not take an X display");
	find_xauth(h, xauth, sizeof xauth);
	snprintf(log_path, sizeof log_path, "/tmp/polo-lock-x-XXXXXX");
	close(mkstemp(log_path));
	client = fork();
	if (client == 0) {
		char name[16];

		snprintf(name, sizeof name, ":%d", display);
		setenv("DISPLAY", name, 1);
		setenv("XAUTHORITY", xauth, 1);
		setenv("X_TEST_OVERRIDE", "1", 1);
		setenv("X_TEST_NO_HINTS", "1", 1);
		execl(path, path, log_path, NULL);
		_exit(127);
	}
	for (int i = 0; i < 500 && !log_has("mapped"); i++)
		pause_ms(h, 20);
	CHECK(log_has("mapped"), "the X client did not map its window");
	return client;
}

int
main(int argc, char **argv)
{
	bool with_x =
		argc >= 3 && system("command -v Xwayland >/dev/null") == 0;
	struct harness *h;
	struct harness_window *w;
	struct wl_surface *surface;
	struct ext_session_lock_surface_v1 *lock_surface;
	struct zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor;
	pid_t client = 0;
	int seen, lit;

	setenv("POLO_TEST_MANAGED_OUTPUTS", "1", 1);
	if (with_x)
		setenv("POLO_XWAYLAND", "lazy", 1);
	h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(inhibit_manager, "no zwp_keyboard_shortcuts_inhibit_manager_v1");
	harness_move_pointer(h, 1020, 764);
	w = harness_window_create(h, 300, 200, "secret");
	pause_ms(h, 100);
	count(h, 0, WINDOW_RGB, &seen, &lit);
	CHECK(seen > 1000, "the window is not on screen before locking (%d)",
	      seen);

	inhibitor = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
		inhibit_manager, w->surface, h->seat);
	harness_roundtrip(h);

	struct locker *l = locker_new(h);

	locker_lock(l);
	CHECK(wait_locked(h, l), "no locked event");
	cover(l, &surface, &lock_surface);
	pump(h, l);
	CHECK(!key_reaches(h, w->surface),
	      "a key reached the window holding a shortcuts inhibitor");
	count(h, 0, WINDOW_RGB, &seen, &lit);
	CHECK(seen == 0, "the window shows while locked (%d pixels)", seen);

	harness_set_session_active(h, false);
	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	harness_set_session_active(h, true);
	pause_ms(h, 200);
	CHECK(strcmp(state(h), "locked") == 0, "a VT switch unlocked (%s)",
	      state(h));
	CHECK(!key_reaches(h, w->surface),
	      "a key reached the window after a VT switch");
	count(h, 0, WINDOW_RGB, &seen, &lit);
	CHECK(seen == 0, "the window shows after a VT switch (%d pixels)",
	      seen);

	harness_add_output(h, "HDMI-A-1", "DEL", 0x4321, 12345, "U2715H", false,
			   597, 336, dell, 2);
	pause_ms(h, 300);
	CHECK(h->n_outputs >= 2, "the new monitor did not come up");
	count(h, h->n_outputs - 1, WINDOW_RGB, &seen, &lit);
	CHECK(lit == 0,
	      "the new monitor shows %d pixels that are not the curtain", lit);
	CHECK(strcmp(state(h), "locked") == 0, "a hotplug unlocked (%s)",
	      state(h));

	if (with_x) {
		client = start_override_client(h, argv[2]);
		pause_ms(h, 500);
		count(h, 0, WHITE, &seen, &lit);
		CHECK(seen <= 400,
		      "the X override-redirect window shows while locked (%d)",
		      seen);
		polo_test_v1_move_pointer(h->test, 200, 150);
		polo_test_v1_button(h->test, BTN_LEFT, 1);
		polo_test_v1_button(h->test, BTN_LEFT, 0);
		harness_key(h, KEY_A, true);
		harness_key(h, KEY_A, false);
		pause_ms(h, 300);
		CHECK(!log_has("key ") && !log_has("button "),
		      "the X window got input while locked");
		polo_test_v1_move_pointer(h->test, 1020, 764);
		harness_roundtrip(h);
	}

	ext_session_lock_surface_v1_destroy(lock_surface);
	wl_surface_destroy(surface);
	unlock(h, l);
	pause_ms(h, 200);
	count(h, 0, WINDOW_RGB, &seen, &lit);
	CHECK(seen > 0, "the window did not come back after unlocking");
	count(h, h->n_outputs - 1, WINDOW_RGB, &seen, &lit);
	CHECK(lit > 1000,
	      "the new monitor is black even unlocked (%d): the curtain check "
	      "above proves nothing",
	      lit);
	if (with_x) {
		count(h, 0, WHITE, &seen, &lit);
		for (int i = 0; i < 100 && seen <= 1000; i++) {
			pause_ms(h, 100);
			count(h, 0, WHITE, &seen, &lit);
		}
		CHECK(seen > 1000,
		      "the X window did not show after unlocking (%d): the "
		      "locked check above proves nothing",
		      seen);
		kill(client, SIGTERM);
		waitpid(client, NULL, 0);
		unlink(log_path);
	}

	zwp_keyboard_shortcuts_inhibitor_v1_destroy(inhibitor);
	zwp_keyboard_shortcuts_inhibit_manager_v1_destroy(inhibit_manager);
	wl_registry_destroy(registry);
	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
