/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * org.mate.polo.background.workspace: a workspace with its own wallpaper
 * shows it on the monitors that use the shared settings while it is the
 * current workspace, a monitor with its own settings keeps them on every
 * workspace, and turning use-shared back on brings the shared wallpaper.
 */
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "mate-output-v1-client-protocol.h"

#define RED 0xff0000u
#define GREEN 0x00ff00u
#define BLUE 0x0000ffu

#define WORKSPACE_2                                                            \
	"org.mate.polo.background.workspace:/org/mate/polo/background/"        \
	"workspaces/2/"

static const int same_size[][3] = { { HARNESS_OUTPUT_WIDTH,
				      HARNESS_OUTPUT_HEIGHT, 60000 } };

struct out {
	struct wl_output *wl;
	struct mate_output_v1 *mate;
	char id[128];
};

struct client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct mate_output_manager_v1 *manager;
	struct out outs[4];
	int n_outs;
};

static struct out *
out_of(struct client *c, struct mate_output_v1 *m)
{
	for (int i = 0; i < c->n_outs; i++) {
		if (c->outs[i].mate == m)
			return &c->outs[i];
	}
	CHECK(false, "unknown mate_output_v1");
	return NULL;
}

static void
mo_id(void *data, struct mate_output_v1 *m, const char *id)
{
	struct out *o = out_of(data, m);

	snprintf(o->id, sizeof o->id, "%s", id);
}

static void
mo_identity(void *data, struct mate_output_v1 *m, const char *vendor,
	    uint32_t product, uint32_t serial, const char *serial_string)
{
}

static void
mo_edid(void *data, struct mate_output_v1 *m, int32_t fd, uint32_t size)
{
	close(fd);
}

static void
mo_string(void *data, struct mate_output_v1 *m, const char *s)
{
}

static void
mo_uint(void *data, struct mate_output_v1 *m, uint32_t v)
{
}

static void
mo_workarea(void *data, struct mate_output_v1 *m, int32_t x, int32_t y,
	    uint32_t width, uint32_t height)
{
}

static void
mo_owner(void *data, struct mate_output_v1 *m, uint32_t owner)
{
}

static void
mo_done(void *data, struct mate_output_v1 *m)
{
}

static const struct mate_output_v1_listener out_listener = {
	mo_id,   mo_identity, mo_edid,     mo_string, mo_uint,
	mo_uint, mo_string,   mo_workarea, mo_owner,  mo_done,
};

static void
capabilities(void *data, struct mate_output_manager_v1 *m, uint32_t caps)
{
}

static void
manager_finished(void *data, struct mate_output_manager_v1 *m)
{
}

