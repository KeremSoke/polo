/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_workspace_manager_v1 (v1) shim: one group holding every workspace on
 * every output, one set of handles per manager resource, requests held until
 * commit. What the workspaces are and what the requests do is decided in
 * src/protocols/workspaces.vala.
 */

#include <libweston/libweston.h>

#include "foreign-toplevel-private.h"
#include "ext-workspace-v1-server-protocol.h"

#define MANAGER_VERSION 1

enum op_kind {
	OP_ACTIVATE,
	OP_REMOVE,
	OP_CREATE,
};

struct op {
	enum op_kind kind;
	PoloBackendWorkspace *workspace; /* owned */
	char *name;
};

struct manager_resource {
	PoloBackendWorkspaceManager *manager;
	struct wl_resource *resource;
	struct wl_resource *group;
	struct wl_list link;
	GPtrArray *entered; /* wl_output resources */
	GPtrArray *ops;     /* struct op, until commit */
	struct wl_listener resource_created;
	struct wl_event_source *idle;
};

struct handle_resource {
	PoloBackendWorkspace *workspace;
	struct manager_resource *manager;
	struct wl_resource *resource;
	struct wl_list link; /* workspace->handles */
};

struct _PoloBackendWorkspaceManager {
	GObject parent_instance;

	PoloBackendServer *server;
	struct wl_global *global;
	struct wl_list resources; /* manager_resource.link */
	GPtrArray *workspaces;    /* PoloBackendWorkspace, owned */
};

struct _PoloBackendWorkspace {
	GObject parent_instance;

	PoloBackendWorkspaceManager *manager;
	char *id;
	char *name;
	uint32_t coordinates[2];
	guint state;
	guint capabilities;
	struct wl_list handles; /* handle_resource.link */
	gboolean removed;
};

G_DEFINE_FINAL_TYPE(PoloBackendWorkspaceManager, polo_backend_workspace_manager,
		    G_TYPE_OBJECT)
G_DEFINE_FINAL_TYPE(PoloBackendWorkspace, polo_backend_workspace, G_TYPE_OBJECT)

enum {
	MANAGER_SIGNAL_CREATE,
	MANAGER_N_SIGNALS,
};

enum {
	WORKSPACE_SIGNAL_ACTIVATE,
	WORKSPACE_SIGNAL_REMOVE,
	WORKSPACE_N_SIGNALS,
};

static guint manager_signals[MANAGER_N_SIGNALS];
static guint workspace_signals[WORKSPACE_N_SIGNALS];

static void
op_free(gpointer data)
{
	struct op *op = data;

	g_clear_object(&op->workspace);
	g_free(op->name);
	g_free(op);
}

static void
queue_op(struct manager_resource *mr, enum op_kind kind,
	 PoloBackendWorkspace *workspace, const char *name)
{
	struct op *op = g_new0(struct op, 1);

	op->kind = kind;
	op->workspace = workspace ? g_object_ref(workspace) : NULL;
	op->name = g_strdup(name);
	g_ptr_array_add(mr->ops, op);
}

/* ---- ext_workspace_handle_v1 -------------------------------------------- */

static void
handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
handle_activate(struct wl_client *client, struct wl_resource *resource)
{
	struct handle_resource *hr = wl_resource_get_user_data(resource);

	if (hr && hr->manager)
		queue_op(hr->manager, OP_ACTIVATE, hr->workspace, NULL);
}

static void
handle_deactivate(struct wl_client *client, struct wl_resource *resource)
{
}

static void
handle_assign(struct wl_client *client, struct wl_resource *resource,
	      struct wl_resource *group)
{
}

static void
handle_remove(struct wl_client *client, struct wl_resource *resource)
{
	struct handle_resource *hr = wl_resource_get_user_data(resource);

	if (hr && hr->manager)
		queue_op(hr->manager, OP_REMOVE, hr->workspace, NULL);
}

static const struct ext_workspace_handle_v1_interface handle_implementation = {
	.destroy = handle_destroy,
	.activate = handle_activate,
	.deactivate = handle_deactivate,
	.assign = handle_assign,
	.remove = handle_remove,
};

