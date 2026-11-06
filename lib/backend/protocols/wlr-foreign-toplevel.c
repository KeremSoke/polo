/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwlr_foreign_toplevel_manager_v1 (v3) shim.
 *
 * Protocol mechanics only: the global, one handle resource per manager
 * resource, replaying the full state to managers that bind later,
 * output_enter/output_leave per client wl_output resource, the parent
 * event, done batching, set_rectangle bookkeeping and the protocol error.
 * Which windows are listed, what their state is and what every request
 * does is decided in src/protocols/foreign-toplevel.vala, which creates
 * one PoloBackendForeignToplevel per window and calls its setters.
 *
 * libweston has no signal for new wl_output binds, so for every client
 * that binds the manager a resource-created listener catches its new
 * wl_output resources and sends their output_enter once the bind is done.
 */

#include <libweston/libweston.h>

#include "foreign-toplevel-private.h"
#include "wlr-foreign-toplevel-management-unstable-v1-server-protocol.h"

#define MANAGER_VERSION 3

/* A client that bound the manager at least once. */
struct ft_client {
	PoloBackendServer *server;
	struct wl_client *client;
	struct wl_listener resource_created;
	struct wl_listener destroyed;
	struct wl_list link;
	struct wl_event_source *idle; /* pending output_bound_idle */
};

