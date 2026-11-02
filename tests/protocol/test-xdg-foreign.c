/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A toplevel of one client made the child of another's through a handle, and the
 * modal hint. A child stays above its parent when the parent is raised.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "xdg-dialog-v1-client-protocol.h"
#include "xdg-foreign-unstable-v2-client-protocol.h"

/* Connections die on purpose here with their proxies live. Polo is its own
 * process and is still checked. */
int
__lsan_is_turned_off(void);

int
__lsan_is_turned_off(void)
{
	return 1;
}

#define STATE_MODAL (1u << 11)

struct peer {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct zxdg_exporter_v2 *exporter;
	struct zxdg_importer_v2 *importer;
	struct xdg_wm_dialog_v1 *dialogs;
};

struct peer_window {
	struct wl_surface *surface;
	struct xdg_surface *xdg;
	struct xdg_toplevel *toplevel;
	uint32_t serial;
};

static char handle[128];
static int imported_destroyed;

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
	else if (strcmp(interface, zxdg_exporter_v2_interface.name) == 0)
		p->exporter = wl_registry_bind(registry, name,
					       &zxdg_exporter_v2_interface, 1);
	else if (strcmp(interface, zxdg_importer_v2_interface.name) == 0)
		p->importer = wl_registry_bind(registry, name,
					       &zxdg_importer_v2_interface, 1);
	else if (strcmp(interface, xdg_wm_dialog_v1_interface.name) == 0)
		p->dialogs = wl_registry_bind(registry, name,
					      &xdg_wm_dialog_v1_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
exported_handle(void *data, struct zxdg_exported_v2 *e, const char *h)
{
	snprintf(handle, sizeof handle, "%s", h);
}

static const struct zxdg_exported_v2_listener exported_listener = {
	exported_handle,
};

static void
imported_gone(void *data, struct zxdg_imported_v2 *i)
{
	imported_destroyed++;
}

static const struct zxdg_imported_v2_listener imported_listener = {
	imported_gone,
};

static void
peer_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
	((struct peer_window *)data)->serial = serial;
}

static const struct xdg_surface_listener peer_listener = { peer_configure };

static void
wm_ping(void *data, struct xdg_wm_base *b, uint32_t serial)
{
	xdg_wm_base_pong(b, serial);
}

static const struct xdg_wm_base_listener wm_listener = { wm_ping };

static void
peer_connect(struct peer *p, struct harness *h)
{
	memset(p, 0, sizeof *p);
	p->display = wl_display_connect(h->socket);
	CHECK(p->display != NULL, "cannot connect");
	wl_registry_add_listener(wl_display_get_registry(p->display),
				 &registry_listener, p);
	wl_display_roundtrip(p->display);
	CHECK(p->exporter && p->importer && p->dialogs, "missing globals");
	xdg_wm_base_add_listener(p->wm_base, &wm_listener, NULL);
}

static void
peer_window_new(struct peer *p, struct peer_window *w)
{
	memset(w, 0, sizeof *w);
	w->surface = wl_compositor_create_surface(p->compositor);
	w->xdg = xdg_wm_base_get_xdg_surface(p->wm_base, w->surface);
	xdg_surface_add_listener(w->xdg, &peer_listener, w);
	w->toplevel = xdg_surface_get_toplevel(w->xdg);
}

static void
peer_window_map(struct peer *p, struct peer_window *w, int width, int height)
{
	wl_surface_commit(w->surface);
	wl_display_roundtrip(p->display);
	xdg_surface_ack_configure(w->xdg, w->serial);
	wl_surface_attach(w->surface, harness_buffer_create(p->shm, width, height),
			  0, 0);
	wl_surface_commit(w->surface);
	wl_display_roundtrip(p->display);
}

static void
sync_all(struct harness *h, struct peer *p)
{
	for (int i = 0; i < 2; i++) {
		harness_roundtrip(h);
		wl_display_roundtrip(p->display);
	}
}

/* Raise the big window that sits behind the small child. Its corner is free. */
static void
click_parent(struct harness *h, struct peer *p, int x, int y)
{
	harness_move_pointer(h, x + 1, y);
	harness_click(h, x, y);
	sync_all(h, p);
}

