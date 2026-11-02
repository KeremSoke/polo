/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * polo_test_v1: integration tests inspect the Vala window model
 * and inject input. Built only with -Dtest-protocol=true; input injection
 * uses libweston's private notify_* API (libweston/backend.h), and this is
 * the only file that includes it.
 */

#include "polo-backend-private.h"

#ifdef POLO_HAVE_TEST_PROTOCOL

#include <wayland-server-core.h>
#include <libweston/libweston.h>

#include "backend.h"
#include "polo-test-v1-server-protocol.h"

static struct weston_seat *
first_seat(PoloBackendServer *server)
{
	struct weston_seat *seat;

	wl_list_for_each(seat, &server->compositor->seat_list,
			 link) return seat;
	return NULL;
}

static void
test_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
test_get_window_info(struct wl_client *client, struct wl_resource *resource,
		     struct wl_resource *surface_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	PoloBackendToplevel *toplevel;
	PoloBackendTestWindowInfo info = { .workspace = -1 };

	toplevel = polo_backend_toplevel_from_surface(surface);
	if (!toplevel || !server->test_inspector ||
	    !server->test_inspector(toplevel, &info,
				    server->test_inspector_data)) {
		polo_test_v1_send_window_unknown(resource, surface_resource);
		return;
	}

	polo_test_v1_send_window_info(
		resource, surface_resource, info.x, info.y, info.width,
		info.height, info.layer, info.stack_index, info.workspace,
		info.state, info.focused ? 1 : 0);
}

static void
test_get_layer_surface_info(struct wl_client *client,
			    struct wl_resource *resource,
			    struct wl_resource *surface_resource)
{
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	PoloBackendLayerSurface *layer_surface =
		polo_backend_layer_surface_from_surface(surface);
	PoloBackendView *view;
	double x, y;
	int width, height;

	if (!layer_surface) {
		polo_test_v1_send_layer_surface_unknown(resource,
							surface_resource);
		return;
	}

	view = polo_backend_layer_surface_get_view(layer_surface);
	polo_backend_view_get_position(view, &x, &y);
	polo_backend_layer_surface_get_size(layer_surface, &width, &height);
	polo_test_v1_send_layer_surface_info(
		resource, surface_resource, (int32_t)x, (int32_t)y, width,
		height,
		(int32_t)polo_backend_layer_surface_get_layer(layer_surface),
		polo_backend_layer_surface_is_mapped(layer_surface) ? 1 : 0);
}

static void
test_move_pointer(struct wl_client *client, struct wl_resource *resource,
		  int32_t x, int32_t y)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_seat *seat = first_seat(server);
	struct weston_pointer *pointer =
		seat ? weston_seat_get_pointer(seat) : NULL;
	struct weston_pointer_motion_event event;
	struct weston_coord_global pos;
	struct weston_coord rel;
	struct timespec time;

	if (!pointer)
		return;

	pos.c = weston_coord(x, y);
	rel = weston_coord_global_sub(pos, pointer->pos).c;
	weston_compositor_get_time(&time);
	weston_pointer_motion_event_init(&event, &time, seat,
					 WESTON_POINTER_MOTION_REL, NULL, &rel,
					 NULL);
	notify_motion(&event);
	notify_pointer_frame(seat);
}

static void
test_button(struct wl_client *client, struct wl_resource *resource,
	    uint32_t button, uint32_t state)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_seat *seat = first_seat(server);
	struct weston_pointer_button_event event;
	struct timespec time;

	if (!seat)
		return;

	weston_compositor_get_time(&time);
	weston_pointer_button_event_init(&event, &time, seat, button, state);
	notify_button(&event);
	notify_pointer_frame(seat);
}

static void
test_key(struct wl_client *client, struct wl_resource *resource, uint32_t key,
	 uint32_t state)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_seat *seat = first_seat(server);
	struct weston_key_event event;
	struct timespec time;

	if (!seat)
		return;

	weston_compositor_get_time(&time);
	weston_key_event_init(&event, &time, seat, key, state,
			      STATE_UPDATE_AUTOMATIC);
	notify_key(&event);
}