enum {
	SIGNAL_REQUEST_MAXIMIZE,
	SIGNAL_REQUEST_MINIMIZE,
	SIGNAL_REQUEST_ACTIVATE,
	SIGNAL_REQUEST_CLOSE,
	SIGNAL_REQUEST_FULLSCREEN,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(PoloBackendForeignToplevel, polo_backend_foreign_toplevel,
		    G_TYPE_OBJECT)

static const struct zwlr_foreign_toplevel_handle_v1_interface
	handle_implementation;

static gboolean
handle_on_output(PoloBackendForeignToplevel *self, struct weston_output *output)
{
	guint i;

	for (i = 0; i < self->outputs->len; i++) {
		if (polo_backend_output_get_weston_output(
			    g_ptr_array_index(self->outputs, i)) == output)
			return TRUE;
	}
	return FALSE;
}

static gboolean
handle_resource_sync_outputs(struct ft_handle_resource *hr)
{
	PoloBackendForeignToplevel *self = hr->handle;
	struct wl_client *client = wl_resource_get_client(hr->resource);
	GPtrArray *entered = g_ptr_array_new();
	struct weston_head *head = NULL;
	gboolean changed = FALSE;
	guint i;

	while ((head = weston_compositor_iterate_heads(self->server->compositor,
						       head))) {
		struct wl_resource *output_resource;
		gboolean on = head->output && head->output->enabled &&
			handle_on_output(self, head->output);

		wl_resource_for_each(output_resource, &head->resource_list)
		{
			gboolean was;

			if (wl_resource_get_client(output_resource) != client)
				continue;
			was = g_ptr_array_find(hr->entered, output_resource,
					       NULL);
			if (on && !was) {
				zwlr_foreign_toplevel_handle_v1_send_output_enter(
					hr->resource, output_resource);
				changed = TRUE;
			} else if (!on && was) {
				zwlr_foreign_toplevel_handle_v1_send_output_leave(
					hr->resource, output_resource);
				changed = TRUE;
			}
			if (on)
				g_ptr_array_add(entered, output_resource);
		}
	}
	/* Entered resources that no longer exist simply drop out. */
	for (i = 0; i < hr->entered->len && !changed; i++)
		changed = !g_ptr_array_find(
			entered, g_ptr_array_index(hr->entered, i), NULL);
	g_ptr_array_unref(hr->entered);
	hr->entered = entered;
	return changed;
}

static void
handle_resource_send_state(struct ft_handle_resource *hr)
{
	PoloBackendForeignToplevel *self = hr->handle;
	struct wl_array states;
	uint32_t *s;

	wl_array_init(&states);
#define ADD_STATE(flag, value)                                                 \
	if (self->state & (flag)) {                                            \
		s = wl_array_add(&states, sizeof *s);                          \
		if (s)                                                         \
			*s = (value);                                          \
	}
	ADD_STATE(POLO_BACKEND_FOREIGN_TOPLEVEL_STATE_MAXIMIZED,
		  ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED)
	ADD_STATE(POLO_BACKEND_FOREIGN_TOPLEVEL_STATE_MINIMIZED,
		  ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED)
	ADD_STATE(POLO_BACKEND_FOREIGN_TOPLEVEL_STATE_ACTIVATED,
		  ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
	if (wl_resource_get_version(hr->resource) >=
	    ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN_SINCE_VERSION) {
		ADD_STATE(POLO_BACKEND_FOREIGN_TOPLEVEL_STATE_FULLSCREEN,
			  ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN)
	}
#undef ADD_STATE
	zwlr_foreign_toplevel_handle_v1_send_state(hr->resource, &states);
	wl_array_release(&states);
}

static struct wl_resource *
parent_resource_for(struct ft_handle_resource *hr)
{
	PoloBackendForeignToplevel *parent = hr->handle->parent;
	struct ft_handle_resource *phr;

	if (!parent)
		return NULL;
	wl_list_for_each(phr, &parent->resources, link)
	{
		if (wl_resource_get_client(phr->resource) ==
		    wl_resource_get_client(hr->resource))
			return phr->resource;
	}
	return NULL;
}

static void
handle_resource_send_parent(struct ft_handle_resource *hr)
{
	if (wl_resource_get_version(hr->resource) >=
	    ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_PARENT_SINCE_VERSION)
		zwlr_foreign_toplevel_handle_v1_send_parent(
			hr->resource, parent_resource_for(hr));
}

static void
handle_resource_send_all(struct ft_handle_resource *hr)
{
	PoloBackendForeignToplevel *self = hr->handle;

	if (self->title)
		zwlr_foreign_toplevel_handle_v1_send_title(hr->resource,
							   self->title);
	if (self->app_id)
		zwlr_foreign_toplevel_handle_v1_send_app_id(hr->resource,
							    self->app_id);
	handle_resource_sync_outputs(hr);
	handle_resource_send_state(hr);
	handle_resource_send_parent(hr);
	zwlr_foreign_toplevel_handle_v1_send_done(hr->resource);
}

static void
handle_resource_clear_rectangle(struct ft_handle_resource *hr)
{
	if (hr->rect_surface) {
		wl_list_remove(&hr->rect_surface_destroyed.link);
		hr->rect_surface = NULL;
	}
}

static void
handle_resource_destroyed(struct wl_resource *resource)
{
	struct ft_handle_resource *hr = wl_resource_get_user_data(resource);

	handle_resource_clear_rectangle(hr);
	polo_backend_xfce_handle_resource_destroyed(hr);
	if (hr->handle)
		wl_list_remove(&hr->link);
	if (hr->entered_workspaces)
		g_ptr_array_unref(hr->entered_workspaces);
	g_ptr_array_unref(hr->entered);
	free(hr);
}

static void
rect_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct ft_handle_resource *hr =
		wl_container_of(listener, hr, rect_surface_destroyed);

	(void)data;
	wl_list_remove(&hr->rect_surface_destroyed.link);
	hr->rect_surface = NULL;
}

static struct ft_handle_resource *
handle_resource_create(PoloBackendForeignToplevel *self,
		       struct wl_resource *manager)
{
	struct wl_client *client = wl_resource_get_client(manager);
	struct ft_handle_resource *hr = zalloc(sizeof *hr);

