/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PoloBackendServer: the wl_display, the weston_compositor and its backend,
 * outputs, seats, layers and clients.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <libweston/libweston.h>
#include <libweston/backend-drm.h>
#include <libweston/backend-headless.h>
#include <libweston/backend-wayland.h>
#include <libweston/windowed-output-api.h>

#include "polo-backend-private.h"

/* The first argument is stringified as the quark name; keep it intact. */
/* clang-format off */
G_DEFINE_QUARK(polo-backend-error-quark, polo_backend_error)
/* clang-format on */

enum {
	SIGNAL_OUTPUT_ADDED,
	SIGNAL_OUTPUT_REMOVED,
	SIGNAL_OUTPUTS_CHANGED,
	SIGNAL_SEAT_ADDED,
	SIGNAL_INPUT_DEVICE_ADDED,
	SIGNAL_NEW_TOPLEVEL,
	SIGNAL_SYSTEM_BELL,
	SIGNAL_NEW_LAYER_SURFACE,
	SIGNAL_SHUTDOWN,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(PoloBackendServer, polo_backend_server, G_TYPE_OBJECT)

static const uint32_t layer_positions[POLO_BACKEND_N_LAYERS] = {
	[POLO_BACKEND_LAYER_BACKGROUND] = WESTON_LAYER_POSITION_BACKGROUND,
	[POLO_BACKEND_LAYER_BOTTOM] = WESTON_LAYER_POSITION_BOTTOM_UI,
	[POLO_BACKEND_LAYER_BELOW] = WESTON_LAYER_POSITION_NORMAL - 1,
	[POLO_BACKEND_LAYER_NORMAL] = WESTON_LAYER_POSITION_NORMAL,
	[POLO_BACKEND_LAYER_ABOVE] = WESTON_LAYER_POSITION_NORMAL + 1,
	[POLO_BACKEND_LAYER_DOCK] = WESTON_LAYER_POSITION_UI,
	[POLO_BACKEND_LAYER_FULLSCREEN] = WESTON_LAYER_POSITION_FULLSCREEN,
	[POLO_BACKEND_LAYER_OVERRIDE_REDIRECT] =
		WESTON_LAYER_POSITION_TOP_UI - 1,
	[POLO_BACKEND_LAYER_OVERLAY] = WESTON_LAYER_POSITION_TOP_UI,
	[POLO_BACKEND_LAYER_LOCK] = WESTON_LAYER_POSITION_LOCK,
};

static PoloLogScope *
log_scope(void)
{
	return polo_backend_log_scope("polo");
}

static PoloBackendOutput *
lookup_output(PoloBackendServer *server, struct weston_output *output)
{
	guint i;

	for (i = 0; i < server->outputs->len; i++) {
		PoloBackendOutput *handle = server->outputs->pdata[i];

		if (polo_backend_output_get_weston_output(handle) == output)
			return handle;
	}
	return NULL;
}

PoloBackendOutput *
polo_backend_server_lookup_output(PoloBackendServer *server,
				  struct weston_output *output)
{
	return lookup_output(server, output);
}

static int
configure_output(PoloBackendServer *server, struct weston_output *output)
{
	if (server->kind == POLO_BACKEND_KIND_DRM) {
		const struct weston_drm_output_api *api =
			weston_drm_output_get_api(server->compositor);

		if (!api ||
		    api->set_mode(output, WESTON_DRM_BACKEND_OUTPUT_PREFERRED,
				  NULL) < 0)
			return -1;
		api->set_gbm_format(output, NULL);
		api->set_seat(output, "");
		weston_output_set_scale(output, 1);
	} else {
		const struct weston_windowed_output_api *api =
			weston_windowed_output_get_api(
				server->compositor,
				server->kind == POLO_BACKEND_KIND_WAYLAND
					? WESTON_WINDOWED_OUTPUT_WAYLAND
					: WESTON_WINDOWED_OUTPUT_HEADLESS);

		/* No API: a fullscreen nested output. The parent compositor
		 * sizes it and the backend configures it entirely, as in
		 * Weston's frontend. */
		if (!api)
			return 0;

		weston_output_set_scale(output, server->output_scale);
		if (api->output_set_size(output, server->output_width,
					 server->output_height) < 0)
			return -1;
		weston_output_set_transform(output, WL_OUTPUT_TRANSFORM_NORMAL);
	}

	/* DRM outputs get their transform from the Vala layout (apply ()). */
	return 0;
}

static int
right_edge(PoloBackendServer *server)
{
	int edge = 0;
	guint i;

	for (i = 0; i < server->outputs->len; i++) {
		struct weston_output *output =
			polo_backend_output_get_weston_output(
				g_ptr_array_index(server->outputs, i));

		edge = MAX(edge, (int)output->pos.c.x + output->width);
	}
	return edge;
}

static void
head_enable(PoloBackendServer *server, struct weston_head *head)
{
	struct weston_output *output;
	PoloBackendOutput *handle;

	output = weston_compositor_create_output(server->compositor, head,
						 weston_head_get_name(head));
	if (!output) {
		polo_log_scope_warning(log_scope(), "cannot create output %s",
				       weston_head_get_name(head));
		return;
	}

	/* Nested outputs use the requested size and go to the right of the
	 * others (also after a reconnection); DRM layout is chosen by Vala. */
	weston_output_set_position(
		output,
		(struct weston_coord_global){
			.c = weston_coord(right_edge(server), 0) });

	if (configure_output(server, output) < 0 ||
	    (!server->manage_outputs && weston_output_enable(output) < 0)) {
		polo_log_scope_warning(log_scope(), "cannot enable output %s",
				       weston_head_get_name(head));
		weston_output_destroy(output);
		return;
	}

	handle = polo_backend_output_new(server, output);
	g_ptr_array_add(server->outputs, handle);
	polo_log_scope_info(log_scope(), "output %s discovered: %dx%d scale %d",
			    output->name, output->width, output->height,
			    output->current_scale);
	g_signal_emit(server, signals[SIGNAL_OUTPUT_ADDED], 0, handle);
}

void
polo_backend_server_remove_output(PoloBackendServer *server,
				  PoloBackendOutput *handle)
{
	struct weston_output *output =
		polo_backend_output_get_weston_output(handle);
	g_object_ref(handle);
	g_ptr_array_remove(server->outputs, handle);
	g_signal_emit(server, signals[SIGNAL_OUTPUT_REMOVED], 0, handle);
	polo_backend_output_destroy(handle);
	g_object_unref(handle);
	weston_output_destroy(output);
}

static void
head_disable(PoloBackendServer *server, struct weston_head *head)
{
	struct weston_output *output = weston_head_get_output(head);
	PoloBackendOutput *handle = lookup_output(server, output);
	if (handle)
		polo_backend_server_remove_output(server, handle);
	else
		weston_output_destroy(output);
}

static void
heads_changed(struct wl_listener *listener, void *data)
{
	PoloBackendServer *server =
		wl_container_of(listener, server, heads_changed_listener);
	struct weston_head *head = NULL;

	while ((head = weston_compositor_iterate_heads(server->compositor,
						       head))) {
		bool connected = weston_head_is_connected(head);

		if (connected && weston_head_get_output(head) &&
		    weston_head_is_device_changed(head))
			head_disable(server, head);
		if (connected && !weston_head_get_output(head) &&
		    !weston_head_is_non_desktop(head))
			head_enable(server, head);
		else if (!connected && weston_head_get_output(head))
			head_disable(server, head);
		weston_head_reset_device_changed(head);
	}
	g_signal_emit(server, signals[SIGNAL_OUTPUTS_CHANGED], 0);
}

PoloBackendSeat *
polo_backend_server_lookup_seat(PoloBackendServer *server,
				struct weston_seat *seat)
{
	guint i;

