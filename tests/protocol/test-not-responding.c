/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * argv[2]: polo-shell-ui. Marco's delete.c through polo-shell-ui: a child
 * process maps a window and never answers pings. Alt-F4 on it shows the
 * "is not responding" question after Marco's 5 s; Return is Wait, Escape
 * (zenity's exit status 1) force-quits. Then the rename-workspace entry
 * answers with what was typed.
 */
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "harness.h"

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;

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
configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
	xdg_surface_ack_configure(xdg_surface, serial);
	*(bool *)data = true;
}

static const struct xdg_surface_listener surface_listener = { configure };

/* A window whose client never pongs: no xdg_wm_base listener at all. */
static void
hung_client(const char *socket)
{
	struct wl_display *display = wl_display_connect(socket);
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	bool configured = false;

	if (!display)
		_exit(2);
	wl_registry_add_listener(wl_display_get_registry(display),
				 &registry_listener, NULL);
	wl_display_roundtrip(display);
	surface = wl_compositor_create_surface(compositor);
	xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
	xdg_surface_add_listener(xdg_surface, &surface_listener, &configured);
	toplevel = xdg_surface_get_toplevel(xdg_surface);
	xdg_toplevel_set_title(toplevel, "Hung & stuck");
	wl_surface_commit(surface);
	while (!configured && wl_display_dispatch(display) != -1)
		;
	wl_surface_attach(surface, harness_buffer_create(shm, 300, 200), 0, 0);
	wl_surface_commit(surface);
	while (wl_display_dispatch(display) != -1)
		;
	_exit(0);
}

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec ts = { 0, 50 * 1000000L };

	for (int i = 0; i < ms / 50; i++) {
		harness_roundtrip(h);
		nanosleep(&ts, NULL);
	}
}

static bool
answered(struct harness *h, const char *text)
{
	for (int i = 0; i < h->n_shell_ui_requests; i++)
		if (strcmp(h->shell_ui_requests[i], text) == 0)
			return true;
	return false;
}

static void
wait_answer(struct harness *h, const char *text)
{
	for (int i = 0; i < 100 && !answered(h, text); i++)
		pause_ms(h, 50);
	CHECK(answered(h, text), "no \"%s\"", text);
}

static void
key(struct harness *h, uint32_t code)
{
	harness_key(h, code, true);
	harness_key(h, code, false);
}

static void
alt_f4(struct harness *h)
{
	harness_key(h, KEY_LEFTALT, true);
	key(h, KEY_F4);
	harness_key(h, KEY_LEFTALT, false);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	pid_t child;
	int status;

	CHECK(argc >= 3, "usage: %s POLO POLO-SHELL-UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	h = harness_start(argc, argv);
	for (int i = 0; i < 100 && h->shell_ui_binds == 0; i++)
		pause_ms(h, 50);
	CHECK(h->shell_ui_binds == 1, "polo-shell-ui did not bind");

	child = fork();
	CHECK(child >= 0, "fork");
	if (child == 0)
		hung_client(h->socket);
	pause_ms(h, 500);

	alt_f4(h);
	pause_ms(h, 6000);
	key(h, KEY_ENTER);
	wait_answer(h, "dialog_response 1 -5 (null)");
	CHECK(waitpid(child, &status, WNOHANG) == 0, "Wait killed the client");

	alt_f4(h);
	pause_ms(h, 6000);
	key(h, KEY_ESC);
	wait_answer(h, "dialog_response 2 -4 (null)");
	pid_t done = 0;
	for (int i = 0; i < 40 && done == 0; i++) {
		done = waitpid(child, &status, WNOHANG);
		if (done == 0)
			pause_ms(h, 50);
	}
	if (done == 0)
		kill(child, SIGKILL);
	CHECK(done == child && WIFSIGNALED(status) &&
		      WTERMSIG(status) == SIGKILL,
	      "Escape did not force the client to quit");

	polo_test_v1_shell_ui_dialog(h->test, 100, 1, "Workspace 1");
	pause_ms(h, 700);
	key(h, KEY_P);
	key(h, KEY_O);
	key(h, KEY_ENTER);
	wait_answer(h, "dialog_response 100 -5 po");

	polo_test_v1_shell_ui_dialog(h->test, 101, 3, "The panel keeps crashing");
	pause_ms(h, 700);
	key(h, KEY_ENTER);
	wait_answer(h, "dialog_response 101 -5 (null)");
	return harness_stop(h);
}
