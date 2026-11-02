/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zxdg_exporter_v2 and zxdg_importer_v2. set_parent_of does what
 * xdg_toplevel.set_parent does, across clients. A child or parent that has not
 * had its first commit has no Polo toplevel yet, so the link waits for it.
 */

#include <string.h>

#include <libweston/desktop.h>
#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "xdg-foreign-unstable-v2-server-protocol.h"

struct exported {
	struct wl_list link;
	struct wl_resource *resource;
	struct weston_surface *surface;
	struct wl_listener surface_destroy;
	char *handle;
	struct wl_list imports;
};

struct imported {
	struct wl_list exported_link;
	struct wl_resource *resource;
	struct exported *exported;
	struct wl_list children;
};

struct child {
	struct wl_list link;
	struct imported *imported;
	struct weston_surface *surface;
	struct wl_listener surface_destroy;
	gboolean applied;
};

static void
apply_child(struct child *child)
{
	struct weston_surface *parent = child->imported->exported
		? child->imported->exported->surface
		: NULL;

	if (!parent ||
	    !polo_backend_toplevel_foreign_parent(
		    weston_surface_get_desktop_surface(child->surface),
		    weston_surface_get_desktop_surface(parent)))
		return;
	child->applied = TRUE;
}

static void
child_free(struct child *child, gboolean unset)
{
	if (unset && child->applied && child->surface)
		polo_backend_toplevel_foreign_parent(
			weston_surface_get_desktop_surface(child->surface),
			NULL);
	wl_list_remove(&child->surface_destroy.link);
	wl_list_remove(&child->link);
	g_free(child);
}

static void
child_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct child *child = wl_container_of(listener, child, surface_destroy);

	child_free(child, FALSE);
}

static void
imported_detach(struct imported *imported)
{
	struct child *child, *next;

	wl_list_for_each_safe(child, next, &imported->children, link)
		child_free(child, TRUE);
	if (imported->exported)
		wl_list_remove(&imported->exported_link);
	imported->exported = NULL;
}

static void
imported_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
imported_set_parent_of(struct wl_client *client, struct wl_resource *resource,
		       struct wl_resource *surface_resource)
{
	struct imported *imported = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct child *child;

	if (!weston_surface_is_desktop_surface(surface)) {
		wl_resource_post_error(resource,
				       ZXDG_IMPORTED_V2_ERROR_INVALID_SURFACE,
				       "the surface is not an xdg_toplevel");
		return;
	}
	if (!imported->exported)
		return;
	child = g_new0(struct child, 1);
	child->imported = imported;
	child->surface = surface;
	child->surface_destroy.notify = child_surface_destroyed;
	wl_signal_add(&surface->destroy_signal, &child->surface_destroy);
	wl_list_insert(&imported->children, &child->link);
	apply_child(child);
}

static const struct zxdg_imported_v2_interface imported_impl = {
	.destroy = imported_destroy,
	.set_parent_of = imported_set_parent_of,
};

static void
imported_resource_destroyed(struct wl_resource *resource)
{
	struct imported *imported = wl_resource_get_user_data(resource);

	imported_detach(imported);
	g_free(imported);
}

static void
exported_end(struct exported *exported)
{
	struct imported *imported, *next;

	wl_list_for_each_safe(imported, next, &exported->imports, exported_link)
	{
		imported_detach(imported);
		zxdg_imported_v2_send_destroyed(imported->resource);
	}
	if (exported->surface)
		wl_list_remove(&exported->surface_destroy.link);
	exported->surface = NULL;
	wl_list_remove(&exported->link);
	wl_list_init(&exported->link);
}

static void
exported_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct exported *exported =
		wl_container_of(listener, exported, surface_destroy);

	exported_end(exported);
}

static void
exported_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zxdg_exported_v2_interface exported_impl = {
	.destroy = exported_destroy,
};

static void
exported_resource_destroyed(struct wl_resource *resource)
{
	struct exported *exported = wl_resource_get_user_data(resource);

	exported_end(exported);
	g_free(exported->handle);
	g_free(exported);
}

