/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Writes the interface of every global the compositor offers this client to
 * argv[1], one per line, then "done".
 */

#include <stdio.h>
#include <wayland-client.h>

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	fprintf(data, "%s\n", interface);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener listener = { global, global_remove };

int
main(int argc, char **argv)
{
	struct wl_display *display;
	struct wl_registry *registry;
	FILE *out;

	if (argc < 2)
		return 2;
	display = wl_display_connect(NULL);
	if (!display)
		return 3;
	out = fopen(argv[1], "w");
	if (!out)
		return 4;
	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &listener, out);
	wl_display_roundtrip(display);
	fprintf(out, "done\n");
	fclose(out);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return 0;
}