static void
test_output_connected(struct wl_client *client, struct wl_resource *resource,
		      uint32_t index, uint32_t connected)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_head *head = NULL;
	while ((head = weston_compositor_iterate_heads(server->compositor,
						       head))) {
		if (index-- == 0) {
			weston_head_set_connection_status(head, connected != 0);
			weston_compositor_flush_heads_changed(
				server->compositor);
			return;
		}
	}
}

static void
test_configure_output(struct wl_client *client, struct wl_resource *resource,
		      uint32_t index, int32_t scale, int32_t transform,
		      int32_t x, int32_t y)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	PoloBackendOutput *output;
	PoloBackendMode mode;
	GError *error = NULL;
	if (index >= server->outputs->len)
		return;
	output = server->outputs->pdata[index];
	if (!polo_backend_output_get_mode(output, 0, &mode))
		return;
	if (!polo_backend_output_apply(output, mode.width, mode.height,
				       mode.refresh, scale, transform, x, y,
				       TRUE, &error)) {
		g_clear_error(&error);
		return;
	}
	g_signal_emit_by_name(server, "outputs-changed");
}

static void
test_disable_output(struct wl_client *client, struct wl_resource *resource,
		    uint32_t index)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	GError *error = NULL;

	if (index >= server->outputs->len)
		return;
	if (!polo_backend_output_apply(server->outputs->pdata[index], 0, 0, 0,
				       1, 0, 0, 0, FALSE, &error)) {
		g_clear_error(&error);
		return;
	}
	g_signal_emit_by_name(server, "outputs-changed");
}

static void
answer_setting(struct wl_resource *resource, const char *schema,
	       const char *key, const char *value)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	char *now = NULL;

	if (server->test_settings)
		now = server->test_settings(schema, key, value,
					    server->test_settings_data);
	polo_test_v1_send_setting(resource, schema, key, now ? now : "");
	g_free(now);
}

static void
test_set_setting(struct wl_client *client, struct wl_resource *resource,
		 const char *schema, const char *key, const char *value)
{
	answer_setting(resource, schema, key, value);
}

static unsigned bell_rings;
static char *bell_title;

static void
on_system_bell(PoloBackendServer *server, PoloBackendToplevel *toplevel,
	       gpointer data)
{
	bell_rings++;
	g_free(bell_title);
	bell_title = toplevel
		? g_strdup(polo_backend_toplevel_get_title(toplevel))
		: NULL;
}

static void
test_get_setting(struct wl_client *client, struct wl_resource *resource,
		 const char *schema, const char *key)
{
	if (strcmp(schema, "polo-test") == 0 && strcmp(key, "bell-last") == 0) {
		g_autofree char *text = g_strdup_printf(
			"%u %s", bell_rings, bell_title ? bell_title : "");

		polo_test_v1_send_setting(resource, schema, key, text);
		return;
	}
	if (strcmp(schema, "polo-test") == 0 &&
	    strcmp(key, "sandbox-clients") == 0) {
		PoloBackendServer *server = wl_resource_get_user_data(resource);
		g_autoptr(GString) text = g_string_new("");
		GHashTableIter iter;
		gpointer value;

		g_hash_table_iter_init(&iter, server->clients);
		while (g_hash_table_iter_next(&iter, NULL, &value)) {
			if (polo_backend_client_get_sandbox_engine(value))
				g_string_append_printf(
					text, "%s %s %s;",
					polo_backend_client_get_sandbox_engine(
						value),
					polo_backend_client_get_sandbox_app_id(
						value),
					polo_backend_client_get_sandbox_instance_id(
						value));
		}
		polo_test_v1_send_setting(resource, schema, key, text->str);
		return;
	}
	if (strcmp(schema, "polo-test") == 0 &&
	    strcmp(key, "idle-inhibited") == 0) {
		polo_test_v1_send_setting(
			resource, schema, key,
			polo_backend_server_get_idle_inhibited(
				wl_resource_get_user_data(resource))
				? "true"
				: "false");
		return;
	}
	answer_setting(resource, schema, key, NULL);
}

