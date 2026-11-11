/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
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
#include "weston-output-capture-client-protocol.h"

static struct harness *current;

void
harness_fail(const char *file, int line, const char *cond, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s:%d: check failed: %s: ", file, line, cond);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);

	if (current && current->pid > 0) {
		kill(current->pid, SIGKILL);
		waitpid(current->pid, NULL, 0);
	}
	exit(1);
}

static void
sleep_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

/* ---- Input listeners ---------------------------------------------------- */

static void
pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
	      struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
	struct harness *h = data;

	h->pointer_focus = surface;
	h->pointer_enter_serial = serial;
}

static void
pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
	      struct wl_surface *surface)
{
	struct harness *h = data;

	h->pointer_focus = NULL;
}

static void
pointer_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x,
	       wl_fixed_t y)
{
	struct harness *h = data;

	h->motions++;
}

static void
pointer_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
	       uint32_t button, uint32_t state)
{
	struct harness *h = data;

	h->last_button_serial = serial;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED)
		h->button_presses++;
	else
		h->button_releases++;
}

static void
pointer_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis,
	     wl_fixed_t value)
{
}

static void
pointer_frame(void *data, struct wl_pointer *p)
{
}

static void
pointer_axis_source(void *data, struct wl_pointer *p, uint32_t source)
{
}

static void
pointer_axis_stop(void *data, struct wl_pointer *p, uint32_t time,
		  uint32_t axis)
{
}

static void
pointer_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis,
		      int32_t discrete)
{
}

static void
pointer_axis_value120(void *data, struct wl_pointer *p, uint32_t axis,
		      int32_t value120)
{
}

static void
pointer_axis_relative_direction(void *data, struct wl_pointer *p, uint32_t axis,
				uint32_t direction)
{
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
	.axis_value120 = pointer_axis_value120,
	.axis_relative_direction = pointer_axis_relative_direction,
};

static void
keyboard_keymap(void *data, struct wl_keyboard *k, uint32_t format, int fd,
		uint32_t size)
{
	struct harness *h = data;

	h->keymaps++;
	close(fd);
}

static void
keyboard_enter(void *data, struct wl_keyboard *k, uint32_t serial,
	       struct wl_surface *surface, struct wl_array *keys)
{
	struct harness *h = data;
	uint32_t *key;

	h->keyboard_focus = surface;
	h->keyboard_enters++;
	h->last_keyboard_serial = serial;
	h->n_enter_keys = 0;
	wl_array_for_each(key, keys)
	{
		if (h->n_enter_keys < 32)
			h->enter_keys[h->n_enter_keys++] = *key;
	}
}

static void
keyboard_leave(void *data, struct wl_keyboard *k, uint32_t serial,
	       struct wl_surface *surface)
{
	struct harness *h = data;

	if (h->keyboard_focus == surface)
		h->keyboard_focus = NULL;
}

static void
keyboard_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time,
	     uint32_t key, uint32_t state)
{
	struct harness *h = data;

	h->last_keyboard_serial = serial;
	h->last_key = key;
	h->last_key_pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
	h->last_key_surface = h->keyboard_focus;
	if (h->last_key_pressed)
		h->key_mods =
			h->mods_depressed | h->mods_latched | h->mods_locked;
}

static void
keyboard_modifiers(void *data, struct wl_keyboard *k, uint32_t serial,
		   uint32_t depressed, uint32_t latched, uint32_t locked,
		   uint32_t group)
{
	struct harness *h = data;

	h->mods_locked = locked;
	h->mods_depressed = depressed;
	h->mods_latched = latched;
	h->group = group;
}

static void
keyboard_repeat_info(void *data, struct wl_keyboard *k, int32_t rate,
		     int32_t delay)
{
	struct harness *h = data;

	h->repeat_rate = rate;
	h->repeat_delay = delay;
	h->repeat_infos++;
}

static const struct wl_keyboard_listener keyboard_listener = {
	keyboard_keymap, keyboard_enter,     keyboard_leave,
	keyboard_key,    keyboard_modifiers, keyboard_repeat_info,
};

static void
seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
	struct harness *h = data;

	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !h->pointer) {
		h->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(h->pointer, &pointer_listener, h);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !h->keyboard) {
		h->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(h->keyboard, &keyboard_listener, h);
	}
}

static void
seat_name(void *data, struct wl_seat *seat, const char *name)
{
}

static const struct wl_seat_listener seat_listener = {
	seat_capabilities,
	seat_name,
};

/* ---- Globals ------------------------------------------------------------ */

static void
wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
	struct harness *h = data;

	if (!h->ignore_pings)
		xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	wm_base_ping,
};

static void
test_window_info(void *data, struct polo_test_v1 *test,
		 struct wl_surface *surface, int32_t x, int32_t y,
		 int32_t width, int32_t height, int32_t layer,
		 int32_t stack_index, int32_t workspace, uint32_t state,
		 uint32_t focused)
{
	struct harness_window *w = wl_surface_get_user_data(surface);

	w->info = (struct harness_window_info){
		.known = true,
		.x = x,
		.y = y,
		.width = width,
		.height = height,
		.layer = layer,
		.stack_index = stack_index,
		.workspace = workspace,
		.state = state,
		.focused = focused != 0,
	};
}

