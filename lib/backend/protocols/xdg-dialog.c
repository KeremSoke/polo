/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * xdg_wm_dialog_v1: the modal hint. A toplevel before its first commit has no
 * Polo toplevel, so the hint waits for it. Destroying the object ends the hint.
 */

#include <libweston/desktop.h>
#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "xdg-dialog-v1-server-protocol.h"

struct dialog {
	struct wl_list link;
	struct wl_resource *toplevel;
	struct wl_listener toplevel_destroy;
	gboolean modal;
	gboolean applied;
};

static void
apply_modal(struct dialog *dialog)
{
	struct weston_desktop_surface *surface = dialog->toplevel
		? wl_resource_get_user_data(dialog->toplevel)
		: NULL;

	if (!surface || dialog->applied == dialog->modal)
		return;
	if (polo_backend_toplevel_set_modal_hint(surface, dialog->modal))
		dialog->applied = dialog->modal;
}

static void
dialog_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
dialog_set_modal(struct wl_client *client, struct wl_resource *resource)
{
	struct dialog *dialog = wl_resource_get_user_data(resource);

	dialog->modal = TRUE;
	apply_modal(dialog);
}

static void
dialog_unset_modal(struct wl_client *client, struct wl_resource *resource)
{
	struct dialog *dialog = wl_resource_get_user_data(resource);

	dialog->modal = FALSE;
	apply_modal(dialog);
}

static const struct xdg_dialog_v1_interface dialog_impl = {
	.destroy = dialog_destroy,
	.set_modal = dialog_set_modal,
	.unset_modal = dialog_unset_modal,
};

static void
dialog_toplevel_destroyed(struct wl_listener *listener, void *data)
{
	struct dialog *dialog =
		wl_container_of(listener, dialog, toplevel_destroy);

	wl_list_remove(&dialog->toplevel_destroy.link);
	wl_list_init(&dialog->toplevel_destroy.link);
	dialog->toplevel = NULL;
}

static void
dialog_resource_destroyed(struct wl_resource *resource)
{
	struct dialog *dialog = wl_resource_get_user_data(resource);

	dialog->modal = FALSE;
	apply_modal(dialog);
	wl_list_remove(&dialog->toplevel_destroy.link);
	wl_list_remove(&dialog->link);
	g_free(dialog);
}

static void
manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
manager_get_xdg_dialog(struct wl_client *client, struct wl_resource *resource,
		       uint32_t id, struct wl_resource *toplevel)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct dialog *dialog;

	if (wl_resource_get_destroy_listener(toplevel,
					     dialog_toplevel_destroyed)) {
		wl_resource_post_error(
			resource, XDG_WM_DIALOG_V1_ERROR_ALREADY_USED,
			"this toplevel already has a dialog object");
		return;
	}
	dialog = g_new0(struct dialog, 1);

	struct wl_resource *object =
		wl_resource_create(client, &xdg_dialog_v1_interface, 1, id);

	if (!object) {
		g_free(dialog);
		wl_client_post_no_memory(client);
		return;
	}
	dialog->toplevel = toplevel;
	dialog->toplevel_destroy.notify = dialog_toplevel_destroyed;
	wl_resource_add_destroy_listener(toplevel, &dialog->toplevel_destroy);
	wl_list_insert(&server->xdg_dialogs, &dialog->link);
	wl_resource_set_implementation(object, &dialog_impl, dialog,
				       dialog_resource_destroyed);
}

static const struct xdg_wm_dialog_v1_interface manager_impl = {
	.destroy = manager_destroy,
	.get_xdg_dialog = manager_get_xdg_dialog,
};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource =
		wl_resource_create(client, &xdg_wm_dialog_v1_interface, 1, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

void
polo_backend_xdg_dialog_surface_added(PoloBackendServer *server)
{
	struct dialog *dialog;

	wl_list_for_each(dialog, &server->xdg_dialogs, link)
		apply_modal(dialog);
}

gboolean
polo_backend_xdg_dialog_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	wl_list_init(&server->xdg_dialogs);
	server->xdg_dialog_global =
		wl_global_create(server->display, &xdg_wm_dialog_v1_interface,
				 1, server, manager_bind);
	return TRUE;
}

void
polo_backend_xdg_dialog_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->xdg_dialog_global, wl_global_destroy);
}