	for (i = 0; i < server->seats->len; i++) {
		PoloBackendSeat *handle = server->seats->pdata[i];

		if (polo_backend_seat_get_weston_seat(handle) == seat)
			return handle;
	}
	return NULL;
}

static void
add_seat(PoloBackendServer *server, struct weston_seat *seat)
{
	PoloBackendSeat *handle;

	if (polo_backend_server_lookup_seat(server, seat))
		return;

	handle = polo_backend_seat_new(server, seat);
	g_ptr_array_add(server->seats, handle);
	g_signal_emit(server, signals[SIGNAL_SEAT_ADDED], 0, handle);
}

static void
seat_created(struct wl_listener *listener, void *data)
{
	PoloBackendServer *server =
		wl_container_of(listener, server, seat_created_listener);

	add_seat(server, data);
}

PoloBackendClient *
polo_backend_server_get_client(PoloBackendServer *server,
			       struct wl_client *wl_client)
{
	PoloBackendClient *client;

	client = g_hash_table_lookup(server->clients, wl_client);
	if (!client) {
		client = polo_backend_client_new(server, wl_client);
		g_hash_table_insert(server->clients, wl_client, client);
	}
	return client;
}

static void
client_created(struct wl_listener *listener, void *data)
{
	PoloBackendServer *server =
		wl_container_of(listener, server, client_created_listener);

	polo_backend_server_get_client(server, data);
}

static bool
global_filter(const struct wl_client *wl_client, const struct wl_global *global,
	      void *data)
{
	PoloBackendServer *server = data;
	PoloBackendClient *client;

	if (!server->global_filter)
		return true;

	client = g_hash_table_lookup(server->clients, wl_client);
	if (!client)
		return true;

	return server->global_filter(client,
				     wl_global_get_interface(global)->name,
				     server->global_filter_data);
}

void
polo_backend_server_set_global_filter(PoloBackendServer *server,
				      PoloBackendGlobalFilter filter,
				      gpointer user_data,
				      GDestroyNotify destroy)
{
	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));

	if (server->global_filter_destroy)
		server->global_filter_destroy(server->global_filter_data);
	server->global_filter = filter;
	server->global_filter_data = user_data;
	server->global_filter_destroy = destroy;
}