static void
test_window_unknown(void *data, struct polo_test_v1 *test,
		    struct wl_surface *surface)
{
	struct harness_window *w = wl_surface_get_user_data(surface);

	w->info = (struct harness_window_info){ .known = false };
}

static void
test_layer_surface_info(void *data, struct polo_test_v1 *test,
			struct wl_surface *surface, int32_t x, int32_t y,
			int32_t width, int32_t height, int32_t layer,
			uint32_t mapped)
{
	struct harness *h = data;

	h->layer_info = (struct harness_layer_info){
		.known = true,
		.x = x,
		.y = y,
		.width = width,
		.height = height,
		.layer = layer,
		.mapped = mapped != 0,
	};
}

static void
test_layer_surface_unknown(void *data, struct polo_test_v1 *test,
			   struct wl_surface *surface)
{
	struct harness *h = data;

	h->layer_info = (struct harness_layer_info){ .known = false };
}

static void
test_setting(void *data, struct polo_test_v1 *test, const char *schema,
	     const char *key, const char *value)
{
	struct harness *h = data;

	snprintf(h->setting, sizeof h->setting, "%s", value);
}

static void
test_shell_ui_bound(void *data, struct polo_test_v1 *test, int32_t pid)
{
	struct harness *h = data;

	h->shell_ui_pid = pid;
	h->shell_ui_binds++;
}

static void
test_shell_ui_unbound(void *data, struct polo_test_v1 *test)
{
	struct harness *h = data;

	h->shell_ui_pid = 0;
	h->shell_ui_unbinds++;
}

static void
test_shell_ui_request(void *data, struct polo_test_v1 *test, const char *text)
{
	struct harness *h = data;

	CHECK(h->n_shell_ui_requests < 16, "too many shell-ui requests");
	snprintf(h->shell_ui_requests[h->n_shell_ui_requests++],
		 sizeof h->shell_ui_requests[0], "%s", text);
}

static void
test_lamp_target(void *data, struct polo_test_v1 *test,
		 struct wl_surface *surface, int32_t x, int32_t y,
		 int32_t width, int32_t height, uint32_t edge, uint32_t source)
{
	struct harness_window *w = wl_surface_get_user_data(surface);

	w->lamp = (struct harness_lamp_target){ .known = true,
						.x = x,
						.y = y,
						.width = width,
						.height = height,
						.edge = edge,
						.source = source };
}

static void
test_pointer_position(void *data, struct polo_test_v1 *test, int32_t x,
		      int32_t y)
{
	struct harness *h = data;
	h->pointer_x = x;
	h->pointer_y = y;
}

static void
test_output_state(void *data, struct polo_test_v1 *test, const char *name,
		  uint32_t enabled, int32_t x, int32_t y, int32_t width,
		  int32_t height, int32_t scale, uint32_t primary,
		  uint32_t refresh, int32_t area_x, int32_t area_y,
		  int32_t area_width, int32_t area_height)
{
	struct harness *h = data;
	struct harness_output_state *o;

	CHECK(h->n_output_states < 16, "too many outputs");
	o = &h->output_states[h->n_output_states++];
	snprintf(o->name, sizeof o->name, "%s", name);
	o->enabled = enabled != 0;
	o->x = x;
	o->y = y;
	o->width = width;
	o->height = height;
	o->scale = scale;
	o->primary = primary != 0;
	o->refresh = refresh;
	o->area_x = area_x;
	o->area_y = area_y;
	o->area_width = area_width;
	o->area_height = area_height;
}

static void
test_outputs_done(void *data, struct polo_test_v1 *test, uint32_t monitors)
{
	struct harness *h = data;

	h->monitors = monitors;
	h->outputs_done = true;
}