static void
handle_resource_destroyed(struct wl_resource *resource)
{
	struct handle_resource *hr = wl_resource_get_user_data(resource);

	if (!hr)
		return;
	polo_backend_xfce_workspace_handle_gone(hr->workspace->manager->server,
						resource);
	wl_list_remove(&hr->link);
	g_free(hr);
}

static void
handle_send_all(struct handle_resource *hr)
{
	PoloBackendWorkspace *self = hr->workspace;
	struct wl_array coordinates;

	ext_workspace_handle_v1_send_id(hr->resource, self->id);
	ext_workspace_handle_v1_send_name(hr->resource, self->name);
	wl_array_init(&coordinates);
	memcpy(wl_array_add(&coordinates, sizeof self->coordinates),
	       self->coordinates, sizeof self->coordinates);
	ext_workspace_handle_v1_send_coordinates(hr->resource, &coordinates);
	wl_array_release(&coordinates);
	ext_workspace_handle_v1_send_state(hr->resource, self->state);
	ext_workspace_handle_v1_send_capabilities(hr->resource,
						  self->capabilities);
}

static void
handle_create(PoloBackendWorkspace *self, struct manager_resource *mr)
{
	struct wl_client *client = wl_resource_get_client(mr->resource);
	struct handle_resource *hr;
	struct wl_resource *resource =
		wl_resource_create(client, &ext_workspace_handle_v1_interface,
				   wl_resource_get_version(mr->resource), 0);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	hr = g_new0(struct handle_resource, 1);
	hr->workspace = self;
	hr->manager = mr;
	hr->resource = resource;
	wl_list_insert(self->handles.prev, &hr->link);
	wl_resource_set_implementation(resource, &handle_implementation, hr,
				       handle_resource_destroyed);
	ext_workspace_manager_v1_send_workspace(mr->resource, resource);
	handle_send_all(hr);
	if (mr->group)
		ext_workspace_group_handle_v1_send_workspace_enter(mr->group,
								   resource);
}

/* ---- ext_workspace_group_handle_v1 -------------------------------------- */

static void
group_create_workspace(struct wl_client *client, struct wl_resource *resource,
		       const char *name)
{
	struct manager_resource *mr = wl_resource_get_user_data(resource);

	if (mr)
		queue_op(mr, OP_CREATE, NULL, name);
}

static void
group_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct ext_workspace_group_handle_v1_interface
	group_implementation = {
		.create_workspace = group_create_workspace,
		.destroy = group_destroy,
	};

static void
group_resource_destroyed(struct wl_resource *resource)
{
	struct manager_resource *mr = wl_resource_get_user_data(resource);

	if (mr)
		mr->group = NULL;
}

/* Bring output_enter and output_leave in line; whether anything was sent. */
static gboolean
group_sync_outputs(struct manager_resource *mr)
{
	PoloBackendServer *server = mr->manager->server;
	struct wl_client *client = wl_resource_get_client(mr->resource);
	GPtrArray *entered = g_ptr_array_new();
	struct weston_head *head = NULL;
	gboolean changed = FALSE;

	if (!mr->group) {
		g_ptr_array_unref(entered);
		return FALSE;
	}
	while ((head = weston_compositor_iterate_heads(server->compositor,
						       head))) {
		struct wl_resource *output;
		gboolean on = head->output && head->output->enabled;

		wl_resource_for_each(output, &head->resource_list)
		{
			gboolean was;

			if (wl_resource_get_client(output) != client)
				continue;
			was = g_ptr_array_find(mr->entered, output, NULL);
			if (on && !was) {
				ext_workspace_group_handle_v1_send_output_enter(
					mr->group, output);
				changed = TRUE;
			} else if (!on && was) {
				ext_workspace_group_handle_v1_send_output_leave(
					mr->group, output);
				changed = TRUE;
			}
			if (on)
				g_ptr_array_add(entered, output);
		}
	}
	g_ptr_array_unref(mr->entered);
	mr->entered = entered;
	return changed;
}

static void
output_bound_idle(void *data)
{
	struct manager_resource *mr = data;

	mr->idle = NULL;
	if (group_sync_outputs(mr))
		ext_workspace_manager_v1_send_done(mr->resource);
}

