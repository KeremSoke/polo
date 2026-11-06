/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_data_control_v1 and zwlr_data_control_v1, as mate-clipboard uses them:
 * a device sees the selection without keyboard focus, sets the clipboard and
 * the primary selection, and the data reaches a reader; an ordinary focused
 * client sees what the device set; a replaced source is cancelled; using a
 * source twice and offering after use are errors. libweston's per-seat
 * clipboard keeper reads every new selection too, and puts its copy back
 * when the selection is cleared.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ext-data-control-v1-client-protocol.h"
#include "harness.h"
#include "wlr-data-control-unstable-v1-client-protocol.h"

#define TEXT "text/plain;charset=utf-8"
#define DATA "copied by polo"

struct globals {
	struct ext_data_control_manager_v1 *ext;
	struct zwlr_data_control_manager_v1 *wlr;
	uint32_t wlr_version;
	struct wl_data_device_manager *data;
	struct wl_seat *seat;
};

struct device {
	void *pending;
	void *selection;
	void *primary;
	char mime[64];
	int selections;
	int primaries;
	bool finished;
};

struct source {
	bool cancelled;
	int sends;
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct globals *g = data;

	if (strcmp(interface, ext_data_control_manager_v1_interface.name) == 0)
		g->ext = wl_registry_bind(
			registry, name, &ext_data_control_manager_v1_interface,
			1);
	else if (strcmp(interface,
			zwlr_data_control_manager_v1_interface.name) == 0) {
		g->wlr_version = version;
		g->wlr = wl_registry_bind(
			registry, name, &zwlr_data_control_manager_v1_interface,
			version);
	} else if (strcmp(interface, wl_data_device_manager_interface.name) ==
		   0)
		g->data = wl_registry_bind(
			registry, name, &wl_data_device_manager_interface, 3);
	else if (strcmp(interface, wl_seat_interface.name) == 0 && !g->seat)
		g->seat =
			wl_registry_bind(registry, name, &wl_seat_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

/* ---- A control device (ext and wlr share the listener layout) ----------- */

static void
offer_offer(void *data, struct ext_data_control_offer_v1 *offer,
	    const char *mime)
{
	struct device *d = data;

	snprintf(d->mime, sizeof d->mime, "%s", mime);
}

static const struct ext_data_control_offer_v1_listener offer_listener = {
	offer_offer,
};

static void
device_data_offer(void *data, struct ext_data_control_device_v1 *device,
		  struct ext_data_control_offer_v1 *offer)
{
	struct device *d = data;

	d->mime[0] = '\0';
	d->pending = offer;
	ext_data_control_offer_v1_add_listener(offer, &offer_listener, d);
}

static void
device_selection(void *data, struct ext_data_control_device_v1 *device,
		 struct ext_data_control_offer_v1 *offer)
{
	struct device *d = data;

	if (d->selection && d->selection != offer)
		ext_data_control_offer_v1_destroy(d->selection);
	d->selection = offer;
	d->selections++;
}

static void
device_finished(void *data, struct ext_data_control_device_v1 *device)
{
	((struct device *)data)->finished = true;
}

static void
device_primary(void *data, struct ext_data_control_device_v1 *device,
	       struct ext_data_control_offer_v1 *offer)
{
	struct device *d = data;

	if (d->primary && d->primary != offer)
		ext_data_control_offer_v1_destroy(d->primary);
	d->primary = offer;
	d->primaries++;
}

static const struct ext_data_control_device_v1_listener device_listener = {
	device_data_offer,
	device_selection,
	device_finished,
	device_primary,
};

/* ---- A control source ---------------------------------------------------- */

static void
source_send(void *data, struct ext_data_control_source_v1 *source,
	    const char *mime, int32_t fd)
{
	struct source *s = data;

	s->sends++;
	if (write(fd, DATA, strlen(DATA)) < 0)
		s->sends = -100;
	close(fd);
}

static void
source_cancelled(void *data, struct ext_data_control_source_v1 *source)
{
	((struct source *)data)->cancelled = true;
}

static const struct ext_data_control_source_v1_listener source_listener = {
	source_send,
	source_cancelled,
};

static struct ext_data_control_source_v1 *
make_source(struct globals *g, struct source *s)
{
	struct ext_data_control_source_v1 *source =
		ext_data_control_manager_v1_create_data_source(g->ext);

	ext_data_control_source_v1_add_listener(source, &source_listener, s);
	ext_data_control_source_v1_offer(source, TEXT);
	return source;
}

static bool
read_offer(struct harness *h, void *offer, const char *wanted)
{
	int fds[2];
	char buffer[64] = { 0 };
	ssize_t total = 0, n;

	if (!offer || pipe(fds) != 0)
		return false;
	ext_data_control_offer_v1_receive(offer, TEXT, fds[1]);
	close(fds[1]);
	harness_roundtrip(h);
	while ((n = read(fds[0], buffer + total,
			 sizeof buffer - 1 - (size_t)total)) > 0)
		total += n;
	close(fds[0]);
	return strcmp(buffer, wanted) == 0;
}

/* ---- An ordinary client's wl_data_device ------------------------------ */

static struct wl_data_offer *focused_offer;
static int focused_selections;

static void
dd_data_offer(void *data, struct wl_data_device *d, struct wl_data_offer *o)
{
}

static void
dd_enter(void *data, struct wl_data_device *d, uint32_t serial,
	 struct wl_surface *s, wl_fixed_t x, wl_fixed_t y,
	 struct wl_data_offer *o)
{
}

static void
dd_leave(void *data, struct wl_data_device *d)
{
}

static void
dd_motion(void *data, struct wl_data_device *d, uint32_t t, wl_fixed_t x,
	  wl_fixed_t y)
{
}

static void
dd_drop(void *data, struct wl_data_device *d)
{
}

static void
dd_selection(void *data, struct wl_data_device *d, struct wl_data_offer *o)
{
	if (focused_offer && focused_offer != o)
		wl_data_offer_destroy(focused_offer);
	focused_offer = o;
	focused_selections++;
}

static const struct wl_data_device_listener dd_listener = {
	dd_data_offer, dd_enter, dd_leave, dd_motion, dd_drop, dd_selection,
};

static void
globals_destroy(struct globals *g)
{
	if (g->ext)
		ext_data_control_manager_v1_destroy(g->ext);
	if (g->wlr)
		zwlr_data_control_manager_v1_destroy(g->wlr);
	if (g->data)
		wl_data_device_manager_destroy(g->data);
	if (g->seat)
		wl_seat_destroy(g->seat);
}

static void
device_offers_destroy(struct device *d)
{
	if (d->pending && d->pending != d->selection &&
	    d->pending != d->primary)
		ext_data_control_offer_v1_destroy(d->pending);
	d->pending = NULL;
	if (d->selection)
		ext_data_control_offer_v1_destroy(d->selection);
	if (d->primary)
		ext_data_control_offer_v1_destroy(d->primary);
	d->selection = d->primary = NULL;
}

/* ---- Errors, each on its own connection ------------------------------ */

static void
expect_error(struct harness *h, bool used_source)
{
	struct wl_display *display = wl_display_connect(h->socket);
	struct globals g = { 0 };
	struct source s = { 0 };

	CHECK(display, "cannot connect");
	struct wl_registry *registry = wl_display_get_registry(display);

	wl_registry_add_listener(registry, &registry_listener, &g);
	wl_display_roundtrip(display);
	CHECK(g.ext && g.seat, "no data control or seat on a new connection");

	struct ext_data_control_source_v1 *source = make_source(&g, &s);
	struct ext_data_control_device_v1 *device =
		ext_data_control_manager_v1_get_data_device(g.ext, g.seat);
	struct device seen = { 0 };

	ext_data_control_device_v1_add_listener(device, &device_listener,
						&seen);
	wl_display_roundtrip(display);
	ext_data_control_device_v1_set_selection(device, source);
	wl_display_roundtrip(display);
	if (used_source)
		ext_data_control_device_v1_set_primary_selection(device,
								 source);
	else
		ext_data_control_source_v1_offer(source, "text/html");
	wl_display_roundtrip(display);

	const struct wl_interface *interface = NULL;
	uint32_t code =
		wl_display_get_protocol_error(display, &interface, NULL);

	if (used_source)
		CHECK(interface == &ext_data_control_device_v1_interface &&
			      code == EXT_DATA_CONTROL_DEVICE_V1_ERROR_USED_SOURCE,
		      "using a source twice was allowed");
	else
		CHECK(interface == &ext_data_control_source_v1_interface &&
			      code == EXT_DATA_CONTROL_SOURCE_V1_ERROR_INVALID_OFFER,
		      "an offer after use was allowed");
	device_offers_destroy(&seen);
	ext_data_control_source_v1_destroy(source);
	ext_data_control_device_v1_destroy(device);
	globals_destroy(&g);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct globals g = { 0 };
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, &g);
	harness_roundtrip(h);
	CHECK(g.ext && g.wlr && g.data, "no data control or data device");
	CHECK(g.wlr_version == 2, "zwlr_data_control_manager_v1 version %u",
	      g.wlr_version);

	struct harness_window *w = harness_window_create(h, 200, 150, "focus");
	struct wl_data_device *dd =
		wl_data_device_manager_get_data_device(g.data, h->seat);

	wl_data_device_add_listener(dd, &dd_listener, NULL);

	struct device ext = { 0 }, wlr = { 0 };
	struct ext_data_control_device_v1 *ext_device =
		ext_data_control_manager_v1_get_data_device(g.ext, h->seat);
	struct zwlr_data_control_device_v1 *wlr_device =
		zwlr_data_control_manager_v1_get_data_device(g.wlr, h->seat);

	ext_data_control_device_v1_add_listener(ext_device, &device_listener,
						&ext);
	zwlr_data_control_device_v1_add_listener(
		wlr_device,
		(const struct zwlr_data_control_device_v1_listener
			 *)&device_listener,
		&wlr);
	harness_roundtrip(h);
	CHECK(ext.selections == 1 && !ext.selection,
	      "no empty selection on get_data_device");
	CHECK(ext.primaries == 1 && !ext.primary,
	      "no empty primary selection on get_data_device");
	CHECK(wlr.selections == 1 && wlr.primaries == 1,
	      "the wlr device did not get both selections");

	struct source first = { 0 };
	struct ext_data_control_source_v1 *source = make_source(&g, &first);

	ext_data_control_device_v1_set_selection(ext_device, source);
	harness_roundtrip(h);
	harness_roundtrip(h);
	CHECK(ext.selection && strcmp(ext.mime, TEXT) == 0,
	      "the device did not see its own selection");
	CHECK(wlr.selection && strcmp(wlr.mime, TEXT) == 0,
	      "the wlr device did not see the selection");
	CHECK(focused_offer, "the focused client did not get the selection");
	CHECK(read_offer(h, ext.selection, DATA),
	      "the selection's data did not arrive");

	struct source second = { 0 };
	struct ext_data_control_source_v1 *primary = make_source(&g, &second);

	ext_data_control_device_v1_set_primary_selection(ext_device, primary);
	harness_roundtrip(h);
	harness_roundtrip(h);
	CHECK(ext.primary && read_offer(h, ext.primary, DATA),
	      "the primary selection did not work");

	struct source third = { 0 };
	struct ext_data_control_source_v1 *next = make_source(&g, &third);

	ext_data_control_device_v1_set_selection(ext_device, next);
	harness_roundtrip(h);
	harness_roundtrip(h);
	CHECK(first.cancelled, "the replaced source was not cancelled");
	ext_data_control_device_v1_set_selection(ext_device, NULL);
	harness_roundtrip(h);
	harness_roundtrip(h);
	CHECK(third.cancelled, "the cleared source was not cancelled");
	CHECK(ext.selection && read_offer(h, ext.selection, DATA),
	      "libweston's clipboard keeper did not restore its copy");

	ext_data_control_source_v1_destroy(source);
	ext_data_control_source_v1_destroy(primary);
	ext_data_control_source_v1_destroy(next);
	ext_data_control_device_v1_destroy(ext_device);
	zwlr_data_control_device_v1_destroy(wlr_device);
	wl_data_device_release(dd);
	harness_roundtrip(h);
	device_offers_destroy(&ext);
	device_offers_destroy(&wlr);
	if (focused_offer)
		wl_data_offer_destroy(focused_offer);
	globals_destroy(&g);
	wl_registry_destroy(registry);
	expect_error(h, true);
	expect_error(h, false);

	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