static const struct polo_test_v1_listener test_listener = {
	test_window_info,
	test_window_unknown,
	test_layer_surface_info,
	test_layer_surface_unknown,
	test_setting,
	test_shell_ui_bound,
	test_shell_ui_unbound,
	test_shell_ui_request,
	test_lamp_target,
	test_pointer_position,
	test_output_state,
	test_outputs_done,
};

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	struct harness *h = data;

	if (strcmp(interface, "polo_shell_ui_v1") == 0)
		h->saw_shell_ui_global = true;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		h->compositor = wl_registry_bind(registry, name,
						 &wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		h->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface,
			  zxdg_decoration_manager_v1_interface.name) == 0) {
		h->decoration_manager = wl_registry_bind(
			registry, name, &zxdg_decoration_manager_v1_interface,
			1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		/* 3+ for xdg_positioner (popup tests). */
		h->wm_base =
			wl_registry_bind(registry, name, &xdg_wm_base_interface,
					 version < 3 ? version : 3);
		xdg_wm_base_add_listener(h->wm_base, &wm_base_listener, h);
	} else if (strcmp(interface, wl_seat_interface.name) == 0 && !h->seat) {
		h->seat = wl_registry_bind(registry, name, &wl_seat_interface,
					   version < 7 ? version : 7);
		wl_seat_add_listener(h->seat, &seat_listener, h);
	} else if (strcmp(interface, polo_test_v1_interface.name) == 0) {
		h->test = wl_registry_bind(registry, name,
					   &polo_test_v1_interface, 14);
		polo_test_v1_add_listener(h->test, &test_listener, h);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		h->layer_shell = wl_registry_bind(
			registry, name, &zwlr_layer_shell_v1_interface,
			version < 5 ? version : 5);
	} else if (strcmp(interface, weston_capture_v1_interface.name) == 0) {
		h->capture = wl_registry_bind(registry, name,
					      &weston_capture_v1_interface, 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		if (h->n_outputs < 8) {
			h->output_globals[h->n_outputs] = name;
			h->outputs[h->n_outputs++] = wl_registry_bind(
				registry, name, &wl_output_interface,
				version < 4 ? version : 4);
		}
	}
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	struct harness *h = data;

	h->globals_removed++;
	for (int i = 0; i < h->n_outputs; i++) {
		if (h->output_globals[i] != name)
			continue;
		wl_output_release(h->outputs[i]);
		h->n_outputs--;
		memmove(&h->outputs[i], &h->outputs[i + 1],
			(size_t)(h->n_outputs - i) * sizeof h->outputs[0]);
		memmove(&h->output_globals[i], &h->output_globals[i + 1],
			(size_t)(h->n_outputs - i) *
				sizeof h->output_globals[0]);
		break;
	}
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove,
};

/* ---- Start and stop ----------------------------------------------------- */

struct harness *
harness_start(int argc, char **argv)
{
	struct harness *h = calloc(1, sizeof *h);
	const char *polo;
	int i;

	CHECK(argc >= 2, "usage: %s POLO", argv[0]);
	polo = argv[1];
	current = h;

	snprintf(h->runtime_dir, sizeof h->runtime_dir, "%s/polo-test-XXXXXX",
		 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	CHECK(mkdtemp(h->runtime_dir) != NULL, "mkdtemp: %s", strerror(errno));
	snprintf(h->socket, sizeof h->socket, "polo-test-%d", (int)getpid());
	setenv("XDG_RUNTIME_DIR", h->runtime_dir, 1);
	unsetenv("WAYLAND_DISPLAY");
	unsetenv("WAYLAND_SOCKET");

	/* Polo saves monitors.xml and reads other files from there: never
	 * the user's own. A test that restarts Polo can name one directory. */
	snprintf(h->config_dir, sizeof h->config_dir, "%s/config",
		 h->runtime_dir);
	if (getenv("POLO_TEST_CONFIG_HOME"))
		snprintf(h->config_dir, sizeof h->config_dir, "%s",
			 getenv("POLO_TEST_CONFIG_HOME"));
	mkdir(h->config_dir, 0700);

	h->pid = fork();
	CHECK(h->pid >= 0, "fork: %s", strerror(errno));
	if (h->pid == 0) {
		char socket_arg[96];

		snprintf(socket_arg, sizeof socket_arg, "--socket=%s",
			 h->socket);
		setenv("XDG_CONFIG_HOME", h->config_dir, 1);
		/* POLO_TEST_DEBUG=scope,scope turns on Polo's debug output. */
		const char *debug = getenv("POLO_TEST_DEBUG");
		char debug_arg[256], exec_arg[1024], log_arg[512];
		const char *scale_arg = getenv("POLO_TEST_SCALE")
			? getenv("POLO_TEST_SCALE")
			: "--scale=1";

		snprintf(exec_arg, sizeof exec_arg, "--exec=%s",
			 getenv("POLO_TEST_EXEC") ? getenv("POLO_TEST_EXEC")
						  : "");
		snprintf(debug_arg, sizeof debug_arg, "--debug-scopes=%s",
			 debug ? debug : "");
		/* POLO_TEST_LOG=PATH: Polo's log goes to that file. */
		snprintf(log_arg, sizeof log_arg, "--log=%s",
			 getenv("POLO_TEST_LOG") ? getenv("POLO_TEST_LOG")
						 : "");
		execl(polo, polo, "--backend=headless",
		      getenv("POLO_TEST_RENDERER")
			      ? getenv("POLO_TEST_RENDERER")
			      : "--renderer=pixman",
		      "--test-mode", socket_arg,
		      getenv("POLO_TEST_WIDTH") ? getenv("POLO_TEST_WIDTH")
						: "--width=1024",
		      getenv("POLO_TEST_HEIGHT") ? getenv("POLO_TEST_HEIGHT")
						 : "--height=768",
		      debug_arg,
		      getenv("POLO_TEST_TWO_OUTPUTS") ? "--outputs=2"
						      : "--outputs=1",
		      getenv("POLO_TEST_KILL") ? "--debug-kill" : scale_arg,
		      getenv("POLO_TEST_EXEC") ? exec_arg : scale_arg,
		      getenv("POLO_TEST_LOG") ? log_arg : scale_arg,
		      (char *)NULL);
		_exit(127);
	}

	for (i = 0; i < 600 && !h->display; i++) {
		int status;

		h->display = wl_display_connect(h->socket);
		if (h->display)
			break;
		/* Report a start-up crash as one, not as a missing socket. */
		if (waitpid(h->pid, &status, WNOHANG) == h->pid) {
			h->pid = 0;
			CHECK(0, "polo died during start-up: %s %d",
			      WIFSIGNALED(status) ? "signal" : "exit status",
			      WIFSIGNALED(status) ? WTERMSIG(status)
						  : WEXITSTATUS(status));
		}
		sleep_ms(25);
	}
	CHECK(h->display != NULL, "cannot connect to %s", h->socket);

	h->registry = wl_display_get_registry(h->display);
	wl_registry_add_listener(h->registry, &registry_listener, h);
	harness_roundtrip(h);
	harness_roundtrip(h);

	CHECK(h->compositor && h->shm && h->wm_base && h->seat,
	      "missing core globals");
	CHECK(h->test != NULL, "polo_test_v1 not advertised");
	CHECK(h->layer_shell != NULL || getenv("POLO_TEST_TRUST"),
	      "zwlr_layer_shell_v1 not advertised");
	CHECK(h->pointer && h->keyboard, "seat lacks pointer or keyboard");
	return h;
}

int
harness_stop(struct harness *h)
{
	int status = 0;
	char cmd[300];
	int i;

	if (h->test)
		polo_test_v1_destroy(h->test);
	if (h->pointer)
		wl_pointer_release(h->pointer);
	if (h->keyboard)
		wl_keyboard_release(h->keyboard);
	wl_seat_release(h->seat);
	xdg_wm_base_destroy(h->wm_base);
	if (h->decoration_manager)
		zxdg_decoration_manager_v1_destroy(h->decoration_manager);
	if (h->layer_shell)
		zwlr_layer_shell_v1_destroy(h->layer_shell);
	if (h->capture)
		weston_capture_v1_destroy(h->capture);
	for (i = 0; i < h->n_outputs; i++)
		wl_output_release(h->outputs[i]);
	wl_shm_destroy(h->shm);
	wl_compositor_destroy(h->compositor);
	wl_registry_destroy(h->registry);
	wl_display_roundtrip(h->display);
	wl_display_disconnect(h->display);

	kill(h->pid, SIGTERM);
	CHECK(waitpid(h->pid, &status, 0) == h->pid, "waitpid: %s",
	      strerror(errno));

	snprintf(cmd, sizeof cmd, "rm -rf '%s'", h->runtime_dir);
	if (system(cmd) != 0)
		fprintf(stderr, "could not remove %s\n", h->runtime_dir);

	current = NULL;
	free(h);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void
harness_roundtrip(struct harness *h)
{
	if (wl_display_roundtrip(h->display) < 0) {
		const struct wl_interface *iface = NULL;
		uint32_t code = 0;

		wl_display_get_protocol_error(h->display, &iface, &code);
		fprintf(stderr, "protocol error: interface %s code %u\n",
			iface ? iface->name : "?", code);
		CHECK(false, "connection lost: %s",
		      strerror(wl_display_get_error(h->display)));
	}
}

/* ---- Repaint synchronisation ------------------------------------------- */

static void
frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
	bool *done = data;

	*done = true;
	wl_callback_destroy(callback);
}

static const struct wl_callback_listener frame_listener = {
	frame_done,
};

void
harness_wait_repaint(struct harness *h)
{
	if (h->n_windows == 0) {
		harness_roundtrip(h);
		return;
	}
	harness_wait_frame(h, h->windows[h->n_windows - 1]->surface);
}

void
harness_wait_frame(struct harness *h, struct wl_surface *surface)
{
	struct wl_callback *callback;
	bool done = false;
	int i;

	callback = wl_surface_frame(surface);
	wl_callback_add_listener(callback, &frame_listener, &done);
	wl_surface_commit(surface);
	for (i = 0; i < 200 && !done; i++)
		CHECK(wl_display_dispatch(h->display) >= 0, "connection lost");
	CHECK(done, "no frame callback");
}

/* ---- Windows ------------------------------------------------------------ */

struct wl_buffer *
harness_buffer_create(struct wl_shm *shm, int width, int height)
{
	return harness_buffer_create_colour(shm, width, height, 0xff3070c0);
}

struct wl_buffer *
harness_buffer_create_colour(struct wl_shm *shm, int width, int height,
			     uint32_t argb)
{
	int stride = width * 4;
	int size = stride * height;
	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;
	uint32_t *pixels;
	int fd, i;

	fd = memfd_create("polo-test-buffer", MFD_CLOEXEC);
	CHECK(fd >= 0, "memfd_create: %s", strerror(errno));
	CHECK(ftruncate(fd, size) == 0, "ftruncate: %s", strerror(errno));
	pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	CHECK(pixels != MAP_FAILED, "mmap: %s", strerror(errno));
	for (i = 0; i < width * height; i++)
		pixels[i] = argb;
	munmap(pixels, size);

	pool = wl_shm_create_pool(shm, fd, size);
	buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
					   WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	return buffer;
}

static void
window_attach(struct harness_window *w, int width, int height)
{
	if (w->buffer)
		wl_buffer_destroy(w->buffer);
	w->buffer = harness_buffer_create(w->harness->shm, width, height);
	w->width = width;
	w->height = height;
	wl_surface_attach(w->surface, w->buffer, 0, 0);
	wl_surface_damage_buffer(w->surface, 0, 0, width, height);
}

static void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
		      uint32_t serial)
{
	struct harness_window *w = data;

	xdg_surface_ack_configure(xdg_surface, serial);
	w->configured = true;
	w->configure_count++;

	int width = w->pending_width, height = w->pending_height;

	if (w->size_inc > 0) {
		width -= width % w->size_inc;
		height -= height % w->size_inc;
	}
	/* Follow the compositor's size like a real client would. */
	if (w->auto_configure && width > 0 && height > 0 &&
	    (width != w->width || height != w->height)) {
		window_attach(w, width, height);
		wl_surface_commit(w->surface);
	}
}

static const struct xdg_surface_listener xdg_surface_listener = {
	xdg_surface_configure,
};

static void
toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width,
		   int32_t height, struct wl_array *states)
{
	struct harness_window *w = data;
	uint32_t *state;

	w->pending_width = width;
	w->pending_height = height;
	w->tiled = 0;
	w->xdg_maximized = false;
	wl_array_for_each(state, states)
	{
		switch (*state) {
		case XDG_TOPLEVEL_STATE_MAXIMIZED:
			w->xdg_maximized = true;
			break;
		case XDG_TOPLEVEL_STATE_TILED_LEFT:
			w->tiled |= 1;
			break;
		case XDG_TOPLEVEL_STATE_TILED_RIGHT:
			w->tiled |= 2;
			break;
		case XDG_TOPLEVEL_STATE_TILED_TOP:
			w->tiled |= 4;
			break;
		case XDG_TOPLEVEL_STATE_TILED_BOTTOM:
			w->tiled |= 8;
			break;
		default:
			break;
		}
	}
}