int
polo_backend_server_create_client_socket(PoloBackendServer *server,
					 PoloBackendClient **client,
					 GError **error)
{
	struct wl_client *wl_client;
	int fds[2];

	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), -1);
	g_return_val_if_fail(server->display != NULL, -1);

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) < 0) {
		int err = errno;

		g_set_error(error, POLO_BACKEND_ERROR,
			    POLO_BACKEND_ERROR_SOCKET, "socketpair: %s",
			    g_strerror(err));
		return -1;
	}

	wl_client = wl_client_create(server->display, fds[0]);
	if (!wl_client) {
		close(fds[0]);
		close(fds[1]);
		g_set_error(error, POLO_BACKEND_ERROR,
			    POLO_BACKEND_ERROR_SOCKET,
			    "cannot create Wayland client");
		return -1;
	}

	*client = polo_backend_server_get_client(server, wl_client);
	return fds[1];
}

PoloBackendView *
polo_backend_server_lookup_view(PoloBackendServer *server,
				struct weston_view *view)
{
	return g_hash_table_lookup(server->views, view);
}

static void
compositor_exit(struct weston_compositor *compositor)
{
	PoloBackendServer *server = weston_compositor_get_user_data(compositor);

	g_signal_emit(server, signals[SIGNAL_SHUTDOWN], 0);
}

static enum weston_renderer_type
renderer_type(PoloBackendRenderer renderer)
{
	switch (renderer) {
	case POLO_BACKEND_RENDERER_GL:
		return WESTON_RENDERER_GL;
	case POLO_BACKEND_RENDERER_PIXMAN:
		return WESTON_RENDERER_PIXMAN;
	default:
		return WESTON_RENDERER_AUTO;
	}
}

