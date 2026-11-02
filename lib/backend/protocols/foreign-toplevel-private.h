/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PoloBackendForeignToplevel, shared by the two task-bar protocols:
 * zwlr_foreign_toplevel_manager_v1 (wlr-foreign-toplevel.c, which owns
 * the object) and ext_foreign_toplevel_list_v1 (ext-foreign-toplevel-list.c,
 * which wlr-foreign-toplevel.c notifies through the functions below).
 */

#ifndef POLO_FOREIGN_TOPLEVEL_PRIVATE_H
#define POLO_FOREIGN_TOPLEVEL_PRIVATE_H

#include "polo-backend-private.h"

struct ext_toplevel;

struct ft_handle_resource {
	PoloBackendForeignToplevel *handle;
	struct wl_resource *resource;
	struct wl_list link;
	GPtrArray *entered;
	struct weston_surface *rect_surface;
	struct wl_listener rect_surface_destroyed;
	int32_t rect_x, rect_y, rect_width, rect_height;
	struct wl_resource *xfce;
	GPtrArray *entered_workspaces; /* ext_workspace_handle_v1 resources */
};

struct _PoloBackendForeignToplevel {
	GObject parent_instance;

	PoloBackendServer *server;
	struct wl_list resources; /* ft_handle_resource.link */
	struct wl_list link;      /* server->foreign_toplevels */
	char *title;
	char *app_id;
	PoloBackendForeignToplevelState state;
	GPtrArray *outputs;                 /* PoloBackendOutput, unowned */
	PoloBackendForeignToplevel *parent; /* unowned; cleared by close () */
	gboolean closed;
	struct ext_toplevel *ext;
	PoloBackendForeignToplevelExtraState extra_state;
	GPtrArray *workspaces; /* PoloBackendWorkspace, owned */
};

void
polo_backend_ext_toplevel_created(PoloBackendForeignToplevel *toplevel);

void
polo_backend_ext_toplevel_title_changed(PoloBackendForeignToplevel *toplevel);

void
polo_backend_ext_toplevel_app_id_changed(PoloBackendForeignToplevel *toplevel);

void
polo_backend_ext_toplevel_done(PoloBackendForeignToplevel *toplevel);

void
polo_backend_ext_toplevel_closed(PoloBackendForeignToplevel *toplevel);

void
polo_backend_xfce_handle_resource_destroyed(struct ft_handle_resource *hr);

void
polo_backend_xfce_send_state(struct ft_handle_resource *hr);

#endif