	if (!hr)
		return NULL;
	hr->resource = wl_resource_create(
		client, &zwlr_foreign_toplevel_handle_v1_interface,
		wl_resource_get_version(manager), 0);
	if (!hr->resource) {
		free(hr);
		wl_client_post_no_memory(client);
		return NULL;
	}
	wl_resource_set_implementation(hr->resource, &handle_implementation, hr,
				       handle_resource_destroyed);
	hr->handle = self;
	hr->entered = g_ptr_array_new();
	hr->entered_workspaces = NULL;
	wl_list_insert(self->resources.prev, &hr->link);
	zwlr_foreign_toplevel_manager_v1_send_toplevel(manager, hr->resource);
	return hr;
}

#define HANDLE_OR_RETURN(resource, var)                                        \
	struct ft_handle_resource *hr_ = wl_resource_get_user_data(resource);  \
	PoloBackendForeignToplevel *var = hr_->handle;                         \
	if (!var || var->closed)                                               \
		return;

static void
handle_set_maximized(struct wl_client *client, struct wl_resource *resource)
{
	HANDLE_OR_RETURN(resource, self);
	g_signal_emit(self, signals[SIGNAL_REQUEST_MAXIMIZE], 0, TRUE);
}

static void
handle_unset_maximized(struct wl_client *client, struct wl_resource *resource)
{
	HANDLE_OR_RETURN(resource, self);
	g_signal_emit(self, signals[SIGNAL_REQUEST_MAXIMIZE], 0, FALSE);
}

static void
handle_set_minimized(struct wl_client *client, struct wl_resource *resource)
{
	HANDLE_OR_RETURN(resource, self);
	g_signal_emit(self, signals[SIGNAL_REQUEST_MINIMIZE], 0, TRUE);
}

static void
handle_unset_minimized(struct wl_client *client, struct wl_resource *resource)
{
	HANDLE_OR_RETURN(resource, self);
	g_signal_emit(self, signals[SIGNAL_REQUEST_MINIMIZE], 0, FALSE);
}

static void
handle_activate(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *seat)
{
	HANDLE_OR_RETURN(resource, self);
	(void)seat; /* Polo has one seat; the policy picks it. */
	g_signal_emit(self, signals[SIGNAL_REQUEST_ACTIVATE], 0);
}

static void
handle_close(struct wl_client *client, struct wl_resource *resource)
{
	HANDLE_OR_RETURN(resource, self);
	g_signal_emit(self, signals[SIGNAL_REQUEST_CLOSE], 0);
}

static void
handle_set_rectangle(struct wl_client *client, struct wl_resource *resource,
		     struct wl_resource *surface_resource, int32_t x, int32_t y,
		     int32_t width, int32_t height)
{
	struct ft_handle_resource *hr = wl_resource_get_user_data(resource);

	if (width < 0 || height < 0) {
		wl_resource_post_error(
			resource,
			ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_ERROR_INVALID_RECTANGLE,
			"negative rectangle size %dx%d", width, height);
		return;
	}
	handle_resource_clear_rectangle(hr);
	if (width == 0 || height == 0)
		return;
	hr->rect_surface = wl_resource_get_user_data(surface_resource);
	hr->rect_surface_destroyed.notify = rect_surface_destroyed;
	wl_signal_add(&hr->rect_surface->destroy_signal,
		      &hr->rect_surface_destroyed);
	hr->rect_x = x;
	hr->rect_y = y;
	hr->rect_width = width;
	hr->rect_height = height;
}

static void
handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
handle_set_fullscreen(struct wl_client *client, struct wl_resource *resource,
		      struct wl_resource *output_resource)
{
	HANDLE_OR_RETURN(resource, self);
	g_signal_emit(self, signals[SIGNAL_REQUEST_FULLSCREEN], 0, TRUE,
		      output_resource
			      ? polo_backend_server_output_from_resource(
					self->server, output_resource)
			      : NULL);
}

static void
handle_unset_fullscreen(struct wl_client *client, struct wl_resource *resource)
{
	HANDLE_OR_RETURN(resource, self);
	g_signal_emit(self, signals[SIGNAL_REQUEST_FULLSCREEN], 0, FALSE, NULL);
}

static const struct zwlr_foreign_toplevel_handle_v1_interface
	handle_implementation = {
		.set_maximized = handle_set_maximized,
		.unset_maximized = handle_unset_maximized,
		.set_minimized = handle_set_minimized,
		.unset_minimized = handle_unset_minimized,
		.activate = handle_activate,
		.close = handle_close,
		.set_rectangle = handle_set_rectangle,
		.destroy = handle_destroy,
		.set_fullscreen = handle_set_fullscreen,
		.unset_fullscreen = handle_unset_fullscreen,
	};

static void
output_bound_idle(void *data)
{
	struct ft_client *ftc = data;
	PoloBackendForeignToplevel *self;

	ftc->idle = NULL;
	wl_list_for_each(self, &ftc->server->foreign_toplevels, link)
	{
		struct ft_handle_resource *hr;

		wl_list_for_each(hr, &self->resources, link)
		{
			if (wl_resource_get_client(hr->resource) ==
				    ftc->client &&
			    handle_resource_sync_outputs(hr))
				zwlr_foreign_toplevel_handle_v1_send_done(
					hr->resource);
		}
	}
}

static void
client_resource_created(struct wl_listener *listener, void *data)
{
	struct ft_client *ftc =
		wl_container_of(listener, ftc, resource_created);
	struct wl_resource *resource = data;
	PoloBackendForeignToplevel *self;

	if (strcmp(wl_resource_get_class(resource), wl_output_interface.name) !=
	    0)
		return;

	wl_list_for_each(self, &ftc->server->foreign_toplevels, link)
	{
		struct ft_handle_resource *hr;

		wl_list_for_each(hr, &self->resources, link)
			g_ptr_array_remove(hr->entered, resource);
	}

	if (!ftc->idle)
		ftc->idle = wl_event_loop_add_idle(
			wl_display_get_event_loop(ftc->server->display),
			output_bound_idle, ftc);
}

static void
client_destroyed(struct wl_listener *listener, void *data)
{
	struct ft_client *ftc = wl_container_of(listener, ftc, destroyed);

	(void)data;
	if (ftc->idle)
		wl_event_source_remove(ftc->idle);
	wl_list_remove(&ftc->resource_created.link);
	wl_list_remove(&ftc->destroyed.link);
	wl_list_remove(&ftc->link);
	free(ftc);
}

static struct ft_client *
ft_client_get(PoloBackendServer *server, struct wl_client *client)
{
	struct ft_client *ftc;

	wl_list_for_each(ftc, &server->foreign_toplevel_clients, link)
	{
		if (ftc->client == client)
			return ftc;
	}
	ftc = zalloc(sizeof *ftc);
	if (!ftc)
		return NULL;
	ftc->server = server;
	ftc->client = client;
	ftc->resource_created.notify = client_resource_created;
	wl_client_add_resource_created_listener(client, &ftc->resource_created);
	ftc->destroyed.notify = client_destroyed;
	wl_client_add_destroy_listener(client, &ftc->destroyed);
	wl_list_insert(&server->foreign_toplevel_clients, &ftc->link);
	return ftc;
}

static void
manager_stop(struct wl_client *client, struct wl_resource *resource)
{
	zwlr_foreign_toplevel_manager_v1_send_finished(resource);
	wl_resource_destroy(resource);
}

static const struct zwlr_foreign_toplevel_manager_v1_interface
	manager_implementation = {
		.stop = manager_stop,
	};

static void
manager_resource_destroyed(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	PoloBackendServer *server = data;
	PoloBackendForeignToplevel *self;
	struct wl_resource *resource;

	resource = wl_resource_create(
		client, &zwlr_foreign_toplevel_manager_v1_interface, version,
		id);
	if (!resource || !ft_client_get(server, client)) {
		if (resource)
			wl_resource_destroy(resource);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_implementation,
				       server, manager_resource_destroyed);
	wl_list_insert(&server->foreign_toplevel_managers,
		       wl_resource_get_link(resource));

	wl_list_for_each_reverse(self, &server->foreign_toplevels, link)
	{
		struct ft_handle_resource *hr;

		if (self->closed)
			continue;
		hr = handle_resource_create(self, resource);
		if (hr)
			handle_resource_send_all(hr);
	}
}

gboolean
polo_backend_foreign_toplevel_manager_init(PoloBackendServer *server,
					   GError **error)
{
	(void)error;
	server->foreign_toplevel_global = wl_global_create(
		server->display, &zwlr_foreign_toplevel_manager_v1_interface,
		MANAGER_VERSION, server, manager_bind);
	return TRUE;
}

void
polo_backend_foreign_toplevel_manager_fini(PoloBackendServer *server)
{
	if (server->foreign_toplevel_global) {
		wl_global_destroy(server->foreign_toplevel_global);
		server->foreign_toplevel_global = NULL;
	}
}

PoloBackendForeignToplevel *
polo_backend_foreign_toplevel_new(PoloBackendServer *server)
{
	PoloBackendForeignToplevel *self;
	struct wl_resource *manager;

	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), NULL);
	self = g_object_new(POLO_BACKEND_TYPE_FOREIGN_TOPLEVEL, NULL);
	self->server = server;
	wl_list_insert(&server->foreign_toplevels, &self->link);
	wl_resource_for_each(manager, &server->foreign_toplevel_managers)
		handle_resource_create(self, manager);
	polo_backend_ext_toplevel_created(self);
	return self;
}