static gboolean
load_backend(PoloBackendServer *server, const PoloBackendConfig *config,
	     GError **error)
{
	const struct weston_windowed_output_api *api;
	enum weston_windowed_output_type windowed;
	const char *prefix;
	int i;

	switch (config->kind) {
	case POLO_BACKEND_KIND_DRM: {
		server->backend = polo_backend_drm_load(server, config);
		break;
	}
	case POLO_BACKEND_KIND_WAYLAND: {
		struct weston_wayland_backend_config wl = { 0 };

		wl.base.struct_version = WESTON_WAYLAND_BACKEND_CONFIG_VERSION;
		wl.base.struct_size = sizeof(wl);
		wl.renderer = renderer_type(config->renderer);
		wl.fullscreen = config->fullscreen;
		wl.cursor_size = server->cursor_size;
		wl.cursor_theme = server->cursor_theme;
		server->backend = weston_compositor_load_backend(
			server->compositor, WESTON_BACKEND_WAYLAND, &wl.base);
		break;
	}
	case POLO_BACKEND_KIND_HEADLESS: {
		struct weston_headless_backend_config hl = { 0 };

		hl.base.struct_version = WESTON_HEADLESS_BACKEND_CONFIG_VERSION;
		hl.base.struct_size = sizeof(hl);
		hl.renderer = renderer_type(config->renderer);
		hl.refresh = -1;
		/* No input devices exist headless; tests drive this seat. */
		hl.fake_seat = true;
		server->backend = weston_compositor_load_backend(
			server->compositor, WESTON_BACKEND_HEADLESS, &hl.base);
		break;
	}
	}

	if (!server->backend) {
		g_set_error(error, POLO_BACKEND_ERROR,
			    POLO_BACKEND_ERROR_BACKEND,
			    "cannot load the libweston backend");
		return FALSE;
	}

	if (config->kind == POLO_BACKEND_KIND_DRM)
		return TRUE;

	if (config->kind == POLO_BACKEND_KIND_WAYLAND) {
		windowed = WESTON_WINDOWED_OUTPUT_WAYLAND;
		prefix = "wayland";
	} else {
		windowed = WESTON_WINDOWED_OUTPUT_HEADLESS;
		prefix = "headless";
	}

	api = weston_windowed_output_get_api(server->compositor, windowed);
	if (!api && config->kind == POLO_BACKEND_KIND_WAYLAND &&
	    config->fullscreen) {
		/* A fullscreen nested backend creates its own output, sized
		 * by the parent compositor, and offers no windowed API. */
		return TRUE;
	}
	if (!api) {
		g_set_error(
			error, POLO_BACKEND_ERROR, POLO_BACKEND_ERROR_BACKEND,
			"the %s backend has no windowed output API", prefix);
		return FALSE;
	}

	for (i = 0; i < MAX(config->output_count, 1); i++) {
		g_autofree char *name = g_strdup_printf("%s%d", prefix, i);

		if (api->create_head(server->backend, name) < 0) {
			g_set_error(error, POLO_BACKEND_ERROR,
				    POLO_BACKEND_ERROR_BACKEND,
				    "cannot create output %s", name);
			return FALSE;
		}
	}

	return TRUE;
}

static char *
dup_or_null(const char *s)
{
	return s && *s ? strdup(s) : NULL;
}