static const struct mate_output_manager_v1_listener manager_listener = {
	capabilities, manager_finished
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct client *c = data;

	if (strcmp(interface, mate_output_manager_v1_interface.name) == 0) {
		c->manager = wl_registry_bind(
			registry, name, &mate_output_manager_v1_interface, 1);
		mate_output_manager_v1_add_listener(c->manager,
						    &manager_listener, c);
	} else if (strcmp(interface, wl_output_interface.name) == 0 &&
		   c->n_outs < 4) {
		c->outs[c->n_outs++].wl = wl_registry_bind(
			registry, name, &wl_output_interface, 2);
	}
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static uint32_t
pixel_at(struct harness *h, int index)
{
	int width, height;
	uint32_t *pixels = harness_capture_output(h, index, &width, &height);
	uint32_t pixel;

	CHECK(pixels, "cannot capture output %d", index);
	pixel = pixels[(height / 2) * width + width / 2] & 0xffffff;
	free(pixels);
	return pixel;
}

static void
expect(struct harness *h, uint32_t first, uint32_t second, const char *what)
{
	uint32_t seen[2] = { 0, 0 };

	for (int i = 0; i < 150; i++) {
		seen[0] = pixel_at(h, 0);
		seen[1] = pixel_at(h, 1);
		if (seen[0] == first && seen[1] == second)
			return;
		harness_roundtrip(h);
		usleep(20000);
	}
	CHECK(false, "%s: outputs show 0x%06x 0x%06x, not 0x%06x 0x%06x", what,
	      seen[0], seen[1], first, second);
}

static void
switch_workspace(struct harness *h, uint32_t arrow)
{
	harness_key(h, KEY_LEFTCTRL, true);
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, arrow, true);
	harness_key(h, arrow, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_key(h, KEY_LEFTCTRL, false);
	harness_roundtrip(h);
}

static void
solid(struct harness *h, const char *schema, const char *colour)
{
	harness_set_setting(h, schema, "picture-filename", "''");
	harness_set_setting(h, schema, "color-shading-type", "'solid'");
	harness_set_setting(h, schema, "primary-color", colour);
}

int
main(int argc, char **argv)
{
	char own[256];

	setenv("POLO_TEST_MANAGED_OUTPUTS", "1", 1);
	struct harness *h = harness_start(argc, argv);

	harness_add_output(h, "DP-1", "QQA", 0x0101, 11, "Second", false, 340,
			   270, same_size, 1);
	for (int i = 0; i < 100 && h->n_outputs < 2; i++)
		harness_roundtrip(h);
	CHECK(h->n_outputs == 2, "%d wl_outputs", h->n_outputs);

	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "2");
	solid(h, "org.mate.background", "'#ff0000'");
	expect(h, RED, RED, "the shared wallpaper");

	struct client *c = calloc(1, sizeof *c);

	c->display = wl_display_connect(h->socket);
	CHECK(c->display, "cannot connect");
	c->registry = wl_display_get_registry(c->display);
	wl_registry_add_listener(c->registry, &registry_listener, c);
	wl_display_roundtrip(c->display);
	CHECK(c->manager, "no mate_output_manager_v1");
	CHECK(c->n_outs == 2, "%d wl_outputs on the client", c->n_outs);
	for (int i = 0; i < c->n_outs; i++) {
		c->outs[i].mate = mate_output_manager_v1_get_output(
			c->manager, c->outs[i].wl);
		mate_output_v1_add_listener(c->outs[i].mate, &out_listener, c);
	}
	wl_display_roundtrip(c->display);
	wl_display_roundtrip(c->display);
	CHECK(c->outs[0].id[0], "no id for the first output");
	snprintf(own, sizeof own,
		 "org.mate.background.monitor:/org/mate/desktop/background/"
		 "monitors/%s/",
		 c->outs[0].id);

	solid(h, WORKSPACE_2, "'#00ff00'");
	harness_set_setting(h, WORKSPACE_2, "use-shared", "false");
	expect(h, RED, RED,
	       "the second workspace's own wallpaper, on the first");

	switch_workspace(h, KEY_RIGHT);
	expect(h, GREEN, GREEN, "on the second workspace");

	solid(h, own, "'#0000ff'");
	harness_set_setting(h, own, "use-shared", "false");
	expect(h, BLUE, GREEN, "the first monitor's own wallpaper");

	switch_workspace(h, KEY_LEFT);
	expect(h, BLUE, RED, "back on the first workspace");

	harness_set_setting(h, own, "use-shared", "true");
	expect(h, RED, RED, "the first monitor shared again");

	switch_workspace(h, KEY_RIGHT);
	expect(h, GREEN, GREEN, "the second workspace again");

	harness_set_setting(h, WORKSPACE_2, "use-shared", "false");
	harness_set_setting(h, WORKSPACE_2, "use-shared", "true");
	expect(h, RED, RED, "the second workspace shared again");

	for (int i = 0; i < c->n_outs; i++) {
		mate_output_v1_destroy(c->outs[i].mate);
		wl_output_destroy(c->outs[i].wl);
	}
	mate_output_manager_v1_destroy(c->manager);
	wl_registry_destroy(c->registry);
	wl_display_disconnect(c->display);
	free(c);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