void
polo_backend_foreign_toplevel_set_title(PoloBackendForeignToplevel *self,
					const char *title)
{
	struct ft_handle_resource *hr;

	g_return_if_fail(POLO_BACKEND_IS_FOREIGN_TOPLEVEL(self));
	if (self->closed || g_strcmp0(self->title, title) == 0)
		return;
	g_free(self->title);
	self->title = g_strdup(title);
	if (!title)
		return;
	polo_backend_ext_toplevel_title_changed(self);
	wl_list_for_each(hr, &self->resources, link)
		zwlr_foreign_toplevel_handle_v1_send_title(hr->resource, title);
}

void
polo_backend_foreign_toplevel_set_app_id(PoloBackendForeignToplevel *self,
					 const char *app_id)
{
	struct ft_handle_resource *hr;

	g_return_if_fail(POLO_BACKEND_IS_FOREIGN_TOPLEVEL(self));
	if (self->closed || g_strcmp0(self->app_id, app_id) == 0)
		return;
	g_free(self->app_id);
	self->app_id = g_strdup(app_id);
	if (!app_id)
		return;
	polo_backend_ext_toplevel_app_id_changed(self);
	wl_list_for_each(hr, &self->resources, link)
		zwlr_foreign_toplevel_handle_v1_send_app_id(hr->resource,
							    app_id);
}

