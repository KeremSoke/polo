/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wp_security_context_manager_v1. Clients accepted on a committed context's
 * socket carry its metadata, and Trust keeps privileged globals from them. A
 * context outlives the client that made it, until its close_fd hangs up.
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "security-context-v1-server-protocol.h"

struct context {
	struct wl_resource *resource;
	PoloBackendServer *server;
	struct wl_list link;
	int listen_fd, close_fd;
	struct wl_event_source *listen_source, *close_source;
	char *engine, *app_id, *instance_id;
	gboolean committed;
};

static void
context_free(struct context *context)
{
	if (context->resource)
		wl_resource_set_user_data(context->resource, NULL);
	g_clear_pointer(&context->listen_source, wl_event_source_remove);
	g_clear_pointer(&context->close_source, wl_event_source_remove);
	if (context->listen_fd >= 0)
		close(context->listen_fd);
	if (context->close_fd >= 0)
		close(context->close_fd);
	wl_list_remove(&context->link);
	g_free(context->engine);
	g_free(context->app_id);
	g_free(context->instance_id);
	g_free(context);
}

static int
context_listen_ready(int fd, uint32_t mask, void *data)
{
	struct context *context = data;
	PoloBackendClient *client;
	struct wl_client *wl_client;
	int accepted;

	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		context_free(context);
		return 0;
	}
	accepted = accept4(fd, NULL, NULL, SOCK_CLOEXEC);
	if (accepted < 0)
		return 0;
	wl_client = wl_client_create(context->server->display, accepted);
	if (!wl_client) {
		close(accepted);
		return 0;
	}
	client = g_hash_table_lookup(context->server->clients, wl_client);
	if (client)
		polo_backend_client_set_sandbox(client, context->engine,
						context->app_id,
						context->instance_id);
	weston_log("sandboxed client: %s %s\n",
		   context->engine ? context->engine : "unknown",
		   context->app_id ? context->app_id : "unknown");
	return 0;
}

static int
context_close_ready(int fd, uint32_t mask, void *data)
{
	context_free(data);
	return 0;
}

static void
context_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static gboolean
check_settable(struct wl_resource *resource, struct context *context,
	       size_t field)
{
	if (!context || context->committed) {
		wl_resource_post_error(
			resource, WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_USED,
			"the security context was committed");
		return FALSE;
	}
	if (G_STRUCT_MEMBER(char *, context, field)) {
		wl_resource_post_error(resource,
				       WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_SET,
				       "this metadata is already set");
		return FALSE;
	}
	return TRUE;
}

static void
context_set_sandbox_engine(struct wl_client *client,
			   struct wl_resource *resource, const char *name)
{
	struct context *context = wl_resource_get_user_data(resource);

	if (check_settable(resource, context,
			   G_STRUCT_OFFSET(struct context, engine)))
		context->engine = g_strdup(name);
}

static void
context_set_app_id(struct wl_client *client, struct wl_resource *resource,
		   const char *app_id)
{
	struct context *context = wl_resource_get_user_data(resource);

	if (check_settable(resource, context,
			   G_STRUCT_OFFSET(struct context, app_id)))
		context->app_id = g_strdup(app_id);
}

static void
context_set_instance_id(struct wl_client *client, struct wl_resource *resource,
			const char *instance_id)
{
	struct context *context = wl_resource_get_user_data(resource);

	if (check_settable(resource, context,
			   G_STRUCT_OFFSET(struct context, instance_id)))
		context->instance_id = g_strdup(instance_id);
}

static void
context_commit(struct wl_client *client, struct wl_resource *resource)
{
	struct context *context = wl_resource_get_user_data(resource);

	if (!context || context->committed) {
		wl_resource_post_error(
			resource, WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_USED,
			"the security context was committed");
		return;
	}

	struct wl_event_loop *loop =
		wl_display_get_event_loop(context->server->display);

	context->committed = TRUE;
	context->listen_source = wl_event_loop_add_fd(
		loop, context->listen_fd, WL_EVENT_READABLE,
		context_listen_ready, context);
	context->close_source = wl_event_loop_add_fd(
		loop, context->close_fd, 0, context_close_ready, context);
	if (!context->listen_source || !context->close_source)
		wl_client_post_no_memory(client);
}

static const struct wp_security_context_v1_interface context_impl = {
	.destroy = context_destroy,
	.set_sandbox_engine = context_set_sandbox_engine,
	.set_app_id = context_set_app_id,
	.set_instance_id = context_set_instance_id,
	.commit = context_commit,
};

static void
context_resource_destroyed(struct wl_resource *resource)
{
	struct context *context = wl_resource_get_user_data(resource);

	if (!context)
		return;
	context->resource = NULL;
	if (!context->committed)
		context_free(context);
}

static void
manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
manager_create_listener(struct wl_client *client, struct wl_resource *resource,
			uint32_t id, int32_t listen_fd, int32_t close_fd)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	PoloBackendClient *creator =
		g_hash_table_lookup(server->clients, client);
	struct context *context;
	int listening = 0;
	socklen_t length = sizeof listening;

	if (creator && polo_backend_client_get_sandbox_engine(creator)) {
		close(listen_fd);
		close(close_fd);
		wl_resource_post_error(
			resource, WP_SECURITY_CONTEXT_MANAGER_V1_ERROR_NESTED,
			"nested security contexts are forbidden");
		return;
	}
	if (getsockopt(listen_fd, SOL_SOCKET, SO_ACCEPTCONN, &listening,
		       &length) < 0 ||
	    !listening) {
		close(listen_fd);
		close(close_fd);
		wl_resource_post_error(
			resource,
			WP_SECURITY_CONTEXT_MANAGER_V1_ERROR_INVALID_LISTEN_FD,
			"listen_fd is not a listening socket");
		return;
	}

	fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFL) | O_NONBLOCK);

	context = g_new0(struct context, 1);
	context->server = server;
	context->listen_fd = listen_fd;
	context->close_fd = close_fd;
	wl_list_insert(&server->security_contexts, &context->link);
	context->resource = wl_resource_create(
		client, &wp_security_context_v1_interface, 1, id);
	if (!context->resource) {
		context_free(context);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(context->resource, &context_impl,
				       context, context_resource_destroyed);
}

static const struct wp_security_context_manager_v1_interface manager_impl = {
	.destroy = manager_destroy,
	.create_listener = manager_create_listener,
};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &wp_security_context_manager_v1_interface, 1, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

gboolean
polo_backend_security_context_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	wl_list_init(&server->security_contexts);
	server->security_context_global = wl_global_create(
		server->display, &wp_security_context_manager_v1_interface, 1,
		server, manager_bind);
	return TRUE;
}

void
polo_backend_security_context_fini(PoloBackendServer *server)
{
	struct context *context, *next;

	wl_list_for_each_safe(context, next, &server->security_contexts, link)
		context_free(context);
	g_clear_pointer(&server->security_context_global, wl_global_destroy);
}
