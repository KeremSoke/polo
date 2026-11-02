/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_foreign_toplevel_list_v1 (v1) shim: a read-only list of
 * the same windows as zwlr_foreign_toplevel_manager_v1, with an identifier
 * per window. It has no decisions of its own: wlr-foreign-toplevel.c tells
 * it when a PoloBackendForeignToplevel appears, changes or closes.
 *
 * Identifiers are random UUIDs written as 32 hexadecimal digits, made once
 * per window and never reused (the protocol asks for at most 32 printable
 * bytes and no reuse, ever).
 */

#include <libweston/libweston.h>

#include "ext-foreign-toplevel-list-v1-server-protocol.h"
#include "foreign-toplevel-private.h"

#define LIST_VERSION 1

struct ext_toplevel {
	char identifier[33];
	struct wl_list resources; /* handle wl_resources */
	gboolean dirty;           /* title or app id changed since done */
};

static const struct ext_foreign_toplevel_handle_v1_interface
	handle_implementation;

static struct ext_toplevel *
ext_state(PoloBackendForeignToplevel *toplevel)
{
	if (!toplevel->ext) {
		g_autofree char *uuid = g_uuid_string_random();
		struct ext_toplevel *ext = g_new0(struct ext_toplevel, 1);
		int i, j;

		/* 8-4-4-4-12 without the dashes: exactly 32 bytes. */
		for (i = j = 0; uuid[i] && j < 32; i++) {
			if (uuid[i] != '-')
				ext->identifier[j++] = uuid[i];
		}
		wl_list_init(&ext->resources);
		toplevel->ext = ext;
	}
	return toplevel->ext;
}

static void
handle_resource_destroyed(struct wl_resource *resource)
{
	/* Closed handles were unlinked already. */
	if (wl_resource_get_user_data(resource))
		wl_list_remove(wl_resource_get_link(resource));
}

static void
handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct ext_foreign_toplevel_handle_v1_interface
	handle_implementation = {
		.destroy = handle_destroy,
	};

static void
handle_create(PoloBackendForeignToplevel *toplevel, struct wl_resource *list)
{
	struct ext_toplevel *ext = ext_state(toplevel);
	struct wl_client *client = wl_resource_get_client(list);
	struct wl_resource *resource;

	resource = wl_resource_create(client,
				      &ext_foreign_toplevel_handle_v1_interface,
				      wl_resource_get_version(list), 0);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &handle_implementation,
				       toplevel, handle_resource_destroyed);
	wl_list_insert(ext->resources.prev, wl_resource_get_link(resource));

	ext_foreign_toplevel_list_v1_send_toplevel(list, resource);
	ext_foreign_toplevel_handle_v1_send_identifier(resource,
						       ext->identifier);
	if (toplevel->title)
		ext_foreign_toplevel_handle_v1_send_title(resource,
							  toplevel->title);
	if (toplevel->app_id)
		ext_foreign_toplevel_handle_v1_send_app_id(resource,
							   toplevel->app_id);
	ext_foreign_toplevel_handle_v1_send_done(resource);
}

void
polo_backend_ext_toplevel_created(PoloBackendForeignToplevel *toplevel)
{
	struct wl_resource *list;

	ext_state(toplevel);
	wl_resource_for_each(list, &toplevel->server->ext_toplevel_lists)
		handle_create(toplevel, list);
}

void
polo_backend_ext_toplevel_title_changed(PoloBackendForeignToplevel *toplevel)
{
	struct ext_toplevel *ext = ext_state(toplevel);
	struct wl_resource *resource;

	wl_resource_for_each(resource, &ext->resources)
		ext_foreign_toplevel_handle_v1_send_title(resource,
							  toplevel->title);
	ext->dirty = TRUE;
}

void
polo_backend_ext_toplevel_app_id_changed(PoloBackendForeignToplevel *toplevel)
{
	struct ext_toplevel *ext = ext_state(toplevel);
	struct wl_resource *resource;

	wl_resource_for_each(resource, &ext->resources)
		ext_foreign_toplevel_handle_v1_send_app_id(resource,
							   toplevel->app_id);
	ext->dirty = TRUE;
}

/* Only changes this protocol shows end in done. */
void
polo_backend_ext_toplevel_done(PoloBackendForeignToplevel *toplevel)
{
	struct ext_toplevel *ext = ext_state(toplevel);
	struct wl_resource *resource;

	if (!ext->dirty)
		return;
	ext->dirty = FALSE;
	wl_resource_for_each(resource, &ext->resources)
		ext_foreign_toplevel_handle_v1_send_done(resource);
}

void
polo_backend_ext_toplevel_closed(PoloBackendForeignToplevel *toplevel)
{
	struct ext_toplevel *ext = toplevel->ext;
	struct wl_resource *resource, *tmp;

	if (!ext)
		return;
	wl_resource_for_each_safe(resource, tmp, &ext->resources)
	{
		ext_foreign_toplevel_handle_v1_send_closed(resource);
		wl_list_remove(wl_resource_get_link(resource));
		wl_list_init(wl_resource_get_link(resource));
		wl_resource_set_user_data(resource, NULL);
	}
	g_free(ext);
	toplevel->ext = NULL;
}

static void
list_resource_destroyed(struct wl_resource *resource)
{
	/* Stopped lists were unlinked already. */
	if (wl_resource_get_user_data(resource))
		wl_list_remove(wl_resource_get_link(resource));
}

static void
list_stop(struct wl_client *client, struct wl_resource *resource)
{
	if (!wl_resource_get_user_data(resource))
		return;
	/* No more toplevel events; the client destroys the list later. */
	wl_list_remove(wl_resource_get_link(resource));
	wl_list_init(wl_resource_get_link(resource));
	wl_resource_set_user_data(resource, NULL);
	ext_foreign_toplevel_list_v1_send_finished(resource);
}

static void
list_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct ext_foreign_toplevel_list_v1_interface
	list_implementation = {
		.stop = list_stop,
		.destroy = list_destroy,
	};

static void
list_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	PoloBackendServer *server = data;
	PoloBackendForeignToplevel *toplevel;
	struct wl_resource *resource;

	resource = wl_resource_create(
		client, &ext_foreign_toplevel_list_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &list_implementation, server,
				       list_resource_destroyed);
	wl_list_insert(&server->ext_toplevel_lists,
		       wl_resource_get_link(resource));

	/* Existing toplevels, oldest first. */
	wl_list_for_each_reverse(toplevel, &server->foreign_toplevels, link)
	{
		if (!toplevel->closed)
			handle_create(toplevel, resource);
	}
}

gboolean
polo_backend_ext_toplevel_list_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	server->ext_toplevel_list_global = wl_global_create(
		server->display, &ext_foreign_toplevel_list_v1_interface,
		LIST_VERSION, server, list_bind);
	return TRUE;
}

void
polo_backend_ext_toplevel_list_fini(PoloBackendServer *server)
{
	if (server->ext_toplevel_list_global) {
		wl_global_destroy(server->ext_toplevel_list_global);
		server->ext_toplevel_list_global = NULL;
	}
}