void
polo_backend_foreign_toplevel_set_state(PoloBackendForeignToplevel *self,
					PoloBackendForeignToplevelState state)
{
	struct ft_handle_resource *hr;

	g_return_if_fail(POLO_BACKEND_IS_FOREIGN_TOPLEVEL(self));
	if (self->closed || self->state == state)
		return;
	self->state = state;
	wl_list_for_each(hr, &self->resources, link)
		handle_resource_send_state(hr);
}

void
polo_backend_foreign_toplevel_set_extra_state(
	PoloBackendForeignToplevel *self,
	PoloBackendForeignToplevelExtraState state)
{
	struct ft_handle_resource *hr;

	g_return_if_fail(POLO_BACKEND_IS_FOREIGN_TOPLEVEL(self));
	if (self->closed || self->extra_state == state)
		return;
	self->extra_state = state;
	wl_list_for_each(hr, &self->resources, link)
		polo_backend_xfce_send_state(hr);
}

void
polo_backend_foreign_toplevel_set_outputs(PoloBackendForeignToplevel *self,
					  PoloBackendOutput **outputs,
					  int n_outputs)
{
	struct ft_handle_resource *hr;
	int i;

	g_return_if_fail(POLO_BACKEND_IS_FOREIGN_TOPLEVEL(self));
	if (self->closed)
		return;
	g_ptr_array_set_size(self->outputs, 0);
	for (i = 0; i < n_outputs; i++)
		g_ptr_array_add(self->outputs, outputs[i]);
	wl_list_for_each(hr, &self->resources, link)
		handle_resource_sync_outputs(hr);
}

void
polo_backend_foreign_toplevel_set_parent(PoloBackendForeignToplevel *self,
					 PoloBackendForeignToplevel *parent)
{
	struct ft_handle_resource *hr;

	g_return_if_fail(POLO_BACKEND_IS_FOREIGN_TOPLEVEL(self));
	if (self->closed || self->parent == parent)
		return;
	self->parent = parent && !parent->closed ? parent : NULL;
	wl_list_for_each(hr, &self->resources, link)
		handle_resource_send_parent(hr);
}

void
polo_backend_foreign_toplevel_set_window(PoloBackendForeignToplevel *self,
					 PoloBackendToplevel *window)
{
	g_return_if_fail(POLO_BACKEND_IS_FOREIGN_TOPLEVEL(self));
	g_set_object(&self->window, window);
}

void
polo_backend_foreign_toplevel_done(PoloBackendForeignToplevel *self)
{
	struct ft_handle_resource *hr;

	g_return_if_fail(POLO_BACKEND_IS_FOREIGN_TOPLEVEL(self));
	if (self->closed)
		return;
	wl_list_for_each(hr, &self->resources, link)
		zwlr_foreign_toplevel_handle_v1_send_done(hr->resource);
	polo_backend_ext_toplevel_done(self);
}

