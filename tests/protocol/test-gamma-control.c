/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwlr_gamma_control_manager_v1 on the headless output's 256-entry ramp:
 * the first control gets gamma_size and sets a ramp; a second one on the
 * same output fails until the first is destroyed or its client goes; a
 * short table is invalid_gamma.
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "harness.h"
#include "wlr-gamma-control-unstable-v1-client-protocol.h"

struct client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct zwlr_gamma_control_manager_v1 *manager;
	struct wl_output *output;
};

struct control {
	struct zwlr_gamma_control_v1 *proxy;
	uint32_t size;
	bool failed;
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct client *c = data;

	if (strcmp(interface, zwlr_gamma_control_manager_v1_interface.name) ==
	    0)
		c->manager = wl_registry_bind(
			registry, name,
			&zwlr_gamma_control_manager_v1_interface, 1);
	else if (strcmp(interface, wl_output_interface.name) == 0 && !c->output)
		c->output = wl_registry_bind(registry, name,
					     &wl_output_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
control_size(void *data, struct zwlr_gamma_control_v1 *proxy, uint32_t size)
{
	((struct control *)data)->size = size;
}

static void
control_failed(void *data, struct zwlr_gamma_control_v1 *proxy)
{
	((struct control *)data)->failed = true;
}

static const struct zwlr_gamma_control_v1_listener control_listener = {
	control_size,
	control_failed,
};

static struct client *
connect_client(struct harness *h)
{
	struct client *c = calloc(1, sizeof *c);

	c->display = wl_display_connect(h->socket);
	CHECK(c->display, "cannot connect");
	c->registry = wl_display_get_registry(c->display);
	wl_registry_add_listener(c->registry, &registry_listener, c);
	wl_display_roundtrip(c->display);
	CHECK(c->manager && c->output, "no gamma control manager or output");
	return c;
}

static void
client_free(struct client *c)
{
	zwlr_gamma_control_manager_v1_destroy(c->manager);
	wl_output_destroy(c->output);
	wl_registry_destroy(c->registry);
	wl_display_disconnect(c->display);
	free(c);
}

static void
get_control(struct client *c, struct control *control)
{
	memset(control, 0, sizeof *control);
	control->proxy = zwlr_gamma_control_manager_v1_get_gamma_control(
		c->manager, c->output);
	zwlr_gamma_control_v1_add_listener(control->proxy, &control_listener,
					   control);
	wl_display_roundtrip(c->display);
}

static int
ramp_fd(uint32_t entries)
{
	size_t bytes = 3 * (size_t)entries * sizeof(uint16_t);
	int fd = memfd_create("gamma", MFD_CLOEXEC);
	uint16_t *table;

	if (fd < 0 || ftruncate(fd, (off_t)bytes) != 0)
		return -1;
	table = mmap(NULL, bytes, PROT_WRITE, MAP_SHARED, fd, 0);
	if (table == MAP_FAILED)
		return -1;
	for (uint32_t i = 0; i < entries; i++) {
		uint16_t v = (uint16_t)(0xffff * i /
					(entries > 1 ? entries - 1 : 1));

		table[i] = v;
		table[entries + i] = (uint16_t)(v * 0.85);
		table[2 * entries + i] = (uint16_t)(v * 0.6);
	}
	munmap(table, bytes);
	return fd;
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct client *a = connect_client(h), *b = connect_client(h);
	struct control first, second;

	get_control(a, &first);
	CHECK(first.size == 256 && !first.failed,
	      "the first control got size %u, failed %d", first.size,
	      first.failed);

	int fd = ramp_fd(first.size);

	CHECK(fd >= 0, "cannot make a ramp");
	zwlr_gamma_control_v1_set_gamma(first.proxy, fd);
	close(fd);
	wl_display_roundtrip(a->display);
	CHECK(wl_display_get_error(a->display) == 0, "a full ramp was refused");

	get_control(b, &second);
	CHECK(second.failed && second.size == 0,
	      "a second control on a held output did not fail");
	zwlr_gamma_control_v1_destroy(second.proxy);

	zwlr_gamma_control_v1_destroy(first.proxy);
	wl_display_roundtrip(a->display);
	get_control(b, &second);
	CHECK(second.size == 256 && !second.failed,
	      "the output stayed held after its control went");

	wl_proxy_destroy((struct wl_proxy *)second.proxy);
	client_free(b);
	harness_roundtrip(h);
	get_control(a, &first);
	CHECK(first.size == 256 && !first.failed,
	      "the output stayed held after its client went");

	fd = ramp_fd(16);
	zwlr_gamma_control_v1_set_gamma(first.proxy, fd);
	close(fd);
	wl_display_roundtrip(a->display);
	const struct wl_interface *interface = NULL;
	uint32_t code =
		wl_display_get_protocol_error(a->display, &interface, NULL);

	CHECK(interface == &zwlr_gamma_control_v1_interface &&
		      code == ZWLR_GAMMA_CONTROL_V1_ERROR_INVALID_GAMMA,
	      "a short table was accepted");
	zwlr_gamma_control_v1_destroy(first.proxy);
	client_free(a);

	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
