/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A client that keeps creating objects is cut off with no_memory instead of
 * filling Polo's memory: too many surfaces, shm pools, activation tokens,
 * idle or shortcut inhibitors, or objects of any kind. A client under the
 * limits, and every other client, carries on.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "harness.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"
#include "keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h"
#include "xdg-activation-v1-client-protocol.h"

struct abuser {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct xdg_activation_v1 *activation;
	struct zwp_idle_inhibit_manager_v1 *idle;
	struct zwp_keyboard_shortcuts_inhibit_manager_v1 *shortcuts;
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct abuser *a = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
		a->compositor = wl_registry_bind(registry, name,
						 &wl_compositor_interface, 4);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		a->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, wl_seat_interface.name) == 0 && !a->seat)
		a->seat =
			wl_registry_bind(registry, name, &wl_seat_interface, 1);
	else if (strcmp(interface, xdg_activation_v1_interface.name) == 0)
		a->activation = wl_registry_bind(
			registry, name, &xdg_activation_v1_interface, 1);
	else if (strcmp(interface,
			zwp_idle_inhibit_manager_v1_interface.name) == 0)
		a->idle = wl_registry_bind(
			registry, name, &zwp_idle_inhibit_manager_v1_interface,
			1);
	else if (strcmp(interface,
			zwp_keyboard_shortcuts_inhibit_manager_v1_interface
				.name) == 0)
		a->shortcuts = wl_registry_bind(
			registry, name,
			&zwp_keyboard_shortcuts_inhibit_manager_v1_interface,
			1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener listener = { global, global_remove };

static struct abuser
connect_abuser(struct harness *h)
{
	struct abuser a = { 0 };

	a.display = wl_display_connect(h->socket);
	CHECK(a.display, "cannot connect");
	a.registry = wl_display_get_registry(a.display);
	wl_registry_add_listener(a.registry, &listener, &a);
	wl_display_roundtrip(a.display);
	CHECK(a.compositor && a.shm && a.seat && a.activation && a.idle &&
		      a.shortcuts,
	      "a global is missing");
	return a;
}

static bool
cut_off(struct abuser *a)
{
	wl_display_roundtrip(a->display);
	return wl_display_get_error(a->display) == ENOMEM;
}

static void
flush(struct abuser *a, int i)
{
	if (i % 512 == 0)
		wl_display_flush(a->display);
	if (i % 4096 == 0)
		wl_display_roundtrip(a->display);
}

static void
forget(void *proxy)
{
	wl_proxy_destroy(proxy);
}

static void
close_abuser(struct abuser *a)
{
	void *proxies[] = { a->shortcuts, a->idle,       a->activation, a->seat,
			    a->shm,       a->compositor, a->registry };

	for (size_t i = 0; i < sizeof proxies / sizeof proxies[0]; i++)
		if (proxies[i])
			wl_proxy_destroy(proxies[i]);
	wl_display_disconnect(a->display);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct abuser a;
	int fd = memfd_create("polo-limits", MFD_CLOEXEC);

	CHECK(fd >= 0 && ftruncate(fd, 4096) == 0, "memfd");

	/* Under every limit: a program with many surfaces is fine. */
	a = connect_abuser(h);
	for (int i = 0; i < 2000; i++) {
		forget(wl_compositor_create_surface(a.compositor));
		flush(&a, i);
	}
	wl_display_roundtrip(a.display);
	CHECK(wl_display_get_error(a.display) == 0,
	      "2000 surfaces were refused");
	close_abuser(&a);

	a = connect_abuser(h);
	for (int i = 0; i < 18500 && wl_display_get_error(a.display) == 0;
	     i++) {
		forget(wl_compositor_create_surface(a.compositor));
		flush(&a, i);
	}
	CHECK(cut_off(&a), "18500 surfaces were allowed");
	close_abuser(&a);

	a = connect_abuser(h);
	for (int i = 0; i < 1200 && wl_display_get_error(a.display) == 0; i++) {
		forget(wl_shm_create_pool(a.shm, fd, 4096));
		if (i % 16 == 15)
			wl_display_roundtrip(a.display);
	}
	CHECK(cut_off(&a), "1200 shm pools were allowed (error %d)",
	      wl_display_get_error(a.display));
	close_abuser(&a);

	a = connect_abuser(h);
	for (int i = 0; i < 1200 && wl_display_get_error(a.display) == 0; i++) {
		forget(xdg_activation_v1_get_activation_token(a.activation));
		flush(&a, i);
	}
	CHECK(cut_off(&a), "1200 activation tokens were allowed");
	close_abuser(&a);

	a = connect_abuser(h);
	struct wl_surface *surface = wl_compositor_create_surface(a.compositor);

	for (int i = 0; i < 400 && wl_display_get_error(a.display) == 0; i++) {
		forget(zwp_idle_inhibit_manager_v1_create_inhibitor(a.idle,
								    surface));
		flush(&a, i);
	}
	CHECK(cut_off(&a), "400 idle inhibitors were allowed");
	forget(surface);
	close_abuser(&a);

	a = connect_abuser(h);
	for (int i = 0; i < 400 && wl_display_get_error(a.display) == 0; i++) {
		struct wl_surface *each =
			wl_compositor_create_surface(a.compositor);

		forget(zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
			a.shortcuts, each, a.seat));
		forget(each);
		flush(&a, i);
	}
	CHECK(cut_off(&a), "400 shortcut inhibitors were allowed");
	close_abuser(&a);

	a = connect_abuser(h);
	for (int i = 0; i < 230000 && wl_display_get_error(a.display) == 0;
	     i++) {
		forget(wl_compositor_create_region(a.compositor));
		flush(&a, i);
	}
	CHECK(cut_off(&a), "230000 regions were allowed");
	close_abuser(&a);

	close(fd);
	struct harness_window *w = harness_window_create(h, 200, 150, "after");

	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
