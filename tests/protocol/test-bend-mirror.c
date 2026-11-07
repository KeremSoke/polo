/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bent mirrors on GL: a mirror of a window with a subsurface, every view of
 * it deformed on its own (Backend.View.mirror_views ()), fills the asked
 * rectangle with both surfaces' colours and nothing outside it; removing it
 * leaves the screen as it was.
 */

#include <stdlib.h>
#include <string.h>

#include "harness.h"

#define WINDOW_COLOUR 0x3070c0u
#define PART_COLOUR 0xc04030u
#define PART_ARGB 0xffc04030u

static struct wl_subcompositor *subcompositor;

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface, wl_subcompositor_interface.name) == 0)
		subcompositor = wl_registry_bind(
			registry, name, &wl_subcompositor_interface, 1);
}

static void
registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_remove,
};

struct counts {
	int window_inside, part_inside, window_outside, part_outside;
};

static struct counts
count(struct harness *h, int x, int y, int width, int height)
{
	int w, hh;
	uint32_t *pixels = harness_capture(h, &w, &hh);
	struct counts c = { 0 };

	CHECK(pixels, "cannot capture");
	for (int i = 0; i < w * hh; i++) {
		int px = i % w, py = i / w;
		bool inside =
			px >= x && px < x + width && py >= y && py < y + height;
		uint32_t colour = pixels[i] & 0xffffff;

		if (colour == WINDOW_COLOUR)
			*(inside ? &c.window_inside : &c.window_outside) += 1;
		else if (colour == PART_COLOUR)
			*(inside ? &c.part_inside : &c.part_outside) += 1;
	}
	free(pixels);
	return c;
}

static void
bend(struct harness *h, struct wl_surface *surface, int x, int y, int width,
     int height)
{
	polo_test_v1_bend_mirror(h->test, surface, x, y, width, height);
	harness_roundtrip(h);
	harness_wait_repaint(h);
}

int
main(int argc, char **argv)
{
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	struct harness *h = harness_start(argc, argv);
	struct harness_window *window =
		harness_window_create(h, 300, 200, "bend");
	const struct harness_window_info *info = harness_window_info(window);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(subcompositor, "no wl_subcompositor");
	struct wl_surface *part = wl_compositor_create_surface(h->compositor);
	struct wl_subsurface *sub = wl_subcompositor_get_subsurface(
		subcompositor, part, window->surface);
	struct wl_buffer *part_buffer =
		harness_buffer_create_colour(h->shm, 60, 60, PART_ARGB);

	wl_subsurface_set_position(sub, 270, 170);
	wl_subsurface_set_desync(sub);
	wl_surface_attach(part, part_buffer, 0, 0);
	wl_surface_damage_buffer(part, 0, 0, 60, 60);
	wl_surface_commit(part);
	harness_wait_frame(h, window->surface);

	/* Half size, away from the window: the tree is 330 x 230. */
	int width = 165, height = 115;
	int x = info->x > width + 20 ? 10 : HARNESS_OUTPUT_WIDTH - width - 10;
	int y = info->y > height + 20 ? 10
				      : HARNESS_OUTPUT_HEIGHT - height - 10;
	struct counts before = count(h, x, y, width, height);

	CHECK(before.window_inside == 0 && before.part_inside == 0 &&
		      before.part_outside == 60 * 60,
	      "the window overlaps %d,%d %dx%d", x, y, width, height);

	bend(h, window->surface, x, y, width, height);
	struct counts bent = count(h, x, y, width, height);

	CHECK(bent.window_outside == before.window_outside &&
		      bent.part_outside == before.part_outside,
	      "the mirror spills out of its rectangle: %d and %d pixels "
	      "outside, %d and %d before",
	      bent.window_outside, bent.part_outside, before.window_outside,
	      before.part_outside);
	CHECK(abs(bent.part_inside - 30 * 30) <= 2 * 30 + 1,
	      "the subsurface shows %d pixels bent, not about %d",
	      bent.part_inside, 30 * 30);
	CHECK(abs(bent.window_inside - (150 * 100 - 15 * 15)) <= 2 * 150,
	      "the window shows %d pixels bent, not about %d",
	      bent.window_inside, 150 * 100 - 15 * 15);

	bend(h, window->surface, x, y, 0, 0);
	struct counts after = count(h, x, y, width, height);

	CHECK(memcmp(&after, &before, sizeof(after)) == 0,
	      "removing the mirror left %d and %d pixels in its rectangle",
	      after.window_inside, after.part_inside);

	bend(h, window->surface, x, y, width, height);
	wl_subsurface_destroy(sub);
	wl_surface_destroy(part);
	harness_roundtrip(h);
	harness_wait_repaint(h);
	bent = count(h, x, y, width, height);
	CHECK(bent.part_inside == 0 && bent.window_inside > 150 * 100 - 400,
	      "after its subsurface went, the mirror shows %d and %d pixels",
	      bent.window_inside, bent.part_inside);

	wl_buffer_destroy(part_buffer);
	wl_subcompositor_destroy(subcompositor);
	wl_registry_destroy(registry);
	harness_window_destroy(window);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
