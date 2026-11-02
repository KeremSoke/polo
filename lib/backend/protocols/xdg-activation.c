/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * xdg_activation_v1 (v1) shim: token objects and the input check. Whether
 * a token is worth anything, and what activating with it does, is decided
 * in Vala (src/policy/activation.vala) through the server's handlers.
 */

#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "xdg-activation-v1-server-protocol.h"

#define ACTIVATION_VERSION 1

struct activation_token {
	struct wl_resource *resource;
	PoloBackendServer *server;
	gboolean committed;
	char *app_id;
	struct weston_surface *surface;
	struct wl_listener surface_destroy;
	struct weston_seat *seat;
	struct wl_listener seat_destroy;
	uint32_t serial;
};

static guint32
timespec_to_msec(const struct timespec *ts)
{
	return (guint32)(ts->tv_sec * 1000 + ts->tv_nsec / 1000000);
}

static gboolean
serial_not_older(uint32_t serial, uint32_t reference)
{
	return serial - reference < UINT32_MAX / 2;
}

static gboolean
surface_of_client(struct weston_surface *surface, struct wl_client *client)
{
	return surface && surface->resource &&
		wl_resource_get_client(surface->resource) == client;
}

static void
token_clear_surface(struct activation_token *token)
{
	if (token->surface) {
		wl_list_remove(&token->surface_destroy.link);
		token->surface = NULL;
	}
}

static void
token_clear_seat(struct activation_token *token)
{
	if (token->seat) {
		wl_list_remove(&token->seat_destroy.link);
		token->seat = NULL;
	}
}

static void
token_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct activation_token *token =
		wl_container_of(listener, token, surface_destroy);

	(void)data;
	wl_list_remove(&token->surface_destroy.link);
	token->surface = NULL;
}

static void
token_seat_destroyed(struct wl_listener *listener, void *data)
{
	struct activation_token *token =
		wl_container_of(listener, token, seat_destroy);

	(void)data;
	wl_list_remove(&token->seat_destroy.link);
	token->seat = NULL;
}

static void
token_destroy_resource(struct wl_resource *resource)
{
	struct activation_token *token = wl_resource_get_user_data(resource);

	token_clear_surface(token);
	token_clear_seat(token);
	g_free(token->app_id);
	g_free(token);
}

static gboolean
token_check_used(struct activation_token *token)
{
	if (token->committed) {
		wl_resource_post_error(
			token->resource,
			XDG_ACTIVATION_TOKEN_V1_ERROR_ALREADY_USED,
			"the activation token was already committed");
		return TRUE;
	}
	return FALSE;
}

static void
token_set_serial(struct wl_client *client, struct wl_resource *resource,
		 uint32_t serial, struct wl_resource *seat_resource)
{
	struct activation_token *token = wl_resource_get_user_data(resource);
	struct weston_seat *seat =
		seat_resource ? wl_resource_get_user_data(seat_resource) : NULL;

	(void)client;
	if (token_check_used(token))
		return;
	token_clear_seat(token);
	token->serial = serial;
	if (seat) {
		token->seat = seat;
		token->seat_destroy.notify = token_seat_destroyed;
		wl_signal_add(&seat->destroy_signal, &token->seat_destroy);
	}
}

static void
token_set_app_id(struct wl_client *client, struct wl_resource *resource,
		 const char *app_id)
{
	struct activation_token *token = wl_resource_get_user_data(resource);

	(void)client;
	if (token_check_used(token))
		return;
	g_free(token->app_id);
	token->app_id = g_strdup(app_id);
}

static void
token_set_surface(struct wl_client *client, struct wl_resource *resource,
		  struct wl_resource *surface_resource)
{
	struct activation_token *token = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);

	(void)client;
	if (token_check_used(token))
		return;
	token_clear_surface(token);
	if (surface) {
		token->surface = surface;
		token->surface_destroy.notify = token_surface_destroyed;
		wl_signal_add(&surface->destroy_signal,
			      &token->surface_destroy);
	}
}

/*
 * The serial must be the client's latest button or key press on the seat
 * (or newer), and that press must have gone to the client
 */
static void
check_input(struct activation_token *token, struct wl_client *client,
	    PoloBackendActivationRequest *request)
{
	struct weston_pointer *pointer;
	struct weston_keyboard *keyboard;

	if (!token->seat)
		return;

	keyboard = weston_seat_get_keyboard(token->seat);
	if (keyboard && keyboard->focus &&
	    surface_of_client(keyboard->focus, client))
		request->surface_focused = TRUE;

	pointer = weston_seat_get_pointer(token->seat);
	if (pointer && pointer->focus &&
	    surface_of_client(pointer->focus->surface, client) &&
	    serial_not_older(token->serial, pointer->grab_serial)) {
		request->input_valid = TRUE;
		request->input_time_msec =
			timespec_to_msec(&pointer->grab_time);
	}
	if (keyboard && request->surface_focused &&
	    serial_not_older(token->serial, keyboard->grab_serial)) {
		guint32 time = timespec_to_msec(&keyboard->grab_time);

		if (!request->input_valid || time > request->input_time_msec)
			request->input_time_msec = time;
		request->input_valid = TRUE;
	}
}