static void
client_resource_created(struct wl_listener *listener, void *data)
{
	struct manager_resource *mr =
		wl_container_of(listener, mr, resource_created);

	if (!mr->idle && strcmp(wl_resource_get_class(data), "wl_output") == 0)
		mr->idle = wl_event_loop_add_idle(
			wl_display_get_event_loop(mr->manager->server->display),
			output_bound_idle, mr);
}

/* ---- ext_workspace_manager_v1 ------------------------------------------- */

static void
manager_commit(struct wl_client *client, struct wl_resource *resource)
{
	struct manager_resource *mr = wl_resource_get_user_data(resource);
	GPtrArray *ops = mr->ops;
	guint i;

	mr->ops = g_ptr_array_new_with_free_func(op_free);
	for (i = 0; i < ops->len; i++) {
		struct op *op = g_ptr_array_index(ops, i);

		switch (op->kind) {
		case OP_ACTIVATE:
			if (!op->workspace->removed)
				g_signal_emit(
					op->workspace,
					workspace_signals
						[WORKSPACE_SIGNAL_ACTIVATE],
					0);
			break;
		case OP_REMOVE:
			if (!op->workspace->removed)
				g_signal_emit(op->workspace,
					      workspace_signals
						      [WORKSPACE_SIGNAL_REMOVE],
					      0);
			break;
		case OP_CREATE:
			g_signal_emit(mr->manager,
				      manager_signals[MANAGER_SIGNAL_CREATE], 0,
				      op->name);
			break;
		}
	}
	g_ptr_array_unref(ops);
}

static void
manager_stop(struct wl_client *client, struct wl_resource *resource)
{
	ext_workspace_manager_v1_send_finished(resource);
	wl_resource_destroy(resource);
}

static const struct ext_workspace_manager_v1_interface
	manager_implementation = {
		.commit = manager_commit,
		.stop = manager_stop,
	};

static void
manager_resource_destroyed(struct wl_resource *resource)
{
	struct manager_resource *mr = wl_resource_get_user_data(resource);
	guint i;

	for (i = 0; i < mr->manager->workspaces->len; i++) {
		PoloBackendWorkspace *workspace =
			g_ptr_array_index(mr->manager->workspaces, i);
		struct handle_resource *hr;

		wl_list_for_each(hr, &workspace->handles,
				 link) if (hr->manager == mr) hr->manager =
			NULL;
	}
	if (mr->group)
		wl_resource_set_user_data(mr->group, NULL);
	if (mr->idle)
		wl_event_source_remove(mr->idle);
	wl_list_remove(&mr->resource_created.link);
	wl_list_remove(&mr->link);
	g_ptr_array_unref(mr->entered);
	g_ptr_array_unref(mr->ops);
	g_free(mr);
}

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	PoloBackendWorkspaceManager *self = data;
	struct manager_resource *mr;
	struct wl_resource *resource = wl_resource_create(
		client, &ext_workspace_manager_v1_interface, (int)version, id);
	guint i;

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	mr = g_new0(struct manager_resource, 1);
	mr->manager = self;
	mr->resource = resource;
	mr->entered = g_ptr_array_new();
	mr->ops = g_ptr_array_new_with_free_func(op_free);
	wl_list_insert(&self->resources, &mr->link);
	wl_resource_set_implementation(resource, &manager_implementation, mr,
				       manager_resource_destroyed);
	mr->resource_created.notify = client_resource_created;
	wl_client_add_resource_created_listener(client, &mr->resource_created);

	mr->group = wl_resource_create(client,
				       &ext_workspace_group_handle_v1_interface,
				       (int)version, 0);
	if (!mr->group) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(mr->group, &group_implementation, mr,
				       group_resource_destroyed);
	ext_workspace_manager_v1_send_workspace_group(resource, mr->group);
	ext_workspace_group_handle_v1_send_capabilities(
		mr->group,
		EXT_WORKSPACE_GROUP_HANDLE_V1_GROUP_CAPABILITIES_CREATE_WORKSPACE);
	group_sync_outputs(mr);
	for (i = 0; i < self->workspaces->len; i++)
		handle_create(g_ptr_array_index(self->workspaces, i), mr);
	ext_workspace_manager_v1_send_done(resource);
	polo_backend_xfce_sync_all_workspaces(self->server);
}

