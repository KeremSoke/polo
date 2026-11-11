/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clients that misbehave leave Polo stable and every grab released. A child
 * process maps a window and, on a button press in it, starts a drag, an
 * interactive move or resize, or a popup grab, and is then killed; or it
 * never acks configures, stops reading its socket while events pile up, or
 * floods requests. Afterwards this test's own window must get the pointer,
 * clicks and keys again, and Polo must have answered quickly throughout.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;
static struct wl_seat *seat;
static struct wl_data_device_manager *data_manager;

static int report_fd = -1;
static const char *mode;
static struct wl_surface *surface;
static struct xdg_toplevel *toplevel;
static bool acking = true;

static double
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void
report(char c)
{
	if (write(report_fd, &c, 1) != 1)
		_exit(3);
}

/* ---- The misbehaving child ---------------------------------------------- */

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface, wl_compositor_interface.name) == 0)
		compositor = wl_registry_bind(registry, name,
					      &wl_compositor_interface, 4);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
		wm_base = wl_registry_bind(registry, name,
					   &xdg_wm_base_interface, 1);
	else if (strcmp(interface, wl_seat_interface.name) == 0)
		seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
	else if (strcmp(interface, wl_data_device_manager_interface.name) == 0)
		data_manager = wl_registry_bind(
			registry, name, &wl_data_device_manager_interface, 3);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	global,
	global_remove,
};

static void
ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
	xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = { ping };

static void
surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
	bool *configured = data;

	if (acking || !*configured)
		xdg_surface_ack_configure(xdg_surface, serial);
	*configured = true;
}

static const struct xdg_surface_listener surface_listener = {
	surface_configure,
};

static void
popup_surface_configure(void *data, struct xdg_surface *xdg_surface,
			uint32_t serial)
{
	struct wl_surface *popup = data;

	xdg_surface_ack_configure(xdg_surface, serial);
	wl_surface_attach(popup, harness_buffer_create(shm, 120, 80), 0, 0);
	wl_surface_commit(popup);
	report('G');
}

static const struct xdg_surface_listener popup_surface_listener = {
	popup_surface_configure,
};

static void
popup_configure(void *data, struct xdg_popup *popup, int32_t x, int32_t y,
		int32_t width, int32_t height)
{
}

static void
popup_done(void *data, struct xdg_popup *popup)
{
}

static const struct xdg_popup_listener popup_listener = {
	.configure = popup_configure,
	.popup_done = popup_done,
};

static void
start_popup(uint32_t serial, struct xdg_surface *parent)
{
	struct wl_surface *popup = wl_compositor_create_surface(compositor);
	struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(wm_base, popup);
	struct xdg_positioner *positioner =
		xdg_wm_base_create_positioner(wm_base);
	struct xdg_popup *xdg_popup;

	xdg_positioner_set_size(positioner, 120, 80);
	xdg_positioner_set_anchor_rect(positioner, 10, 10, 1, 1);
	xdg_surface_add_listener(xdg, &popup_surface_listener, popup);
	xdg_popup = xdg_surface_get_popup(xdg, parent, positioner);
	xdg_popup_add_listener(xdg_popup, &popup_listener, NULL);
	xdg_popup_grab(xdg_popup, seat, serial);
	wl_surface_commit(popup);
}

static struct xdg_surface *main_xdg_surface;

static void
pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
	      struct wl_surface *entered, wl_fixed_t x, wl_fixed_t y)
{
	if (entered != surface)
		return;
	report('E');
	if (strcmp(mode, "stall") == 0)
		raise(SIGSTOP);
}

static void
pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
	      struct wl_surface *left)
{
}

static void
pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
	       wl_fixed_t x, wl_fixed_t y)
{
}