static void
test_gesture(struct wl_client *client, struct wl_resource *resource,
	     uint32_t kind, uint32_t phase, uint32_t fingers, wl_fixed_t dx,
	     wl_fixed_t dy, wl_fixed_t scale, wl_fixed_t rotation,
	     int32_t cancelled)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_seat *seat;
	struct weston_pointer_gesture_event ev = {
		.kind = kind,
		.phase = phase,
		.fingers = fingers,
		.dx = wl_fixed_to_double(dx),
		.dy = wl_fixed_to_double(dy),
		.scale = wl_fixed_to_double(scale),
		.rotation = wl_fixed_to_double(rotation),
		.cancelled = cancelled,
	};

	wl_list_for_each(seat, &server->compositor->seat_list, link)
	{
		struct weston_pointer *pointer = weston_seat_get_pointer(seat);

		if (pointer)
			wl_signal_emit(&pointer->gesture_signal, &ev);
		return;
	}
}

static PoloBackendOutput *
first_output(PoloBackendServer *server)
{
	return server->outputs->len > 0 ? g_ptr_array_index(server->outputs, 0)
					: NULL;
}

static void
test_shell_ui_samples(struct wl_client *client, struct wl_resource *resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	PoloBackendShellUi *ui = server->shell_ui;
	PoloBackendOutput *output = first_output(server);

	polo_backend_shell_ui_switcher_entry(ui, 1, "One", "one", 0);
	polo_backend_shell_ui_switcher_entry(
		ui, 2, "Two", NULL, POLO_BACKEND_SWITCHER_ENTRY_HIDDEN);
	polo_backend_shell_ui_show_switcher(ui, output, 2, 5, FALSE, 48);
	polo_backend_shell_ui_update_selected(ui, 1);
	polo_backend_shell_ui_hide_switcher(ui);
	polo_backend_shell_ui_window_menu_workspace(ui, 0, "Workspace 1");
	polo_backend_shell_ui_window_menu_workspace(ui, 1, "Workspace 2");
	polo_backend_shell_ui_show_window_menu(ui, 7, output, 10, 20, 0x3ffff,
					       POLO_BACKEND_MENU_OP_MINIMIZE, 1,
					       2);
	polo_backend_shell_ui_hide_window_menu(ui, 7);
	polo_backend_shell_ui_show_osd(ui, POLO_BACKEND_OSD_WORKSPACE, output,
				       "Workspace 2", -1);
	polo_backend_shell_ui_show_dialog(
		ui, 3, POLO_BACKEND_DIALOG_NOT_RESPONDING, output, "Hung");
	polo_backend_shell_ui_hide_dialog(ui, 3);
}

static void
test_shell_ui_dialog(struct wl_client *client, struct wl_resource *resource,
		     uint32_t dialog_id, uint32_t kind, const char *payload)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);

	polo_backend_shell_ui_show_dialog(server->shell_ui, dialog_id, kind,
					  first_output(server), payload);
}

static void
broadcast_shell_ui_request(PoloBackendServer *server, const char *text)
{
	struct wl_resource *resource;

	wl_resource_for_each(resource, &server->test_resources) if (
		wl_resource_get_version(resource) >= 4)
		polo_test_v1_send_shell_ui_request(resource, text);
}

static void
shell_ui_bound(PoloBackendShellUi *ui, PoloBackendClient *client,
	       PoloBackendServer *server)
{
	struct wl_resource *resource;

	wl_resource_for_each(resource, &server->test_resources) if (
		wl_resource_get_version(resource) >= 4)
		polo_test_v1_send_shell_ui_bound(
			resource, polo_backend_client_get_pid(client));
}

static void
shell_ui_unbound(PoloBackendShellUi *ui, PoloBackendServer *server)
{
	struct wl_resource *resource;

	wl_resource_for_each(resource, &server->test_resources) if (
		wl_resource_get_version(resource) >= 4)
		polo_test_v1_send_shell_ui_unbound(resource);
}

static void
shell_ui_menu_action(PoloBackendShellUi *ui, guint window_id, guint op,
		     int workspace, PoloBackendServer *server)
{
	g_autofree char *text = g_strdup_printf("menu_action %u %u %d",
						window_id, op, workspace);

	broadcast_shell_ui_request(server, text);
}

