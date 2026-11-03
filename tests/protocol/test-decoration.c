/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tests decoration negotiation: server-side is the default, client-side is
 * granted when requested, mode changes trigger xdg_surface.configure, and
 * invalid use is rejected.
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "server-decoration-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

/* Connections die on purpose here with their proxies live. Polo is its own
 * process and is still checked. */
int
__lsan_is_turned_off(void);

int
__lsan_is_turned_off(void)
{
	return 1;
}

struct client {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct zxdg_decoration_manager_v1 *xdg;
	struct org_kde_kwin_server_decoration_manager *kde;
	int kde_default;
};

struct toplevel {
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct zxdg_toplevel_decoration_v1 *decoration;
	uint32_t serial;
	int surface_configures;
	int decoration_configures;
	uint32_t mode;
	int surface_configures_at_mode;
};

static void
kde_default_mode(void *data, struct org_kde_kwin_server_decoration_manager *m,
		 uint32_t mode)
{
	((struct client *)data)->kde_default = (int)mode;
}

static const struct org_kde_kwin_server_decoration_manager_listener
	kde_listener = {
		kde_default_mode,
	};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct client *c = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
		c->compositor = wl_registry_bind(registry, name,
						 &wl_compositor_interface, 4);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		c->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
		c->wm_base = wl_registry_bind(registry, name,
					      &xdg_wm_base_interface, 1);
	else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) ==
		 0)
		c->xdg = wl_registry_bind(registry, name,
					  &zxdg_decoration_manager_v1_interface,
					  1);
	else if (strcmp(interface,
			org_kde_kwin_server_decoration_manager_interface
				.name) == 0) {
		c->kde = wl_registry_bind(
			registry, name,
			&org_kde_kwin_server_decoration_manager_interface, 1);
		org_kde_kwin_server_decoration_manager_add_listener(
			c->kde, &kde_listener, c);
	}
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
connect_client(struct client *c, struct harness *h)
{
	memset(c, 0, sizeof *c);
	c->kde_default = -1;
	c->display = wl_display_connect(h->socket);
	CHECK(c->display != NULL, "cannot connect");
	wl_registry_add_listener(wl_display_get_registry(c->display),
				 &registry_listener, c);
	wl_display_roundtrip(c->display);
	wl_display_roundtrip(c->display);
	CHECK(c->xdg != NULL, "no zxdg_decoration_manager_v1");
	CHECK(c->kde != NULL, "no org_kde_kwin_server_decoration_manager");
}

static void
surface_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
	struct toplevel *t = data;

	t->serial = serial;
	t->surface_configures++;
}

static const struct xdg_surface_listener surface_listener = {
	surface_configure
};

static void
decoration_configure(void *data, struct zxdg_toplevel_decoration_v1 *d,
		     uint32_t mode)
{
	struct toplevel *t = data;

	t->mode = mode;
	t->decoration_configures++;
	t->surface_configures_at_mode = t->surface_configures;
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
	decoration_configure,
};

static void
create_toplevel(struct client *c, struct toplevel *t)
{
	memset(t, 0, sizeof *t);
	t->surface = wl_compositor_create_surface(c->compositor);
	t->xdg_surface = xdg_wm_base_get_xdg_surface(c->wm_base, t->surface);
	xdg_surface_add_listener(t->xdg_surface, &surface_listener, t);
	t->xdg_toplevel = xdg_surface_get_toplevel(t->xdg_surface);
}

static void
draw(struct client *c, struct toplevel *t)
{
	wl_surface_commit(t->surface);
	wl_display_roundtrip(c->display);
	CHECK(t->surface_configures > 0, "no initial configure");
	xdg_surface_ack_configure(t->xdg_surface, t->serial);
	wl_surface_attach(t->surface, harness_buffer_create(c->shm, 200, 150),
			  0, 0);
	wl_surface_commit(t->surface);
	wl_display_roundtrip(c->display);
}

static void
expect_error(struct client *c, uint32_t code)
{
	const struct wl_interface *interface = NULL;

	for (int i = 0; i < 10 && !wl_display_get_error(c->display); i++)
		wl_display_roundtrip(c->display);
	CHECK(wl_display_get_error(c->display) == EPROTO, "no protocol error");
	CHECK(wl_display_get_protocol_error(c->display, &interface, NULL) ==
		      code,
	      "wrong protocol error");
	CHECK(interface == &zxdg_decoration_manager_v1_interface,
	      "error from the wrong interface");
	wl_display_disconnect(c->display);
}