static void
pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
	       uint32_t time, uint32_t button, uint32_t state)
{
	struct wl_data_source *source;

	if (state != WL_POINTER_BUTTON_STATE_PRESSED)
		return;
	if (strcmp(mode, "drag") == 0) {
		struct wl_data_device *device =
			wl_data_device_manager_get_data_device(data_manager,
							       seat);

		source =
			wl_data_device_manager_create_data_source(data_manager);
		wl_data_source_offer(source, "text/plain;charset=utf-8");
		wl_data_source_set_actions(
			source, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
		wl_data_device_start_drag(device, source, surface, NULL,
					  serial);
		report('G');
	} else if (strcmp(mode, "move") == 0) {
		xdg_toplevel_move(toplevel, seat, serial);
		report('G');
	} else if (strcmp(mode, "resize") == 0) {
		xdg_toplevel_resize(toplevel, seat, serial,
				    XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT);
		report('G');
	} else if (strcmp(mode, "popup") == 0) {
		start_popup(serial, main_xdg_surface);
	}
}

static void
pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
	     uint32_t axis, wl_fixed_t value)
{
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
};

static void
child(const char *socket, const char *what, int fd)
{
	struct wl_display *display = wl_display_connect(socket);
	bool configured = false;

	mode = what;
	report_fd = fd;
	if (!display)
		_exit(2);
	wl_registry_add_listener(wl_display_get_registry(display),
				 &registry_listener, NULL);
	wl_display_roundtrip(display);
	xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
	wl_pointer_add_listener(wl_seat_get_pointer(seat), &pointer_listener,
				NULL);
	surface = wl_compositor_create_surface(compositor);
	main_xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
	xdg_surface_add_listener(main_xdg_surface, &surface_listener,
				 &configured);
	toplevel = xdg_surface_get_toplevel(main_xdg_surface);
	xdg_toplevel_set_title(toplevel, what);
	wl_surface_commit(surface);
	while (!configured && wl_display_dispatch(display) != -1)
		;
	wl_surface_attach(surface, harness_buffer_create(shm, 300, 200), 0, 0);
	wl_surface_commit(surface);
	wl_display_roundtrip(display);
	report('M');

	if (strcmp(what, "noack") == 0) {
		acking = false;
	} else if (strcmp(what, "flood") == 0) {
		for (double end = now_ms() + 3000; now_ms() < end;) {
			for (int i = 0; i < 1000; i++)
				wl_surface_damage_buffer(surface, 0, 0, 10, 10);
			if (wl_display_flush(display) < 0 && errno != EAGAIN)
				_exit(4);
		}
		report('F');
	}
	while (wl_display_dispatch(display) != -1)
		;
	_exit(0);
}

/* ---- The test ----------------------------------------------------------- */

struct bad {
	pid_t pid;
	int fd;
};

static double worst_roundtrip;