gboolean
polo_backend_server_start(PoloBackendServer *server,
			  const PoloBackendConfig *config, GError **error)
{
	struct xkb_rule_names names = { 0 };
	struct weston_seat *seat;
	const char *socket;
	int i;

	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), FALSE);
	g_return_val_if_fail(!server->started, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	server->started = TRUE;
	/* polo_backend_server_stop() unhooks these even when start-up
	 * failed before adding them. */
	wl_list_init(&server->seat_created_listener.link);
	wl_list_init(&server->heads_changed_listener.link);
	wl_list_init(&server->client_created_listener.link);
	server->kind = config->kind;
	server->manage_outputs =
		config->kind == POLO_BACKEND_KIND_DRM || config->manage_outputs;
	server->cursor_theme = g_strdup(config->cursor_theme);
	server->cursor_size = config->cursor_size > 0
		? CLAMP(config->cursor_size, 8, 128)
		: 24;
	server->output_width = config->width > 0 ? config->width : 1024;
	server->output_height = config->height > 0 ? config->height : 768;
	server->output_scale = config->scale > 0 ? config->scale : 1;

	server->display = wl_display_create();
	server->compositor = weston_compositor_create(
		server->display, polo_backend_log_get_context(), server, NULL);
	if (!server->compositor) {
		g_set_error(error, POLO_BACKEND_ERROR,
			    POLO_BACKEND_ERROR_FAILED,
			    "cannot create the compositor");
		return FALSE;
	}
	server->compositor->exit = compositor_exit;
	polo_backend_drm_install_bindings(server, config->debug_kill);

	for (i = 0; i < POLO_BACKEND_N_LAYERS; i++) {
		server->layers[i].kind = i;
		weston_layer_init(&server->layers[i].layer, server->compositor);
		weston_layer_set_position(&server->layers[i].layer,
					  layer_positions[i]);
	}

	/* libweston takes ownership of the strings. */
	names.model = dup_or_null(config->xkb_model);
	names.layout = dup_or_null(config->xkb_layout);
	names.variant = dup_or_null(config->xkb_variant);
	names.options = dup_or_null(config->xkb_options);
	if (weston_compositor_set_xkb_rule_names(server->compositor, &names) <
	    0) {
		g_set_error(error, POLO_BACKEND_ERROR,
			    POLO_BACKEND_ERROR_FAILED,
			    "cannot set up the XKB keymap");
		return FALSE;
	}

	server->seat_created_listener.notify = seat_created;
	wl_signal_add(&server->compositor->seat_created_signal,
		      &server->seat_created_listener);
	server->heads_changed_listener.notify = heads_changed;
	weston_compositor_add_heads_changed_listener(
		server->compositor, &server->heads_changed_listener);

	if (!polo_backend_desktop_init(server, error))
		return FALSE;

	if (!polo_backend_layer_shell_init(server, error))
		return FALSE;

	if (!polo_backend_foreign_toplevel_manager_init(server, error))
		return FALSE;

	if (!polo_backend_ext_toplevel_list_init(server, error))
		return FALSE;

	if (!polo_backend_xfce_private_init(server, error))
		return FALSE;

	if (!polo_backend_xdg_activation_init(server, error))
		return FALSE;

	if (!polo_backend_system_bell_init(server, error))
		return FALSE;

	if (!polo_backend_security_context_init(server, error))
		return FALSE;

	if (!polo_backend_toplevel_icon_init(server, error))
		return FALSE;

	if (!polo_backend_xdg_foreign_init(server, error))
		return FALSE;

	if (!polo_backend_xdg_dialog_init(server, error))
		return FALSE;

	if (!polo_backend_pointer_gestures_init(server, error))
		return FALSE;

	if (!polo_backend_cursor_shape_init(server, error))
		return FALSE;

	if (!polo_backend_shortcuts_inhibit_init(server, error))
		return FALSE;

	if (!polo_backend_idle_inhibit_init(server, error))
		return FALSE;

	if (!polo_backend_text_input_init(server, error))
		return FALSE;

	if (!polo_backend_virtual_keyboard_init(server, error))
		return FALSE;

	if (!polo_backend_primary_selection_init(server, error))
		return FALSE;

	if (!polo_backend_xdg_decoration_init(server, error))
		return FALSE;

	if (!polo_backend_kde_decoration_init(server, error))
		return FALSE;

	if (!polo_backend_xwayland_keyboard_grab_init(server, error))
		return FALSE;

	if (!polo_backend_shell_ui_global_init(server, error))
		return FALSE;

	if (!polo_backend_workspace_manager_init_global(server, error))
		return FALSE;

	/* Before outputs appear: the first layout already draws wallpapers
	 * into pixel buffers. */
	polo_backend_internal_client_init(server);

	if (!load_backend(server, config, error))
		return FALSE;

	/* Picks the presentation clock and installs the (no-op) colour
	 * manager; outputs cannot be created before this. */
	if (weston_compositor_backends_loaded(server->compositor) < 0) {
		g_set_error(error, POLO_BACKEND_ERROR,
			    POLO_BACKEND_ERROR_BACKEND,
			    "the libweston backend failed to initialise");
		return FALSE;
	}

	/* Seats the backend created before our listener existed. */
	wl_list_for_each(seat, &server->compositor->seat_list, link)
		add_seat(server, seat);

	weston_compositor_flush_heads_changed(server->compositor);

	server->client_created_listener.notify = client_created;
	wl_display_add_client_created_listener(
		server->display, &server->client_created_listener);
	wl_display_set_global_filter(server->display, global_filter, server);

	if (config->socket_name && *config->socket_name) {
		if (wl_display_add_socket(server->display,
					  config->socket_name) < 0) {
			g_set_error(error, POLO_BACKEND_ERROR,
				    POLO_BACKEND_ERROR_SOCKET,
				    "cannot create socket %s",
				    config->socket_name);
			return FALSE;
		}
		socket = config->socket_name;
	} else {
		socket = wl_display_add_socket_auto(server->display);
		if (!socket) {
			g_set_error(error, POLO_BACKEND_ERROR,
				    POLO_BACKEND_ERROR_SOCKET,
				    "cannot create a Wayland socket");
			return FALSE;
		}
	}
	server->socket_name = g_strdup(socket);

	if (config->test_mode &&
	    !polo_backend_test_protocol_init(server, error))
		return FALSE;

	if (weston_compositor_enable_drm_syncobj(server->compositor))
		polo_log_scope_info(
			log_scope(),
			"explicit sync: wp_linux_drm_syncobj is on");
	polo_backend_cursor_init(server);
	weston_compositor_wake(server->compositor);
	polo_log_scope_info(log_scope(), "listening on %s",
			    server->socket_name);
	return TRUE;
}