gboolean
polo_backend_workspace_manager_init_global(PoloBackendServer *server,
					   GError **error)
{
	PoloBackendWorkspaceManager *self =
		g_object_new(POLO_BACKEND_TYPE_WORKSPACE_MANAGER, NULL);

	self->server = server;
	self->global = wl_global_create(server->display,
					&ext_workspace_manager_v1_interface,
					MANAGER_VERSION, self, manager_bind);
	if (!self->global) {
		g_object_unref(self);
		g_set_error(
			error, POLO_BACKEND_ERROR, POLO_BACKEND_ERROR_FAILED,
			"cannot create the ext_workspace_manager_v1 global");
		return FALSE;
	}
	server->workspace_manager = self;
	return TRUE;
}

void
polo_backend_workspace_manager_fini_global(PoloBackendServer *server)
{
	PoloBackendWorkspaceManager *self = server->workspace_manager;
	struct manager_resource *mr, *tmp;
	guint i;

	if (!self)
		return;
	wl_list_for_each_safe(mr, tmp, &self->resources, link)
	{
		wl_resource_set_destructor(mr->resource, NULL);
		manager_resource_destroyed(mr->resource);
	}
	for (i = 0; i < self->workspaces->len; i++) {
		PoloBackendWorkspace *workspace =
			g_ptr_array_index(self->workspaces, i);
		struct handle_resource *hr, *next;

		wl_list_for_each_safe(hr, next, &workspace->handles, link)
		{
			wl_resource_set_user_data(hr->resource, NULL);
			wl_list_remove(&hr->link);
			g_free(hr);
		}
		workspace->removed = TRUE;
	}
	wl_global_destroy(self->global);
	self->global = NULL;
	server->workspace_manager = NULL;
	g_object_unref(self);
}

PoloBackendWorkspaceManager *
polo_backend_server_get_workspace_manager(PoloBackendServer *server)
{
	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), NULL);
	return server->workspace_manager;
}

PoloBackendWorkspace *
polo_backend_workspace_manager_add(PoloBackendWorkspaceManager *self,
				   const char *id, const char *name, guint col,
				   guint row, guint state, guint capabilities)
{
	PoloBackendWorkspace *workspace;
	struct manager_resource *mr;

	g_return_val_if_fail(POLO_BACKEND_IS_WORKSPACE_MANAGER(self), NULL);
	workspace = g_object_new(POLO_BACKEND_TYPE_WORKSPACE, NULL);
	workspace->manager = self;
	workspace->id = g_strdup(id);
	workspace->name = g_strdup(name);
	workspace->coordinates[0] = col;
	workspace->coordinates[1] = row;
	workspace->state = state;
	workspace->capabilities = capabilities;
	g_ptr_array_add(self->workspaces, workspace);
	wl_list_for_each(mr, &self->resources, link)
		handle_create(workspace, mr);
	return workspace;
}

void
polo_backend_workspace_manager_done(PoloBackendWorkspaceManager *self)
{
	struct manager_resource *mr;

	g_return_if_fail(POLO_BACKEND_IS_WORKSPACE_MANAGER(self));
	wl_list_for_each(mr, &self->resources, link)
	{
		group_sync_outputs(mr);
		ext_workspace_manager_v1_send_done(mr->resource);
	}
}

/* ---- Workspace ---------------------------------------------------------- */

void
polo_backend_workspace_set_name(PoloBackendWorkspace *self, const char *name)
{
	struct handle_resource *hr;

	g_return_if_fail(POLO_BACKEND_IS_WORKSPACE(self));
	if (g_strcmp0(self->name, name) == 0)
		return;
	g_free(self->name);
	self->name = g_strdup(name);
	wl_list_for_each(hr, &self->handles, link)
		ext_workspace_handle_v1_send_name(hr->resource, name);
}