static void
shell_ui_dialog_response(PoloBackendShellUi *ui, guint dialog_id, int response,
			 const char *answer, PoloBackendServer *server)
{
	g_autofree char *text =
		g_strdup_printf("dialog_response %u %d %s", dialog_id, response,
				answer ? answer : "(null)");

	broadcast_shell_ui_request(server, text);
}

static void
shell_ui_color_table(PoloBackendShellUi *ui, GBytes *colors,
		     PoloBackendServer *server)
{
	gsize size;
	const float *values = g_bytes_get_data(colors, &size);
	g_autofree char *text =
		g_strdup_printf("color_table %g %g", values[0],
				values[size / sizeof(float) - 1]);

	broadcast_shell_ui_request(server, text);
}

static const struct polo_test_v1_interface test_implementation = {
	.destroy = test_destroy,
	.get_window_info = test_get_window_info,
	.get_layer_surface_info = test_get_layer_surface_info,
	.move_pointer = test_move_pointer,
	.button = test_button,
	.key = test_key,
	.output_connected = test_output_connected,
	.configure_output = test_configure_output,
	.disable_output = test_disable_output,
	.set_setting = test_set_setting,
	.get_setting = test_get_setting,
	.gesture = test_gesture,
	.shell_ui_samples = test_shell_ui_samples,
	.shell_ui_dialog = test_shell_ui_dialog,
};

static void
test_resource_destroyed(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
test_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	PoloBackendServer *server = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &polo_test_v1_interface, version,
				      id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &test_implementation, server,
				       test_resource_destroyed);
	wl_list_insert(&server->test_resources, wl_resource_get_link(resource));
	if (version >= 4 && polo_backend_shell_ui_is_bound(server->shell_ui))
		polo_test_v1_send_shell_ui_bound(
			resource,
			polo_backend_client_get_pid(
				polo_backend_shell_ui_get_client(
					server->shell_ui)));
}

gboolean
polo_backend_server_has_test_protocol(void)
{
	return TRUE;
}

/* Test mode lets weston_capture_v1 clients (weston-screenshooter) take
 * screenshots, for checking real clients by eye. */
static void
authorize_screenshot(struct wl_listener *listener,
		     struct weston_output_capture_attempt *attempt)
{
	(void)listener;
	attempt->authorized = true;
}

gboolean
polo_backend_test_protocol_init(PoloBackendServer *server, GError **error)
{
	weston_compositor_add_screenshot_authority(
		server->compositor, &server->screenshot_authority,
		authorize_screenshot);
	server->test_global = wl_global_create(
		server->display, &polo_test_v1_interface, 5, server, test_bind);
	if (!server->test_global) {
		g_set_error(error, POLO_BACKEND_ERROR,
			    POLO_BACKEND_ERROR_FAILED,
			    "cannot create the polo_test_v1 global");
		return FALSE;
	}
	g_signal_connect(server->shell_ui, "bound", G_CALLBACK(shell_ui_bound),
			 server);
	g_signal_connect(server->shell_ui, "unbound",
			 G_CALLBACK(shell_ui_unbound), server);
	g_signal_connect(server->shell_ui, "menu-action",
			 G_CALLBACK(shell_ui_menu_action), server);
	g_signal_connect(server->shell_ui, "dialog-response",
			 G_CALLBACK(shell_ui_dialog_response), server);
	g_signal_connect(server->shell_ui, "color-table",
			 G_CALLBACK(shell_ui_color_table), server);
	polo_log_scope_warning(polo_backend_log_scope("polo"),
			       "test mode: polo_test_v1 is enabled");
	g_signal_connect(server, "system-bell", G_CALLBACK(on_system_bell),
			 NULL);
	return TRUE;
}

#else /* !POLO_HAVE_TEST_PROTOCOL */

gboolean
polo_backend_server_has_test_protocol(void)
{
	return FALSE;
}

gboolean
polo_backend_test_protocol_init(PoloBackendServer *server, GError **error)
{
	g_set_error(error, POLO_BACKEND_ERROR, POLO_BACKEND_ERROR_FAILED,
		    "this build has no test protocol (-Dtest-protocol=false)");
	return FALSE;
}

#endif /* POLO_HAVE_TEST_PROTOCOL */