static void
exporter_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
exporter_export_toplevel(struct wl_client *client, struct wl_resource *resource,
			 uint32_t id, struct wl_resource *surface_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct exported *exported;

	if (!weston_surface_is_desktop_surface(surface)) {
		wl_resource_post_error(resource,
				       ZXDG_EXPORTER_V2_ERROR_INVALID_SURFACE,
				       "the surface is not an xdg_toplevel");
		return;
	}
	exported = g_new0(struct exported, 1);
	exported->resource =
		wl_resource_create(client, &zxdg_exported_v2_interface, 1, id);
	if (!exported->resource) {
		g_free(exported);
		wl_client_post_no_memory(client);
		return;
	}
	exported->surface = surface;
	exported->handle = g_uuid_string_random();
	wl_list_init(&exported->imports);
	exported->surface_destroy.notify = exported_surface_destroyed;
	wl_signal_add(&surface->destroy_signal, &exported->surface_destroy);
	wl_list_insert(&server->xdg_exports, &exported->link);
	wl_resource_set_implementation(exported->resource, &exported_impl,
				       exported, exported_resource_destroyed);
	zxdg_exported_v2_send_handle(exported->resource, exported->handle);
}

static const struct zxdg_exporter_v2_interface exporter_impl = {
	.destroy = exporter_destroy,
	.export_toplevel = exporter_export_toplevel,
};

static void
exporter_bind(struct wl_client *client, void *data, uint32_t version,
	      uint32_t id)
{
	struct wl_resource *resource =
		wl_resource_create(client, &zxdg_exporter_v2_interface, 1, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &exporter_impl, data, NULL);
}

static void
importer_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
importer_import_toplevel(struct wl_client *client, struct wl_resource *resource,
			 uint32_t id, const char *handle)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct imported *imported = g_new0(struct imported, 1);
	struct exported *exported;

	imported->resource =
		wl_resource_create(client, &zxdg_imported_v2_interface, 1, id);
	if (!imported->resource) {
		g_free(imported);
		wl_client_post_no_memory(client);
		return;
	}
	wl_list_init(&imported->children);
	wl_list_init(&imported->exported_link);
	wl_resource_set_implementation(imported->resource, &imported_impl,
				       imported, imported_resource_destroyed);
	wl_list_for_each(exported, &server->xdg_exports, link)
	{
		if (strcmp(exported->handle, handle) == 0) {
			imported->exported = exported;
			wl_list_insert(&exported->imports,
				       &imported->exported_link);
			return;
		}
	}
	zxdg_imported_v2_send_destroyed(imported->resource);
}

static const struct zxdg_importer_v2_interface importer_impl = {
	.destroy = importer_destroy,
	.import_toplevel = importer_import_toplevel,
};

static void
importer_bind(struct wl_client *client, void *data, uint32_t version,
	      uint32_t id)
{
	struct wl_resource *resource =
		wl_resource_create(client, &zxdg_importer_v2_interface, 1, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &importer_impl, data, NULL);
}

void
polo_backend_xdg_foreign_surface_added(PoloBackendServer *server)
{
	struct exported *exported;
	struct imported *imported;
	struct child *child;

	wl_list_for_each(exported, &server->xdg_exports, link)
	{
		wl_list_for_each(imported, &exported->imports, exported_link)
		{
			wl_list_for_each(child, &imported->children, link)
			{
				if (!child->applied)
					apply_child(child);
			}
		}
	}
}

gboolean
polo_backend_xdg_foreign_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	wl_list_init(&server->xdg_exports);
	server->xdg_exporter_global =
		wl_global_create(server->display, &zxdg_exporter_v2_interface,
				 1, server, exporter_bind);
	server->xdg_importer_global =
		wl_global_create(server->display, &zxdg_importer_v2_interface,
				 1, server, importer_bind);
	return TRUE;
}

void
polo_backend_xdg_foreign_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->xdg_exporter_global, wl_global_destroy);
	g_clear_pointer(&server->xdg_importer_global, wl_global_destroy);
}