static void
test_xdg(struct harness *h)
{
	struct client c;
	struct toplevel t;

	connect_client(&c, h);
	create_toplevel(&c, &t);
	t.decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
		c.xdg, t.xdg_toplevel);
	zxdg_toplevel_decoration_v1_add_listener(t.decoration,
						 &decoration_listener, &t);
	wl_display_roundtrip(c.display);
	CHECK(t.decoration_configures > 0 &&
		      t.mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE,
	      "a new decoration is not server-side: told mode %u", t.mode);
	zxdg_toplevel_decoration_v1_set_mode(
		t.decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	draw(&c, &t);
	CHECK(t.decoration_configures > 0, "no decoration configure");
	CHECK(t.mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE,
	      "server-side refused: told mode %u", t.mode);

	for (int i = 0; i < 2; i++) {
		int before = t.decoration_configures;

		if (i == 0)
			zxdg_toplevel_decoration_v1_set_mode(
				t.decoration,
				ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
		else
			zxdg_toplevel_decoration_v1_unset_mode(t.decoration);
		wl_display_roundtrip(c.display);
		wl_display_roundtrip(c.display);
		CHECK(t.decoration_configures == before + 1,
		      "no reply to a change of mode on a mapped toplevel");
		CHECK(t.mode ==
			      (i == 0 ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
				      : ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE),
		      "told mode %u", t.mode);
		CHECK(t.surface_configures > t.surface_configures_at_mode,
		      "no xdg_surface.configure after the decoration "
		      "configure");
		xdg_surface_ack_configure(t.xdg_surface, t.serial);
		wl_surface_commit(t.surface);
	}

	xdg_toplevel_destroy(t.xdg_toplevel);
	zxdg_toplevel_decoration_v1_set_mode(
		t.decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
	wl_display_roundtrip(c.display);
	CHECK(wl_display_get_error(c.display) == 0,
	      "a decoration outliving its toplevel killed the client");
	zxdg_toplevel_decoration_v1_destroy(t.decoration);
	wl_display_roundtrip(c.display);
	wl_display_disconnect(c.display);

	connect_client(&c, h);
	create_toplevel(&c, &t);
	zxdg_decoration_manager_v1_get_toplevel_decoration(c.xdg,
							   t.xdg_toplevel);
	zxdg_decoration_manager_v1_get_toplevel_decoration(c.xdg,
							   t.xdg_toplevel);
	expect_error(&c, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ALREADY_CONSTRUCTED);

	connect_client(&c, h);
	create_toplevel(&c, &t);
	t.decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
		c.xdg, t.xdg_toplevel);
	zxdg_toplevel_decoration_v1_destroy(t.decoration);
	wl_display_roundtrip(c.display);
	t.decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
		c.xdg, t.xdg_toplevel);
	wl_display_roundtrip(c.display);
	CHECK(wl_display_get_error(c.display) == 0,
	      "a new decoration after destroying the old one was refused");
	wl_display_disconnect(c.display);

	connect_client(&c, h);
	create_toplevel(&c, &t);
	draw(&c, &t);
	zxdg_decoration_manager_v1_get_toplevel_decoration(c.xdg,
							   t.xdg_toplevel);
	expect_error(&c, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_UNCONFIGURED_BUFFER);
}

static uint32_t kde_mode;
static int kde_modes;

static void
kde_mode_event(void *data, struct org_kde_kwin_server_decoration *d,
	       uint32_t mode)
{
	kde_mode = mode;
	kde_modes++;
}

static const struct org_kde_kwin_server_decoration_listener
	kde_decoration_listener = {
		kde_mode_event,
	};

