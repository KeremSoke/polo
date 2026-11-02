/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Primary selection between two native clients: it goes to the client with keyboard
 * focus, only that client can set it, and it ends with its source.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/input.h>

#include "harness.h"
#include "primary-selection-unstable-v1-client-protocol.h"

/* Connections die on purpose here with their proxies live. Polo is its own
 * process and is still checked. */
int
__lsan_is_turned_off(void);

int
__lsan_is_turned_off(void)
{
	return 1;
}

struct peer {
	struct wl_display *display;
	struct wl_seat *seat;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct zwp_primary_selection_device_manager_v1 *manager;
	struct zwp_primary_selection_device_v1 *device;
	struct zwp_primary_selection_offer_v1 *offer;
	char mimes[128];
	int selections;
	int cancelled;
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct peer *p = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
		p->compositor = wl_registry_bind(registry, name,
						 &wl_compositor_interface, 4);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		p->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
		p->wm_base = wl_registry_bind(registry, name,
					      &xdg_wm_base_interface, 1);
	else if (strcmp(interface, wl_seat_interface.name) == 0 && !p->seat)
		p->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
	else if (strcmp(interface,
			zwp_primary_selection_device_manager_v1_interface.name) == 0)
		p->manager = wl_registry_bind(
			registry, name,
			&zwp_primary_selection_device_manager_v1_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
offer_mime(void *data, struct zwp_primary_selection_offer_v1 *offer,
	   const char *mime)
{
	struct peer *p = data;

	snprintf(p->mimes, sizeof p->mimes, "%s", mime);
}

static const struct zwp_primary_selection_offer_v1_listener offer_listener = {
	offer_mime,
};

static void
device_data_offer(void *data, struct zwp_primary_selection_device_v1 *device,
		  struct zwp_primary_selection_offer_v1 *offer)
{
	zwp_primary_selection_offer_v1_add_listener(offer, &offer_listener, data);
}

static void
device_selection(void *data, struct zwp_primary_selection_device_v1 *device,
		 struct zwp_primary_selection_offer_v1 *offer)
{
	struct peer *p = data;

	if (p->offer)
		zwp_primary_selection_offer_v1_destroy(p->offer);
	p->offer = offer;
	p->selections++;
}

static const struct zwp_primary_selection_device_v1_listener device_listener = {
	device_data_offer,
	device_selection,
};

static void
source_send(void *data, struct zwp_primary_selection_source_v1 *source,
	    const char *mime, int fd)
{
	CHECK(write(fd, "hello", 5) == 5, "write");
	close(fd);
}

static void
source_cancelled(void *data, struct zwp_primary_selection_source_v1 *source)
{
	((struct peer *)data)->cancelled++;
}

static const struct zwp_primary_selection_source_v1_listener source_listener = {
	source_send,
	source_cancelled,
};

static void
peer_connect(struct peer *p, struct wl_display *display)
{
	memset(p, 0, sizeof *p);
	p->display = display;
	wl_registry_add_listener(wl_display_get_registry(display),
				 &registry_listener, p);
	wl_display_roundtrip(display);
	CHECK(p->manager != NULL, "no primary selection manager");
	p->device = zwp_primary_selection_device_manager_v1_get_device(p->manager,
								       p->seat);
	zwp_primary_selection_device_v1_add_listener(p->device, &device_listener, p);
	wl_display_roundtrip(display);
}

static struct zwp_primary_selection_source_v1 *
new_source(struct peer *p)
{
	struct zwp_primary_selection_source_v1 *source =
		zwp_primary_selection_device_manager_v1_create_source(p->manager);

	zwp_primary_selection_source_v1_add_listener(source, &source_listener, p);
	zwp_primary_selection_source_v1_offer(source, "text/plain");
	return source;
}

static uint32_t peer_serial;

static void
peer_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
	peer_serial = serial;
}

static const struct xdg_surface_listener peer_surface_listener = { peer_configure };

/* A window of this connection's own, which takes focus when it maps. */
static struct wl_surface *
peer_window(struct peer *p)
{
	struct wl_surface *surface = wl_compositor_create_surface(p->compositor);
	struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(p->wm_base, surface);

	xdg_surface_add_listener(xdg, &peer_surface_listener, NULL);
	xdg_surface_get_toplevel(xdg);
	wl_surface_commit(surface);
	wl_display_roundtrip(p->display);
	xdg_surface_ack_configure(xdg, peer_serial);
	wl_surface_attach(surface, harness_buffer_create(p->shm, 200, 150), 0, 0);
	wl_surface_commit(surface);
	wl_display_roundtrip(p->display);
	return surface;
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct peer a, b;
	char text[16] = "";
	int fds[2];

	struct harness_window *wa = harness_window_create(h, 200, 150, "a");

	peer_connect(&a, h->display);
	harness_roundtrip(h);
	CHECK(a.selections == 1 && a.offer == NULL,
	      "a focused client's new device did not hear the empty selection");

	struct wl_display *second = wl_display_connect(h->socket);

	CHECK(second != NULL, "second connection");
	peer_connect(&b, second);

	struct zwp_primary_selection_source_v1 *unfocused = new_source(&b);

	zwp_primary_selection_device_v1_set_selection(b.device, unfocused, 1);
	wl_display_roundtrip(second);
	harness_roundtrip(h);
	CHECK(a.offer == NULL, "a client without focus set the selection");

	struct zwp_primary_selection_source_v1 *source = new_source(&a);

	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	harness_roundtrip(h);
	zwp_primary_selection_device_v1_set_selection(a.device, source,
						      h->last_keyboard_serial);
	harness_roundtrip(h);
	CHECK(a.offer != NULL && strcmp(a.mimes, "text/plain") == 0,
	      "the focused client does not see its own selection");
	wl_display_roundtrip(second);
	CHECK(b.selections == 0, "an unfocused client heard the selection");

	/* Focus moves to another client, which hears it and can read it. */
	peer_window(&b);
	harness_roundtrip(h);
	wl_display_roundtrip(second);
	CHECK(!harness_window_info(wa)->focused, "the new window did not take focus");
	CHECK(b.selections == 1 && b.offer != NULL &&
		      strcmp(b.mimes, "text/plain") == 0,
	      "the client given focus did not hear the selection");
	CHECK(pipe(fds) == 0, "pipe");
	zwp_primary_selection_offer_v1_receive(b.offer, "text/plain", fds[1]);
	close(fds[1]);
	wl_display_flush(second);
	wl_display_roundtrip(second);
	harness_roundtrip(h);
	CHECK(read(fds[0], text, sizeof text) == 5 && memcmp(text, "hello", 5) == 0,
	      "wrong data: %s", text);
	close(fds[0]);

	/* Focus comes back, and a client without it still cannot set it. */
	struct zwp_primary_selection_offer_v1 *old_offer = a.offer;
	int a_heard = a.selections;

	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_TAB, true);
	harness_key(h, KEY_TAB, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_roundtrip(h);
	CHECK(harness_window_info(wa)->focused, "Alt+Tab did not focus a");
	CHECK(a.selections == a_heard + 1 && a.offer != old_offer,
	      "focus coming back did not resend the selection");

	struct zwp_primary_selection_source_v1 *late = new_source(&b);

	zwp_primary_selection_device_v1_set_selection(b.device, late,
						      h->last_keyboard_serial + 100);
	wl_display_roundtrip(second);
	harness_roundtrip(h);
	CHECK(a.cancelled == 0, "a client that lost focus replaced the selection");

	/* Replacing cancels the old source. */
	struct zwp_primary_selection_source_v1 *replacement = new_source(&a);

	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	harness_roundtrip(h);
	zwp_primary_selection_device_v1_set_selection(a.device, replacement,
						      h->last_keyboard_serial);
	harness_roundtrip(h);
	CHECK(a.cancelled == 1, "the replaced source was not cancelled");
	zwp_primary_selection_source_v1_destroy(source);

	/* An older serial loses. */
	int before = a.selections;
	struct zwp_primary_selection_source_v1 *stale = new_source(&a);

	zwp_primary_selection_device_v1_set_selection(
		a.device, stale, h->last_keyboard_serial - 1);
	harness_roundtrip(h);
	CHECK(a.selections == before, "an older serial replaced the selection");
	zwp_primary_selection_source_v1_destroy(stale);

	/* Destroying the source clears it. */
	zwp_primary_selection_source_v1_destroy(replacement);
	harness_roundtrip(h);
	CHECK(a.offer == NULL, "the selection outlived its source");

	/* Clearing with null. */
	source = new_source(&a);
	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	harness_roundtrip(h);
	zwp_primary_selection_device_v1_set_selection(a.device, source,
						      h->last_keyboard_serial);
	harness_roundtrip(h);
	CHECK(a.offer != NULL, "no selection to clear");
	zwp_primary_selection_device_v1_set_selection(a.device, NULL,
						      h->last_keyboard_serial);
	harness_roundtrip(h);
	CHECK(a.offer == NULL, "a null source did not clear it");
	CHECK(a.cancelled == 2, "clearing did not cancel the source");

	wl_display_disconnect(second);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
