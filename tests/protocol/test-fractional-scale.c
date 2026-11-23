/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wp_fractional_scale_manager_v1: a surface is told the scale of the output
 * it is on, times 120, before and after it is mapped; a commit that changes
 * nothing sends nothing; and a second object for one surface is an error.
 *
 *   test-fractional-scale POLO SCALE
 */

#include <stdlib.h>
#include <string.h>

#include "fractional-scale-v1-client-protocol.h"
#include "harness.h"

static struct wp_fractional_scale_manager_v1 *manager;

struct preferred {
	uint32_t scale;
	int count;
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	(void)data;
	(void)version;
	if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) ==
	    0)
		manager = wl_registry_bind(
			registry, name,
			&wp_fractional_scale_manager_v1_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
preferred_scale(void *data, struct wp_fractional_scale_v1 *fs, uint32_t scale)
{
	struct preferred *p = data;

	(void)fs;
	p->scale = scale;
	p->count++;
}

static const struct wp_fractional_scale_v1_listener scale_listener = {
	preferred_scale,
};

static struct wp_fractional_scale_v1 *
watch(struct harness *h, struct wl_surface *surface, struct preferred *p)
{
	struct wp_fractional_scale_v1 *fs =
		wp_fractional_scale_manager_v1_get_fractional_scale(manager,
								    surface);

	wp_fractional_scale_v1_add_listener(fs, &scale_listener, p);
	for (int i = 0; i < 20 && p->count == 0; i++)
		harness_roundtrip(h);
	return fs;
}

int
main(int argc, char **argv)
{
	int scale = argc > 2 ? atoi(argv[2]) : 1;
	uint32_t expected = (uint32_t)scale * 120;
	struct harness *h;
	struct wl_registry *registry;
	struct harness_window *w;
	struct preferred early = { 0 }, mapped = { 0 };
	struct wl_surface *unmapped;
	struct wp_fractional_scale_v1 *early_fs, *mapped_fs, *dup;
	int count;

	if (scale == 2)
		setenv("POLO_TEST_SCALE", "--scale=2", 1);
	h = harness_start(argc, argv);
	registry = wl_display_get_registry(h->display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(manager, "no wp_fractional_scale_manager_v1 at scale %d", scale);

	unmapped = wl_compositor_create_surface(h->compositor);
	early_fs = watch(h, unmapped, &early);
	CHECK(early.count == 1 && early.scale == expected,
	      "a surface on no output got %d events, last %u, not %u",
	      early.count, early.scale, expected);

	w = harness_window_create(h, 200, 150, "fractional");
	mapped_fs = watch(h, w->surface, &mapped);
	CHECK(mapped.count == 1 && mapped.scale == expected,
	      "a mapped window got %d events, last %u, not %u", mapped.count,
	      mapped.scale, expected);

	count = mapped.count;
	wl_surface_damage_buffer(w->surface, 0, 0, 200, 150);
	wl_surface_commit(w->surface);
	harness_wait_repaint(h);
	harness_roundtrip(h);
	CHECK(mapped.count == count,
	      "a commit that changed nothing sent the scale again");

	wp_fractional_scale_v1_destroy(early_fs);
	early.count = 0;
	early_fs = watch(h, unmapped, &early);
	CHECK(early.count == 1 && early.scale == expected,
	      "a new object after destroying the old one got %d events, "
	      "last %u, not %u",
	      early.count, early.scale, expected);

	wp_fractional_scale_v1_destroy(mapped_fs);
	harness_window_destroy(w);

	dup = wp_fractional_scale_manager_v1_get_fractional_scale(manager,
								  unmapped);
	CHECK(wl_display_roundtrip(h->display) < 0,
	      "a second object for one surface was accepted");
	{
		const struct wl_interface *iface = NULL;
		uint32_t code =
			wl_display_get_protocol_error(h->display, &iface, NULL);

		CHECK(iface == &wp_fractional_scale_manager_v1_interface &&
			      code == WP_FRACTIONAL_SCALE_MANAGER_V1_ERROR_FRACTIONAL_SCALE_EXISTS,
		      "the second object raised %s error %u",
		      iface ? iface->name : "no", code);
	}

	wp_fractional_scale_v1_destroy(dup);
	wp_fractional_scale_v1_destroy(early_fs);
	wl_surface_destroy(unmapped);
	wp_fractional_scale_manager_v1_destroy(manager);
	wl_registry_destroy(registry);
	harness_stop(h);
	return 0;
}
