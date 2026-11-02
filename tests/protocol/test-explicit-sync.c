/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * libweston's zwp_linux_explicit_synchronization_v1 reaches clients through Polo's
 * global filter, and an object on a surface works.
 */

#include <string.h>

#include "harness.h"
#include "linux-explicit-synchronization-unstable-v1-client-protocol.h"

static struct zwp_linux_explicit_synchronization_v1 *manager;

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface,
		   zwp_linux_explicit_synchronization_v1_interface.name) == 0)
		manager = wl_registry_bind(
			registry, name,
			&zwp_linux_explicit_synchronization_v1_interface, 2);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(manager, "no zwp_linux_explicit_synchronization_v1");

	struct harness_window *w = harness_window_create(h, 200, 150, "sync");
	struct zwp_linux_surface_synchronization_v1 *sync =
		zwp_linux_explicit_synchronization_v1_get_synchronization(manager,
									  w->surface);

	harness_roundtrip(h);
	zwp_linux_surface_synchronization_v1_destroy(sync);
	zwp_linux_explicit_synchronization_v1_destroy(manager);
	wl_registry_destroy(registry);
	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