void
polo_backend_server_attach(PoloBackendServer *server, GMainContext *context)
{
	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	g_return_if_fail(server->display != NULL);
	g_return_if_fail(server->source == NULL);

	server->source = polo_backend_loop_source_new(server->display);
	g_source_attach(server->source, context);
}

static void
destroy_clients(PoloBackendServer *server)
{
	GHashTableIter iter;
	gpointer value;

	/* Normally empty: each client's destroy listener removes it. */
	g_hash_table_iter_init(&iter, server->clients);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		g_object_ref(value);
		g_hash_table_iter_remove(&iter);
		polo_backend_client_destroy(value);
		g_object_unref(value);
	}
}

void
polo_backend_server_stop(PoloBackendServer *server)
{
	int i;

	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));

	if (!server->started || server->stopped)
		return;
	server->stopped = TRUE;
	polo_backend_cursor_fini(server);
	polo_backend_internal_client_fini(server);

	if (server->source) {
		g_source_destroy(server->source);
		g_clear_pointer(&server->source, g_source_unref);
	}

	if (server->display) {
		/* Toplevels emit "destroyed" as their clients go. */
		polo_backend_security_context_fini(server);
		wl_display_destroy_clients(server->display);
		weston_compositor_disable_drm_syncobj(server->compositor);
		destroy_clients(server);
	}

	if (server->compositor) {
		polo_backend_desktop_fini(server);

		while (server->outputs->len > 0) {
			PoloBackendOutput *output = server->outputs->pdata[0];

			g_object_ref(output);
			g_ptr_array_remove_index(server->outputs, 0);
			g_signal_emit(server, signals[SIGNAL_OUTPUT_REMOVED], 0,
				      output);
			polo_backend_output_destroy(output);
			g_object_unref(output);
		}
		while (server->seats->len > 0) {
			polo_backend_seat_destroy(server->seats->pdata[0]);
			g_ptr_array_remove_index(server->seats, 0);
		}

		if (server->test_global)
			wl_global_destroy(server->test_global);
		server->test_global = NULL;

		wl_list_remove(&server->seat_created_listener.link);
		wl_list_remove(&server->heads_changed_listener.link);
		for (i = 0; i < POLO_BACKEND_N_LAYERS; i++)
			weston_layer_fini(&server->layers[i].layer);

		weston_compositor_destroy(server->compositor);
		server->compositor = NULL;
	}

	if (server->display) {
		wl_list_remove(&server->client_created_listener.link);
		polo_backend_layer_shell_fini(server);
		polo_backend_foreign_toplevel_manager_fini(server);
		polo_backend_ext_toplevel_list_fini(server);
		polo_backend_xfce_private_fini(server);
		polo_backend_xdg_activation_fini(server);
		polo_backend_xwayland_keyboard_grab_fini(server);
		polo_backend_xdg_decoration_fini(server);
		polo_backend_primary_selection_fini(server);
		polo_backend_text_input_fini(server);
		polo_backend_idle_inhibit_fini(server);
		polo_backend_shortcuts_inhibit_fini(server);
		polo_backend_cursor_shape_fini(server);
		polo_backend_pointer_gestures_fini(server);
		polo_backend_xdg_foreign_fini(server);
		polo_backend_toplevel_icon_fini(server);
		polo_backend_system_bell_fini(server);
		polo_backend_xdg_dialog_fini(server);
		polo_backend_virtual_keyboard_fini(server);
		polo_backend_kde_decoration_fini(server);
		polo_backend_shell_ui_global_fini(server);
		polo_backend_workspace_manager_fini_global(server);
		wl_display_destroy(server->display);
		server->display = NULL;
	}
}

