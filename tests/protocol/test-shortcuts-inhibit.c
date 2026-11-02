/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A shortcuts inhibitor suspends Polo's bindings while its surface has focus, and
 * Ctrl+Alt+Shift+Escape gets them back.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>

#include <linux/input.h>

#include "harness.h"
#include "keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h"

/* The last check kills this client's connection with its proxies live. Polo is
 * its own process and is still checked. */
int
__lsan_is_turned_off(void);

int
__lsan_is_turned_off(void)
{
	return 1;
}

static struct zwp_keyboard_shortcuts_inhibit_manager_v1 *manager;
static int actives, inactives;

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface,
		   zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name) == 0)
		manager = wl_registry_bind(
			registry, name,
			&zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
on_active(void *data, struct zwp_keyboard_shortcuts_inhibitor_v1 *i)
{
	actives++;
}

static void
on_inactive(void *data, struct zwp_keyboard_shortcuts_inhibitor_v1 *i)
{
	inactives++;
}

static const struct zwp_keyboard_shortcuts_inhibitor_v1_listener listener = {
	on_active,
	on_inactive,
};

static void
alt_f10(struct harness *h)
{
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_F10, true);
	harness_key(h, KEY_F10, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_roundtrip(h);
	harness_roundtrip(h);
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
	harness_roundtrip(h);
}

static struct zwp_keyboard_shortcuts_inhibitor_v1 *
inhibit(struct harness *h, struct wl_surface *surface)
{
	struct zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor =
		zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
			manager, surface, h->seat);

	zwp_keyboard_shortcuts_inhibitor_v1_add_listener(inhibitor, &listener, NULL);
	harness_roundtrip(h);
	return inhibitor;
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(manager, "no shortcuts inhibit manager");

	struct harness_window *w = harness_window_create(h, 300, 200, "vm");

	alt_f10(h);
	CHECK(w->xdg_maximized, "Alt+F10 does not work to begin with");
	alt_f10(h);
	CHECK(!w->xdg_maximized, "Alt+F10 did not unmaximize");

	/* Active at once on the focused surface. */
	struct zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor =
		inhibit(h, w->surface);

	CHECK(actives == 1 && inactives == 0, "active %d inactive %d", actives,
	      inactives);
	h->last_key = 0;
	alt_f10(h);
	CHECK(!w->xdg_maximized, "a binding ran during the inhibitor");
	CHECK(h->last_key == KEY_LEFTALT, "the client missed the keys: last %u",
	      h->last_key);

	/* Focus leaves and comes back. */
	struct harness_window *other = harness_window_create(h, 300, 200, "other");

	harness_roundtrip(h);
	CHECK(inactives == 1, "still active with focus elsewhere");
	alt_f10(h);
	CHECK(other->xdg_maximized, "bindings stayed off on another window");
	alt_f10(h);
	CHECK(!w->xdg_maximized, "the shortcut hit the inhibiting window");
	harness_window_destroy(other);
	harness_roundtrip(h);
	harness_roundtrip(h);
	CHECK(actives == 2, "not active when focus came back");

	/* The escape hatch, until focus has left and come back. */
	escape_hatch(h);
	CHECK(inactives == 2, "the escape hatch did not end it");
	alt_f10(h);
	CHECK(w->xdg_maximized, "bindings did not come back after the escape hatch");
	alt_f10(h);
	other = harness_window_create(h, 300, 200, "other");
	harness_roundtrip(h);
	CHECK(actives == 2, "active with focus elsewhere after the escape hatch");
	harness_window_destroy(other);
	harness_roundtrip(h);
	harness_roundtrip(h);
	CHECK(actives == 3, "not active again after focus came back");

	/* Destroying it gives the shortcuts back. */
	zwp_keyboard_shortcuts_inhibitor_v1_destroy(inhibitor);
	harness_roundtrip(h);
	alt_f10(h);
	CHECK(w->xdg_maximized, "bindings did not come back after destroy");
	alt_f10(h);

	/* On an unfocused surface it waits for focus. */
	other = harness_window_create(h, 300, 200, "other");
	harness_roundtrip(h);
	int before = actives;

	inhibitor = inhibit(h, w->surface);
	CHECK(actives == before, "active without focus");
	zwp_keyboard_shortcuts_inhibitor_v1_destroy(inhibitor);
	harness_window_destroy(other);
	harness_roundtrip(h);

	/* A surface that goes first leaves it harmless. */
	inhibitor = inhibit(h, w->surface);
	harness_window_destroy(w);
	harness_roundtrip(h);
	zwp_keyboard_shortcuts_inhibitor_v1_destroy(inhibitor);
	harness_roundtrip(h);

	/* A second inhibitor on one surface and seat is an error. */
	struct harness_window *last = harness_window_create(h, 200, 150, "last");

	inhibit(h, last->surface);
	zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
		manager, last->surface, h->seat);
	for (int i = 0; i < 10 && !wl_display_get_error(h->display); i++)
		wl_display_roundtrip(h->display);
	CHECK(wl_display_get_error(h->display) == EPROTO,
	      "no error for a second inhibitor");
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");

	/* libweston's own bindings, as VT switching is, are never inhibited. */
	setenv("POLO_TEST_KILL", "1", 1);
	h = harness_start(argc, argv);
	registry = wl_display_get_registry(h->display);
	manager = NULL;
	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	last = harness_window_create(h, 200, 150, "last");
	before = actives;
	inhibit(h, last->surface);
	CHECK(actives == before + 1, "not active before the kill binding");
	polo_test_v1_key(h->test, KEY_LEFTCTRL, 1);
	polo_test_v1_key(h->test, KEY_LEFTALT, 1);
	polo_test_v1_key(h->test, KEY_BACKSPACE, 1);
	wl_display_flush(h->display);

	siginfo_t info = { 0 };

	for (int i = 0; i < 100 && info.si_pid == 0; i++) {
		usleep(10000);
		CHECK(waitid(P_PID, h->pid, &info, WEXITED | WNOHANG | WNOWAIT) == 0,
		      "waitid");
	}
	CHECK(info.si_pid == h->pid && info.si_code == CLD_EXITED,
	      "the kill binding did not stop polo through the inhibitor");
	return 0;
}
