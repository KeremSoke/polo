/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tests client-side decoration negotiation: changing a requested mode triggers
 * xdg_surface.configure, and invalid use is rejected.
 */

#include <errno.h>
#include <string.h>

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
	zxdg_toplevel_decoration_v1_set_mode(
		t.decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	draw(&c, &t);
	CHECK(t.decoration_configures > 0, "no decoration configure");
	CHECK(t.mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE,
	      "told mode %u before Polo draws frames", t.mode);

	for (int i = 0; i < 2; i++) {
		int before = t.decoration_configures;

		if (i == 0)
			zxdg_toplevel_decoration_v1_set_mode(
				t.decoration,
				ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		else
			zxdg_toplevel_decoration_v1_unset_mode(t.decoration);
		wl_display_roundtrip(c.display);
		wl_display_roundtrip(c.display);
		CHECK(t.decoration_configures == before + 1,
		      "no reply to a change of mode on a mapped toplevel");
		CHECK(t.mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE,
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
		      ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_CLIENT,
	      "default mode %d", c.kde_default);
	create_toplevel(&c, &t);
	struct org_kde_kwin_server_decoration *d =
		org_kde_kwin_server_decoration_manager_create(c.kde, t.surface);

	org_kde_kwin_server_decoration_add_listener(d, &kde_decoration_listener,
						    NULL);
	wl_display_roundtrip(c.display);
	CHECK(kde_modes == 1 &&
		      kde_mode == ORG_KDE_KWIN_SERVER_DECORATION_MODE_CLIENT,
	      "created in mode %u", kde_mode);

	org_kde_kwin_server_decoration_request_mode(
		d, ORG_KDE_KWIN_SERVER_DECORATION_MODE_SERVER);
	wl_display_roundtrip(c.display);
	CHECK(kde_modes == 2 &&
		      kde_mode == ORG_KDE_KWIN_SERVER_DECORATION_MODE_CLIENT,
	      "granted %u for Server before Polo draws frames", kde_mode);

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

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);

	test_xdg(h);
	test_kde(h);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