static void
toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
	struct harness_window *w = data;

	w->close_requested = true;
}

static void
toplevel_configure_bounds(void *data, struct xdg_toplevel *toplevel,
			  int32_t width, int32_t height)
{
}

static void
toplevel_wm_capabilities(void *data, struct xdg_toplevel *toplevel,
			 struct wl_array *caps)
{
}

static const struct xdg_toplevel_listener toplevel_listener = {
	toplevel_configure,
	toplevel_close,
	toplevel_configure_bounds,
	toplevel_wm_capabilities,
};

static struct harness_window *
window_create(struct harness *h, int width, int height, const char *title,
	      bool decorated, bool ask_server_side)
{
	struct harness_window *w = calloc(1, sizeof *w);
	int i;

	w->harness = h;
	w->auto_configure = true;
	w->surface = wl_compositor_create_surface(h->compositor);
	wl_surface_set_user_data(w->surface, w);
	w->xdg_surface = xdg_wm_base_get_xdg_surface(h->wm_base, w->surface);
	xdg_surface_add_listener(w->xdg_surface, &xdg_surface_listener, w);
	w->xdg_toplevel = xdg_surface_get_toplevel(w->xdg_surface);
	xdg_toplevel_add_listener(w->xdg_toplevel, &toplevel_listener, w);
	xdg_toplevel_set_title(w->xdg_toplevel, title);
	xdg_toplevel_set_app_id(w->xdg_toplevel, "org.mate.PoloTest");
	if (decorated) {
		CHECK(h->decoration_manager, "no zxdg_decoration_manager_v1");
		w->decoration =
			zxdg_decoration_manager_v1_get_toplevel_decoration(
				h->decoration_manager, w->xdg_toplevel);
		if (ask_server_side)
			zxdg_toplevel_decoration_v1_set_mode(
				w->decoration,
				ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	wl_surface_commit(w->surface);

	for (i = 0; i < 20 && !w->configured; i++)
		harness_roundtrip(h);
	CHECK(w->configured, "%s: no initial configure", title);

	/* Ignore the compositor's suggested size: use ours, like a dialog. */
	w->pending_width = w->pending_height = 0;
	window_attach(w, width, height);
	wl_surface_commit(w->surface);
	harness_roundtrip(h);

	CHECK(h->n_windows < 32, "too many windows");
	h->windows[h->n_windows++] = w;
	harness_wait_repaint(h);

	CHECK(harness_window_info(w)->known, "%s: not mapped", title);
	return w;
}

struct harness_window *
harness_window_create(struct harness *h, int width, int height,
		      const char *title)
{
	return window_create(h, width, height, title, false, false);
}

struct harness_window *
harness_window_create_decorated(struct harness *h, int width, int height,
				const char *title)
{
	return window_create(h, width, height, title, true, true);
}

struct harness_window *
harness_window_create_with_decoration(struct harness *h, int width, int height,
				      const char *title)
{
	return window_create(h, width, height, title, true, false);
}

static void
forget_window(struct harness *h, struct harness_window *w)
{
	int i;

	for (i = 0; i < h->n_windows; i++) {
		if (h->windows[i] == w) {
			h->windows[i] = h->windows[--h->n_windows];
			break;
		}
	}
}

void
harness_window_unmap(struct harness_window *w)
{
	struct harness *h = w->harness;

	forget_window(h, w);
	wl_surface_attach(w->surface, NULL, 0, 0);
	wl_surface_commit(w->surface);
	if (w->buffer) {
		wl_buffer_destroy(w->buffer);
		w->buffer = NULL;
	}
	w->configured = false;
	w->width = w->height = 0;
	harness_roundtrip(h);
	harness_wait_repaint(h);
}

void
harness_window_remap(struct harness_window *w, int width, int height)
{
	struct harness *h = w->harness;
	int i;

	wl_surface_commit(w->surface);
	for (i = 0; i < 20 && !w->configured; i++)
		harness_roundtrip(h);
	CHECK(w->configured, "no initial configure after unmap");
	w->pending_width = w->pending_height = 0;
	window_attach(w, width, height);
	wl_surface_commit(w->surface);
	harness_roundtrip(h);
	CHECK(h->n_windows < 32, "too many windows");
	h->windows[h->n_windows++] = w;
	harness_wait_repaint(h);
	CHECK(harness_window_info(w)->known, "not mapped again");
}

void
harness_window_destroy(struct harness_window *w)
{
	struct harness *h = w->harness;
	int i;

	for (i = 0; i < h->n_windows; i++) {
		if (h->windows[i] == w) {
			h->windows[i] = h->windows[--h->n_windows];
			break;
		}
	}
	if (w->decoration)
		zxdg_toplevel_decoration_v1_destroy(w->decoration);
	xdg_toplevel_destroy(w->xdg_toplevel);
	xdg_surface_destroy(w->xdg_surface);
	wl_surface_destroy(w->surface);
	if (w->buffer)
		wl_buffer_destroy(w->buffer);
	if (h->keyboard_focus == w->surface)
		h->keyboard_focus = NULL;
	if (h->pointer_focus == w->surface)
		h->pointer_focus = NULL;
	free(w);
	harness_wait_repaint(h);
}

const struct harness_window_info *
harness_window_info(struct harness_window *w)
{
	polo_test_v1_get_window_info(w->harness->test, w->surface);
	harness_roundtrip(w->harness);
	return &w->info;
}

const struct harness_lamp_target *
harness_lamp_target(struct harness_window *w)
{
	w->lamp = (struct harness_lamp_target){ .known = false };
	polo_test_v1_get_lamp_target(w->harness->test, w->surface);
	harness_roundtrip(w->harness);
	return &w->lamp;
}

void
harness_window_draw_pending(struct harness_window *w)
{
	CHECK(w->pending_width > 0 && w->pending_height > 0,
	      "window has no pending size");
	window_attach(w, w->pending_width, w->pending_height);
	wl_surface_commit(w->surface);
	harness_roundtrip(w->harness);
}

const struct harness_layer_info *
harness_layer_info(struct harness *h, struct wl_surface *surface)
{
	polo_test_v1_get_layer_surface_info(h->test, surface);
	harness_roundtrip(h);
	return &h->layer_info;
}

/* ---- Input -------------------------------------------------------------- */

const char *
harness_set_setting(struct harness *h, const char *schema, const char *key,
		    const char *value)
{
	polo_test_v1_set_setting(h->test, schema, key, value);
	harness_roundtrip(h);
	CHECK(h->setting[0] != '\0', "cannot set %s %s to %s", schema, key,
	      value);
	harness_roundtrip(h);
	return h->setting;
}

const char *
harness_get_setting(struct harness *h, const char *schema, const char *key)
{
	polo_test_v1_get_setting(h->test, schema, key);
	harness_roundtrip(h);
	return h->setting;
}

void
harness_move_pointer(struct harness *h, int x, int y)
{
	polo_test_v1_move_pointer(h->test, x, y);
	harness_wait_repaint(h);
}

void
harness_move_pointer_absolute(struct harness *h, int x, int y)
{
	polo_test_v1_move_pointer_absolute(h->test, x, y);
	harness_wait_repaint(h);
}

void
harness_pointer_position(struct harness *h, int *x, int *y)
{
	polo_test_v1_get_pointer_position(h->test);
	harness_roundtrip(h);
	*x = h->pointer_x;
	*y = h->pointer_y;
}

void
harness_scroll(struct harness *h, int value)
{
	polo_test_v1_pointer_axis(h->test, value);
	harness_wait_repaint(h);
}

void
harness_button(struct harness *h, uint32_t button, bool pressed)
{
	polo_test_v1_button(h->test, button,
			    pressed ? WL_POINTER_BUTTON_STATE_PRESSED
				    : WL_POINTER_BUTTON_STATE_RELEASED);
	harness_wait_repaint(h);
}

void
harness_click(struct harness *h, int x, int y)
{
	harness_move_pointer(h, x, y);
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
}

void
harness_key(struct harness *h, uint32_t key, bool pressed)
{
	polo_test_v1_key(h->test, key,
			 pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
				 : WL_KEYBOARD_KEY_STATE_RELEASED);
	harness_roundtrip(h);
}

long
harness_cpu_ticks(struct harness *h)
{
	char path[64], buf[1024], *p;
	unsigned long utime, stime;
	FILE *f;
	int i;

	snprintf(path, sizeof path, "/proc/%d/stat", (int)h->pid);
	f = fopen(path, "r");
	CHECK(f != NULL, "cannot open %s", path);
	CHECK(fgets(buf, sizeof buf, f) != NULL, "cannot read %s", path);
	fclose(f);

	/* Fields 14 and 15, counted after the ")" closing the comm field. */
	p = strrchr(buf, ')');
	CHECK(p != NULL, "malformed %s", path);
	for (i = 0; i < 12 && p; i++)
		p = strchr(p + 1, ' ');
	CHECK(p && sscanf(p, " %lu %lu", &utime, &stime) == 2, "malformed %s",
	      path);
	return (long)(utime + stime);
}

#define FOURCC_ARGB8888 0x34325241u
#define FOURCC_XRGB8888 0x34325258u
#define FOURCC_ABGR8888 0x34324241u
#define FOURCC_XBGR8888 0x34324258u

struct capture_state {
	int width, height;
	uint32_t format; /* DRM fourcc */
	int done;        /* 1 complete, 2 retry, 3 failed */
};

static void
capture_format(void *data, struct weston_capture_source_v1 *source,
	       uint32_t format)
{
	((struct capture_state *)data)->format = format;
}

static uint32_t
shm_format(uint32_t fourcc)
{
	if (fourcc == FOURCC_ARGB8888)
		return WL_SHM_FORMAT_ARGB8888;
	if (fourcc == FOURCC_XRGB8888)
		return WL_SHM_FORMAT_XRGB8888;
	return fourcc;
}

static void
capture_size(void *data, struct weston_capture_source_v1 *source, int32_t width,
	     int32_t height)
{
	struct capture_state *state = data;

	state->width = width;
	state->height = height;
}

static void
capture_complete(void *data, struct weston_capture_source_v1 *source)
{
	((struct capture_state *)data)->done = 1;
}

static void
capture_retry(void *data, struct weston_capture_source_v1 *source)
{
	((struct capture_state *)data)->done = 2;
}

static void
capture_failed(void *data, struct weston_capture_source_v1 *source,
	       const char *msg)
{
	((struct capture_state *)data)->done = 3;
}

static const struct weston_capture_source_v1_listener capture_listener = {
	.format = capture_format,
	.size = capture_size,
	.complete = capture_complete,
	.retry = capture_retry,
	.failed = capture_failed,
};

uint32_t *
harness_capture(struct harness *h, int *width, int *height)
{
	return harness_capture_output(h, 0, width, height);
}

uint32_t *
harness_capture_output(struct harness *h, int index, int *width, int *height)
{
	struct capture_state state = { 0 };
	struct weston_capture_source_v1 *source;
	uint32_t *copy = NULL;

	*width = *height = 0;
	if (!h->capture || index < 0 || index >= h->n_outputs)
		return NULL;
	source = weston_capture_v1_create(h->capture, h->outputs[index],
					  WESTON_CAPTURE_V1_SOURCE_FRAMEBUFFER);
	weston_capture_source_v1_add_listener(source, &capture_listener,
					      &state);
	harness_roundtrip(h);
	size_t size = (size_t)state.width * state.height * 4;
	int fd = state.width > 0 ? memfd_create("capture", MFD_CLOEXEC) : -1;

	if (fd < 0 || ftruncate(fd, (off_t)size) != 0) {
		if (fd >= 0)
			close(fd);
		weston_capture_source_v1_destroy(source);
		return NULL;
	}
	uint32_t *pixels =
		mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	struct wl_shm_pool *pool = wl_shm_create_pool(h->shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(
		pool, 0, state.width, state.height, state.width * 4,
		shm_format(state.format ? state.format : FOURCC_XRGB8888));

	for (int attempt = 0; attempt < 20 && state.done != 1; attempt++) {
		state.done = 0;
		weston_capture_source_v1_capture(source, buffer);
		for (int i = 0; i < 200 && !state.done; i++) {
			harness_roundtrip(h);
			sleep_ms(5);
		}
	}
	if (state.done == 1 && pixels != MAP_FAILED) {
		copy = malloc(size);
		if (copy) {
			memcpy(copy, pixels, size);
			if (state.format == FOURCC_ABGR8888 ||
			    state.format == FOURCC_XBGR8888) {
				for (size_t i = 0; i < size / 4; i++)
					copy[i] = (copy[i] & 0xff00ff00u) |
						((copy[i] & 0xffu) << 16) |
						((copy[i] >> 16) & 0xffu);
			}
			*width = state.width;
			*height = state.height;
		}
	}
	wl_buffer_destroy(buffer);
	wl_shm_pool_destroy(pool);
	weston_capture_source_v1_destroy(source);
	if (pixels != MAP_FAILED)
		munmap(pixels, size);
	close(fd);
	return copy;
}

/* ---- Fake monitors ---------------------------------------------------- */

static void
edid_detailed_timing(uint8_t *d, int width, int height, int width_mm,
		     int height_mm)
{
	int hblank = 160, vblank = 30;
	long clock = (long)(width + hblank) * (height + vblank) * 60 / 10000;

	d[0] = clock & 0xff;
	d[1] = clock >> 8 & 0xff;
	d[2] = width & 0xff;
	d[3] = hblank & 0xff;
	d[4] = (width >> 8 & 0xf) << 4 | (hblank >> 8 & 0xf);
	d[5] = height & 0xff;
	d[6] = vblank & 0xff;
	d[7] = (height >> 8 & 0xf) << 4 | (vblank >> 8 & 0xf);
	d[8] = 48;
	d[9] = 32;
	d[10] = 3 << 4 | 5;
	d[11] = 0;
	d[12] = width_mm & 0xff;
	d[13] = height_mm & 0xff;
	d[14] = (width_mm >> 8 & 0xf) << 4 | (height_mm >> 8 & 0xf);
	d[17] = 0x18;
}

static void
edid_text(uint8_t *d, uint8_t tag, const char *text)
{
	size_t n = strlen(text);

	d[3] = tag;
	for (int i = 0; i < 13; i++)
		d[5 + i] = i < (int)n ? (uint8_t)text[i]
			: i == (int)n ? 0x0a
				      : 0x20;
}

void
harness_make_edid(uint8_t edid[128], const char *vendor, uint16_t product,
		  uint32_t serial, const char *model, int width_mm,
		  int height_mm, int width, int height)
{
	uint16_t id = (uint16_t)((vendor[0] - '@') << 10 |
				 (vendor[1] - '@') << 5 | (vendor[2] - '@'));
	uint8_t sum = 0;

	memset(edid, 0, 128);
	memcpy(edid, "\x00\xff\xff\xff\xff\xff\xff\x00", 8);
	edid[8] = id >> 8;
	edid[9] = id & 0xff;
	edid[10] = product & 0xff;
	edid[11] = product >> 8;
	edid[12] = serial & 0xff;
	edid[13] = serial >> 8 & 0xff;
	edid[14] = serial >> 16 & 0xff;
	edid[15] = serial >> 24 & 0xff;
	edid[16] = 1;
	edid[17] = 30;
	edid[18] = 1;
	edid[19] = 4;
	edid[20] = 0xa5;
	edid[21] = (uint8_t)((width_mm + 5) / 10);
	edid[22] = (uint8_t)((height_mm + 5) / 10);
	edid[23] = 120;
	edid[24] = 0x06;
	for (int i = 38; i < 54; i++)
		edid[i] = 0x01;
	edid_detailed_timing(edid + 54, width, height, width_mm, height_mm);
	edid_text(edid + 72, 0xfc, model);
	edid_text(edid + 90, 0xfe, "polo test");
	edid_text(edid + 108, 0x10, "");
	edid[108 + 3] = 0x10;
	for (int i = 0; i < 127; i++)
		sum += edid[i];
	edid[127] = (uint8_t)(256 - sum);
}

void
harness_add_output(struct harness *h, const char *name, const char *vendor,
		   uint16_t product, uint32_t serial, const char *model,
		   bool internal, int width_mm, int height_mm,
		   const int modes[][3], int n_modes)
{
	struct wl_array edid, list;
	uint8_t *bytes;
	int32_t *values;

	CHECK(n_modes > 0, "a monitor needs a mode");
	wl_array_init(&edid);
	wl_array_init(&list);
	if (vendor) {
		bytes = wl_array_add(&edid, 128);
		harness_make_edid(bytes, vendor, product, serial, model,
				  width_mm, height_mm, modes[0][0],
				  modes[0][1]);
	}
	values = wl_array_add(&list, (size_t)n_modes * 3 * sizeof(int32_t));
	for (int i = 0; i < n_modes; i++) {
		values[i * 3] = modes[i][0];
		values[i * 3 + 1] = modes[i][1];
		values[i * 3 + 2] = modes[i][2];
	}
	polo_test_v1_add_output(h->test, name, &edid, internal, &list);
	wl_array_release(&edid);
	wl_array_release(&list);
	harness_roundtrip(h);
	harness_roundtrip(h);
}

void
harness_connect_output(struct harness *h, const char *name, bool connected)
{
	polo_test_v1_connect_output(h->test, name, connected);
	harness_roundtrip(h);
	harness_roundtrip(h);
}

void
harness_set_session_active(struct harness *h, bool active)
{
	polo_test_v1_set_session_active(h->test, active);
	harness_roundtrip(h);
}

void
harness_remove_output(struct harness *h, const char *name)
{
	polo_test_v1_remove_output(h->test, name);
	harness_roundtrip(h);
	harness_roundtrip(h);
}

const struct harness_output_state *
harness_output_state(struct harness *h, const char *name)
{
	h->n_output_states = 0;
	h->outputs_done = false;
	polo_test_v1_get_outputs(h->test);
	while (!h->outputs_done)
		harness_roundtrip(h);
	for (int i = 0; i < h->n_output_states; i++)
		if (strcmp(h->output_states[i].name, name) == 0)
			return &h->output_states[i];
	return NULL;
}

int
harness_output_count(struct harness *h)
{
	harness_output_state(h, "");
	return h->n_output_states;
}
