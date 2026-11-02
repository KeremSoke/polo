/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Maps a toplevel or a layer surface, then stays. Appends "start NAME" and,
 * just before the commit that maps, "mapping NAME" to the file in argv[1].
 *
 *   map-client LOG NAME window|layer [delay-ms]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wl_surface *surface;
static int width = 64, height = 64;
static int configured;

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
		   uint32_t version)
{
	if (!strcmp(interface, wl_compositor_interface.name))
		compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	else if (!strcmp(interface, wl_shm_interface.name))
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (!strcmp(interface, xdg_wm_base_interface.name))
		wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
	else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name))
		layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global, global_remove };

static void ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
	xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = { ping };

static void surface_configure(void *data, struct xdg_surface *xdg, uint32_t serial)
{
	xdg_surface_ack_configure(xdg, serial);
	configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = { surface_configure };

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t w, int32_t h,
			       struct wl_array *states)
{
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *layer, uint32_t serial,
			    uint32_t w, uint32_t h)
{
	zwlr_layer_surface_v1_ack_configure(layer, serial);
	if (w)
		width = w;
	if (h)
		height = h;
	configured = 1;
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *layer)
{
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = layer_configure,
	.closed = layer_closed,
};

static void mark(const char *path, const char *word, const char *name)
{
	FILE *out = fopen(path, "a");

	if (out) {
		fprintf(out, "%s %s\n", word, name);
		fclose(out);
	}
}

static void attach(void)
{
	int size = width * height * 4;
	int fd = memfd_create("map-client", 0);
	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;

	if (fd < 0 || ftruncate(fd, size) < 0)
		exit(1);
	pool = wl_shm_create_pool(shm, fd, size);
	buffer = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_ARGB8888);
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);
}

int main(int argc, char **argv)
{
	struct wl_display *display;
	struct wl_registry *registry;

	if (argc < 4)
		return 2;
	mark(argv[1], "start", argv[2]);
	if (argc > 4)
		usleep(atoi(argv[4]) * 1000);

	display = wl_display_connect(NULL);
	if (!display)
		return 1;
	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!compositor || !shm)
		return 1;

	surface = wl_compositor_create_surface(compositor);
	if (!strcmp(argv[3], "layer")) {
		struct zwlr_layer_surface_v1 *layer;

		if (!layer_shell)
			return 1;
		layer = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface, NULL,
							      ZWLR_LAYER_SHELL_V1_LAYER_TOP, "map-client");
		zwlr_layer_surface_v1_set_size(layer, 64, 64);
		zwlr_layer_surface_v1_add_listener(layer, &layer_listener, NULL);
	} else {
		struct xdg_surface *xdg;
		struct xdg_toplevel *toplevel;

		if (!wm_base)
			return 1;
		xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
		xdg = xdg_wm_base_get_xdg_surface(wm_base, surface);
		xdg_surface_add_listener(xdg, &xdg_surface_listener, NULL);
		toplevel = xdg_surface_get_toplevel(xdg);
		xdg_toplevel_add_listener(toplevel, &toplevel_listener, NULL);
	}
	wl_surface_commit(surface);
	while (!configured && wl_display_dispatch(display) != -1)
		;
	mark(argv[1], "mapping", argv[2]);
	attach();
	wl_display_roundtrip(display);
	while (wl_display_dispatch(display) != -1)
		;
	return 0;
}
