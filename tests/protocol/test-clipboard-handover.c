/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The clipboard handed back and forth between a client and libweston's
 * clipboard keeper: a client sets the selection, the keeper copies it, the
 * client destroys its source (so the keeper's copy becomes the selection),
 * then a new client source replaces the copy. Polo must survive every round
 * and a reader must get the latest text.
 *
 *   test-clipboard-handover POLO
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"

#define TEXT "text/plain;charset=utf-8"
#define ROUNDS 25

static struct wl_data_device_manager *manager;
static struct wl_data_offer *selection;
static char contents[64];

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface, wl_data_device_manager_interface.name) == 0)
		manager = wl_registry_bind(
			registry, name, &wl_data_device_manager_interface, 3);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	global,
	global_remove,
};

static void
source_target(void *data, struct wl_data_source *source, const char *mime)
{
}

static void
source_send(void *data, struct wl_data_source *source, const char *mime,
	    int32_t fd)
{
	const char *text = data;

	if (write(fd, text, strlen(text)) < 0)
		perror("write");
	close(fd);
}

static void
source_cancelled(void *data, struct wl_data_source *source)
{
}

static const struct wl_data_source_listener source_listener = {
	.target = source_target,
	.send = source_send,
	.cancelled = source_cancelled,
};

static void
device_data_offer(void *data, struct wl_data_device *device,
		  struct wl_data_offer *offer)
{
}

static void
device_enter(void *data, struct wl_data_device *device, uint32_t serial,
	     struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y,
	     struct wl_data_offer *offer)
{
}

static void
device_leave(void *data, struct wl_data_device *device)
{
}

static void
device_motion(void *data, struct wl_data_device *device, uint32_t time,
	      wl_fixed_t x, wl_fixed_t y)
{
}

static void
device_drop(void *data, struct wl_data_device *device)
{
}

static void
device_selection(void *data, struct wl_data_device *device,
		 struct wl_data_offer *offer)
{
	if (selection)
		wl_data_offer_destroy(selection);
	selection = offer;
}

static const struct wl_data_device_listener device_listener = {
	device_data_offer, device_enter, device_leave,
	device_motion,     device_drop,  device_selection,
};

static struct wl_data_source *
offer_text(struct harness *h, struct wl_data_device *device, const char *text,
	   uint32_t serial)
{
	struct wl_data_source *source =
		wl_data_device_manager_create_data_source(manager);

	wl_data_source_add_listener(source, &source_listener, (void *)text);
	wl_data_source_offer(source, TEXT);
	wl_data_device_set_selection(device, source, serial);
	harness_roundtrip(h);
	harness_roundtrip(h);
	return source;
}

static const char *
read_selection(struct harness *h)
{
	int fds[2];
	ssize_t n, got = 0;

	contents[0] = '\0';
	if (!selection || pipe2(fds, O_CLOEXEC) < 0)
		return contents;
	wl_data_offer_receive(selection, TEXT, fds[1]);
	close(fds[1]);
	harness_roundtrip(h);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	for (int i = 0; i < 200; i++) {
		n = read(fds[0], contents + got, sizeof contents - 1 - got);
		if (n == 0)
			break;
		if (n > 0)
			got += n;
		else
			harness_roundtrip(h);
	}
	close(fds[0]);
	contents[got] = '\0';
	return contents;
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);
	struct harness_window *w;
	struct wl_data_device *device;
	char text[32], copied[32];
	uint32_t serial = 100;

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(manager, "no wl_data_device_manager");
	w = harness_window_create(h, 200, 150, "clipboard");
	harness_wait_frame(h, w->surface);
	device = wl_data_device_manager_get_data_device(manager, h->seat);
	wl_data_device_add_listener(device, &device_listener, NULL);
	harness_roundtrip(h);

	for (int round = 0; round < ROUNDS; round++) {
		struct wl_data_source *source;

		snprintf(text, sizeof text, "round %d", round);
		source = offer_text(h, device, text, serial += 10);
		CHECK(strcmp(read_selection(h), text) == 0,
		      "round %d: the selection reads \"%s\"", round, contents);

		wl_data_source_destroy(source);
		harness_roundtrip(h);
		harness_roundtrip(h);
		snprintf(copied, sizeof copied, "%s", text);
		CHECK(strcmp(read_selection(h), copied) == 0,
		      "round %d: the keeper's copy reads \"%s\", not \"%s\"",
		      round, contents, copied);
	}

	if (selection)
		wl_data_offer_destroy(selection);
	wl_data_device_release(device);
	wl_data_device_manager_destroy(manager);
	wl_registry_destroy(registry);
	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