void
polo_backend_workspace_set_coordinates(PoloBackendWorkspace *self, guint col,
				       guint row)
{
	struct handle_resource *hr;
	struct wl_array coordinates;

	g_return_if_fail(POLO_BACKEND_IS_WORKSPACE(self));
	if (self->coordinates[0] == col && self->coordinates[1] == row)
		return;
	self->coordinates[0] = col;
	self->coordinates[1] = row;
	wl_array_init(&coordinates);
	memcpy(wl_array_add(&coordinates, sizeof self->coordinates),
	       self->coordinates, sizeof self->coordinates);
	wl_list_for_each(hr, &self->handles, link)
		ext_workspace_handle_v1_send_coordinates(hr->resource,
							 &coordinates);
	wl_array_release(&coordinates);
}

void
polo_backend_workspace_set_state(PoloBackendWorkspace *self, guint state)
{
	struct handle_resource *hr;

	g_return_if_fail(POLO_BACKEND_IS_WORKSPACE(self));
	if (self->state == state)
		return;
	self->state = state;
	wl_list_for_each(hr, &self->handles, link)
		ext_workspace_handle_v1_send_state(hr->resource, state);
}

void
polo_backend_workspace_remove(PoloBackendWorkspace *self)
{
	struct handle_resource *hr, *next;
	PoloBackendWorkspaceManager *manager;

	g_return_if_fail(POLO_BACKEND_IS_WORKSPACE(self));
	if (self->removed)
		return;
	self->removed = TRUE;
	manager = self->manager;
	polo_backend_xfce_workspace_removed(manager->server, self);
	wl_list_for_each_safe(hr, next, &self->handles, link)
	{
		if (hr->manager && hr->manager->group)
			ext_workspace_group_handle_v1_send_workspace_leave(
				hr->manager->group, hr->resource);
		ext_workspace_handle_v1_send_removed(hr->resource);
		wl_resource_set_user_data(hr->resource, NULL);
		wl_list_remove(&hr->link);
		g_free(hr);
	}
	g_ptr_array_remove(manager->workspaces, self);
}

/* The client's handle of a workspace, or NULL. */
struct wl_resource *
polo_backend_workspace_resource_for(PoloBackendWorkspace *self,
				    struct wl_client *client)
{
	struct handle_resource *hr;

	wl_list_for_each(hr, &self->handles,
			 link) if (wl_resource_get_client(hr->resource) ==
				   client) return hr->resource;
	return NULL;
}

/* The workspace behind a handle a client passes, or NULL when inert. */
PoloBackendWorkspace *
polo_backend_workspace_from_resource(struct wl_resource *resource)
{
	struct handle_resource *hr = wl_resource_get_user_data(resource);

	return hr ? hr->workspace : NULL;
}

/* ---- GObject ------------------------------------------------------------ */

static void
polo_backend_workspace_finalize(GObject *object)
{
	PoloBackendWorkspace *self = POLO_BACKEND_WORKSPACE(object);

	g_free(self->id);
	g_free(self->name);
	G_OBJECT_CLASS(polo_backend_workspace_parent_class)->finalize(object);
}

static void
polo_backend_workspace_class_init(PoloBackendWorkspaceClass *klass)
{
	G_OBJECT_CLASS(klass)->finalize = polo_backend_workspace_finalize;
	workspace_signals[WORKSPACE_SIGNAL_ACTIVATE] = g_signal_new(
		"activate", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
	workspace_signals[WORKSPACE_SIGNAL_REMOVE] = g_signal_new(
		"remove", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_backend_workspace_init(PoloBackendWorkspace *self)
{
	wl_list_init(&self->handles);
}

static void
polo_backend_workspace_manager_finalize(GObject *object)
{
	PoloBackendWorkspaceManager *self =
		POLO_BACKEND_WORKSPACE_MANAGER(object);

	g_ptr_array_unref(self->workspaces);
	G_OBJECT_CLASS(polo_backend_workspace_manager_parent_class)
		->finalize(object);
}

static void
polo_backend_workspace_manager_class_init(
	PoloBackendWorkspaceManagerClass *klass)
{
	G_OBJECT_CLASS(klass)->finalize =
		polo_backend_workspace_manager_finalize;
	manager_signals[MANAGER_SIGNAL_CREATE] = g_signal_new(
		"create-workspace", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
polo_backend_workspace_manager_init(PoloBackendWorkspaceManager *self)
{
	wl_list_init(&self->resources);
	self->workspaces = g_ptr_array_new_with_free_func(g_object_unref);
}
