/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwlr_output_manager_v1 v4 on two managed headless outputs: heads and
 * modes, test, apply (seen in the next done), a stale serial cancelled,
 * refusals (every head off, unsupported adaptive sync) and protocol errors
 * (a head left out, another head's mode).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"

struct mode {
	struct zwlr_output_mode_v1 *mode;
	int width, height, refresh;
};

struct head {
	struct zwlr_output_head_v1 *head;
	char name[64];
	struct mode modes[16];
	int n_modes;
	struct zwlr_output_mode_v1 *current;
	int enabled, x, y, transform;
	wl_fixed_t scale;
};

struct client {
	struct wl_display *display;
	struct zwlr_output_manager_v1 *manager;
	struct head heads[4];
	int n_heads;
	uint32_t serial;
	int dones;
};

enum { PENDING, SUCCEEDED, FAILED, CANCELLED };

static struct head *
head_of(struct client *c, struct zwlr_output_head_v1 *head)
{
	for (int i = 0; i < c->n_heads; i++) {
		if (c->heads[i].head == head)
			return &c->heads[i];
	}
	return NULL;
}

static struct mode *
mode_of(struct client *c, struct zwlr_output_mode_v1 *mode)
{
	for (int i = 0; i < c->n_heads; i++) {
		for (int j = 0; j < c->heads[i].n_modes; j++) {
			if (c->heads[i].modes[j].mode == mode)
				return &c->heads[i].modes[j];
		}
	}
	return NULL;
}

static struct zwlr_output_mode_v1 *
mode_to_set(struct head *h)
{
	return h->current ? h->current : h->modes[0].mode;
}

static void
mode_size(void *data, struct zwlr_output_mode_v1 *mode, int32_t w, int32_t h)
{
	struct mode *m = mode_of(data, mode);

	m->width = w;
	m->height = h;
}

static void
mode_refresh(void *data, struct zwlr_output_mode_v1 *mode, int32_t refresh)
{
	mode_of(data, mode)->refresh = refresh;
}

static void
mode_nothing(void *data, struct zwlr_output_mode_v1 *mode)
{
}

static const struct zwlr_output_mode_v1_listener mode_listener = {
	mode_size,
	mode_refresh,
	mode_nothing,
	mode_nothing,
};

static void
head_name(void *data, struct zwlr_output_head_v1 *head, const char *name)
{
	snprintf(head_of(data, head)->name, 64, "%s", name);
}

static void
head_string(void *data, struct zwlr_output_head_v1 *head, const char *s)
{
}

static void
head_physical_size(void *data, struct zwlr_output_head_v1 *head, int32_t w,
		   int32_t h)
{
}

static void
head_mode(void *data, struct zwlr_output_head_v1 *head,
	  struct zwlr_output_mode_v1 *mode)
{
	struct head *h = head_of(data, head);

	h->modes[h->n_modes++].mode = mode;
	zwlr_output_mode_v1_add_listener(mode, &mode_listener, data);
}

static void
head_enabled(void *data, struct zwlr_output_head_v1 *head, int32_t enabled)
{
	head_of(data, head)->enabled = enabled;
}

static void
head_current_mode(void *data, struct zwlr_output_head_v1 *head,
		  struct zwlr_output_mode_v1 *mode)
{
	head_of(data, head)->current = mode;
}

static void
head_position(void *data, struct zwlr_output_head_v1 *head, int32_t x,
	      int32_t y)
{
	head_of(data, head)->x = x;
	head_of(data, head)->y = y;
}

static void
head_transform(void *data, struct zwlr_output_head_v1 *head, int32_t t)
{
	head_of(data, head)->transform = t;
}

static void
head_scale(void *data, struct zwlr_output_head_v1 *head, wl_fixed_t scale)
{
	head_of(data, head)->scale = scale;
}

static void
head_finished(void *data, struct zwlr_output_head_v1 *head)
{
}

static void
head_adaptive_sync(void *data, struct zwlr_output_head_v1 *head, uint32_t state)
{
}

static const struct zwlr_output_head_v1_listener head_listener = {
	.name = head_name,
	.description = head_string,
	.physical_size = head_physical_size,
	.mode = head_mode,
	.enabled = head_enabled,
	.current_mode = head_current_mode,
	.position = head_position,
	.transform = head_transform,
	.scale = head_scale,
	.finished = head_finished,
	.make = head_string,
	.model = head_string,
	.serial_number = head_string,
	.adaptive_sync = head_adaptive_sync,
};

static void
manager_head(void *data, struct zwlr_output_manager_v1 *manager,
	     struct zwlr_output_head_v1 *head)
{
	struct client *c = data;

	c->heads[c->n_heads++].head = head;
	zwlr_output_head_v1_add_listener(head, &head_listener, c);
}

static void
manager_done(void *data, struct zwlr_output_manager_v1 *manager,
	     uint32_t serial)
{
	struct client *c = data;

	c->serial = serial;
	c->dones++;
}

static void
manager_finished(void *data, struct zwlr_output_manager_v1 *manager)
{
}

static const struct zwlr_output_manager_v1_listener manager_listener = {
	manager_head,
	manager_done,
	manager_finished,
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct client *c = data;

	if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0 &&
	    version >= 4) {
		c->manager = wl_registry_bind(
			registry, name, &zwlr_output_manager_v1_interface, 4);
		zwlr_output_manager_v1_add_listener(c->manager,
						    &manager_listener, c);
	}
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static struct client *
connect_client(struct harness *h)
{
	struct client *c = calloc(1, sizeof *c);
	struct wl_registry *registry;

	c->display = wl_display_connect(h->socket);
	CHECK(c->display, "cannot connect");
	registry = wl_display_get_registry(c->display);
	wl_registry_add_listener(registry, &registry_listener, c);
	wl_display_roundtrip(c->display);
	CHECK(c->manager, "no zwlr_output_manager_v1 version 4");
	wl_display_roundtrip(c->display);
	wl_display_roundtrip(c->display);
	return c;
}

static void
config_succeeded(void *data, struct zwlr_output_configuration_v1 *config)
{
	*(int *)data = SUCCEEDED;
}

static void
config_failed(void *data, struct zwlr_output_configuration_v1 *config)
{
	*(int *)data = FAILED;
}

static void
config_cancelled(void *data, struct zwlr_output_configuration_v1 *config)
{
	*(int *)data = CANCELLED;
}

static const struct zwlr_output_configuration_v1_listener config_listener = {
	config_succeeded,
	config_failed,
	config_cancelled,
};

static int
run(struct harness *h, struct client *c, uint32_t serial, int test, int x2,
    int all_off, int adaptive_sync, wl_fixed_t scale)
{
	struct zwlr_output_configuration_v1 *config =
		zwlr_output_manager_v1_create_configuration(c->manager, serial);
	int result = PENDING;

	zwlr_output_configuration_v1_add_listener(config, &config_listener,
						  &result);
	for (int i = 0; i < c->n_heads; i++) {
		struct zwlr_output_configuration_head_v1 *ch;

		if (all_off) {
			zwlr_output_configuration_v1_disable_head(
				config, c->heads[i].head);
			continue;
		}
		ch = zwlr_output_configuration_v1_enable_head(config,
							      c->heads[i].head);
		zwlr_output_configuration_head_v1_set_mode(
			ch, mode_to_set(&c->heads[i]));
		if (i == 1)
			zwlr_output_configuration_head_v1_set_position(ch, x2,
								       0);
		if (scale)
			zwlr_output_configuration_head_v1_set_scale(ch, scale);
		if (adaptive_sync)
			zwlr_output_configuration_head_v1_set_adaptive_sync(
				ch,
				ZWLR_OUTPUT_HEAD_V1_ADAPTIVE_SYNC_STATE_ENABLED);
	}
	if (test)
		zwlr_output_configuration_v1_test(config);
	else
		zwlr_output_configuration_v1_apply(config);
	for (int i = 0; i < 100 && result == PENDING; i++) {
		wl_display_roundtrip(c->display);
		harness_roundtrip(h);
	}
	zwlr_output_configuration_v1_destroy(config);
	wl_display_roundtrip(c->display);
	return result;
}

int
main(int argc, char **argv)
{
	setenv("POLO_TEST_MANAGED_OUTPUTS", "1", 1);
	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	struct harness *h = harness_start(argc, argv);
	struct client *c = connect_client(h);

	CHECK(c->n_heads == 2, "expected two heads, got %d", c->n_heads);
	for (int i = 0; i < c->n_heads; i++) {
		CHECK(c->heads[i].name[0], "head %d has no name", i);
		CHECK(c->heads[i].n_modes > 0, "head %s has no modes",
		      c->heads[i].name);
	}
	CHECK(c->heads[0].enabled && c->heads[0].current,
	      "the first head is not on");
	CHECK(c->heads[0].scale == wl_fixed_from_int(1),
	      "the first head's scale is not 1");
	CHECK(!c->heads[1].enabled,
	      "an external head was turned on at startup");
	struct mode *first = mode_of(c, c->heads[0].current);
	int x2 = first->width + 100;

	CHECK(run(h, c, c->serial, 1, x2, 0, 0, 0) == SUCCEEDED,
	      "a valid configuration did not test");
	CHECK(c->heads[1].x != x2, "test moved the output");

	int dones = c->dones;

	CHECK(run(h, c, c->serial, 0, x2, 0, 0, 0) == SUCCEEDED,
	      "a valid configuration was not applied");
	for (int i = 0; i < 50 && c->dones == dones; i++) {
		wl_display_roundtrip(c->display);
		harness_roundtrip(h);
	}
	CHECK(c->heads[1].x == x2 && c->heads[1].y == 0,
	      "apply did not move %s to %d,0 (it is at %d,%d)",
	      c->heads[1].name, x2, c->heads[1].x, c->heads[1].y);

	CHECK(run(h, c, c->serial - 1, 0, 0, 0, 0, 0) == CANCELLED,
	      "a stale serial was not cancelled");
	CHECK(run(h, c, c->serial, 0, 0, 1, 0, 0) == FAILED,
	      "turning every head off was accepted");
	CHECK(run(h, c, c->serial, 0, x2, 0, 1, 0) == FAILED,
	      "adaptive sync on a head without it was accepted");
	CHECK(run(h, c, c->serial, 1, x2, 0, 0, wl_fixed_from_double(1.5)) ==
		      SUCCEEDED,
	      "a fractional scale did not test (it rounds)");
	CHECK(c->heads[1].x == x2, "a refused configuration moved the output");

	struct client *bad = connect_client(h);
	struct zwlr_output_configuration_v1 *config =
		zwlr_output_manager_v1_create_configuration(bad->manager,
							    bad->serial);

	zwlr_output_configuration_v1_enable_head(config, bad->heads[0].head);
	zwlr_output_configuration_v1_apply(config);
	wl_display_roundtrip(bad->display);
	CHECK(wl_display_get_error(bad->display) != 0,
	      "a configuration without every head was applied");
	wl_display_disconnect(bad->display);
	free(bad);

	bad = connect_client(h);
	config = zwlr_output_manager_v1_create_configuration(bad->manager,
							     bad->serial);
	struct zwlr_output_configuration_head_v1 *ch =
		zwlr_output_configuration_v1_enable_head(config,
							 bad->heads[0].head);

	zwlr_output_configuration_head_v1_set_mode(ch,
						   mode_to_set(&bad->heads[1]));
	wl_display_roundtrip(bad->display);
	CHECK(wl_display_get_error(bad->display) != 0,
	      "another head's mode was accepted");
	wl_display_disconnect(bad->display);
	free(bad);

	wl_display_disconnect(c->display);
	free(c);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