const char *
polo_backend_server_get_socket_name(PoloBackendServer *server)
{
	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), NULL);
	return server->socket_name;
}

PoloBackendLayer *
polo_backend_server_get_layer(PoloBackendServer *server,
			      PoloBackendLayerKind kind)
{
	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), NULL);
	g_return_val_if_fail(kind < POLO_BACKEND_N_LAYERS, NULL);
	return &server->layers[kind];
}

static void
send_repeat_info(struct wl_list *resources, int rate, int delay)
{
	struct wl_resource *resource;

	wl_resource_for_each(resource, resources)
	{
		if (wl_resource_get_version(resource) >=
		    WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
			wl_keyboard_send_repeat_info(resource, rate, delay);
	}
}

void
polo_backend_server_set_keyboard_repeat(PoloBackendServer *server, int rate,
					int delay)
{
	struct weston_seat *seat;

	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));

	server->compositor->kb_repeat_rate = rate;
	server->compositor->kb_repeat_delay = delay;
	wl_list_for_each(seat, &server->compositor->seat_list, link)
	{
		struct weston_keyboard *keyboard =
			weston_seat_get_keyboard(seat);

		if (!keyboard)
			continue;
		send_repeat_info(&keyboard->resource_list, rate, delay);
		send_repeat_info(&keyboard->focus_resource_list, rate, delay);
	}
}

void
polo_backend_server_set_test_inspector(PoloBackendServer *server,
				       PoloBackendTestInspector inspector,
				       gpointer user_data,
				       GDestroyNotify destroy)
{
	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));

	if (server->test_inspector_destroy)
		server->test_inspector_destroy(server->test_inspector_data);
	server->test_inspector = inspector;
	server->test_inspector_data = user_data;
	server->test_inspector_destroy = destroy;
}

void
polo_backend_server_set_test_settings(PoloBackendServer *server,
				      PoloBackendTestSettings settings,
				      gpointer user_data,
				      GDestroyNotify destroy)
{
	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));

	if (server->test_settings_destroy)
		server->test_settings_destroy(server->test_settings_data);
	server->test_settings = settings;
	server->test_settings_data = user_data;
	server->test_settings_destroy = destroy;
}

void
polo_backend_layer_restack(PoloBackendLayer *layer, PoloBackendView **views,
			   int n_views)
{
	int i;

	g_return_if_fail(layer != NULL);

	/* Each move inserts at the top, so bottom-to-top order results. A
	 * view already in a visible layer is not unmapped by the move, so
	 * focus is unaffected. */
	for (i = 0; i < n_views; i++)
		polo_backend_view_move_to_layer(views[i], layer);
}

static void
polo_backend_server_dispose(GObject *object)
{
	PoloBackendServer *server = POLO_BACKEND_SERVER(object);

	polo_backend_server_stop(server);
	polo_backend_server_set_global_filter(server, NULL, NULL, NULL);
	polo_backend_server_set_test_inspector(server, NULL, NULL, NULL);

	G_OBJECT_CLASS(polo_backend_server_parent_class)->dispose(object);
}

static void
polo_backend_server_finalize(GObject *object)
{
	PoloBackendServer *server = POLO_BACKEND_SERVER(object);

	g_ptr_array_unref(server->outputs);
	g_ptr_array_unref(server->seats);
	g_hash_table_unref(server->input_devices);
	g_hash_table_unref(server->clients);
	g_hash_table_unref(server->views);
	g_free(server->socket_name);
	g_free(server->cursor_theme);

	G_OBJECT_CLASS(polo_backend_server_parent_class)->finalize(object);
}