static void
token_commit(struct wl_client *client, struct wl_resource *resource)
{
	struct activation_token *token = wl_resource_get_user_data(resource);
	PoloBackendServer *server = token->server;
	PoloBackendActivationRequest request = { 0 };
	g_autofree char *name = NULL;

	if (token_check_used(token))
		return;
	token->committed = TRUE;

	request.app_id = token->app_id;
	if (token->surface) {
		struct weston_surface *main_surface =
			weston_surface_get_main_surface(token->surface);

		request.has_surface = TRUE;
		request.toplevel =
			polo_backend_toplevel_from_surface(main_surface);
	}
	check_input(token, client, &request);

	if (server->activation_mint)
		name = server->activation_mint(&request,
					       server->activation_mint_data);
	if (!name)
		name = g_uuid_string_random();
	xdg_activation_token_v1_send_done(resource, name);
}

static void
token_destroy(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	wl_resource_destroy(resource);
}

static const struct xdg_activation_token_v1_interface token_implementation = {
	.set_serial = token_set_serial,
	.set_app_id = token_set_app_id,
	.set_surface = token_set_surface,
	.commit = token_commit,
	.destroy = token_destroy,
};

static void
activation_destroy(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	wl_resource_destroy(resource);
}

static void
activation_get_token(struct wl_client *client, struct wl_resource *resource,
		     uint32_t id)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct activation_token *token = g_new0(struct activation_token, 1);

	token->server = server;
	token->resource =
		wl_resource_create(client, &xdg_activation_token_v1_interface,
				   wl_resource_get_version(resource), id);
	if (!token->resource) {
		g_free(token);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(token->resource, &token_implementation,
				       token, token_destroy_resource);
}

static void
activation_activate(struct wl_client *client, struct wl_resource *resource,
		    const char *token, struct wl_resource *surface_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	PoloBackendToplevel *toplevel;

	(void)client;
	if (!surface || !server->activation_activate)
		return;
	toplevel = polo_backend_toplevel_from_surface(
		weston_surface_get_main_surface(surface));
	if (!toplevel)
		return;
	server->activation_activate(token, toplevel,
				    server->activation_activate_data);
}

static const struct xdg_activation_v1_interface activation_implementation = {
	.destroy = activation_destroy,
	.get_activation_token = activation_get_token,
	.activate = activation_activate,
};

static void
activation_bind(struct wl_client *client, void *data, uint32_t version,
		uint32_t id)
{
	struct wl_resource *resource;

	resource = wl_resource_create(client, &xdg_activation_v1_interface,
				      version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &activation_implementation,
				       data, NULL);
}

gboolean
polo_backend_xdg_activation_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	server->activation_global =
		wl_global_create(server->display, &xdg_activation_v1_interface,
				 ACTIVATION_VERSION, server, activation_bind);
	return TRUE;
}

void
polo_backend_xdg_activation_fini(PoloBackendServer *server)
{
	if (server->activation_global) {
		wl_global_destroy(server->activation_global);
		server->activation_global = NULL;
	}
	polo_backend_server_set_activation_mint(server, NULL, NULL, NULL);
	polo_backend_server_set_activation_activate(server, NULL, NULL, NULL);
}

void
polo_backend_server_set_activation_mint(PoloBackendServer *server,
					PoloBackendActivationMint mint,
					gpointer user_data,
					GDestroyNotify destroy)
{
	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));

	if (server->activation_mint_destroy)
		server->activation_mint_destroy(server->activation_mint_data);
	server->activation_mint = mint;
	server->activation_mint_data = user_data;
	server->activation_mint_destroy = destroy;
}

void
polo_backend_server_set_activation_activate(
	PoloBackendServer *server, PoloBackendActivationActivate activate,
	gpointer user_data, GDestroyNotify destroy)
{
	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));

	if (server->activation_activate_destroy)
		server->activation_activate_destroy(
			server->activation_activate_data);
	server->activation_activate = activate;
	server->activation_activate_data = user_data;
	server->activation_activate_destroy = destroy;
}

guint32
polo_backend_server_get_time_msec(PoloBackendServer *server)
{
	struct timespec now;

	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), 0);

	weston_compositor_get_time(&now);
	return timespec_to_msec(&now);
}
