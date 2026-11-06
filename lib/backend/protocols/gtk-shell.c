/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * gtk_shell1, version 4: what GTK 3 needs of it on Polo. GTK 3 rings the
 * bell only through system_bell, and sets the modal hint only through
 * gtk_surface1. Version 5 is not offered, so GTK keeps titlebar gestures
 * to itself. Polo has no global menus, so capabilities are none and
 * set_dbus_properties is ignored; GTK focuses windows through
 * xdg_activation_v1, so present and request_focus are too.
 */

#include <libweston/desktop.h>

#include "gtk-shell-server-protocol.h"
#include "protocol-util.h"

#define GTK_SHELL_VERSION 4

struct gtk_surface {
	struct wl_list link;
	struct polo_surface_ref surface;
	gboolean modal;
	gboolean applied;
};

static void
apply_modal(struct gtk_surface *gtk)
{
	struct weston_surface *surface = gtk->surface.surface;

	if (!surface || gtk->applied == gtk->modal ||
	    !weston_surface_is_desktop_surface(surface))
		return;
	if (polo_backend_toplevel_set_modal_hint(
		    weston_surface_get_desktop_surface(surface), gtk->modal))
		gtk->applied = gtk->modal;
}

static void
surface_set_dbus_properties(struct wl_client *client,
			    struct wl_resource *resource,
			    const char *application_id,
			    const char *app_menu_path, const char *menubar_path,
			    const char *window_object_path,
			    const char *application_object_path,
			    const char *unique_bus_name)
{
}

static void
surface_set_modal(struct wl_client *client, struct wl_resource *resource)
{
	struct gtk_surface *gtk = wl_resource_get_user_data(resource);

	gtk->modal = TRUE;
	apply_modal(gtk);
}

static void
surface_unset_modal(struct wl_client *client, struct wl_resource *resource)
{
	struct gtk_surface *gtk = wl_resource_get_user_data(resource);

	gtk->modal = FALSE;
	apply_modal(gtk);
}

static void
surface_present(struct wl_client *client, struct wl_resource *resource,
		uint32_t time)
{
}

static void
surface_request_focus(struct wl_client *client, struct wl_resource *resource,
		      const char *startup_id)
{
}

static const struct gtk_surface1_interface surface_impl = {
	.set_dbus_properties = surface_set_dbus_properties,
	.set_modal = surface_set_modal,
	.unset_modal = surface_unset_modal,
	.present = surface_present,
	.request_focus = surface_request_focus,
	.release = polo_protocol_destroy,
};

static void
surface_resource_destroyed(struct wl_resource *resource)
{
	struct gtk_surface *gtk = wl_resource_get_user_data(resource);

	gtk->modal = FALSE;
	apply_modal(gtk);
	polo_surface_ref_clear(&gtk->surface);
	wl_list_remove(&gtk->link);
	g_free(gtk);
}

static void
shell_get_gtk_surface(struct wl_client *client, struct wl_resource *resource,
		      uint32_t id, struct wl_resource *surface_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct gtk_surface *gtk = g_new0(struct gtk_surface, 1);

	if (!polo_protocol_child_new(resource, &gtk_surface1_interface, id,
				     &surface_impl, gtk,
				     surface_resource_destroyed)) {
		g_free(gtk);
		return;
	}
	polo_surface_ref_set(&gtk->surface,
			     wl_resource_get_user_data(surface_resource), NULL);
	wl_list_insert(&server->gtk_surfaces, &gtk->link);
}

static void
shell_set_startup_id(struct wl_client *client, struct wl_resource *resource,
		     const char *startup_id)
{
}

static void
shell_system_bell(struct wl_client *client, struct wl_resource *resource,
		  struct wl_resource *gtk_resource)
{
	struct gtk_surface *gtk =
		gtk_resource ? wl_resource_get_user_data(gtk_resource) : NULL;
	struct weston_surface *surface = gtk ? gtk->surface.surface : NULL;

	polo_backend_server_emit_system_bell(
		wl_resource_get_user_data(resource),
		surface ? polo_backend_toplevel_from_surface(
				  weston_surface_get_main_surface(surface))
			: NULL);
}

static void
shell_notify_launch(struct wl_client *client, struct wl_resource *resource,
		    const char *startup_id)
{
}

static const struct gtk_shell1_interface shell_impl = {
	.get_gtk_surface = shell_get_gtk_surface,
	.set_startup_id = shell_set_startup_id,
	.system_bell = shell_system_bell,
	.notify_launch = shell_notify_launch,
};

static void
shell_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource = polo_protocol_resource_new(
		client, &gtk_shell1_interface, MIN(version, GTK_SHELL_VERSION),
		id, &shell_impl, data, NULL);

	if (resource)
		gtk_shell1_send_capabilities(resource, 0);
}

void
polo_backend_gtk_shell_surface_added(PoloBackendServer *server)
{
	struct gtk_surface *gtk;

	wl_list_for_each(gtk, &server->gtk_surfaces, link) apply_modal(gtk);
}

gboolean
polo_backend_gtk_shell_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	wl_list_init(&server->gtk_surfaces);
	server->gtk_shell_global =
		wl_global_create(server->display, &gtk_shell1_interface,
				 GTK_SHELL_VERSION, server, shell_bind);
	return TRUE;
}

void
polo_backend_gtk_shell_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->gtk_shell_global, wl_global_destroy);
}