static void
timed_roundtrip(struct harness *h)
{
	double start = now_ms();

	harness_roundtrip(h);
	if (now_ms() - start > worst_roundtrip)
		worst_roundtrip = now_ms() - start;
}

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (double end = now_ms() + ms; now_ms() < end;) {
		timed_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static bool
reported(struct harness *h, struct bad *b, char want, int ms)
{
	char c;

	for (double end = now_ms() + ms; now_ms() < end;) {
		while (read(b->fd, &c, 1) == 1)
			if (c == want)
				return true;
		pause_ms(h, 20);
	}
	return false;
}

static struct bad
start_bad(struct harness *h, const char *what)
{
	int fds[2];
	struct bad b;

	CHECK(pipe(fds) == 0, "pipe");
	b.pid = fork();
	if (b.pid == 0) {
		close(fds[0]);
		child(h->socket, what, fds[1]);
	}
	close(fds[1]);
	b.fd = fds[0];
	fcntl(b.fd, F_SETFL, O_NONBLOCK);
	CHECK(reported(h, &b, 'M', 5000), "the %s client did not map", what);
	pause_ms(h, 200);
	return b;
}

static void
end_bad(struct bad *b)
{
	kill(b->pid, SIGKILL);
	waitpid(b->pid, NULL, 0);
	close(b->fd);
}

static void
point_at_child(struct harness *h, struct bad *b, int *x, int *y)
{
	char c;

	harness_move_pointer(h, 2, 2);
	pause_ms(h, 50);
	while (read(b->fd, &c, 1) == 1)
		;
	harness_move_pointer(h, HARNESS_OUTPUT_WIDTH / 2,
			     HARNESS_OUTPUT_HEIGHT / 2);
	if (reported(h, b, 'E', 200)) {
		*x = HARNESS_OUTPUT_WIDTH / 2;
		*y = HARNESS_OUTPUT_HEIGHT / 2;
		return;
	}
	for (int py = 20; py < HARNESS_OUTPUT_HEIGHT; py += 40)
		for (int px = 20; px < HARNESS_OUTPUT_WIDTH; px += 40) {
			harness_move_pointer(h, px, py);
			if (reported(h, b, 'E', 30)) {
				*x = px;
				*y = py;
				return;
			}
		}
	CHECK(false, "the child's window is nowhere");
}

static void
expect_free(struct harness *h, struct harness_window *w, const char *when)
{
	const struct harness_window_info *info = harness_window_info(w);
	int presses = h->button_presses;
	bool reached = false;

	for (int y = info->y + 10; y < info->y + info->height && !reached;
	     y += 30)
		for (int x = info->x + 10;
		     x < info->x + info->width && !reached; x += 30) {
			harness_move_pointer(h, x, y);
			pause_ms(h, 20);
			reached = h->pointer_focus == w->surface;
		}
	CHECK(reached, "%s: the pointer does not reach the window", when);
	harness_button(h, BTN_LEFT, true);
	harness_button(h, BTN_LEFT, false);
	pause_ms(h, 100);
	CHECK(h->button_presses > presses, "%s: the click did not arrive",
	      when);
	CHECK(h->keyboard_focus == w->surface,
	      "%s: the click did not focus the window", when);
	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	pause_ms(h, 50);
	CHECK(h->last_key == KEY_A && h->last_key_surface == w->surface,
	      "%s: the key did not arrive", when);
	CHECK(worst_roundtrip < 1000,
	      "%s: Polo took %.0f ms to answer a roundtrip", when,
	      worst_roundtrip);
	worst_roundtrip = 0;
}

static void
crash_in_grab(struct harness *h, struct harness_window *w, const char *what)
{
	struct bad b = start_bad(h, what);
	int x = 0, y = 0;
	char when[64];

	point_at_child(h, &b, &x, &y);
	harness_button(h, BTN_LEFT, true);
	CHECK(reported(h, &b, 'G', 3000), "the %s client got no grab", what);
	pause_ms(h, 100);
	harness_move_pointer(h, x + 40, y + 30);
	pause_ms(h, 100);
	end_bad(&b);
	pause_ms(h, 200);
	harness_move_pointer(h, x + 60, y + 40);
	harness_button(h, BTN_LEFT, false);
	pause_ms(h, 100);
	snprintf(when, sizeof when, "after a crash mid-%s", what);
	expect_free(h, w, when);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct harness_window *w = harness_window_create(h, 700, 500, "me");
	struct bad b;
	long ticks;
	double start;

	expect_free(h, w, "at start");

	crash_in_grab(h, w, "drag");
	crash_in_grab(h, w, "move");
	crash_in_grab(h, w, "resize");
	crash_in_grab(h, w, "popup");

	b = start_bad(h, "noack");
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_F10, true);
	harness_key(h, KEY_F10, false);
	harness_key(h, KEY_LEFTALT, false);
	ticks = harness_cpu_ticks(h);
	start = now_ms();
	pause_ms(h, 2000);
	ticks = harness_cpu_ticks(h) - ticks;
	CHECK(ticks * 1000.0 / sysconf(_SC_CLK_TCK) < (now_ms() - start) / 2,
	      "Polo spun while a client did not ack: %ld ticks in %.0f ms",
	      ticks, now_ms() - start);
	expect_free(h, w, "beside a client that never acks");
	end_bad(&b);
	expect_free(h, w, "after the client that never acked");

	b = start_bad(h, "stall");
	{
		int x = 0, y = 0;

		point_at_child(h, &b, &x, &y);
		for (int i = 0; i < 20000; i++) {
			polo_test_v1_move_pointer(h->test, x + i % 50, y);
			if (i % 500 == 0)
				timed_roundtrip(h);
		}
		timed_roundtrip(h);
	}
	expect_free(h, w, "beside a client that stopped reading");
	kill(b.pid, SIGCONT);
	end_bad(&b);

	b = start_bad(h, "flood");
	for (int i = 0; i < 100 && !reported(h, &b, 'F', 30); i++)
		timed_roundtrip(h);
	expect_free(h, w, "while a client flooded requests");
	end_bad(&b);

	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