void
polo_backend_foreign_toplevel_close(PoloBackendForeignToplevel *self)
{
	PoloBackendForeignToplevel *other;
	struct ft_handle_resource *hr, *tmp;

	g_return_if_fail(POLO_BACKEND_IS_FOREIGN_TOPLEVEL(self));
	if (self->closed)
		return;
	self->closed = TRUE;
	polo_backend_ext_toplevel_closed(self);

	wl_list_for_each(other, &self->server->foreign_toplevels, link)
	{
		if (other->parent == self) {
			polo_backend_foreign_toplevel_set_parent(other, NULL);
			polo_backend_foreign_toplevel_done(other);
		}
	}

	wl_list_for_each_safe(hr, tmp, &self->resources, link)
	{
		zwlr_foreign_toplevel_handle_v1_send_closed(hr->resource);
		wl_list_remove(&hr->link);
		hr->handle = NULL;
	}
	wl_list_init(&self->resources);
	wl_list_remove(&self->link);
	wl_list_init(&self->link);
	self->parent = NULL;
}

gboolean
polo_backend_foreign_toplevel_get_rectangle(PoloBackendForeignToplevel *self,
					    int *x, int *y, int *width,
					    int *height)
{
	struct ft_handle_resource *hr;

	g_return_val_if_fail(POLO_BACKEND_IS_FOREIGN_TOPLEVEL(self), FALSE);
	wl_list_for_each_reverse(hr, &self->resources, link)
	{
		struct weston_view *view;

		if (!hr->rect_surface)
			continue;
		wl_list_for_each(view, &hr->rect_surface->views, surface_link)
		{
			*x = (int)view->geometry.pos_offset.x + hr->rect_x;
			*y = (int)view->geometry.pos_offset.y + hr->rect_y;
			*width = hr->rect_width;
			*height = hr->rect_height;
			return TRUE;
		}
	}
	return FALSE;
}

static void
polo_backend_foreign_toplevel_dispose(GObject *object)
{
	polo_backend_foreign_toplevel_close(
		POLO_BACKEND_FOREIGN_TOPLEVEL(object));
	G_OBJECT_CLASS(polo_backend_foreign_toplevel_parent_class)
		->dispose(object);
}

static void
polo_backend_foreign_toplevel_finalize(GObject *object)
{
	PoloBackendForeignToplevel *self =
		POLO_BACKEND_FOREIGN_TOPLEVEL(object);

	g_free(self->title);
	g_free(self->app_id);
	g_clear_object(&self->window);
	g_ptr_array_unref(self->workspaces);
	g_ptr_array_unref(self->outputs);
	G_OBJECT_CLASS(polo_backend_foreign_toplevel_parent_class)
		->finalize(object);
}

static void
polo_backend_foreign_toplevel_class_init(PoloBackendForeignToplevelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->dispose = polo_backend_foreign_toplevel_dispose;
	object_class->finalize = polo_backend_foreign_toplevel_finalize;

	signals[SIGNAL_REQUEST_MAXIMIZE] = g_signal_new(
		"request-maximize", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals[SIGNAL_REQUEST_MINIMIZE] = g_signal_new(
		"request-minimize", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals[SIGNAL_REQUEST_ACTIVATE] = g_signal_new(
		"request-activate", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_REQUEST_CLOSE] = g_signal_new(
		"request-close", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_REQUEST_FULLSCREEN] = g_signal_new(
		"request-fullscreen", G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
		G_TYPE_BOOLEAN, POLO_BACKEND_TYPE_OUTPUT);
	g_signal_new("request-shade", G_TYPE_FROM_CLASS(klass),
		     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
		     G_TYPE_BOOLEAN);
	g_signal_new("request-sticky", G_TYPE_FROM_CLASS(klass),
		     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
		     G_TYPE_BOOLEAN);
	g_signal_new("request-above", G_TYPE_FROM_CLASS(klass),
		     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
		     G_TYPE_BOOLEAN);
	g_signal_new("request-below", G_TYPE_FROM_CLASS(klass),
		     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
		     G_TYPE_BOOLEAN);
	g_signal_new("request-move", G_TYPE_FROM_CLASS(klass),
		     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	g_signal_new("request-resize", G_TYPE_FROM_CLASS(klass),
		     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	g_signal_new("request-move-to-workspace", G_TYPE_FROM_CLASS(klass),
		     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
		     POLO_BACKEND_TYPE_WORKSPACE);
	g_signal_new("request-move-to-output", G_TYPE_FROM_CLASS(klass),
		     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
		     POLO_BACKEND_TYPE_OUTPUT);
}

static void
polo_backend_foreign_toplevel_init(PoloBackendForeignToplevel *self)
{
	wl_list_init(&self->resources);
	wl_list_init(&self->link);
	self->outputs = g_ptr_array_new();
	self->workspaces = g_ptr_array_new_with_free_func(g_object_unref);
}
