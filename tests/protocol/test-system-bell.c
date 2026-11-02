/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A ring reaches Polo with the toplevel it is for, if any. polo_test_v1's
 * bell-last reads "rings title".
 */

#include <string.h>

#include "harness.h"
#include "xdg-system-bell-v1-client-protocol.h"

static struct xdg_system_bell_v1 *bell;
static struct wl_subcompositor *subcompositor;

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface, xdg_system_bell_v1_interface.name) == 0)
		bell = wl_registry_bind(registry, name,
					&xdg_system_bell_v1_interface, 1);
	else if (strcmp(interface, wl_subcompositor_interface.name) == 0)
		subcompositor = wl_registry_bind(registry, name,
						 &wl_subcompositor_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
expect(struct harness *h, const char *wanted)
{
	const char *got = harness_get_setting(h, "polo-test", "bell-last");

	CHECK(strcmp(got, wanted) == 0, "bell-last is \"%s\", wanted \"%s\"", got,
	      wanted);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(bell, "no xdg_system_bell_v1");
	expect(h, "0 ");

	xdg_system_bell_v1_ring(bell, NULL);
	expect(h, "1 ");

	struct harness_window *w = harness_window_create(h, 200, 150, "ringer");

	xdg_system_bell_v1_ring(bell, w->surface);
	expect(h, "2 ringer");

	/* A subsurface rings for its toplevel. */
	struct wl_surface *part = wl_compositor_create_surface(h->compositor);
	struct wl_subsurface *sub =
		wl_subcompositor_get_subsurface(subcompositor, part, w->surface);

	xdg_system_bell_v1_ring(bell, part);
	expect(h, "3 ringer");
	wl_subsurface_destroy(sub);
	wl_surface_destroy(part);

	/* A surface with no toplevel is the same as null. */
	struct wl_surface *child = wl_compositor_create_surface(h->compositor);

	xdg_system_bell_v1_ring(bell, child);
	expect(h, "4 ");
	wl_surface_destroy(child);

	/* A toplevel before its first commit has no Polo toplevel yet. */
	struct wl_surface *early = wl_compositor_create_surface(h->compositor);
	struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(h->wm_base, early);
	struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg);

	xdg_system_bell_v1_ring(bell, early);
	expect(h, "5 ");
	xdg_toplevel_destroy(toplevel);
	xdg_surface_destroy(xdg);
	wl_surface_destroy(early);

	harness_window_destroy(w);
	xdg_system_bell_v1_ring(bell, NULL);
	expect(h, "6 ");

	xdg_system_bell_v1_destroy(bell);
	wl_subcompositor_destroy(subcompositor);
	wl_registry_destroy(registry);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