static void
polo_backend_server_class_init(PoloBackendServerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->dispose = polo_backend_server_dispose;
	object_class->finalize = polo_backend_server_finalize;

	signals[SIGNAL_OUTPUTS_CHANGED] = g_signal_new(
		"outputs-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_OUTPUT_ADDED] = g_signal_new(
		"output-added", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 1, POLO_BACKEND_TYPE_OUTPUT);
	signals[SIGNAL_OUTPUT_REMOVED] = g_signal_new(
		"output-removed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 1, POLO_BACKEND_TYPE_OUTPUT);
	signals[SIGNAL_SEAT_ADDED] = g_signal_new(
		"seat-added", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 1, POLO_BACKEND_TYPE_SEAT);
	signals[SIGNAL_INPUT_DEVICE_ADDED] =
		g_signal_new("input-device-added", G_TYPE_FROM_CLASS(klass),
			     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			     G_TYPE_NONE, 1, POLO_BACKEND_TYPE_INPUT_DEVICE);
	signals[SIGNAL_NEW_TOPLEVEL] = g_signal_new(
		"new-toplevel", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 1, POLO_BACKEND_TYPE_TOPLEVEL);
	signals[SIGNAL_SYSTEM_BELL] = g_signal_new(
		"system-bell", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 1, POLO_BACKEND_TYPE_TOPLEVEL);
	signals[SIGNAL_NEW_LAYER_SURFACE] =
		g_signal_new("new-layer-surface", G_TYPE_FROM_CLASS(klass),
			     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			     G_TYPE_NONE, 1, POLO_BACKEND_TYPE_LAYER_SURFACE);
	signals[SIGNAL_SHUTDOWN] = g_signal_new(
		"shutdown", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_backend_server_init(PoloBackendServer *server)
{
	server->outputs = g_ptr_array_new_with_free_func(g_object_unref);
	server->seats = g_ptr_array_new_with_free_func(g_object_unref);
	server->input_devices = g_hash_table_new_full(
		g_direct_hash, g_direct_equal, NULL, g_object_unref);
	server->clients = g_hash_table_new_full(g_direct_hash, g_direct_equal,
						NULL, g_object_unref);
	server->views = g_hash_table_new(g_direct_hash, g_direct_equal);
	wl_list_init(&server->test_resources);
	wl_list_init(&server->foreign_toplevels);
	wl_list_init(&server->foreign_toplevel_managers);
	wl_list_init(&server->foreign_toplevel_clients);
	wl_list_init(&server->ext_toplevel_lists);
	wl_list_init(&server->xfce_outputs);
	wl_list_init(&server->xfce_seats);
}

PoloBackendServer *
polo_backend_server_new(void)
{
	return g_object_new(POLO_BACKEND_TYPE_SERVER, NULL);
}

void
polo_backend_server_input_device_added(PoloBackendServer *server,
				       struct libinput_device *device)
{
	PoloBackendInputDevice *handle = polo_backend_input_device_new(device);

	g_hash_table_replace(server->input_devices, device, handle);
	g_signal_emit(server, signals[SIGNAL_INPUT_DEVICE_ADDED], 0, handle);
}

void
polo_backend_server_input_device_removed(PoloBackendServer *server,
					 struct libinput_device *device)
{
	PoloBackendInputDevice *handle =
		g_hash_table_lookup(server->input_devices, device);

	if (!handle)
		return;
	g_object_ref(handle);
	g_hash_table_remove(server->input_devices, device);
	polo_backend_input_device_remove(handle);
	g_object_unref(handle);
}

void
polo_backend_server_emit_system_bell(PoloBackendServer *server,
				     PoloBackendToplevel *toplevel)
{
	g_signal_emit(server, signals[SIGNAL_SYSTEM_BELL], 0, toplevel);
}

void
polo_backend_server_emit_new_toplevel(PoloBackendServer *server,
				      PoloBackendToplevel *toplevel)
{
	g_signal_emit(server, signals[SIGNAL_NEW_TOPLEVEL], 0, toplevel);
}

void
polo_backend_server_emit_new_layer_surface(PoloBackendServer *server,
					   PoloBackendLayerSurface *surface)
{
	g_signal_emit(server, signals[SIGNAL_NEW_LAYER_SURFACE], 0, surface);
}

/* The Polo output handle for a client's wl_output resource, or NULL (an
 * output that is gone or not enabled). */
PoloBackendOutput *
polo_backend_server_output_from_resource(PoloBackendServer *server,
					 struct wl_resource *resource)
{
	struct weston_head *head = weston_head_from_resource(resource);
	guint i;

	if (!head || !head->output)
		return NULL;
	for (i = 0; i < server->outputs->len; i++) {
		PoloBackendOutput *output =
			g_ptr_array_index(server->outputs, i);

		if (polo_backend_output_get_weston_output(output) ==
		    head->output)
			return output;
	}
	return NULL;
}
