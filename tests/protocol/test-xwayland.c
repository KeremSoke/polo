/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An X client through the vendored XWM and all of Polo: its white window is on
 * screen, keys and buttons reach it, and it leaves the screen when it goes.
 * argv[2]: tests/xwm/x-test-client.c. Skips without Xwayland.
 */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

#include "harness.h"
#include "primary-selection-unstable-v1-client-protocol.h"
#include "weston-output-capture-client-protocol.h"

static struct weston_capture_v1 *capture;
static int cap_width, cap_height, cap_done;
static char log_path[64] = "/tmp/polo-xwayland-XXXXXX";

static bool saw_x_keyboard_grab;

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface, "zwp_xwayland_keyboard_grab_manager_v1") == 0)
		saw_x_keyboard_grab = true;
	if (strcmp(interface, weston_capture_v1_interface.name) == 0)
		capture = wl_registry_bind(registry, name,
					   &weston_capture_v1_interface, 1);
}

static void
registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_remove,
};

static void
source_format(void *data, struct weston_capture_source_v1 *s, uint32_t format)
{
}

static void
source_size(void *data, struct weston_capture_source_v1 *s, int32_t w,
	    int32_t h)
{
	cap_width = w;
	cap_height = h;
}

static void
source_complete(void *data, struct weston_capture_source_v1 *s)
{
	cap_done = 1;
}

static void
source_retry(void *data, struct weston_capture_source_v1 *s)
{
	cap_done = 2;
}

static void
source_failed(void *data, struct weston_capture_source_v1 *s, const char *msg)
{
	cap_done = 3;
}

static const struct weston_capture_source_v1_listener source_listener = {
	.format = source_format,
	.size = source_size,
	.complete = source_complete,
	.retry = source_retry,
	.failed = source_failed,
};

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec ts = { 0, 20 * 1000000L };

	for (int i = 0; i < ms / 20; i++) {
		harness_roundtrip(h);
		nanosleep(&ts, NULL);
	}
}

