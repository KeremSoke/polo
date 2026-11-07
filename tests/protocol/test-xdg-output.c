/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zxdg_output_manager_v1 (provided by libweston) against headless
 * Polo: logical position, logical size and name of each output, initially
 * and after scale, transform and position changes and a reconnection.
 * Logical sizes are the mode divided by the scale, with width and height
 * swapped for 90° and 270° transforms.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#define WL_OUTPUT_TRANSFORM_90 1
#define WL_OUTPUT_TRANSFORM_FLIPPED_270 7

struct xdg_output_state {
	struct zxdg_output_v1 *xdg_output;
	int x, y, width, height;
	char name[64];
	char wl_name[64]; /* wl_output.name, for comparison */
	int dones;
};

static struct zxdg_output_manager_v1 *manager;
static struct xdg_output_state outputs[8];

static void
xdg_logical_position(void *data, struct zxdg_output_v1 *xdg_output, int32_t x,
		     int32_t y)
{
	struct xdg_output_state *o = data;

	o->x = x;
	o->y = y;
}

static void
xdg_logical_size(void *data, struct zxdg_output_v1 *xdg_output, int32_t width,
		 int32_t height)
{
	struct xdg_output_state *o = data;

	o->width = width;
	o->height = height;
}

static void
xdg_done(void *data, struct zxdg_output_v1 *xdg_output)
{
	struct xdg_output_state *o = data;

	o->dones++;
}

static void
xdg_name(void *data, struct zxdg_output_v1 *xdg_output, const char *name)
{
	struct xdg_output_state *o = data;

	snprintf(o->name, sizeof o->name, "%s", name);
}

static void
xdg_description(void *data, struct zxdg_output_v1 *xdg_output,
		const char *description)
{
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
	xdg_logical_position, xdg_logical_size, xdg_done, xdg_name,
	xdg_description,
};

/* wl_output.name, so the xdg name can be checked against it. */
static void
wl_geometry(void *data, struct wl_output *output, int32_t x, int32_t y,
	    int32_t pw, int32_t ph, int32_t subpixel, const char *make,
	    const char *model, int32_t transform)
{
}

static void
wl_mode(void *data, struct wl_output *output, uint32_t flags, int32_t width,
	int32_t height, int32_t refresh)
{
}

static void
wl_done(void *data, struct wl_output *output)
{
}

static void
wl_scale(void *data, struct wl_output *output, int32_t factor)
{
}

static void
wl_name(void *data, struct wl_output *output, const char *name)
{
	struct xdg_output_state *o = data;

	snprintf(o->wl_name, sizeof o->wl_name, "%s", name);
}

static void
wl_description(void *data, struct wl_output *output, const char *description)
{
}

static const struct wl_output_listener wl_output_listener = {
	wl_geometry, wl_mode, wl_done, wl_scale, wl_name, wl_description,
};

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0)
		manager = wl_registry_bind(registry, name,
					   &zxdg_output_manager_v1_interface,
					   version < 2 ? version : 2);
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove,
};

/* Bind xdg_output for every wl_output the harness knows. */
static void
bind_outputs(struct harness *h)
{
	int i;

	for (i = 0; i < h->n_outputs; i++) {
		struct xdg_output_state *o = &outputs[i];

		if (o->xdg_output)
			continue;
		memset(o, 0, sizeof *o);
		o->xdg_output = zxdg_output_manager_v1_get_xdg_output(
			manager, h->outputs[i]);
		zxdg_output_v1_add_listener(o->xdg_output, &xdg_output_listener,
					    o);
		/* The harness does not listen to its wl_outputs. */
		if (!wl_proxy_get_listener((struct wl_proxy *)h->outputs[i]))
			wl_output_add_listener(h->outputs[i],
					       &wl_output_listener, o);
	}
	harness_roundtrip(h);
	harness_roundtrip(h);
}