static void
expect_error(struct peer *p, const struct wl_interface *interface, uint32_t code)
{
	const struct wl_interface *got = NULL;

	for (int i = 0; i < 10 && !wl_display_get_error(p->display); i++)
		wl_display_roundtrip(p->display);
	CHECK(wl_display_get_error(p->display) == EPROTO, "no protocol error");
	CHECK(wl_display_get_protocol_error(p->display, &got, NULL) == code &&
		      got == interface,
	      "wrong protocol error");
	wl_display_disconnect(p->display);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct peer owner, me;
	struct peer_window parent;

	peer_connect(&owner, h);
	peer_connect(&me, h);
	wl_display_disconnect(me.display);

	/* me again, on the harness connection, for the importer. */
	struct wl_registry *registry = wl_display_get_registry(h->display);

	memset(&me, 0, sizeof me);
	wl_registry_add_listener(registry, &registry_listener, &me);
	harness_roundtrip(h);
	CHECK(me.importer && me.dialogs, "no importer on the harness connection");
	me.display = h->display;

	/* The owner's big window, then a small child on the harness connection. */
	peer_window_new(&owner, &parent);
	peer_window_map(&owner, &parent, 800, 600);
	struct zxdg_exported_v2 *exported =
		zxdg_exporter_v2_export_toplevel(owner.exporter, parent.surface);

	zxdg_exported_v2_add_listener(exported, &exported_listener, NULL);
	wl_display_roundtrip(owner.display);
	CHECK(handle[0], "no handle");
	sync_all(h, &owner);

	struct harness_window *child = harness_window_create(h, 100, 100, "child");

	sync_all(h, &owner);
	const struct harness_window_info *info = harness_window_info(child);

	CHECK(info->stack_index == 1, "the child is not on top: %d", info->stack_index);
	int px = 112, py = 84 + 4;

	/* The corner of an 800x600 window centred on 1024x768. */
	click_parent(h, &owner, px, py);
	CHECK(harness_window_info(child)->stack_index == 0,
	      "no link yet, and the click did not raise the owner's window");

	struct zxdg_imported_v2 *imported =
		zxdg_importer_v2_import_toplevel(me.importer, handle);

	zxdg_imported_v2_add_listener(imported, &imported_listener, NULL);
	zxdg_imported_v2_set_parent_of(imported, child->surface);
	sync_all(h, &owner);
	click_parent(h, &owner, px, py);
	CHECK(harness_window_info(child)->stack_index == 1,
	      "the child fell behind its foreign parent");

	/* Destroying the import unsets the parent. */
	zxdg_imported_v2_destroy(imported);
	sync_all(h, &owner);
	click_parent(h, &owner, px, py);
	CHECK(harness_window_info(child)->stack_index == 0,
	      "destroying the import left the child attached");

	/* An exported handle that ends tells the importer and unsets the parent. */
	imported = zxdg_importer_v2_import_toplevel(me.importer, handle);
	zxdg_imported_v2_add_listener(imported, &imported_listener, NULL);
	zxdg_imported_v2_set_parent_of(imported, child->surface);
	sync_all(h, &owner);
	click_parent(h, &owner, px, py);
	CHECK(harness_window_info(child)->stack_index == 1, "no second link");
	zxdg_exported_v2_destroy(exported);
	sync_all(h, &owner);
	CHECK(imported_destroyed == 1, "the importer was not told");
	click_parent(h, &owner, px, py);
	CHECK(harness_window_info(child)->stack_index == 0,
	      "the export ended and the child stayed attached");
	zxdg_imported_v2_destroy(imported);

	/* An old or made-up handle: destroyed at once. */
	imported = zxdg_importer_v2_import_toplevel(me.importer, handle);
	zxdg_imported_v2_add_listener(imported, &imported_listener, NULL);
	harness_roundtrip(h);
	CHECK(imported_destroyed == 2, "an old handle was accepted");
	zxdg_imported_v2_set_parent_of(imported, child->surface);
	sync_all(h, &owner);
	click_parent(h, &owner, px, py);
	CHECK(harness_window_info(child)->stack_index == 0,
	      "a dead import set a parent");
	zxdg_imported_v2_destroy(imported);

	/* A parent exported before its first commit: the link waits for it. */
	struct peer_window late;

	peer_window_new(&owner, &late);
	handle[0] = '\0';
	exported = zxdg_exporter_v2_export_toplevel(owner.exporter, late.surface);
	zxdg_exported_v2_add_listener(exported, &exported_listener, NULL);
	wl_display_roundtrip(owner.display);
	CHECK(handle[0], "no handle for an unmapped toplevel");
	imported = zxdg_importer_v2_import_toplevel(me.importer, handle);
	zxdg_imported_v2_add_listener(imported, &imported_listener, NULL);
	zxdg_imported_v2_set_parent_of(imported, child->surface);
	sync_all(h, &owner);
	peer_window_map(&owner, &late, 800, 600);
	sync_all(h, &owner);
	CHECK(harness_window_info(child)->stack_index == 2,
	      "the child is not above the late parent: %d",
	      harness_window_info(child)->stack_index);
	click_parent(h, &owner, px + 2, py + 2);
	CHECK(harness_window_info(child)->stack_index == 2,
	      "the link did not wait for the first commit");
	zxdg_imported_v2_destroy(imported);
	zxdg_exported_v2_destroy(exported);
	xdg_toplevel_destroy(late.toplevel);
	xdg_surface_destroy(late.xdg);
	wl_surface_destroy(late.surface);
	sync_all(h, &owner);

	/* The surface going ends the export too. */
	struct peer_window short_lived;

	peer_window_new(&owner, &short_lived);
	handle[0] = '\0';
	exported = zxdg_exporter_v2_export_toplevel(owner.exporter,
						    short_lived.surface);
	zxdg_exported_v2_add_listener(exported, &exported_listener, NULL);
	wl_display_roundtrip(owner.display);
	imported = zxdg_importer_v2_import_toplevel(me.importer, handle);
	zxdg_imported_v2_add_listener(imported, &imported_listener, NULL);
	harness_roundtrip(h);
	int before = imported_destroyed;

	xdg_toplevel_destroy(short_lived.toplevel);
	xdg_surface_destroy(short_lived.xdg);
	wl_surface_destroy(short_lived.surface);
	sync_all(h, &owner);
	CHECK(imported_destroyed == before + 1,
	      "destroying the surface did not end the export");
	zxdg_imported_v2_destroy(imported);
	zxdg_exported_v2_destroy(exported);

	/* The modal hint. */
	CHECK(!(harness_window_info(child)->state & STATE_MODAL), "modal already");
	struct xdg_dialog_v1 *dialog =
		xdg_wm_dialog_v1_get_xdg_dialog(me.dialogs, child->xdg_toplevel);

	harness_roundtrip(h);
	CHECK(!(harness_window_info(child)->state & STATE_MODAL),
	      "modal with no set_modal");
	xdg_dialog_v1_set_modal(dialog);
	harness_roundtrip(h);
	CHECK(harness_window_info(child)->state & STATE_MODAL, "set_modal did nothing");
	xdg_dialog_v1_unset_modal(dialog);
	harness_roundtrip(h);
	CHECK(!(harness_window_info(child)->state & STATE_MODAL),
	      "unset_modal did nothing");
	xdg_dialog_v1_set_modal(dialog);
	harness_roundtrip(h);
	CHECK(harness_window_info(child)->state & STATE_MODAL, "set_modal again");
	xdg_dialog_v1_destroy(dialog);
	harness_roundtrip(h);
	CHECK(!(harness_window_info(child)->state & STATE_MODAL),
	      "destroying the object left the hint");

	/* Asked for before the first commit: it waits for the toplevel. */
	struct peer_window early;

	peer_window_new(&me, &early);
	dialog = xdg_wm_dialog_v1_get_xdg_dialog(me.dialogs, early.toplevel);
	xdg_dialog_v1_set_modal(dialog);
	peer_window_map(&me, &early, 100, 100);
	harness_roundtrip(h);
	struct harness_window early_window = { .harness = h,
					       .surface = early.surface };

	wl_surface_set_user_data(early.surface, &early_window);
	CHECK(harness_window_info(&early_window)->state & STATE_MODAL,
	      "a hint before the first commit was lost");
	xdg_dialog_v1_destroy(dialog);
	xdg_toplevel_destroy(early.toplevel);
	xdg_surface_destroy(early.xdg);
	wl_surface_destroy(early.surface);
	harness_roundtrip(h);

	/* A second object for one toplevel is an error, and a new one after destroy
	 * is not. */
	dialog = xdg_wm_dialog_v1_get_xdg_dialog(me.dialogs, child->xdg_toplevel);
	harness_roundtrip(h);
	xdg_dialog_v1_destroy(dialog);

	struct peer bad;
	struct peer_window w;

	peer_connect(&bad, h);
	peer_window_new(&bad, &w);
	xdg_wm_dialog_v1_get_xdg_dialog(bad.dialogs, w.toplevel);
	xdg_wm_dialog_v1_get_xdg_dialog(bad.dialogs, w.toplevel);
	expect_error(&bad, &xdg_wm_dialog_v1_interface,
		     XDG_WM_DIALOG_V1_ERROR_ALREADY_USED);

	/* Only toplevels. */
	peer_connect(&bad, h);
	zxdg_exporter_v2_export_toplevel(bad.exporter,
					 wl_compositor_create_surface(bad.compositor));
	expect_error(&bad, &zxdg_exporter_v2_interface,
		     ZXDG_EXPORTER_V2_ERROR_INVALID_SURFACE);

	peer_connect(&bad, h);
	struct zxdg_imported_v2 *any =
		zxdg_importer_v2_import_toplevel(bad.importer, "made-up");

	zxdg_imported_v2_set_parent_of(any,
				       wl_compositor_create_surface(bad.compositor));
	expect_error(&bad, &zxdg_imported_v2_interface,
		     ZXDG_IMPORTED_V2_ERROR_INVALID_SURFACE);

	wl_display_disconnect(owner.display);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
