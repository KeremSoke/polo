/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A ring reaches Polo with the toplevel it is for, if any, through
 * xdg_system_bell_v1 or GTK 3's gtk_shell1.system_bell. polo_test_v1's
 * bell-last reads "rings title". Marco's visual bell: fullscreen flashes
 * the output for a frame, frame_flash inverts a frame's focus style for
 * 100 ms.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gtk-shell-client-protocol.h"
#include "harness.h"
#include "xdg-system-bell-v1-client-protocol.h"

static struct xdg_system_bell_v1 *bell;
static struct wl_subcompositor *subcompositor;
static struct gtk_shell1 *gtk_shell;
static uint32_t gtk_shell_version;
static int capabilities = -1;

static void
shell_capabilities(void *data, struct gtk_shell1 *shell, uint32_t value)
{
	capabilities = (int)value;
}

static const struct gtk_shell1_listener shell_listener = {
	shell_capabilities,
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface, xdg_system_bell_v1_interface.name) == 0)
		bell = wl_registry_bind(registry, name,
					&xdg_system_bell_v1_interface, 1);
	else if (strcmp(interface, wl_subcompositor_interface.name) == 0)
		subcompositor = wl_registry_bind(
			registry, name, &wl_subcompositor_interface, 1);
	else if (strcmp(interface, gtk_shell1_interface.name) == 0) {
		gtk_shell_version = version;
		gtk_shell = wl_registry_bind(registry, name,
					     &gtk_shell1_interface, version);
		gtk_shell1_add_listener(gtk_shell, &shell_listener, NULL);
	}
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
pause_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static int
white_pixels(struct harness *h, int *total)
{
	int width, height, white = 0;
	uint32_t *pixels = harness_capture(h, &width, &height);

	for (int i = 0; pixels && i < width * height; i++)
		white += (pixels[i] & 0xffffff) == 0xffffff;
	free(pixels);
	*total = width * height;
	return white;
}

static uint32_t *
titlebar(struct harness *h, const struct harness_window_info *info, int *n)
{
	int width, height, top = info->y > 40 ? info->y - 40 : 0;
	uint32_t *pixels = harness_capture(h, &width, &height);
	uint32_t *strip = calloc((size_t)info->width * 40, sizeof *strip);

	*n = 0;
	for (int y = top; pixels && y < info->y && y < height; y++)
		for (int x = info->x; x < info->x + info->width && x < width;
		     x++)
			strip[(*n)++] = pixels[y * width + x] & 0xffffff;
	free(pixels);
	return strip;
}

static int
differing(const uint32_t *a, const uint32_t *b, int n)
{
	int count = 0;

	for (int i = 0; i < n; i++)
		count += a[i] != b[i];
	return count;
}

static void
test_visual_bell(struct harness *h)
{
	int total;

	harness_set_setting(h, "org.mate.Marco.general", "visual-bell", "true");
	harness_set_setting(h, "org.mate.Marco.general", "visual-bell-type",
			    "'fullscreen'");
	pause_ms(150);
	xdg_system_bell_v1_ring(bell, NULL);
	CHECK(white_pixels(h, &total) > total * 9 / 10,
	      "fullscreen did not flash the output");
	pause_ms(300);
	CHECK(white_pixels(h, &total) < total / 100, "the flash stayed");

	struct harness_window *w =
		harness_window_create_decorated(h, 300, 200, "flash");
	const struct harness_window_info *info = harness_window_info(w);
	int n;
	uint32_t *focused = titlebar(h, info, &n);

	harness_set_setting(h, "org.mate.Marco.general", "visual-bell-type",
			    "'frame_flash'");
	pause_ms(150);
	xdg_system_bell_v1_ring(bell, w->surface);
	harness_roundtrip(h);
	uint32_t *flashing = titlebar(h, info, &n);

	CHECK(differing(focused, flashing, n) > n / 20,
	      "frame_flash did not change the titlebar");
	CHECK(white_pixels(h, &total) < total / 2,
	      "frame_flash flashed the output");
	pause_ms(300);
	uint32_t *after = titlebar(h, info, &n);

	CHECK(differing(focused, after, n) == 0,
	      "the titlebar did not come back after the flash");
	free(focused);
	free(flashing);
	free(after);

	harness_window_destroy(w);
	harness_set_setting(h, "org.mate.Marco.general", "visual-bell",
			    "false");
}

static void
expect(struct harness *h, const char *wanted)
{
	const char *got = harness_get_setting(h, "polo-test", "bell-last");

	CHECK(strcmp(got, wanted) == 0, "bell-last is \"%s\", wanted \"%s\"",
	      got, wanted);
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
	struct wl_subsurface *sub = wl_subcompositor_get_subsurface(
		subcompositor, part, w->surface);

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
	struct xdg_surface *xdg =
		xdg_wm_base_get_xdg_surface(h->wm_base, early);
	struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg);

	xdg_system_bell_v1_ring(bell, early);
	expect(h, "5 ");
	xdg_toplevel_destroy(toplevel);
	xdg_surface_destroy(xdg);
	wl_surface_destroy(early);

	harness_roundtrip(h);
	CHECK(gtk_shell, "no gtk_shell1");
	CHECK(gtk_shell_version == 4, "gtk_shell1 version %u",
	      gtk_shell_version);
	CHECK(capabilities == 0, "capabilities %d", capabilities);
	struct gtk_surface1 *gtk_surface =
		gtk_shell1_get_gtk_surface(gtk_shell, w->surface);

	gtk_shell1_system_bell(gtk_shell, gtk_surface);
	expect(h, "6 ringer");
	gtk_shell1_system_bell(gtk_shell, NULL);
	expect(h, "7 ");
	gtk_surface1_set_modal(gtk_surface);
	gtk_surface1_unset_modal(gtk_surface);
	gtk_surface1_release(gtk_surface);
	harness_roundtrip(h);
	CHECK(wl_display_get_error(h->display) == 0, "gtk_surface1 errors");

	harness_window_destroy(w);
	xdg_system_bell_v1_ring(bell, NULL);
	expect(h, "8 ");

	test_visual_bell(h);

	xdg_system_bell_v1_destroy(bell);
	gtk_shell1_destroy(gtk_shell);
	wl_subcompositor_destroy(subcompositor);
	wl_registry_destroy(registry);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