static void
test_kde(struct harness *h)
{
	struct client c;
	struct toplevel t;

	connect_client(&c, h);
	CHECK(c.kde_default ==
		      ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_SERVER,
	      "default mode %d", c.kde_default);
	create_toplevel(&c, &t);
	struct org_kde_kwin_server_decoration *d =
		org_kde_kwin_server_decoration_manager_create(c.kde, t.surface);

	org_kde_kwin_server_decoration_add_listener(d, &kde_decoration_listener,
						    NULL);
	wl_display_roundtrip(c.display);
	CHECK(kde_modes == 1 &&
		      kde_mode == ORG_KDE_KWIN_SERVER_DECORATION_MODE_SERVER,
	      "created in mode %u", kde_mode);

	org_kde_kwin_server_decoration_request_mode(
		d, ORG_KDE_KWIN_SERVER_DECORATION_MODE_CLIENT);
	wl_display_roundtrip(c.display);
	CHECK(kde_modes == 2 &&
		      kde_mode == ORG_KDE_KWIN_SERVER_DECORATION_MODE_CLIENT,
	      "granted %u for Client", kde_mode);

	org_kde_kwin_server_decoration_request_mode(
		d, ORG_KDE_KWIN_SERVER_DECORATION_MODE_NONE);
	wl_display_roundtrip(c.display);
	CHECK(kde_modes == 3 &&
		      kde_mode == ORG_KDE_KWIN_SERVER_DECORATION_MODE_NONE,
	      "granted %u for None", kde_mode);

	org_kde_kwin_server_decoration_release(d);
	wl_display_roundtrip(c.display);
	CHECK(wl_display_get_error(c.display) == 0, "release failed");
	wl_display_disconnect(c.display);
}

struct desktop {
	struct harness *h;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_buffer *buffer;
	bool mapped;
};

static void
desktop_configure(void *data, struct zwlr_layer_surface_v1 *ls,
		  uint32_t serial, uint32_t width, uint32_t height)
{
	struct desktop *d = data;

	zwlr_layer_surface_v1_ack_configure(ls, serial);
	if (!d->mapped) {
		d->buffer = harness_buffer_create(d->h->shm, (int)width,
						  (int)height);
		wl_surface_attach(d->surface, d->buffer, 0, 0);
		d->mapped = true;
	}
	wl_surface_commit(d->surface);
}

static void
desktop_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
}

static const struct zwlr_layer_surface_v1_listener desktop_listener = {
	desktop_configure,
	desktop_closed,
};

/* Count sample points whose pointer focus is not the desktop. */
static int
points_off_desktop(struct harness *h, struct desktop *d)
{
	int covered = 0;

	for (int y = 180; y <= 580; y += 20)
		for (int x = 300; x <= 720; x += 40) {
			harness_move_pointer(h, x, y);
			if (h->pointer_focus != d->surface)
				covered++;
		}
	return covered;
}

/* Destroying a client and its KDE decoration must remove its frame. Otherwise
 * the frame can cover the desktop after live windows restack. */
static void
test_kde_disconnect(struct harness *h)
{
	struct desktop desk = { .h = h };
	struct client c;
	struct toplevel t;

	desk.surface = wl_compositor_create_surface(h->compositor);
	desk.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		h->layer_shell, desk.surface, NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "desktop");
	zwlr_layer_surface_v1_add_listener(desk.layer_surface,
					   &desktop_listener, &desk);
	zwlr_layer_surface_v1_set_anchor(
		desk.layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
					    ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
					    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
					    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(desk.layer_surface, -1);
	wl_surface_commit(desk.surface);
	for (int i = 0; i < 20 && !harness_layer_info(h, desk.surface)->mapped;
	     i++)
		harness_roundtrip(h);
	harness_wait_frame(h, desk.surface);
	CHECK(points_off_desktop(h, &desk) == 0,
	      "something covers the desktop, such as a frame an earlier "
	      "window left");

	connect_client(&c, h);
	create_toplevel(&c, &t);
	struct org_kde_kwin_server_decoration *d =
		org_kde_kwin_server_decoration_manager_create(c.kde, t.surface);

	org_kde_kwin_server_decoration_request_mode(
		d, ORG_KDE_KWIN_SERVER_DECORATION_MODE_SERVER);
	draw(&c, &t);
	for (int i = 0; i < 6; i++) {
		wl_display_roundtrip(c.display);
		harness_roundtrip(h);
	}
	usleep(50000);
	CHECK(points_off_desktop(h, &desk) > 0,
	      "the decorated window is not shown");
	wl_display_disconnect(c.display);
	for (int i = 0; i < 6; i++)
		harness_roundtrip(h);
	usleep(50000);
	int left = points_off_desktop(h, &desk);

	CHECK(left == 0, "%d points still hit a frame after the client left",
	      left);
	zwlr_layer_surface_v1_destroy(desk.layer_surface);
	wl_surface_destroy(desk.surface);
	wl_buffer_destroy(desk.buffer);
	harness_roundtrip(h);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);

	test_xdg(h);
	test_kde(h);
	test_kde_disconnect(h);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
