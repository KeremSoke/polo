/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An idle inhibitor counts only while its surface is mapped on an output.
 */

#include <string.h>
#include <time.h>

#include <linux/input.h>

#include "harness.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"

static struct zwp_idle_inhibit_manager_v1 *manager;

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0)
		manager = wl_registry_bind(
			registry, name, &zwp_idle_inhibit_manager_v1_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static bool
inhibited(struct harness *h)
{
	return strcmp(harness_get_setting(h, "polo-test", "idle-inhibited"),
		      "true") == 0;
}

/* Output assignment follows a repaint. */
static void
expect(struct harness *h, bool wanted, const char *what)
{
	struct timespec tick = { 0, 20 * 1000000L };

	for (int i = 0; i < 100 && inhibited(h) != wanted; i++) {
		nanosleep(&tick, NULL);
		harness_roundtrip(h);
	}
	CHECK(inhibited(h) == wanted, "%s: inhibited should be %d", what, wanted);
}

static void
chord(struct harness *h, uint32_t a, uint32_t b, uint32_t key)
{
	harness_key(h, a, true);
	if (b)
		harness_key(h, b, true);
	harness_key(h, key, true);
	harness_key(h, key, false);
	if (b)
		harness_key(h, b, false);
	harness_key(h, a, false);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(manager, "no zwp_idle_inhibit_manager_v1");
	expect(h, false, "nothing asked");

	struct wl_surface *unmapped = wl_compositor_create_surface(h->compositor);
	struct zwp_idle_inhibitor_v1 *early =
		zwp_idle_inhibit_manager_v1_create_inhibitor(manager, unmapped);

	harness_roundtrip(h);
	expect(h, false, "a surface that was never mapped");
	zwp_idle_inhibitor_v1_destroy(early);
	wl_surface_destroy(unmapped);

	struct harness_window *w = harness_window_create(h, 200, 150, "video");
	struct zwp_idle_inhibitor_v1 *inhibitor =
		zwp_idle_inhibit_manager_v1_create_inhibitor(manager, w->surface);

	expect(h, true, "a visible window");

	/* A second window on top: covered still counts. */
	struct harness_window *cover = harness_window_create(h, 900, 700, "cover");

	harness_roundtrip(h);
	expect(h, true, "a covered window");
	harness_window_destroy(cover);
	expect(h, true, "after the cover left");

	zwp_idle_inhibitor_v1_destroy(inhibitor);
	expect(h, false, "the inhibitor destroyed");
	inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(manager, w->surface);
	expect(h, true, "a new inhibitor");

	/* Another workspace hides it. */
	chord(h, KEY_LEFTCTRL, KEY_LEFTALT, KEY_RIGHT);
	expect(h, false, "on another workspace");
	chord(h, KEY_LEFTCTRL, KEY_LEFTALT, KEY_LEFT);
	expect(h, true, "back on its workspace");

	/* Minimized. */
	xdg_toplevel_set_minimized(w->xdg_toplevel);
	harness_roundtrip(h);
	expect(h, false, "minimized");
	chord(h, KEY_LEFTALT, 0, KEY_TAB);
	expect(h, true, "restored");

	/* Unmapped. */
	harness_window_unmap(w);
	expect(h, false, "unmapped");
	harness_window_remap(w, 200, 150);
	expect(h, true, "mapped again");

	/* The surface goes first: the inhibitor stays, inert. */
	harness_window_destroy(w);
	expect(h, false, "the surface destroyed");
	zwp_idle_inhibitor_v1_destroy(inhibitor);
	harness_roundtrip(h);

	zwp_idle_inhibit_manager_v1_destroy(manager);
	wl_registry_destroy(registry);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