static void
check_output(int index, int x, int y, int width, int height, const char *name)
{
	const struct xdg_output_state *o = &outputs[index];

	CHECK(o->dones > 0, "output %d: no done", index);
	CHECK(o->x == x && o->y == y,
	      "output %d: logical position %d,%d, expected %d,%d", index, o->x,
	      o->y, x, y);
	CHECK(o->width == width && o->height == height,
	      "output %d: logical size %dx%d, expected %dx%d", index, o->width,
	      o->height, width, height);
	CHECK(strcmp(o->name, name) == 0, "output %d: name \"%s\", expected %s",
	      index, o->name, name);
}

/* Apply a test-protocol output configuration and wait for its done. */
static void
configure(struct harness *h, int index, int scale, int transform, int x, int y)
{
	int dones = outputs[index].dones;
	int i;

	polo_test_v1_configure_output(h->test, index, scale, transform, x, y);
	for (i = 0; i < 10 && outputs[index].dones == dones; i++)
		harness_roundtrip(h);
	CHECK(outputs[index].dones > dones,
	      "output %d: no done after a configuration change", index);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct wl_registry *registry;
	int w = HARNESS_OUTPUT_WIDTH, hgt = HARNESS_OUTPUT_HEIGHT;
	int status;

	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	h = harness_start(argc, argv);

	registry = wl_display_get_registry(h->display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(manager != NULL, "no zxdg_output_manager_v1");
	CHECK(h->n_outputs == 2, "expected two outputs, got %d", h->n_outputs);
	bind_outputs(h);

	/* Initial layout: side by side, scale 1, names as wl_output's. */
	check_output(0, 0, 0, w, hgt, "headless0");
	check_output(1, w, 0, w, hgt, "headless1");
	for (int i = 0; i < 2; i++)
		CHECK(outputs[i].wl_name[0] == '\0' ||
			      strcmp(outputs[i].wl_name, outputs[i].name) == 0,
		      "output %d: xdg name %s, wl_output name %s", i,
		      outputs[i].name, outputs[i].wl_name);

	/* Scale 2 halves the logical size. */
	configure(h, 0, 2, 0, 0, 0);
	check_output(0, 0, 0, w / 2, hgt / 2, "headless0");

	/* 90°: width and height swap. */
	configure(h, 0, 1, WL_OUTPUT_TRANSFORM_90, 0, 0);
	check_output(0, 0, 0, hgt, w, "headless0");

	/* Both, and a move to the left of the other output. */
	configure(h, 0, 2, WL_OUTPUT_TRANSFORM_90, -hgt / 2, 0);
	check_output(0, -hgt / 2, 0, hgt / 2, w / 2, "headless0");

	/* Flipped 270° swaps too. */
	configure(h, 1, 1, WL_OUTPUT_TRANSFORM_FLIPPED_270, w, 0);
	check_output(1, w, 0, hgt, w, "headless1");

	/* Back to normal. */
	configure(h, 0, 1, 0, 0, 0);
	configure(h, 1, 1, 0, w, 0);
	check_output(0, 0, 0, w, hgt, "headless0");
	check_output(1, w, 0, w, hgt, "headless1");

	/* Reconnect output 1: a new wl_output global, and a new xdg_output
	 * for it reports the same geometry and name. */
	polo_test_v1_output_connected(h->test, 1, 0);
	harness_roundtrip(h);
	CHECK(h->n_outputs == 1, "disconnected output still listed (%d)",
	      h->n_outputs);
	zxdg_output_v1_destroy(outputs[1].xdg_output);
	memset(&outputs[1], 0, sizeof outputs[1]);
	polo_test_v1_output_connected(h->test, 1, 1);
	harness_roundtrip(h);
	harness_roundtrip(h);
	CHECK(h->n_outputs == 2, "reconnected output not announced (%d)",
	      h->n_outputs);
	bind_outputs(h);
	check_output(1, w, 0, w, hgt, "headless1");

	for (int i = 0; i < 2; i++)
		if (outputs[i].xdg_output)
			zxdg_output_v1_destroy(outputs[i].xdg_output);
	zxdg_output_manager_v1_destroy(manager);
	wl_registry_destroy(registry);

	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
