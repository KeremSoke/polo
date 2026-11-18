/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate_output_v1.background_owner on two outputs, a transparent desktop
 * surface on the first: the owner changes exactly when what is drawn
 * changes, through seven steps (the desktop mapping, draw-background off and
 * on, the first monitor's own draw-background off and back to the shared
 * settings, draw-background off again and the desktop unmapping), and the
 * pixels agree.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "mate-output-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define RED 0xff0000u

#define NONE MATE_OUTPUT_V1_BACKGROUND_OWNER_NONE
#define CLIENT MATE_OUTPUT_V1_BACKGROUND_OWNER_CLIENT
#define COMPOSITOR MATE_OUTPUT_V1_BACKGROUND_OWNER_COMPOSITOR

static const int same_size[][3] = { { HARNESS_OUTPUT_WIDTH,
				      HARNESS_OUTPUT_HEIGHT, 60000 } };

struct out {
	struct wl_output *wl;
	struct mate_output_v1 *mate;
	char id[128];
	int owner, owner_events, dones;
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
	struct out *o = out_of(data, m);

	o->owner = owner;
	o->owner_events++;
}

static void
mo_done(void *data, struct mate_output_v1 *m)
{
	out_of(data, m)->dones++;
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

static void
settle(struct harness *h, struct client *c)
{
	for (int i = 0; i < 5; i++) {
		harness_roundtrip(h);
		wl_display_roundtrip(c->display);
		usleep(10000);
	}
}

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
expect_red(struct harness *h, int index, bool red, const char *what)
{
	uint32_t seen = 0;

	for (int i = 0; i < 100; i++) {
		seen = pixel_at(h, index);
		if ((seen == RED) == red)
			return;
		harness_roundtrip(h);
		usleep(20000);
	}
	CHECK(false, "%s: output %d shows 0x%06x, %s", what, index, seen,
	      red ? "not the compositor's red" : "still the compositor's red");
}

static void
step(struct harness *h, struct client *c, int a, int b, const char *what)
{
	static int last[2] = { -1, -1 };
	static int events[2];
	int want[2] = { a, b };

	for (int i = 0; i < 100; i++) {
		settle(h, c);
		if (c->outs[0].owner == a && c->outs[1].owner == b)
			break;
	}
	settle(h, c);
	for (int i = 0; i < 2; i++) {
		int got = c->outs[i].owner_events - events[i];

		CHECK(c->outs[i].owner == want[i],
		      "%s: output %d owner %d, not %d", what, i,
		      c->outs[i].owner, want[i]);
		if (last[i] != -1)
			CHECK(got == (last[i] != want[i]),
			      "%s: output %d got %d owner events for %d -> %d",
			      what, i, got, last[i], want[i]);
		last[i] = want[i];
		events[i] = c->outs[i].owner_events;
	}
	expect_red(h, 0, a == COMPOSITOR, what);
	expect_red(h, 1, b == COMPOSITOR, what);
}

static void
desktop_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
		  uint32_t serial, uint32_t width, uint32_t height)
{
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
}

static void
desktop_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
}

static const struct zwlr_layer_surface_v1_listener desktop_listener = {
	desktop_configure,
	desktop_closed,
};

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

	harness_set_setting(h, "org.mate.background", "picture-filename", "''");
	harness_set_setting(h, "org.mate.background", "color-shading-type",
			    "'solid'");
	harness_set_setting(h, "org.mate.background", "primary-color",
			    "'#ff0000'");

	struct client *c = calloc(1, sizeof *c);

	c->display = wl_display_connect(h->socket);
	CHECK(c->display, "cannot connect");
	c->registry = wl_display_get_registry(c->display);
	wl_registry_add_listener(c->registry, &registry_listener, c);
	wl_display_roundtrip(c->display);
	CHECK(c->manager, "no mate_output_manager_v1");
	CHECK(c->n_outs == 2, "%d wl_outputs on the client", c->n_outs);
	for (int i = 0; i < c->n_outs; i++) {
		c->outs[i].owner = -1;
		c->outs[i].mate = mate_output_manager_v1_get_output(
			c->manager, c->outs[i].wl);
		mate_output_v1_add_listener(c->outs[i].mate, &out_listener, c);
	}
	settle(h, c);
	CHECK(c->outs[0].id[0], "no id for the first output");
	snprintf(own, sizeof own,
		 "org.mate.background.monitor:/org/mate/desktop/background/"
		 "monitors/%s/",
		 c->outs[0].id);

	step(h, c, COMPOSITOR, COMPOSITOR, "no desktop");

	struct wl_surface *surface =
		wl_compositor_create_surface(h->compositor);
	struct zwlr_layer_surface_v1 *desktop =
		zwlr_layer_shell_v1_get_layer_surface(
			h->layer_shell, surface, h->outputs[0],
			ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "desktop");

	zwlr_layer_surface_v1_add_listener(desktop, &desktop_listener, h);
	zwlr_layer_surface_v1_set_anchor(
		desktop,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(desktop, -1);
	wl_surface_commit(surface);
	harness_roundtrip(h);

	struct wl_buffer *clear = harness_buffer_create_colour(
		h->shm, HARNESS_OUTPUT_WIDTH, HARNESS_OUTPUT_HEIGHT, 0);

	wl_surface_attach(surface, clear, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, HARNESS_OUTPUT_WIDTH,
				 HARNESS_OUTPUT_HEIGHT);
	harness_wait_frame(h, surface);
	step(h, c, CLIENT, COMPOSITOR, "the desktop mapped");

	harness_set_setting(h, "org.mate.background", "draw-background",
			    "false");
	step(h, c, COMPOSITOR, COMPOSITOR, "draw-background off");

	harness_set_setting(h, "org.mate.background", "draw-background",
			    "true");
	step(h, c, CLIENT, COMPOSITOR, "draw-background on");

	harness_set_setting(h, own, "color-shading-type", "'solid'");
	harness_set_setting(h, own, "primary-color", "'#ff0000'");
	harness_set_setting(h, own, "picture-filename", "''");
	harness_set_setting(h, own, "draw-background", "false");
	harness_set_setting(h, own, "use-shared", "false");
	step(h, c, COMPOSITOR, COMPOSITOR,
	     "the first monitor's own draw-background off");

	harness_set_setting(h, own, "use-shared", "true");
	step(h, c, CLIENT, COMPOSITOR, "the first monitor shared again");

	harness_set_setting(h, "org.mate.background", "draw-background",
			    "false");
	step(h, c, COMPOSITOR, COMPOSITOR, "draw-background off again");

	zwlr_layer_surface_v1_destroy(desktop);
	wl_surface_destroy(surface);
	wl_buffer_destroy(clear);
	harness_roundtrip(h);
	step(h, c, COMPOSITOR, COMPOSITOR,
	     "the desktop unmapped with draw-background off");

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