static uint32_t *
screenshot(struct harness *h)
{
	struct weston_capture_source_v1 *source = weston_capture_v1_create(
		capture, h->outputs[0], WESTON_CAPTURE_V1_SOURCE_FRAMEBUFFER);

	cap_done = 0;
	weston_capture_source_v1_add_listener(source, &source_listener, NULL);
	harness_roundtrip(h);
	CHECK(cap_width > 0, "no capture size");
	size_t size = (size_t)cap_width * cap_height * 4;
	int fd = memfd_create("capture", MFD_CLOEXEC);

	CHECK(fd >= 0 && ftruncate(fd, size) == 0, "capture buffer");
	uint32_t *pixels =
		mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	struct wl_shm_pool *pool = wl_shm_create_pool(h->shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(
		pool, 0, cap_width, cap_height, cap_width * 4,
		WL_SHM_FORMAT_XRGB8888);

	for (int attempt = 0; attempt < 20 && cap_done != 1; attempt++) {
		cap_done = 0;
		weston_capture_source_v1_capture(source, buffer);
		for (int i = 0; i < 200 && !cap_done; i++)
			pause_ms(h, 20);
	}
	CHECK(cap_done == 1, "capture ended with %d", cap_done);
	wl_buffer_destroy(buffer);
	wl_shm_pool_destroy(pool);
	weston_capture_source_v1_destroy(source);
	close(fd);
	return pixels;
}

static int
white_pixels(const uint32_t *pixels, int *x, int *y)
{
	int n = 0;

	for (int row = cap_height - 1; row >= 0; row--)
		for (int col = cap_width - 1; col >= 0; col--)
			if ((pixels[row * cap_width + col] & 0xffffff) ==
			    0xffffff) {
				n++;
				*x = col;
				*y = row;
			}
	return n;
}

static bool
log_has(const char *wanted)
{
	char line[4096];
	bool found = false;
	FILE *file = fopen(log_path, "r");

	if (!file)
		return false;
	while (!found && fgets(line, sizeof line, file)) {
		line[strcspn(line, "\n")] = '\0';
		found = strcmp(line, wanted) == 0;
	}
	fclose(file);
	return found;
}

static void
wait_for_line(struct harness *h, const char *line)
{
	for (int i = 0; i < 500 && !log_has(line); i++)
		pause_ms(h, 20);
	if (!log_has(line)) {
		char command[96];

		snprintf(command, sizeof command, "cat %s >&2", log_path);
		if (system(command) != 0)
			fprintf(stderr, "no client log\n");
	}
	CHECK(log_has(line), "the X client never reported \"%s\"", line);
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

static pid_t
start_x_client(int display, const char *path, bool with_auth, const char *xauth)
{
	pid_t client = fork();

	if (client == 0) {
		char name[16];

		snprintf(name, sizeof name, ":%d", display);
		setenv("DISPLAY", name, 1);
		setenv("HOME", "/nonexistent", 1);
		if (with_auth)
			setenv("XAUTHORITY", xauth, 1);
		else
			unsetenv("XAUTHORITY");
		execl(path, path, log_path, NULL);
		_exit(127);
	}
	return client;
}

/* Polo's Xwayland child, or 0. */
static pid_t
xwayland_pid(struct harness *h)
{
	char command[96];
	int pid = 0;

	snprintf(command, sizeof command, "pgrep -P %d -x Xwayland",
		 (int)h->pid);
	FILE *pipe = popen(command, "r");

	if (pipe) {
		if (fscanf(pipe, "%d", &pid) != 1)
			pid = 0;
		pclose(pipe);
	}
	return pid;
}

static void
wait_xwayland(struct harness *h, bool running)
{
	for (int i = 0; i < 400 && (xwayland_pid(h) != 0) != running; i++)
		pause_ms(h, 20);
	CHECK((xwayland_pid(h) != 0) == running, "Xwayland should%s be running",
	      running ? "" : " not");
}

static void
find_xauth(struct harness *h, char *path, size_t size)
{
	char command[512];

	snprintf(command, sizeof command, "ls %s/polo-xauth-* 2>/dev/null",
		 h->runtime_dir);
	FILE *pipe = popen(command, "r");

	CHECK(pipe && fgets(path, size, pipe), "no polo-xauth file in %s",
	      h->runtime_dir);
	pclose(pipe);
	path[strcspn(path, "\n")] = '\0';
	struct stat info;

	CHECK(stat(path, &info) == 0 && (info.st_mode & 0777) == 0600,
	      "%s must be 0600", path);
	CHECK(info.st_size > 18 + 16, "%s holds no cookie", path);
}

static void
start_log(void)
{
	strcpy(log_path, "/tmp/polo-xwayland-XXXXXX");
	int fd = mkstemp(log_path);

	CHECK(fd >= 0, "cannot create the client's log");
	close(fd);
}

static void
stop_client(pid_t client)
{
	kill(client, SIGTERM);
	waitpid(client, NULL, 0);
	unlink(log_path);
}

/* Lazy: nothing until the first client, a cookie is needed, and a later client
 * gets in after the first left. */
static void
test_lazy_and_auth(int argc, char **argv)
{
	char xauth[512];

	setenv("POLO_XWAYLAND", "lazy", 1);
	struct harness *h = harness_start(argc, argv);

	pause_ms(h, 200);
	int display = x_display(h);

	CHECK(display >= 0, "Polo did not take an X display");
	CHECK(xwayland_pid(h) == 0,
	      "lazy mode started Xwayland without a client");
	find_xauth(h, xauth, sizeof xauth);

	start_log();
	pid_t refused = start_x_client(display, argv[2], false, xauth);
	wait_xwayland(h, true);
	for (int i = 0; i < 200 && waitpid(refused, NULL, WNOHANG) == 0; i++)
		pause_ms(h, 20);
	CHECK(!log_has("focus") && !log_has("disconnected"),
	      "a client without the cookie reached the X server");
	kill(refused, SIGKILL);
	waitpid(refused, NULL, WNOHANG);
	unlink(log_path);

	start_log();
	pid_t client = start_x_client(display, argv[2], true, xauth);

	wait_for_line(h, "focus");
	stop_client(client);
	pause_ms(h, 200);

	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	wait_for_line(h, "focus");
	stop_client(client);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	CHECK(access(xauth, F_OK) != 0, "%s outlived Polo", xauth);
}

/* Eager: running with no client, staying after the last, back after a crash
 * while Wayland clients carry on; giving up after too many crashes. */
static void
test_eager_and_crash(int argc, char **argv)
{
	char xauth[512];

	setenv("POLO_XWAYLAND", "eager", 1);
	struct harness *h = harness_start(argc, argv);
	struct harness_window *native =
		harness_window_create(h, 200, 150, "native");

	wait_xwayland(h, true);
	pause_ms(h, 300);
	int display = x_display(h);

	find_xauth(h, xauth, sizeof xauth);
	start_log();
	pid_t client = start_x_client(display, argv[2], true, xauth);

	wait_for_line(h, "focus");
	stop_client(client);
	pause_ms(h, 500);
	CHECK(xwayland_pid(h) != 0, "eager Xwayland left with its last client");

	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	wait_for_line(h, "focus");

	pid_t first = xwayland_pid(h);

	kill(first, SIGKILL);
	wait_for_line(h, "disconnected");
	waitpid(client, NULL, 0);
	unlink(log_path);
	for (int i = 0;
	     i < 400 && (xwayland_pid(h) == 0 || xwayland_pid(h) == first); i++)
		pause_ms(h, 20);
	CHECK(xwayland_pid(h) != 0 && xwayland_pid(h) != first,
	      "Xwayland did not come back after a crash");
	harness_roundtrip(h);
	CHECK(harness_window_info(native)->known,
	      "the Wayland window went with X");

	pause_ms(h, 500);
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	wait_for_line(h, "focus");
	stop_client(client);

	for (int crash = 1; crash < 6; crash++) {
		pid_t now = 0;

		for (int i = 0; i < 400 && now == 0; i++) {
			now = xwayland_pid(h);
			if (!now)
				pause_ms(h, 20);
		}
		CHECK(now != 0, "Xwayland did not restart for crash %d", crash);
		kill(now, SIGKILL);
		for (int i = 0; i < 200 && xwayland_pid(h) == now; i++)
			pause_ms(h, 20);
	}
	pause_ms(h, 1000);
	CHECK(xwayland_pid(h) == 0,
	      "Xwayland kept restarting after six crashes");
	CHECK(kill(h->pid, 0) == 0, "Polo went with X");
	CHECK(harness_window_info(native)->known,
	      "the Wayland window went with X");
	harness_window_destroy(native);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

static bool
last_position(int *x, int *y)
{
	char line[128];
	bool found = false;
	FILE *file = fopen(log_path, "r");

	if (!file)
		return false;
	while (fgets(line, sizeof line, file))
		if (sscanf(line, "at %d %d", x, y) == 2)
			found = true;
	fclose(file);
	return found;
}

static void
press(struct harness *h, uint32_t modifier, uint32_t key)
{
	harness_key(h, modifier, true);
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_key(h, modifier, false);
}

/* Placement, stacking, focus and state come from Polo's policy. */
static void
test_policy(int argc, char **argv)
{
	char xauth[512];
	int x = 0, y = 0, at_x = -1, at_y = -1;

	setenv("POLO_XWAYLAND", "lazy", 1);
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	/* The cursor has white pixels too. */
	harness_move_pointer(h, 1020, 764);
	pause_ms(h, 200);
	int display = x_display(h);

	find_xauth(h, xauth, sizeof xauth);
	struct harness_window *native =
		harness_window_create(h, 200, 150, "native");

	pause_ms(h, 200);

	start_log();
	pid_t client = start_x_client(display, argv[2], true, xauth);

	wait_for_line(h, "focus");
	for (int i = 0; i < 100 && !last_position(&at_x, &at_y); i++)
		pause_ms(h, 20);
	pause_ms(h, 300);
	CHECK(last_position(&at_x, &at_y),
	      "the X client was never told its position");
	uint32_t *placed = screenshot(h);

	CHECK(white_pixels(placed, &x, &y) >= 300 * 200 * 9 / 10,
	      "the placed window is not on screen");
	CHECK(x == at_x && y == at_y,
	      "the X client was told %d,%d and is drawn at %d,%d", at_x, at_y,
	      x, y);
	CHECK(!(at_x == 10 && at_y == 20),
	      "the requested position won over placement");

	press(h, KEY_LEFTALT, KEY_F10);
	wait_for_line(h, "size 1024x768");
	wait_for_line(h,
		      "prop _NET_WM_STATE _NET_WM_STATE_MAXIMIZED_HORZ "
		      "_NET_WM_STATE_MAXIMIZED_VERT _NET_WM_STATE_FOCUSED");
	pause_ms(h, 300);
	int whites = white_pixels(screenshot(h), &x, &y);

	CHECK(whites >= 1024 * 768 * 9 / 10, "not maximized on screen: %d",
	      whites);

	press(h, KEY_LEFTALT, KEY_TAB);
	pause_ms(h, 400);
	int under = white_pixels(screenshot(h), &x, &y);

	CHECK(under <= whites - 200 * 150 * 9 / 10,
	      "Alt-Tab did not raise the native window over the X one (%d of "
	      "%d)",
	      under, whites);
	CHECK(harness_window_info(native)->focused,
	      "Alt-Tab did not focus the native window");

	press(h, KEY_LEFTALT, KEY_TAB);
	pause_ms(h, 400);
	CHECK(white_pixels(screenshot(h), &x, &y) >= whites - 400,
	      "Alt-Tab did not bring the X window back");

	press(h, KEY_LEFTALT, KEY_F4);
	wait_for_line(h, "message WM_DELETE_WINDOW");
	stop_client(client);
	pause_ms(h, 300);
	CHECK(harness_window_info(native)->focused,
	      "focus did not return to the native window");

	/* WM_NORMAL_HINTS: base 10x20, increments 7x13, so 300x200 becomes
	 * 290x176. */
	unsetenv("X_TEST_NO_HINTS");
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	wait_for_line(h, "size 290x176");
	stop_client(client);
	setenv("X_TEST_NO_HINTS", "1", 1);
	pause_ms(h, 300);

	setenv("X_TEST_TYPE", "_NET_WM_WINDOW_TYPE_UTILITY", 1);
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	wait_for_line(h, "mapped");
	pause_ms(h, 500);
	CHECK(white_pixels(screenshot(h), &x, &y) >= 300 * 200 * 9 / 10,
	      "the utility window is not managed and on screen");
	CHECK(!log_has("focus"),
	      "a utility window took focus on map, unlike Marco");
	harness_move_pointer(h, x + 20, y + 20);
	pause_ms(h, 200);
	harness_move_pointer(h, x + 25, y + 25);
	pause_ms(h, 200);
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	wait_for_line(h, "focus");
	stop_client(client);
	unsetenv("X_TEST_TYPE");
	pause_ms(h, 300);

	xdg_toplevel_set_fullscreen(native->xdg_toplevel, NULL);
	pause_ms(h, 300);
	setenv("X_TEST_OVERRIDE", "1", 1);
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	wait_for_line(h, "mapped");
	pause_ms(h, 500);
	whites = white_pixels(screenshot(h), &x, &y);
	CHECK(whites >= 300 * 200 * 9 / 10,
	      "the override-redirect window is not on screen");
	CHECK(x == 10 && y == 20,
	      "override-redirect at %d,%d, not where X put it", x, y);
	CHECK(!log_has("focus"), "the override-redirect window took focus");
	CHECK(harness_window_info(native)->focused,
	      "the override-redirect window took focus");
	stop_client(client);
	unsetenv("X_TEST_OVERRIDE");

	harness_window_destroy(native);
	weston_capture_v1_destroy(capture);
	capture = NULL;
	wl_registry_destroy(registry);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

static bool
file_has(const char *path, const char *text)
{
	char content[8192];
	FILE *file = fopen(path, "r");
	size_t got;

	if (!file)
		return false;
	got = fread(content, 1, sizeof content - 1, file);
	fclose(file);
	content[got] = '\0';
	return strstr(content, text) != NULL;
}

static void
wait_for_file(struct harness *h, const char *path, const char *text)
{
	for (int i = 0; i < 500 && !file_has(path, text); i++)
		pause_ms(h, 20);
	CHECK(file_has(path, text), "%s never had \"%s\"", path, text);
}

/* ICCCM through to what Polo shows: names, iconic, WM_CHANGE_STATE, ping. */
static void
test_icccm(int argc, char **argv)
{
	char xauth[512], ui_log[] = "/tmp/polo-xwayland-ui-XXXXXX";
	int x = 0, y = 0, fd = mkstemp(ui_log);

	CHECK(fd >= 0, "cannot create the helper's log");
	close(fd);
	setenv("POLO_SHELL_UI", argv[3], 1);
	setenv("FAKE_SHELL_UI_LOG", ui_log, 1);
	setenv("FAKE_SHELL_UI_DIALOG_RESPONSE", "-6", 1);
	setenv("POLO_XWAYLAND", "lazy", 1);
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	pause_ms(h, 200);
	int display = x_display(h);

	find_xauth(h, xauth, sizeof xauth);
	struct harness_window *native =
		harness_window_create(h, 200, 150, "native");

	pause_ms(h, 200);

	/* _NET_WM_NAME over WM_NAME, WM_CLASS class as the id. */
	start_log();
	pid_t client = start_x_client(display, argv[2], true, xauth);

	wait_for_line(h, "focus");
	pause_ms(h, 200);
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_TAB, true);
	harness_key(h, KEY_TAB, false);
	wait_for_file(h, ui_log, "\xc3\xbcn\xc3\xaf PoloClient");
	harness_key(h, KEY_ESC, true);
	harness_key(h, KEY_ESC, false);
	harness_key(h, KEY_LEFTALT, false);
	pause_ms(h, 200);

	/* _NET_WM_PING answered: no dialog. */
	press(h, KEY_LEFTALT, KEY_F4);
	wait_for_line(h, "message _NET_WM_PING");
	wait_for_line(h, "message WM_DELETE_WINDOW");
	stop_client(client);
	pause_ms(h, 300);

	/* WM_HINTS initial state. */
	setenv("X_TEST_ICONIC", "1", 1);
	unsetenv("X_TEST_NO_HINTS");
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	wait_for_line(h, "prop WM_STATE 3 0");
	pause_ms(h, 300);
	CHECK(white_pixels(screenshot(h), &x, &y) <= 400,
	      "an initially iconic window is on screen");
	unsetenv("X_TEST_ICONIC");
	setenv("X_TEST_NO_HINTS", "1", 1);
	press(h, KEY_LEFTALT, KEY_TAB);
	wait_for_line(h, "prop WM_STATE 1 0");
	pause_ms(h, 300);
	CHECK(white_pixels(screenshot(h), &x, &y) > 1000,
	      "Alt-Tab did not restore it");
	stop_client(client);
	pause_ms(h, 300);

	/* WM_CHANGE_STATE. */
	setenv("X_TEST_ICONIFY", "1", 1);
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	wait_for_line(h, "focus");
	pause_ms(h, 200);
	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	wait_for_line(h, "prop WM_STATE 3 0");
	pause_ms(h, 300);
	CHECK(white_pixels(screenshot(h), &x, &y) <= 400,
	      "WM_CHANGE_STATE did not take the window off screen");
	CHECK(harness_window_info(native)->focused, "focus did not move on");
	stop_client(client);
	unsetenv("X_TEST_ICONIFY");
	pause_ms(h, 300);

	/* No pong: the not-responding dialog comes, Force Quit reaches X. */
	setenv("X_TEST_NO_PONG", "1", 1);
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	wait_for_line(h, "focus");
	pause_ms(h, 200);
	press(h, KEY_LEFTALT, KEY_F4);
	wait_for_line(h, "message _NET_WM_PING");
	wait_for_file(h, ui_log, "show_dialog");
	wait_for_line(h, "disconnected");
	waitpid(client, NULL, 0);
	unlink(log_path);
	unsetenv("X_TEST_NO_PONG");

	unlink(ui_log);
	unsetenv("FAKE_SHELL_UI_LOG");
	unsetenv("FAKE_SHELL_UI_DIALOG_RESPONSE");
	setenv("POLO_SHELL_UI", "", 1);
	harness_window_destroy(native);
	weston_capture_v1_destroy(capture);
	capture = NULL;
	wl_registry_destroy(registry);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

static int
count_lines_with(const char *text)
{
	char line[1024];
	int n = 0;
	FILE *file = fopen(log_path, "r");

	if (!file)
		return 0;
	while (fgets(line, sizeof line, file))
		if (strstr(line, text))
			n++;
	fclose(file);
	return n;
}

/* The root properties a pager or wmctrl reads. */
static void
test_ewmh_root(int argc, char **argv)
{
	char xauth[512], line[256];

	setenv("POLO_XWAYLAND", "eager", 1);
	setenv("POLO_TEST_PREFS", "general/num-workspaces=4", 1);
	struct harness *h = harness_start(argc, argv);

	wait_xwayland(h, true);
	pause_ms(h, 500);
	int display = x_display(h);

	find_xauth(h, xauth, sizeof xauth);
	struct harness_window *native =
		harness_window_create(h, 200, 150, "native");

	pause_ms(h, 200);
	setenv("X_TEST_DUMP_ROOT", "1", 1);
	setenv("X_TEST_LAYOUT", "1", 1);
	start_log();
	pid_t client = start_x_client(display, argv[2], true, xauth);

	unsetenv("X_TEST_DUMP_ROOT");
	unsetenv("X_TEST_LAYOUT");
	wait_for_line(h, "focus");
	CHECK(log_has("prop _NET_WM_NAME Marco"),
	      "the WM check window is not called Marco");
	CHECK(count_lines_with(
		      "prop _NET_SUPPORTED _NET_WM_NAME _NET_CLOSE_WINDOW") ==
		      1,
	      "_NET_SUPPORTED is not Marco's list");
	CHECK(count_lines_with("_NET_RESTACK_WINDOW _GTK_FRAME_EXTENTS "
			       "_GTK_SHOW_WINDOW_MENU _GTK_WORKAREAS") == 1,
	      "_NET_SUPPORTED lacks the end of Marco's list");

	FILE *file;

	pause_ms(h, 300);
	file = fopen(log_path, "r");
	unsigned int id = 0;

	CHECK(file, "no client log");
	while (fgets(line, sizeof line, file))
		sscanf(line, "prop _NET_ACTIVE_WINDOW %u", &id);
	fclose(file);
	CHECK(id != 0, "_NET_ACTIVE_WINDOW does not name the focused X window");
	snprintf(line, sizeof line, "prop _NET_CLIENT_LIST %u", id);
	wait_for_line(h, line);
	snprintf(line, sizeof line, "prop _NET_CLIENT_LIST_STACKING %u", id);
	wait_for_line(h, line);

	/* A native window is not representable: None while it has focus. */
	press(h, KEY_LEFTALT, KEY_TAB);
	wait_for_line(h, "prop _NET_ACTIVE_WINDOW 0");
	CHECK(harness_window_info(native)->focused,
	      "Alt-Tab did not focus the native window");

	press(h, KEY_LEFTALT, KEY_TAB);
	pause_ms(h, 200);

	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_RIGHT, true);
	harness_key(h, KEY_RIGHT, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	wait_for_line(h, "prop _NET_CURRENT_DESKTOP 1");
	wait_for_line(h,
		      "prop _NET_WORKAREA 0 0 1024 768 0 0 1024 768 0 0 1024 "
		      "768 0 0 1024 768");
	wait_for_line(h, "prop _GTK_WORKAREAS_D3 0 0 1024 768");
	wait_for_line(h, "prop _NET_DESKTOP_GEOMETRY 1024 768");
	wait_for_line(h, "prop _NET_DESKTOP_VIEWPORT 0 0 0 0 0 0 0 0");
	wait_for_line(h, "prop _NET_NUMBER_OF_DESKTOPS 4");

	/* With the layout's two rows, Down reaches workspace 4 from 2. */
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_DOWN, true);
	harness_key(h, KEY_DOWN, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	wait_for_line(h, "prop _NET_CURRENT_DESKTOP 3");

	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_D, true);
	harness_key(h, KEY_D, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	wait_for_line(h, "prop _NET_SHOWING_DESKTOP 1");

	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "2");
	wait_for_line(h, "prop _NET_NUMBER_OF_DESKTOPS 2");

	stop_client(client);
	unsetenv("POLO_TEST_PREFS");

	/* Back to the native window's workspace: it needs frames to go away. */
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	for (int i = 0; i < 4; i++) {
		harness_key(h, KEY_UP, true);
		harness_key(h, KEY_UP, false);
		harness_key(h, KEY_LEFT, true);
		harness_key(h, KEY_LEFT, false);
	}
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	pause_ms(h, 300);
	harness_window_destroy(native);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

static void
x_send(int display, const char *helper, unsigned int window, const char *type,
       const char *d0, const char *d1, const char *d2, const char *d3,
       const char *d4, const char *xauth)
{
	char command[1024];

	snprintf(command, sizeof command,
		 "DISPLAY=:%d XAUTHORITY=%s %s %u %s %s %s %s %s %s", display,
		 xauth, helper, window, type, d0, d1, d2, d3, d4);
	CHECK(system(command) == 0, "x-send failed: %s", command);
}

static unsigned int
client_window(void)
{
	char line[128];
	unsigned int id = 0;
	FILE *file = fopen(log_path, "r");

	if (file) {
		while (fgets(line, sizeof line, file))
			sscanf(line, "id %u", &id);
		fclose(file);
	}
	return id;
}

/* Every EWMH client message Marco acts on, and what comes back. */
static void
test_ewmh_messages(int argc, char **argv)
{
	char xauth[512];
	const char *send = argv[4];
	int x = 0, y = 0;

	setenv("POLO_XWAYLAND", "eager", 1);
	setenv("POLO_TEST_PREFS", "general/num-workspaces=4", 1);
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	wait_xwayland(h, true);
	pause_ms(h, 500);
	int display = x_display(h);

	find_xauth(h, xauth, sizeof xauth);
	struct harness_window *native =
		harness_window_create(h, 200, 150, "native");

	pause_ms(h, 200);
	start_log();
	setenv("X_TEST_CONFIGURE_ON_KEY", "1", 1);
	pid_t client = start_x_client(display, argv[2], true, xauth);

	unsetenv("X_TEST_CONFIGURE_ON_KEY");
	wait_for_line(h, "focus");
	pause_ms(h, 300);
	unsigned int id = client_window();

	CHECK(id != 0, "no window id");
	wait_for_line(h, "prop _NET_WM_DESKTOP 0");
	CHECK(count_lines_with("_NET_WM_ALLOWED_ACTIONS _NET_WM_ACTION_MOVE "
			       "_NET_WM_ACTION_RESIZE") > 0,
	      "_NET_WM_ALLOWED_ACTIONS lacks move and resize");
	CHECK(count_lines_with("_NET_WM_ACTION_ABOVE _NET_WM_ACTION_BELOW") > 0,
	      "_NET_WM_ALLOWED_ACTIONS lacks above and below");
	CHECK(count_lines_with("_NET_WM_STATE_FOCUSED") > 0,
	      "_NET_WM_STATE lacks FOCUSED");

	/* _NET_WM_STATE: add, toggle and remove. */
	x_send(display, send, id, "_NET_WM_STATE", "1",
	       "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ",
	       "0", "0", xauth);
	wait_for_line(h, "size 1024x768");
	wait_for_line(h,
		      "prop _NET_WM_STATE _NET_WM_STATE_MAXIMIZED_HORZ "
		      "_NET_WM_STATE_MAXIMIZED_VERT _NET_WM_STATE_FOCUSED");
	x_send(display, send, id, "_NET_WM_STATE", "0",
	       "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ",
	       "0", "0", xauth);
	wait_for_line(h, "size 300x200");
	x_send(display, send, id, "_NET_WM_STATE", "2", "_NET_WM_STATE_ABOVE",
	       "0", "0", "0", xauth);
	wait_for_line(
		h,
		"prop _NET_WM_STATE _NET_WM_STATE_ABOVE _NET_WM_STATE_FOCUSED");
	x_send(display, send, id, "_NET_WM_STATE", "2", "_NET_WM_STATE_ABOVE",
	       "0", "0", "0", xauth);
	x_send(display, send, id, "_NET_WM_STATE", "1", "_NET_WM_STATE_BELOW",
	       "0", "0", "0", xauth);
	wait_for_line(
		h,
		"prop _NET_WM_STATE _NET_WM_STATE_BELOW _NET_WM_STATE_FOCUSED");
	x_send(display, send, id, "_NET_WM_STATE", "0", "_NET_WM_STATE_BELOW",
	       "0", "0", "0", xauth);
	x_send(display, send, id, "_NET_WM_STATE", "1",
	       "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_SKIP_PAGER", "0",
	       "0", xauth);
	wait_for_line(h,
		      "prop _NET_WM_STATE _NET_WM_STATE_SKIP_PAGER "
		      "_NET_WM_STATE_SKIP_TASKBAR _NET_WM_STATE_FOCUSED");
	x_send(display, send, id, "_NET_WM_STATE", "0",
	       "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_SKIP_PAGER", "0",
	       "0", xauth);
	x_send(display, send, id, "_NET_WM_STATE", "1", "_NET_WM_STATE_STICKY",
	       "0", "0", "0", xauth);
	wait_for_line(h, "prop _NET_WM_DESKTOP 4294967295");
	x_send(display, send, id, "_NET_WM_STATE", "0", "_NET_WM_STATE_STICKY",
	       "0", "0", "0", xauth);

	/* _NET_WM_DESKTOP and _NET_CURRENT_DESKTOP. */
	x_send(display, send, id, "_NET_WM_DESKTOP", "2", "0", "0", "0", "0",
	       xauth);
	wait_for_line(h, "prop _NET_WM_DESKTOP 2");
	pause_ms(h, 300);
	CHECK(white_pixels(screenshot(h), &x, &y) <= 400,
	      "it did not leave this workspace");
	wait_for_line(h, "prop WM_STATE 3 0");
	x_send(display, send, 0, "_NET_CURRENT_DESKTOP", "2", "0", "0", "0",
	       "0", xauth);
	wait_for_line(h, "prop _NET_CURRENT_DESKTOP 2");
	pause_ms(h, 300);
	CHECK(white_pixels(screenshot(h), &x, &y) >= 300 * 200 * 9 / 10,
	      "the X window is not on workspace 3");
	x_send(display, send, 0, "_NET_NUMBER_OF_DESKTOPS", "3", "0", "0", "0",
	       "0", xauth);
	wait_for_line(h, "prop _NET_NUMBER_OF_DESKTOPS 3");

	x_send(display, send, id, "_NET_REQUEST_FRAME_EXTENTS", "0", "0", "0",
	       "0", "0", xauth);
	wait_for_line(h, "prop _NET_FRAME_EXTENTS 0 0 0 0");

	/* _NET_MOVERESIZE_WINDOW: x, y, width and height, north-west gravity.
	 */
	x_send(display, send, id, "_NET_MOVERESIZE_WINDOW", "0xf01", "30", "40",
	       "320", "210", xauth);
	wait_for_line(h, "at 30 40");
	wait_for_line(h, "size 320x210");
	pause_ms(h, 300);
	white_pixels(screenshot(h), &x, &y);
	CHECK(x == 30 && y == 40, "_NET_MOVERESIZE_WINDOW drew it at %d,%d", x,
	      y);

	/* A ConfigureRequest goes through policy: the same answer as a
	 * synthetic event. */
	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	wait_for_line(h, "size 400x300");

	/* _NET_WM_MOVERESIZE, keyboard move. */
	x_send(display, send, id, "_NET_WM_MOVERESIZE", "0", "0", "10", "0",
	       "0", xauth);
	pause_ms(h, 200);
	harness_key(h, KEY_RIGHT, true);
	harness_key(h, KEY_RIGHT, false);
	harness_key(h, KEY_ENTER, true);
	harness_key(h, KEY_ENTER, false);
	wait_for_line(h, "at 40 40");

	/* _NET_SHOWING_DESKTOP. */
	x_send(display, send, 0, "_NET_SHOWING_DESKTOP", "1", "0", "0", "0",
	       "0", xauth);
	wait_for_line(h, "prop _NET_SHOWING_DESKTOP 1");
	pause_ms(h, 300);
	CHECK(white_pixels(screenshot(h), &x, &y) <= 400,
	      "show desktop left the X window up");
	wait_for_line(h, "prop _NET_WM_STATE _NET_WM_STATE_HIDDEN");
	x_send(display, send, 0, "_NET_SHOWING_DESKTOP", "0", "0", "0", "0",
	       "0", xauth);
	wait_for_line(h, "prop _NET_SHOWING_DESKTOP 0");

	/* _NET_ACTIVE_WINDOW from a pager takes the user there; WM_CHANGE_STATE
	 * minimizes. */
	x_send(display, send, id, "WM_CHANGE_STATE", "3", "0", "0", "0", "0",
	       xauth);
	pause_ms(h, 300);
	CHECK(white_pixels(screenshot(h), &x, &y) <= 400,
	      "WM_CHANGE_STATE did not minimize");
	x_send(display, send, id, "_NET_ACTIVE_WINDOW", "2", "0", "0", "0", "0",
	       xauth);
	pause_ms(h, 300);
	CHECK(white_pixels(screenshot(h), &x, &y) >= 400 * 300 * 9 / 10,
	      "_NET_ACTIVE_WINDOW did not bring it back");

	/* _NET_CLOSE_WINDOW. */
	x_send(display, send, id, "_NET_CLOSE_WINDOW", "0", "2", "0", "0", "0",
	       xauth);
	wait_for_line(h, "message WM_DELETE_WINDOW");
	stop_client(client);
	pause_ms(h, 200);

	/* Initial _NET_WM_STATE and _NET_WM_DESKTOP are read before Polo owns
	 * them. */
	x_send(display, send, 0, "_NET_CURRENT_DESKTOP", "0", "0", "0", "0",
	       "0", xauth);
	pause_ms(h, 200);
	setenv("X_TEST_INITIAL_STATE", "_NET_WM_STATE_ABOVE", 1);
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	unsetenv("X_TEST_INITIAL_STATE");
	wait_for_line(
		h,
		"prop _NET_WM_STATE _NET_WM_STATE_ABOVE _NET_WM_STATE_FOCUSED");
	stop_client(client);
	pause_ms(h, 200);

	setenv("X_TEST_INITIAL_DESKTOP", "1", 1);
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	unsetenv("X_TEST_INITIAL_DESKTOP");
	wait_for_line(h, "prop _NET_WM_DESKTOP 1");
	pause_ms(h, 300);
	CHECK(white_pixels(screenshot(h), &x, &y) <= 400,
	      "it should have mapped on workspace 2");
	stop_client(client);
	pause_ms(h, 200);

	/* _NET_WM_USER_TIME 0: Marco does not focus it. */
	setenv("X_TEST_USER_TIME", "0", 1);
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	unsetenv("X_TEST_USER_TIME");
	wait_for_line(h, "mapped");
	pause_ms(h, 500);
	CHECK(!log_has("focus"), "a window with user time 0 took focus");
	stop_client(client);
	pause_ms(h, 200);

	/* _NET_WM_STRUT reaches the work area. */
	setenv("X_TEST_STRUT", "30", 1);
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	unsetenv("X_TEST_STRUT");
	wait_for_line(h, "prop _GTK_WORKAREAS_D0 0 30 1024 738");
	stop_client(client);
	pause_ms(h, 200);

	/* _GTK_FRAME_EXTENTS: maximized, the visible part fills the screen, not
	 * the X window. */
	setenv("X_TEST_FRAME_EXTENTS", "10", 1);
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	unsetenv("X_TEST_FRAME_EXTENTS");
	wait_for_line(h, "focus");
	pause_ms(h, 200);
	press(h, KEY_LEFTALT, KEY_F10);
	wait_for_line(h, "size 1044x788");
	wait_for_line(h, "at -10 -10");
	stop_client(client);

	unsetenv("POLO_TEST_PREFS");
	harness_window_destroy(native);
	weston_capture_v1_destroy(capture);
	capture = NULL;
	wl_registry_destroy(registry);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

static void
last_allowed_actions(char *actions, size_t size)
{
	char line[1024];
	FILE *file = fopen(log_path, "r");

	actions[0] = '\0';
	if (file) {
		while (fgets(line, sizeof line, file))
			if (strstr(line, "prop _NET_WM_ALLOWED_ACTIONS"))
				snprintf(actions, size, "%s", line);
		fclose(file);
	}
}

/* Match Marco's advertised actions except SHADE: Polo uses client-side X
 * decorations. */
static void
test_motif(int argc, char **argv)
{
	char xauth[512], actions[1024];
	const char *send = argv[4];

	setenv("POLO_XWAYLAND", "eager", 1);
	struct harness *h = harness_start(argc, argv);

	wait_xwayland(h, true);
	pause_ms(h, 500);
	int display = x_display(h);

	find_xauth(h, xauth, sizeof xauth);
	start_log();
	setenv("X_TEST_MOTIF", "1,21,0", 1);
	setenv("X_TEST_MOTIF_ON_KEY", "1,32,0", 1);
	pid_t client = start_x_client(display, argv[2], true, xauth);

	unsetenv("X_TEST_MOTIF");
	unsetenv("X_TEST_MOTIF_ON_KEY");
	wait_for_line(h, "focus");
	pause_ms(h, 300);
	unsigned int id = client_window();

	last_allowed_actions(actions, sizeof actions);
	CHECK(strstr(actions, "_NET_WM_ACTION_RESIZE"),
	      "MWM_FUNC_RESIZE is not Marco's to obey: %s", actions);
	CHECK(strstr(actions, "_NET_WM_ACTION_CLOSE") &&
		      strstr(actions, "_NET_WM_ACTION_MINIMIZE") &&
		      strstr(actions, "_NET_WM_ACTION_FULLSCREEN"),
	      "all minus move and maximize: %s", actions);
	CHECK(!strstr(actions, "_NET_WM_ACTION_MOVE") &&
		      !strstr(actions, "_NET_WM_ACTION_MAXIMIZE"),
	      "move and maximize were turned off: %s", actions);

	x_send(display, send, id, "_NET_WM_STATE", "1",
	       "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ",
	       "0", "0", xauth);
	pause_ms(h, 300);
	CHECK(count_lines_with("_NET_WM_STATE_MAXIMIZED") == 0,
	      "maximized without MWM_FUNC_MAXIMIZE");

	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	pause_ms(h, 300);
	last_allowed_actions(actions, sizeof actions);
	CHECK(strstr(actions, "_NET_WM_ACTION_CLOSE") &&
		      strstr(actions, "_NET_WM_ACTION_RESIZE") &&
		      !strstr(actions, "_NET_WM_ACTION_MINIMIZE"),
	      "a changed hint did not reach the allowed actions: %s", actions);

	stop_client(client);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

static pid_t
start_focus_model(struct harness *h, int display, const char *client,
		  const char *xauth, const char *model, bool respond)
{
	start_log();
	setenv("X_TEST_FOCUS_MODEL", model, 1);
	if (respond)
		setenv("X_TEST_TAKE_FOCUS_RESPOND", "1", 1);
	pid_t pid = start_x_client(display, client, true, xauth);

	unsetenv("X_TEST_FOCUS_MODEL");
	unsetenv("X_TEST_TAKE_FOCUS_RESPOND");
	pause_ms(h, 500);
	return pid;
}

static int
keys_after_pressing(struct harness *h)
{
	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	pause_ms(h, 200);
	return count_lines_with("key 38");
}

/* meta_window_focus () for the four ICCCM models, and focus crossing to native.
 */
static void
test_focus_models(int argc, char **argv)
{
	char xauth[512];
	const char *send = argv[4];

	setenv("POLO_XWAYLAND", "eager", 1);
	struct harness *h = harness_start(argc, argv);

	wait_xwayland(h, true);
	pause_ms(h, 500);
	int display = x_display(h);

	find_xauth(h, xauth, sizeof xauth);
	struct harness_window *native =
		harness_window_create(h, 200, 150, "native");

	pause_ms(h, 200);

	pid_t client =
		start_focus_model(h, display, argv[2], xauth, "passive", false);

	CHECK(log_has("focus"), "passive: no SetInputFocus");
	CHECK(!log_has("message WM_TAKE_FOCUS"), "passive: WM_TAKE_FOCUS sent");
	stop_client(client);
	pause_ms(h, 200);

	client =
		start_focus_model(h, display, argv[2], xauth, "locally", false);
	CHECK(log_has("focus"), "locally active: no SetInputFocus");
	CHECK(log_has("message WM_TAKE_FOCUS"),
	      "locally active: no WM_TAKE_FOCUS");
	stop_client(client);
	pause_ms(h, 200);

	client = start_focus_model(h, display, argv[2], xauth, "globally",
				   false);
	CHECK(log_has("message WM_TAKE_FOCUS"),
	      "globally active: no WM_TAKE_FOCUS");
	CHECK(!log_has("focus"),
	      "globally active: focus was set for the client");
	CHECK(!harness_window_info(native)->focused,
	      "globally active: the Wayland keyboard stayed on the native "
	      "window");
	stop_client(client);
	pause_ms(h, 200);

	client =
		start_focus_model(h, display, argv[2], xauth, "globally", true);
	CHECK(log_has("focus"),
	      "globally active: the client could not take focus");
	CHECK(keys_after_pressing(h) == 1, "globally active: no key");
	stop_client(client);
	pause_ms(h, 200);

	client = start_focus_model(h, display, argv[2], xauth, "none", false);
	CHECK(!log_has("focus") && !log_has("message WM_TAKE_FOCUS"),
	      "no input: the window was offered focus");
	CHECK(harness_window_info(native)->focused,
	      "no input: the native window lost focus");
	stop_client(client);
	pause_ms(h, 200);

	/* X to native and back: keyboard and X focus move together. */
	client =
		start_focus_model(h, display, argv[2], xauth, "passive", false);
	unsigned int id = client_window();

	CHECK(!harness_window_info(native)->focused,
	      "the X window has no keyboard");
	CHECK(keys_after_pressing(h) == 1, "no key on the focused X window");
	press(h, KEY_LEFTALT, KEY_TAB);
	pause_ms(h, 400);
	CHECK(harness_window_info(native)->focused,
	      "Alt-Tab: native not focused");
	CHECK(log_has("blur"), "X focus stayed on the X window");
	CHECK(keys_after_pressing(h) == 1,
	      "a key for native reached the X window");
	int focused_before = count_lines_with("focus");

	press(h, KEY_LEFTALT, KEY_TAB);
	pause_ms(h, 400);
	CHECK(count_lines_with("focus") > focused_before,
	      "X focus did not return");
	CHECK(!harness_window_info(native)->focused,
	      "native kept the keyboard");
	CHECK(keys_after_pressing(h) == 2, "no key after coming back");

	/* A client focuses itself: Polo's focus window follows. */
	press(h, KEY_LEFTALT, KEY_TAB);
	pause_ms(h, 400);
	CHECK(harness_window_info(native)->focused,
	      "Alt-Tab: native not focused");
	x_send(display, send, id, "SET_INPUT_FOCUS", "0", "0", "0", "0", "0",
	       xauth);
	pause_ms(h, 300);
	CHECK(!harness_window_info(native)->focused,
	      "the Wayland keyboard did not follow X focus");
	CHECK(keys_after_pressing(h) == 3,
	      "no key after the client took focus");

	/* Focus set to None goes to the default window: the one on top. A
	 * client that focuses itself is not raised, so that is the native one.
	 */
	x_send(display, send, 0, "SET_INPUT_FOCUS", "0", "0", "0", "0", "0",
	       xauth);
	pause_ms(h, 300);
	CHECK(harness_window_info(native)->focused,
	      "focus fell to None and did not reach the top window");
	CHECK(keys_after_pressing(h) == 3,
	      "a key for native reached the X window");

	press(h, KEY_LEFTALT, KEY_TAB);
	pause_ms(h, 400);
	CHECK(keys_after_pressing(h) == 4, "no key after Alt-Tab");
	focused_before = count_lines_with("focus");
	x_send(display, send, 0, "SET_INPUT_FOCUS", "0", "0", "0", "0", "0",
	       xauth);
	pause_ms(h, 300);
	CHECK(count_lines_with("focus") > focused_before,
	      "focus fell to None and stayed there");
	CHECK(keys_after_pressing(h) == 5, "no key after focus fell to None");

	/* A minimized window cannot take focus. */
	press(h, KEY_LEFTALT, KEY_F9);
	pause_ms(h, 400);
	CHECK(harness_window_info(native)->focused,
	      "Alt-F9: native not focused");
	int blurred_before = count_lines_with("blur");

	x_send(display, send, id, "SET_INPUT_FOCUS", "0", "0", "0", "0", "0",
	       xauth);
	pause_ms(h, 300);
	CHECK(harness_window_info(native)->focused,
	      "a minimized window took focus");
	CHECK(count_lines_with("blur") > blurred_before,
	      "X focus stayed on the minimized window");
	CHECK(keys_after_pressing(h) == 5,
	      "a key reached the minimized window");
	stop_client(client);
	pause_ms(h, 200);

	/* Nothing left to focus: X focus must not stay on the window. */
	harness_window_destroy(native);
	pause_ms(h, 200);
	client =
		start_focus_model(h, display, argv[2], xauth, "passive", false);
	CHECK(log_has("focus"), "no focus without other windows");
	press(h, KEY_LEFTALT, KEY_F9);
	pause_ms(h, 400);
	CHECK(log_has("blur"), "X focus stayed with nothing focused");
	stop_client(client);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

/* ---- Clipboard: a wl_data_device on one side, x-clip on the other. ---- */

static struct wl_data_device_manager *data_manager;
static struct wl_data_offer *selection_offer;
static char offer_mimes[1024];
static size_t source_bytes;
static struct wl_data_offer *drag_offer;
static int drag_enters;
static bool drag_dropped, source_target_seen, source_was_cancelled;
static bool source_finished, source_drop_performed;

static void
data_registry_global(void *data, struct wl_registry *registry, uint32_t name,
		     const char *interface, uint32_t version)
{
	if (strcmp(interface, wl_data_device_manager_interface.name) == 0)
		data_manager = wl_registry_bind(
			registry, name, &wl_data_device_manager_interface, 3);
}

static const struct wl_registry_listener data_registry_listener = {
	data_registry_global,
	registry_remove,
};

static void
offer_offer(void *data, struct wl_data_offer *offer, const char *mime)
{
	strncat(offer_mimes, mime,
		sizeof offer_mimes - strlen(offer_mimes) - 2);
	strcat(offer_mimes, " ");
}

static void
offer_source_actions(void *data, struct wl_data_offer *offer, uint32_t actions)
{
}

static void
offer_action(void *data, struct wl_data_offer *offer, uint32_t action)
{
}

static const struct wl_data_offer_listener offer_listener = {
	offer_offer,
	offer_source_actions,
	offer_action,
};

static void
device_data_offer(void *data, struct wl_data_device *device,
		  struct wl_data_offer *offer)
{
	offer_mimes[0] = '\0';
	wl_data_offer_add_listener(offer, &offer_listener, NULL);
}

static void
device_enter(void *data, struct wl_data_device *device, uint32_t serial,
	     struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y,
	     struct wl_data_offer *offer)
{
	drag_offer = offer;
	drag_enters++;
	if (offer) {
		wl_data_offer_accept(offer, serial, "text/plain;charset=utf-8");
		wl_data_offer_set_actions(
			offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
			WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
	}
}

static void
device_leave(void *data, struct wl_data_device *device)
{
	if (drag_offer && !drag_dropped)
		wl_data_offer_destroy(drag_offer);
	if (!drag_dropped)
		drag_offer = NULL;
}

static void
device_motion(void *data, struct wl_data_device *device, uint32_t time,
	      wl_fixed_t x, wl_fixed_t y)
{
}

static void
device_drop(void *data, struct wl_data_device *device)
{
	drag_dropped = true;
}

static void
device_selection(void *data, struct wl_data_device *device,
		 struct wl_data_offer *offer)
{
	if (selection_offer)
		wl_data_offer_destroy(selection_offer);
	selection_offer = offer;
}

static const struct wl_data_device_listener device_listener = {
	device_data_offer, device_enter, device_leave,
	device_motion,     device_drop,  device_selection,
};

static void
source_target(void *data, struct wl_data_source *source, const char *mime)
{
	source_target_seen = mime != NULL;
}

/* Written from a child: the reader is at the far side of Polo, not this loop.
 */
static void
source_send(void *data, struct wl_data_source *source, const char *mime, int fd)
{
	if (fork() == 0) {
		char chunk[4096];
		size_t done = 0;

		signal(SIGPIPE, SIG_DFL);
		fcntl(fd, F_SETFL, 0);
		while (done < source_bytes) {
			size_t n = source_bytes - done;

			if (n > sizeof chunk)
				n = sizeof chunk;
			for (size_t i = 0; i < n; i++)
				chunk[i] = 'a' + (done + i) % 26;
			if (write(fd, chunk, n) != (ssize_t)n)
				_exit(1);
			done += n;
		}
		_exit(0);
	}
	close(fd);
}

static void
source_cancelled(void *data, struct wl_data_source *source)
{
	source_was_cancelled = true;
	wl_data_source_destroy(source);
}

static void
source_dnd_drop_performed(void *data, struct wl_data_source *source)
{
	source_drop_performed = true;
}

static void
source_dnd_finished(void *data, struct wl_data_source *source)
{
	source_finished = true;
	wl_data_source_destroy(source);
}

static void
source_action(void *data, struct wl_data_source *source, uint32_t action)
{
}

static const struct wl_data_source_listener data_source_listener = {
	source_target,       source_send,
	source_cancelled,    source_dnd_drop_performed,
	source_dnd_finished, source_action,
};

static void
native_copy(struct harness *h, struct wl_data_device *device, const char *mime,
	    size_t bytes)
{
	struct wl_data_source *source =
		wl_data_device_manager_create_data_source(data_manager);

	source_bytes = bytes;
	wl_data_source_add_listener(source, &data_source_listener, NULL);
	wl_data_source_offer(source, mime);
	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	harness_roundtrip(h);
	wl_data_device_set_selection(device, source, h->last_keyboard_serial);
	pause_ms(h, 200);
}

/* Bytes of the pattern read from the offer, -1 if they are wrong. */
static long
native_paste(struct harness *h, const char *mime, int delay_ms)
{
	int fds[2];
	char chunk[65536];
	long total = 0;
	ssize_t n;

	CHECK(selection_offer, "no selection on offer");
	CHECK(pipe(fds) == 0, "pipe");
	wl_data_offer_receive(selection_offer, mime, fds[1]);
	wl_display_flush(h->display);
	close(fds[1]);
	if (delay_ms)
		usleep(delay_ms * 1000);
	while ((n = read(fds[0], chunk, sizeof chunk)) > 0) {
		for (ssize_t i = 0; i < n; i++)
			if (chunk[i] != 'a' + (total + i) % 26)
				total = -1;
		if (total < 0)
			break;
		total += n;
	}
	close(fds[0]);
	return total;
}

/* Dispatches while it runs: a native source answers from this loop. */
static int
x_clip(struct harness *h, int display, const char *helper, const char *xauth,
       const char *args)
{
	char command[1024];
	int status = 0;

	snprintf(command, sizeof command,
		 "ASAN_OPTIONS=detect_leaks=0 DISPLAY=:%d XAUTHORITY=%s %s %s",
		 display, xauth, helper, args);
	pid_t pid = fork();

	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", command, NULL);
		_exit(127);
	}
	while (waitpid(pid, &status, WNOHANG) == 0)
		pause_ms(h, 20);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static pid_t
x_clip_owner(struct harness *h, int display, const char *helper,
	     const char *xauth, const char *target, const char *size)
{
	char name[16];
	pid_t owner = fork();

	if (owner == 0) {
		snprintf(name, sizeof name, ":%d", display);
		setenv("DISPLAY", name, 1);
		setenv("XAUTHORITY", xauth, 1);
		execl(helper, helper, "copy", target, size, NULL);
		_exit(127);
	}
	return owner;
}

#define BIG 1048576

static void
test_clipboard(int argc, char **argv)
{
	char xauth[512];
	const char *clip = argv[5];

	setenv("POLO_XWAYLAND", "eager", 1);
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &data_registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(data_manager, "no wl_data_device_manager");
	struct wl_data_device *device =
		wl_data_device_manager_get_data_device(data_manager, h->seat);

	wl_data_device_add_listener(device, &device_listener, NULL);
	wait_xwayland(h, true);
	pause_ms(h, 500);
	int display = x_display(h);

	find_xauth(h, xauth, sizeof xauth);
	struct harness_window *native =
		harness_window_create(h, 200, 150, "native");

	pause_ms(h, 200);

	/* Native to X. */
	native_copy(h, device, "text/plain;charset=utf-8", 1000);
	CHECK(x_clip(h, display, clip, xauth, "targets UTF8_STRING") == 0,
	      "no UTF8_STRING");
	CHECK(x_clip(h, display, clip, xauth, "paste UTF8_STRING 1000") == 0,
	      "UTF8_STRING");
	CHECK(x_clip(h, display, clip, xauth, "paste TEXT 1000") == 0, "TEXT");
	CHECK(x_clip(h, display, clip, xauth, "paste image/png 0") == 3,
	      "a target not on offer was served");

	native_copy(h, device, "text/plain;charset=utf-8", BIG);
	CHECK(x_clip(h, display, clip, xauth, "paste UTF8_STRING 1048576") == 0,
	      "large native to X (INCR)");
	setenv("X_CLIP_CHILD", "1", 1);
	CHECK(x_clip(h, display, clip, xauth, "paste UTF8_STRING 1048576") == 0,
	      "large native to X, requestor not a toplevel");
	unsetenv("X_CLIP_CHILD");

	native_copy(h, device, "image/png", 200000);
	CHECK(x_clip(h, display, clip, xauth, "targets image/png") == 0,
	      "no image/png");
	CHECK(x_clip(h, display, clip, xauth, "paste image/png 200000") == 0,
	      "image native to X");
	CHECK(x_clip(h, display, clip, xauth, "paste UTF8_STRING 0") == 3,
	      "an image was served as text");

	/* X to native. */
	pid_t owner =
		x_clip_owner(h, display, clip, xauth, "UTF8_STRING", "1000");

	pause_ms(h, 400);
	CHECK(strstr(offer_mimes, "text/plain;charset=utf-8"), "offers: %s",
	      offer_mimes);
	CHECK(native_paste(h, "text/plain;charset=utf-8", 0) == 1000,
	      "text X to native");
	CHECK(native_paste(h, "image/png", 0) == 0,
	      "the owner refused: the reader must see the end, not hang");

	/* The owner leaves: libweston's clipboard kept a copy. */
	kill(owner, SIGTERM);
	waitpid(owner, NULL, 0);
	pause_ms(h, 300);
	CHECK(native_paste(h, "text/plain;charset=utf-8", 0) == 1000,
	      "the copy did not outlive the X client");

	owner = x_clip_owner(h, display, clip, xauth, "image/png", "200000");
	pause_ms(h, 400);
	CHECK(strstr(offer_mimes, "image/png"), "offers: %s", offer_mimes);
	CHECK(native_paste(h, "image/png", 0) == 200000, "image X to native");
	kill(owner, SIGTERM);
	waitpid(owner, NULL, 0);
	pause_ms(h, 200);

	/* Large (INCR), read during libweston's own read and by a slow reader
	 * that lets the pipe fill. */
	wl_data_offer_destroy(selection_offer);
	selection_offer = NULL;
	owner = x_clip_owner(h, display, clip, xauth, "UTF8_STRING", "1048576");
	for (int i = 0; i < 5000 && !selection_offer; i++) {
		harness_roundtrip(h);
		usleep(1000);
	}
	CHECK(native_paste(h, "text/plain;charset=utf-8", 0) == BIG,
	      "large X to native during the clipboard's read");
	CHECK(native_paste(h, "text/plain;charset=utf-8", 400) == BIG,
	      "large X to native with a slow reader");

	/* A reader that gives up must not take the compositor with it. */
	int fds[2];

	CHECK(pipe(fds) == 0, "pipe");
	wl_data_offer_receive(selection_offer, "text/plain;charset=utf-8",
			      fds[1]);
	wl_display_flush(h->display);
	close(fds[1]);
	close(fds[0]);
	pause_ms(h, 500);
	CHECK(native_paste(h, "text/plain;charset=utf-8", 0) == BIG,
	      "no paste after a reader gave up");
	kill(owner, SIGTERM);
	waitpid(owner, NULL, 0);
	pause_ms(h, 200);
	CHECK(native_paste(h, "text/plain;charset=utf-8", 0) == BIG,
	      "libweston's copy did not survive the paste during its read");

	/* A window asks for a large selection and never reads it. */
	native_copy(h, device, "text/plain;charset=utf-8", BIG);
	start_log();
	setenv("X_TEST_CONVERT_ON_KEY", "UTF8_STRING", 1);
	setenv("X_TEST_MOTIF_ON_KEY", "1,32,0", 1);
	pid_t client = start_x_client(display, argv[2], true, xauth);

	unsetenv("X_TEST_CONVERT_ON_KEY");
	unsetenv("X_TEST_MOTIF_ON_KEY");
	wait_for_line(h, "focus");
	pause_ms(h, 300);
	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	pause_ms(h, 400);
	char actions[1024];

	last_allowed_actions(actions, sizeof actions);
	CHECK(!strstr(actions, "_NET_WM_ACTION_MINIMIZE"),
	      "the requestor's property change went missing: %s", actions);
	CHECK(x_clip(h, display, clip, xauth, "paste UTF8_STRING 1048576") == 3,
	      "a second transfer ran over the first");

	/* It dies: the clipboard must not stay blocked. */
	stop_client(client);
	pause_ms(h, 300);
	CHECK(x_clip(h, display, clip, xauth, "paste UTF8_STRING 1048576") == 0,
	      "a dead requestor blocked the clipboard");

	/* Xwayland crashes while it owns the clipboard. */
	owner = x_clip_owner(h, display, clip, xauth, "UTF8_STRING", "1000");
	pause_ms(h, 400);
	kill(xwayland_pid(h), SIGKILL);
	waitpid(owner, NULL, 0);
	pause_ms(h, 400);
	CHECK(native_paste(h, "text/plain;charset=utf-8", 0) == 1000,
	      "the clipboard did not survive Xwayland");

	harness_window_destroy(native);
	wl_data_device_destroy(device);
	wl_data_device_manager_destroy(data_manager);
	data_manager = NULL;
	if (selection_offer)
		wl_data_offer_destroy(selection_offer);
	selection_offer = NULL;
	wl_registry_destroy(registry);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

/* ---- Drag and drop ---------------------------------------------------- */

static char dnd_log[2][300];

static pid_t
start_x_dnd(int display, const char *helper, const char *xauth,
	    const char *role, const char *log, bool refuse)
{
	char name[16];
	pid_t pid = fork();

	if (pid == 0) {
		snprintf(name, sizeof name, ":%d", display);
		setenv("DISPLAY", name, 1);
		setenv("XAUTHORITY", xauth, 1);
		if (refuse)
			setenv("X_DND_REFUSE", "1", 1);
		execl(helper, helper, role, log, NULL);
		_exit(127);
	}
	return pid;
}

/* x-dnd maps at a fixed place: the source at 20,20 and the target at 690,20. */
static void
x_dnd_centre(struct harness *h, const char *log, bool source, int *x, int *y)
{
	wait_for_file(h, log, source ? "at 20 20" : "at 690 20");
	pause_ms(h, 300);
	*x = (source ? 20 : 690) + 150;
	*y = 20 + 100;
}

static void
native_centre(struct harness *h, struct harness_window *window, int *x, int *y)
{
	for (int py = 5; py < 768; py += 20)
		for (int px = 5; px < 1024; px += 20) {
			harness_move_pointer(h, px, py);
			harness_roundtrip(h);
			if (h->pointer_focus == window->surface) {
				*x = px + 50;
				*y = py + 50;
				return;
			}
		}
	CHECK(false, "the native window is nowhere");
}

static void
glide(struct harness *h, int from_x, int from_y, int to_x, int to_y)
{
	for (int i = 1; i <= 8; i++) {
		harness_move_pointer(h, from_x + (to_x - from_x) * i / 8,
				     from_y + (to_y - from_y) * i / 8);
		pause_ms(h, 40);
	}
}

static void
fresh_log(int i)
{
	unlink(dnd_log[i]);
	close(open(dnd_log[i], O_CREAT | O_WRONLY, 0600));
}

static void
native_drag_to(struct harness *h, struct wl_data_device *device,
	       struct harness_window *native, int nx, int ny, int tx, int ty)
{
	struct wl_data_source *source =
		wl_data_device_manager_create_data_source(data_manager);

	source_bytes = 1000;
	source_target_seen = source_was_cancelled = false;
	source_finished = source_drop_performed = false;
	wl_data_source_add_listener(source, &data_source_listener, NULL);
	wl_data_source_offer(source, "text/plain;charset=utf-8");
	wl_data_source_set_actions(source,
				   WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
	harness_move_pointer(h, nx, ny);
	harness_roundtrip(h);
	harness_button(h, BTN_LEFT, true);
	harness_roundtrip(h);
	harness_roundtrip(h);
	wl_data_device_start_drag(device, source, native->surface, NULL,
				  h->last_button_serial);
	pause_ms(h, 100);
	glide(h, nx, ny, tx, ty);
}

/* Both directions, X to X beside them, and refusals. */
static void
test_dnd(int argc, char **argv)
{
	char xauth[512];
	const char *dnd = argv[6];
	int nx = 0, ny = 0, tx = 0, ty = 0;

	start_log();
	snprintf(dnd_log[0], sizeof dnd_log[0], "%s.dnd0", log_path);
	snprintf(dnd_log[1], sizeof dnd_log[1], "%s.dnd1", log_path);
	setenv("POLO_XWAYLAND", "eager", 1);
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &data_registry_listener, NULL);
	harness_roundtrip(h);
	struct wl_data_device *device =
		wl_data_device_manager_get_data_device(data_manager, h->seat);

	wl_data_device_add_listener(device, &device_listener, NULL);
	wait_xwayland(h, true);
	pause_ms(h, 500);
	int display = x_display(h);

	find_xauth(h, xauth, sizeof xauth);
	struct harness_window *native =
		harness_window_create(h, 200, 150, "native");

	pause_ms(h, 200);
	native_centre(h, native, &nx, &ny);

	/* Native to X. */
	fresh_log(0);
	pid_t target =
		start_x_dnd(display, dnd, xauth, "target", dnd_log[0], false);

	x_dnd_centre(h, dnd_log[0], false, &tx, &ty);
	native_drag_to(h, device, native, nx, ny, tx, ty);
	wait_for_file(h, dnd_log[0], "enter");
	wait_for_file(h, dnd_log[0], "position");
	pause_ms(h, 200);
	CHECK(source_target_seen,
	      "the X target's accept did not reach the source");
	harness_button(h, BTN_LEFT, false);
	wait_for_file(h, dnd_log[0], "drop 1000");
	for (int i = 0; i < 100 && !source_finished; i++)
		pause_ms(h, 20);
	CHECK(source_drop_performed, "no dnd_drop_performed");
	CHECK(source_finished, "no dnd_finished after XdndFinished");
	CHECK(!file_has(dnd_log[0], "leave"), "XdndLeave after a drop");
	stop_client(target);
	pause_ms(h, 200);

	/* Native to X, refused. */
	fresh_log(0);
	target = start_x_dnd(display, dnd, xauth, "target", dnd_log[0], true);
	x_dnd_centre(h, dnd_log[0], false, &tx, &ty);
	native_drag_to(h, device, native, nx, ny, tx, ty);
	wait_for_file(h, dnd_log[0], "position");
	pause_ms(h, 200);
	CHECK(!source_target_seen, "a refusal was reported as an accept");
	harness_button(h, BTN_LEFT, false);
	for (int i = 0; i < 100 && !source_was_cancelled; i++)
		pause_ms(h, 20);
	CHECK(source_was_cancelled, "a refused drop was not cancelled");
	CHECK(file_has(dnd_log[0], "leave"), "no XdndLeave for a refused drop");
	CHECK(!file_has(dnd_log[0], "drop"), "a refused drop was dropped");
	stop_client(target);
	pause_ms(h, 200);

	/* X to native. */
	fresh_log(0);
	pid_t source =
		start_x_dnd(display, dnd, xauth, "source", dnd_log[0], false);
	int sx = 0, sy = 0;

	x_dnd_centre(h, dnd_log[0], true, &sx, &sy);
	harness_move_pointer(h, sx - 2, sy);
	pause_ms(h, 100);
	harness_move_pointer(h, sx, sy);
	pause_ms(h, 100);
	harness_button(h, BTN_LEFT, true);
	pause_ms(h, 200);
	drag_enters = 0;
	drag_dropped = false;
	glide(h, sx, sy, nx, ny);
	for (int i = 0; i < 100 && !drag_enters; i++)
		pause_ms(h, 20);
	CHECK(drag_enters > 0, "the X drag never reached the native window");
	wait_for_file(h, dnd_log[0], "status 1");
	harness_button(h, BTN_LEFT, false);
	for (int i = 0; i < 100 && !drag_dropped; i++)
		pause_ms(h, 20);
	CHECK(drag_dropped, "the native window saw no drop");
	CHECK(drag_offer, "no offer to read the drop from");
	selection_offer = drag_offer;
	CHECK(native_paste(h, "text/plain;charset=utf-8", 0) == 1000,
	      "the dropped bytes are wrong");
	selection_offer = NULL;
	wl_data_offer_finish(drag_offer);
	wl_data_offer_destroy(drag_offer);
	drag_offer = NULL;
	drag_dropped = false;
	wait_for_file(h, dnd_log[0], "finished 1");

	/* X to X, and back over a native window on the way. */
	fresh_log(1);
	target = start_x_dnd(display, dnd, xauth, "target", dnd_log[1], false);
	x_dnd_centre(h, dnd_log[1], false, &tx, &ty);
	x_dnd_centre(h, dnd_log[0], true, &sx, &sy);
	harness_move_pointer(h, sx - 2, sy);
	pause_ms(h, 100);
	harness_move_pointer(h, sx, sy);
	pause_ms(h, 100);
	harness_button(h, BTN_LEFT, true);
	pause_ms(h, 200);
	glide(h, sx, sy, nx, ny);
	pause_ms(h, 100);
	int enters_over_native = drag_enters;

	glide(h, nx, ny, tx, ty);
	wait_for_file(h, dnd_log[1], "position");
	pause_ms(h, 100);
	harness_button(h, BTN_LEFT, false);
	wait_for_file(h, dnd_log[1], "drop 1000");
	CHECK(enters_over_native > 0, "no native enter on the way across");
	CHECK(!drag_dropped, "the X-to-X drop reached the native window");
	stop_client(target);
	stop_client(source);
	pause_ms(h, 200);

	if (drag_offer)
		wl_data_offer_destroy(drag_offer);
	drag_offer = NULL;
	harness_window_destroy(native);
	wl_data_device_destroy(device);
	wl_data_device_manager_destroy(data_manager);
	data_manager = NULL;
	wl_registry_destroy(registry);
	unlink(dnd_log[0]);
	unlink(dnd_log[1]);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

/* Everything else off unless asked. */
static void
test_hidpi(int argc, char **argv)
{
	char xauth[512];
	int x = 0, y = 0, at_x = -1, at_y = -1;

	setenv("POLO_XWAYLAND", "lazy", 1);
	setenv("POLO_TEST_SCALE", "--scale=2", 1);
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	unsetenv("POLO_TEST_SCALE");
	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	/* The cursor has white pixels too. */
	harness_move_pointer(h, 1020, 764);
	pause_ms(h, 200);
	int display = x_display(h);

	find_xauth(h, xauth, sizeof xauth);
	start_log();
	pid_t client = start_x_client(display, argv[2], true, xauth);

	wait_for_line(h, "focus");
	CHECK(log_has("screen 1024x768"),
	      "X does not see the output's logical size");
	for (int i = 0; i < 100 && !last_position(&at_x, &at_y); i++)
		pause_ms(h, 20);
	pause_ms(h, 300);
	CHECK(last_position(&at_x, &at_y),
	      "the X client was never told its position");
	int whites = white_pixels(screenshot(h), &x, &y);

	CHECK(cap_width == 2048 && cap_height == 1536, "the output is %dx%d",
	      cap_width, cap_height);
	CHECK(whites >= 600 * 400 * 9 / 10 && whites <= 600 * 400 + 400,
	      "a 300x200 X window covers %d pixels at scale 2", whites);
	CHECK(x == at_x * 2 && y == at_y * 2,
	      "the X client was told %d,%d and is drawn at %d,%d", at_x, at_y,
	      x, y);

	harness_move_pointer(h, at_x + 310, at_y + 100);
	pause_ms(h, 200);
	harness_move_pointer(h, at_x + 305, at_y + 100);
	pause_ms(h, 200);
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	pause_ms(h, 300);
	CHECK(!log_has("button 1"), "a click right of the window reached it");
	harness_move_pointer(h, at_x + 290, at_y + 190);
	pause_ms(h, 200);
	harness_move_pointer(h, at_x + 295, at_y + 195);
	pause_ms(h, 200);
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	wait_for_line(h, "button 1");

	press(h, KEY_LEFTALT, KEY_F10);
	wait_for_line(h, "size 1024x768");
	pause_ms(h, 300);
	whites = white_pixels(screenshot(h), &x, &y);
	CHECK(whites >= 2048 * 1536 * 9 / 10, "not maximized on screen: %d",
	      whites);

	stop_client(client);
	weston_capture_v1_destroy(capture);
	wl_registry_destroy(registry);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

static bool
x_maximized_by_alt_f10(struct harness *h)
{
	press(h, KEY_LEFTALT, KEY_F10);
	pause_ms(h, 500);
	bool maximized = log_has("size 1024x768");

	if (maximized) {
		press(h, KEY_LEFTALT, KEY_F10);
		pause_ms(h, 500);
	}
	return maximized;
}

static void
escape_hatch(struct harness *h)
{
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_LEFTSHIFT, true);
	harness_key(h, KEY_ESC, true);
	harness_key(h, KEY_ESC, false);
	harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	pause_ms(h, 200);
}

/* KEY_F1 is X keycode 67. */
static struct zwp_primary_selection_device_manager_v1 *primary_manager;
static struct zwp_primary_selection_offer_v1 *primary_offer;
static bool primary_cancelled;

static void
primary_global(void *data, struct wl_registry *registry, uint32_t name,
	       const char *interface, uint32_t version)
{
	if (strcmp(interface,
		   zwp_primary_selection_device_manager_v1_interface.name) == 0)
		primary_manager = wl_registry_bind(
			registry, name,
			&zwp_primary_selection_device_manager_v1_interface, 1);
}

static const struct wl_registry_listener primary_registry_listener = {
	primary_global,
	registry_remove,
};

static void
primary_data_offer(void *data, struct zwp_primary_selection_device_v1 *device,
		   struct zwp_primary_selection_offer_v1 *offer)
{
}

static void
primary_selection(void *data, struct zwp_primary_selection_device_v1 *device,
		  struct zwp_primary_selection_offer_v1 *offer)
{
	if (primary_offer)
		zwp_primary_selection_offer_v1_destroy(primary_offer);
	primary_offer = offer;
}

static const struct zwp_primary_selection_device_v1_listener primary_device_listener = {
	primary_data_offer,
	primary_selection,
};

static void
primary_send(void *data, struct zwp_primary_selection_source_v1 *source,
	     const char *mime, int fd)
{
	source_send(NULL, NULL, mime, fd);
}

static void
primary_source_cancelled(void *data,
			 struct zwp_primary_selection_source_v1 *source)
{
	primary_cancelled = true;
	zwp_primary_selection_source_v1_destroy(source);
}

static const struct zwp_primary_selection_source_v1_listener primary_source_listener = {
	primary_send,
	primary_source_cancelled,
};

static long
primary_paste(struct harness *h, const char *mime)
{
	int fds[2];
	char chunk[65536];
	long total = 0;
	ssize_t n;

	CHECK(primary_offer, "no primary selection on offer");
	CHECK(pipe(fds) == 0, "pipe");
	zwp_primary_selection_offer_v1_receive(primary_offer, mime, fds[1]);
	wl_display_flush(h->display);
	close(fds[1]);
	while ((n = read(fds[0], chunk, sizeof chunk)) > 0) {
		for (ssize_t i = 0; i < n; i++)
			if (chunk[i] != 'a' + (total + i) % 26)
				total = -1;
		if (total < 0)
			break;
		total += n;
	}
	close(fds[0]);
	return total;
}

static void
test_primary(int argc, char **argv)
{
	char xauth[512];
	const char *clip = argv[5];

	setenv("POLO_XWAYLAND", "eager", 1);
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &primary_registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(primary_manager, "no primary selection manager");
	struct zwp_primary_selection_device_v1 *device =
		zwp_primary_selection_device_manager_v1_get_device(primary_manager,
								   h->seat);

	zwp_primary_selection_device_v1_add_listener(
		device, &primary_device_listener, NULL);
	wait_xwayland(h, true);
	pause_ms(h, 500);
	int display = x_display(h);

	find_xauth(h, xauth, sizeof xauth);
	struct harness_window *native =
		harness_window_create(h, 200, 150, "native");
	pause_ms(h, 200);
	setenv("X_CLIP_SELECTION", "PRIMARY", 1);

	/* Native to X, including INCR. */
	struct zwp_primary_selection_source_v1 *source =
		zwp_primary_selection_device_manager_v1_create_source(primary_manager);

	zwp_primary_selection_source_v1_add_listener(
		source, &primary_source_listener, NULL);
	zwp_primary_selection_source_v1_offer(source, "text/plain;charset=utf-8");
	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	harness_roundtrip(h);
	zwp_primary_selection_device_v1_set_selection(device, source,
						      h->last_keyboard_serial);
	pause_ms(h, 200);
	source_bytes = 1000;
	CHECK(x_clip(h, display, clip, xauth, "paste UTF8_STRING 1000") == 0,
	      "X did not paste the native primary selection");
	CHECK(x_clip(h, display, clip, xauth, "targets UTF8_STRING") == 0,
	      "the native primary selection has no UTF8_STRING target");
	source_bytes = 1048576;
	CHECK(x_clip(h, display, clip, xauth, "paste UTF8_STRING 1048576") == 0,
	      "X did not paste a large native primary selection");
	unsetenv("X_CLIP_SELECTION");
	CHECK(x_clip(h, display, clip, xauth, "paste UTF8_STRING 1000") != 0,
	      "the native primary selection reached CLIPBOARD");
	setenv("X_CLIP_SELECTION", "PRIMARY", 1);

	/* X to native; taking it over cancels the native source. */
	pid_t owner = x_clip_owner(h, display, clip, xauth, "UTF8_STRING", "1000");

	for (int i = 0; i < 100 && !primary_cancelled; i++)
		pause_ms(h, 20);
	CHECK(primary_cancelled, "the native source was not cancelled");
	for (int i = 0; i < 100 && !primary_offer; i++)
		pause_ms(h, 20);
	CHECK(primary_paste(h, "text/plain;charset=utf-8") == 1000,
	      "native did not paste the X primary selection");
	kill(owner, SIGKILL);
	waitpid(owner, NULL, 0);

	/* X to native, large. */
	owner = x_clip_owner(h, display, clip, xauth, "UTF8_STRING", "1048576");
	pause_ms(h, 400);
	CHECK(primary_paste(h, "text/plain;charset=utf-8") == 1048576,
	      "native did not paste a large X primary selection");
	kill(owner, SIGKILL);
	waitpid(owner, NULL, 0);
	for (int i = 0; i < 100 && primary_offer; i++)
		pause_ms(h, 20);
	CHECK(!primary_offer,
	      "the primary selection stayed after its X owner exited");

	unsetenv("X_CLIP_SELECTION");
	harness_window_destroy(native);
	zwp_primary_selection_device_v1_destroy(device);
	zwp_primary_selection_device_manager_v1_destroy(primary_manager);
	primary_manager = NULL;
	wl_registry_destroy(registry);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

static void
test_keyboard_grab(int argc, char **argv)
{
	char xauth[512];

	setenv("POLO_XWAYLAND", "lazy", 1);
	setenv("X_TEST_GRAB_ON_KEY", "67", 1);
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	saw_x_keyboard_grab = false;
	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	pause_ms(h, 200);
	CHECK(!saw_x_keyboard_grab,
	      "a client other than Xwayland sees the X keyboard grab");
	int display = x_display(h);

	find_xauth(h, xauth, sizeof xauth);
	struct harness_window *native =
		harness_window_create(h, 900, 700, "native");

	pause_ms(h, 200);
	start_log();
	pid_t client = start_x_client(display, argv[2], true, xauth);

	wait_for_line(h, "focus");
	pause_ms(h, 300);

	/* Granted: Polo's shortcuts stop and the keys reach the client. */
	harness_key(h, KEY_F1, true);
	harness_key(h, KEY_F1, false);
	wait_for_line(h, "grabbed");
	pause_ms(h, 300);
	CHECK(!x_maximized_by_alt_f10(h),
	      "Polo's shortcut ran during a granted grab");
	wait_for_line(h, "key 76");

	/* The escape hatch ends it, though the X grab goes on. */
	escape_hatch(h);
	CHECK(x_maximized_by_alt_f10(h),
	      "the escape hatch did not give the shortcuts back");
	stop_client(client);
	pause_ms(h, 300);

	/* The X client letting go gives them back. */
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	wait_for_line(h, "focus");
	pause_ms(h, 300);
	harness_key(h, KEY_F1, true);
	harness_key(h, KEY_F1, false);
	wait_for_line(h, "grabbed");
	pause_ms(h, 300);
	harness_key(h, KEY_F1, true);
	harness_key(h, KEY_F1, false);
	pause_ms(h, 300);
	CHECK(x_maximized_by_alt_f10(h),
	      "the shortcuts did not come back after the ungrab");
	stop_client(client);
	pause_ms(h, 300);

	/* Focus moving to a native window ends it. */
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	wait_for_line(h, "focus");
	pause_ms(h, 300);
	harness_key(h, KEY_F1, true);
	harness_key(h, KEY_F1, false);
	wait_for_line(h, "grabbed");
	pause_ms(h, 300);
	harness_move_pointer(h, harness_window_info(native)->x + 5,
			     harness_window_info(native)->y + 5);
	pause_ms(h, 100);
	harness_move_pointer(h, harness_window_info(native)->x + 8,
			     harness_window_info(native)->y + 8);
	pause_ms(h, 100);
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	pause_ms(h, 300);
	CHECK(harness_window_info(native)->focused,
	      "the click did not focus the native window");
	press(h, KEY_LEFTALT, KEY_TAB);
	pause_ms(h, 400);
	CHECK(!harness_window_info(native)->focused,
	      "Alt-Tab did nothing after focus left the grabbing window");
	stop_client(client);
	pause_ms(h, 300);

	/* Refused: the keyboard is on a native window when the grab comes. */
	setenv("X_TEST_GRAB_DELAY", "2", 1);
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	unsetenv("X_TEST_GRAB_DELAY");
	wait_for_line(h, "focus");
	pause_ms(h, 300);
	harness_key(h, KEY_F1, true);
	harness_key(h, KEY_F1, false);
	pause_ms(h, 300);
	harness_move_pointer(h, harness_window_info(native)->x + 5,
			     harness_window_info(native)->y + 5);
	pause_ms(h, 100);
	harness_move_pointer(h, harness_window_info(native)->x + 8,
			     harness_window_info(native)->y + 8);
	pause_ms(h, 100);
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	pause_ms(h, 300);
	CHECK(harness_window_info(native)->focused,
	      "the click did not focus the native window");
	wait_for_line(h, "grabbed");
	pause_ms(h, 300);
	harness_window_destroy(native);
	pause_ms(h, 400);
	CHECK(x_maximized_by_alt_f10(h),
	      "a grab from an unfocused X window stopped the shortcuts");
	stop_client(client);
	pause_ms(h, 300);

	/* Refused: no key or button for over five seconds before the grab. */
	setenv("X_TEST_GRAB_DELAY", "6", 1);
	start_log();
	client = start_x_client(display, argv[2], true, xauth);
	unsetenv("X_TEST_GRAB_DELAY");
	wait_for_line(h, "focus");
	pause_ms(h, 300);
	harness_key(h, KEY_F1, true);
	harness_key(h, KEY_F1, false);
	wait_for_line(h, "grabbed");
	pause_ms(h, 300);
	CHECK(x_maximized_by_alt_f10(h),
	      "a grab with no recent user action stopped the shortcuts");
	stop_client(client);

	unsetenv("X_TEST_GRAB_ON_KEY");
	weston_capture_v1_destroy(capture);
	wl_registry_destroy(registry);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
}

static void
test_off(int argc, char **argv)
{
	for (int named = 0; named < 2; named++) {
		if (named)
			setenv("POLO_XWAYLAND", "off", 1);
		else
			unsetenv("POLO_XWAYLAND");
		struct harness *h = harness_start(argc, argv);

		pause_ms(h, 200);
		CHECK(x_display(h) < 0,
		      "no XWayland was asked for, yet Polo took an X display");
		CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	}
}

int
main(int argc, char **argv)
{
	CHECK(argc >= 7,
	      "usage: %s POLO X-TEST-CLIENT FAKE-SHELL-UI X-SEND X-CLIP X-DND",
	      argv[0]);
	if (system("command -v Xwayland >/dev/null") != 0)
		return 77;

	setenv("X_TEST_NO_HINTS", "1", 1);
	test_off(argc, argv);
	test_lazy_and_auth(argc, argv);
	test_eager_and_crash(argc, argv);
	test_policy(argc, argv);
	test_icccm(argc, argv);
	test_ewmh_root(argc, argv);
	test_ewmh_messages(argc, argv);
	test_motif(argc, argv);
	test_focus_models(argc, argv);
	test_clipboard(argc, argv);
	test_dnd(argc, argv);
	test_hidpi(argc, argv);
	test_primary(argc, argv);
	test_keyboard_grab(argc, argv);

	setenv("POLO_XWAYLAND", "lazy", 1);
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(capture, "no weston_capture_v1");
	pause_ms(h, 100);

	int display = x_display(h);

	CHECK(display >= 0, "Polo did not take an X display");
	char xauth[512];

	find_xauth(h, xauth, sizeof xauth);
	start_log();
	/* The cursor has white pixels too. */
	harness_move_pointer(h, 1020, 764);
	uint32_t *before = screenshot(h);
	int x = 0, y = 0;

	CHECK(white_pixels(before, &x, &y) <= 400, "white on an empty desktop");

	pid_t client = start_x_client(display, argv[2], true, xauth);

	wait_for_line(h, "focus");
	pause_ms(h, 300);

	uint32_t *shown = screenshot(h);
	int whites = white_pixels(shown, &x, &y);

	CHECK(whites >= 300 * 200 * 9 / 10,
	      "only %d white pixels: the X window is not on screen", whites);

	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	wait_for_line(h, "key 38");

	harness_move_pointer(h, x + 20, y + 20);
	pause_ms(h, 300);
	harness_move_pointer(h, x + 25, y + 25);
	pause_ms(h, 300);
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	wait_for_line(h, "button 1");

	kill(client, SIGTERM);
	waitpid(client, NULL, 0);
	int left = whites;

	/* The cursor has white pixels of its own. */
	for (int i = 0; i < 40 && left > 400; i++) {
		pause_ms(h, 100);
		left = white_pixels(screenshot(h), &x, &y);
	}
	CHECK(left <= 400, "%d white pixels stayed after the X client went",
	      left);

	CHECK(kill(h->pid, 0) == 0, "Polo died");
	unlink(log_path);
	weston_capture_v1_destroy(capture);
	wl_registry_destroy(registry);
	return harness_stop(h);
}
