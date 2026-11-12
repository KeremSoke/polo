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

#include <string.h>

#include <wayland-server-core.h>
#include <malloc.h>

#include <libweston/libweston.h>

#include "backend.h"
#include <libweston/backend-headless.h>
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
test_get_lamp_target(struct wl_client *client, struct wl_resource *resource,
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
				    server->test_inspector_data) ||
	    !info.lamp_known) {
		polo_test_v1_send_window_unknown(resource, surface_resource);
		return;
	}
	polo_test_v1_send_lamp_target(resource, surface_resource, info.lamp_x,
				      info.lamp_y, info.lamp_width,
				      info.lamp_height, info.lamp_edge,
				      info.lamp_source);
}

static void
view_rect(PoloBackendView *view, double *x, double *y, double *width,
	  double *height)
{
	int w, h;

	polo_backend_view_get_position(view, x, y);
	polo_backend_view_get_size(view, &w, &h);
	*width = w;
	*height = h;
}

static void
test_bend_mirror(struct wl_client *client, struct wl_resource *resource,
		 struct wl_resource *surface_resource, int32_t x, int32_t y,
		 int32_t width, int32_t height)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	PoloBackendToplevel *toplevel;
	PoloBackendView *source;
	GPtrArray *views;
	double sx, sy, x1, y1, x2, y2;

	g_clear_object(&server->test_mirror);
	toplevel = polo_backend_toplevel_from_surface(surface);
	if (width <= 0 || height <= 0 || !toplevel)
		return;
	source = polo_backend_toplevel_get_view(toplevel);
	server->test_mirror = polo_backend_view_mirror(source);
	if (!server->test_mirror)
		return;
	polo_backend_view_get_position(source, &sx, &sy);
	polo_backend_view_set_position(server->test_mirror, sx, sy);
	views = polo_backend_view_mirror_views(server->test_mirror);
	x1 = y1 = G_MAXDOUBLE;
	x2 = y2 = -G_MAXDOUBLE;
	for (guint i = 0; i < views->len; i++) {
		double vx, vy, vw, vh;

		view_rect(views->pdata[i], &vx, &vy, &vw, &vh);
		x1 = MIN(x1, vx);
		y1 = MIN(y1, vy);
		x2 = MAX(x2, vx + vw);
		y2 = MAX(y2, vy + vh);
	}
	for (guint i = 0; i < views->len && x2 > x1 && y2 > y1; i++) {
		double vx, vy, vw, vh;
		float grid[16];

		view_rect(views->pdata[i], &vx, &vy, &vw, &vh);
		for (int corner = 0; corner < 4; corner++) {
			double u = corner & 1, v = corner >> 1;

			grid[corner * 4] =
				x + (vx + u * vw - x1) / (x2 - x1) * width;
			grid[corner * 4 + 1] =
				y + (vy + v * vh - y1) / (y2 - y1) * height;
			grid[corner * 4 + 2] = u;
			grid[corner * 4 + 3] = v;
		}
		polo_backend_view_set_deformation(views->pdata[i], grid, 2, 2);
	}
	g_ptr_array_unref(views);
	polo_backend_view_move_to_layer(
		server->test_mirror,
		polo_backend_server_get_layer(server,
					      POLO_BACKEND_LAYER_OVERLAY));
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
test_move_pointer_absolute(struct wl_client *client,
			   struct wl_resource *resource, int32_t x, int32_t y)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_seat *seat = first_seat(server);
	struct weston_pointer_motion_event event;
	struct weston_coord_global pos;
	struct timespec time;

	if (!seat || !weston_seat_get_pointer(seat))
		return;
	pos.c = weston_coord(x, y);
	weston_compositor_get_time(&time);
	weston_pointer_motion_event_init(&event, &time, seat,
					 WESTON_POINTER_MOTION_ABS, &pos, NULL,
					 NULL);
	notify_motion(&event);
	notify_pointer_frame(seat);
}

static void
test_move_pointer_output(struct wl_client *client, struct wl_resource *resource,
			 const char *name, wl_fixed_t x, wl_fixed_t y)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_seat *seat = first_seat(server);
	struct weston_pointer_motion_event event;
	struct weston_output *output;
	struct weston_coord_global pos;
	struct timespec time;

	if (!seat || !weston_seat_get_pointer(seat))
		return;
	wl_list_for_each(output, &server->compositor->output_list, link)
	{
		if (g_strcmp0(output->name, name) != 0)
			continue;
		pos.c = weston_matrix_transform_coord(
			&output->inverse_matrix,
			weston_coord(wl_fixed_to_double(x),
				     wl_fixed_to_double(y)));
		weston_compositor_get_time(&time);
		weston_pointer_motion_event_init(&event, &time, seat,
						 WESTON_POINTER_MOTION_ABS,
						 &pos, NULL, NULL);
		notify_motion(&event);
		notify_pointer_frame(seat);
		return;
	}
}

static enum wl_iterator_result
count_resource(struct wl_resource *resource, void *data)
{
	(*(uint32_t *)data)++;
	return WL_ITERATOR_CONTINUE;
}

static enum wl_iterator_result
count_output(struct wl_resource *resource, void *data)
{
	const char *name = wl_resource_get_class(resource);

	if (g_strcmp0(name, "wl_output") == 0 ||
	    g_strcmp0(name, "zxdg_output_v1") == 0)
		(*(uint32_t *)data)++;
	return WL_ITERATOR_CONTINUE;
}

static void
test_get_resources(struct wl_client *client, struct wl_resource *resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct wl_client *shell_ui =
		polo_backend_shell_ui_get_wl_client(server->shell_ui);
	struct wl_list *list = wl_display_get_client_list(server->display);
	struct wl_client *each;
	uint32_t clients = 0, objects = 0, shell_ui_objects = 0,
		 shell_ui_outputs = 0;

	wl_client_for_each(each, list)
	{
		clients++;
		wl_client_for_each_resource(each, count_resource, &objects);
		if (each == shell_ui) {
			wl_client_for_each_resource(each, count_resource,
						    &shell_ui_objects);
			wl_client_for_each_resource(each, count_output,
						    &shell_ui_outputs);
		}
	}
	polo_test_v1_send_resources(resource, clients, objects,
				    shell_ui_objects, shell_ui_outputs);
}

static void
test_pointer_axis(struct wl_client *client, struct wl_resource *resource,
		  int32_t value)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_seat *seat = first_seat(server);
	struct weston_pointer_axis_event event;
	struct timespec time;

	if (!seat)
		return;
	weston_compositor_get_time(&time);
	weston_pointer_axis_event_init(&event, &time, seat, 0, value, true,
				       value > 0 ? 1 : -1);
	notify_axis(&event);
	notify_pointer_frame(seat);
}

static void
test_get_pointer_position(struct wl_client *client,
			  struct wl_resource *resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_seat *seat = first_seat(server);
	struct weston_pointer *pointer =
		seat ? weston_seat_get_pointer(seat) : NULL;

	if (pointer)
		polo_test_v1_send_pointer_position(resource, pointer->pos.c.x,
						   pointer->pos.c.y);
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
test_add_output(struct wl_client *client, struct wl_resource *resource,
		const char *name, struct wl_array *edid, uint32_t internal,
		struct wl_array *modes)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	const struct weston_headless_head_api *api =
		weston_headless_head_get_api(server->compositor);
	const int32_t *values = modes->data;
	size_t n = modes->size / (3 * sizeof(int32_t));
	g_autofree struct weston_headless_mode *list = NULL;

	if (!api || n == 0 || modes->size % (3 * sizeof(int32_t)) != 0) {
		wl_resource_post_error(resource, 0, "cannot add output %s",
				       name);
		return;
	}
	list = g_new0(struct weston_headless_mode, n);
	for (size_t i = 0; i < n; i++)
		list[i] = (struct weston_headless_mode){
			values[i * 3], values[i * 3 + 1],
			(uint32_t)values[i * 3 + 2]
		};
	g_hash_table_add(server->fake_heads, g_strdup(name));
	if (api->create_head(server->backend, name, edid->data, edid->size,
			     internal != 0, list, (int)n) < 0) {
		wl_resource_post_error(resource, 0, "cannot add output %s",
				       name);
		return;
	}
	weston_compositor_flush_heads_changed(server->compositor);
}

static void
test_connect_output(struct wl_client *client, struct wl_resource *resource,
		    const char *name, uint32_t connected)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_head *head = NULL;

	while ((head = weston_compositor_iterate_heads(server->compositor,
						       head))) {
		if (strcmp(weston_head_get_name(head), name) == 0) {
			weston_head_set_connection_status(head, connected != 0);
			weston_compositor_flush_heads_changed(
				server->compositor);
			return;
		}
	}
}

static void
test_set_session_active(struct wl_client *client, struct wl_resource *resource,
			uint32_t active)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);

	server->compositor->session_active = active != 0;
	wl_signal_emit(&server->compositor->session_signal, server->compositor);
	if (active)
		weston_compositor_damage_all(server->compositor);
}

static void
test_trim_heap(struct wl_client *client, struct wl_resource *resource)
{
	malloc_trim(0);
}

static void
test_gpu_wedged(struct wl_client *client, struct wl_resource *resource,
		const char *recovery)
{
	polo_backend_gpu_wedged(wl_resource_get_user_data(resource), "test",
				recovery);
}

static void
test_remove_output(struct wl_client *client, struct wl_resource *resource,
		   const char *name)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	const struct weston_headless_head_api *api =
		weston_headless_head_get_api(server->compositor);

	if (!api || !g_hash_table_contains(server->fake_heads, name) ||
	    api->destroy_head(server->backend, name) < 0) {
		wl_resource_post_error(resource, 0, "cannot remove output %s",
				       name);
		return;
	}
	g_hash_table_remove(server->fake_heads, name);
	weston_compositor_flush_heads_changed(server->compositor);
}

static void
test_get_outputs(struct wl_client *client, struct wl_resource *resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);

	for (guint i = 0; i < server->outputs->len; i++) {
		PoloBackendOutput *output = server->outputs->pdata[i];
		struct weston_output *wo =
			polo_backend_output_get_weston_output(output);
		int x, y, width, height;
		struct weston_geometry area = { 0 };

		polo_backend_output_get_geometry(output, &x, &y, &width,
						 &height);
		if (!wo->enabled ||
		    !polo_backend_output_get_work_area(output, &area))
			area = (struct weston_geometry){ 0 };
		polo_test_v1_send_output_state(
			resource, polo_backend_output_get_name(output),
			wo->enabled, x, y, width, height,
			polo_backend_output_get_scale(output),
			polo_backend_output_get_primary(output),
			wo->enabled && wo->current_mode
				? wo->current_mode->refresh
				: 0,
			area.x, area.y, area.width, area.height);
	}
	polo_test_v1_send_outputs_done(resource, server->monitor_count);
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
	    strcmp(key, "session-lock") == 0) {
		static const char *const names[] = { "unlocked", "locking",
						     "locked", "abandoned" };

		polo_test_v1_send_setting(
			resource, schema, key,
			names[polo_backend_server_get_lock_state(
				wl_resource_get_user_data(resource))]);
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
				       NULL, "Workspace 2", -1);
	polo_backend_shell_ui_show_dialog(
		ui, 3, POLO_BACKEND_DIALOG_NOT_RESPONDING, output, "Hung");
	polo_backend_shell_ui_hide_dialog(ui, 3);
}

static void
test_shell_ui_lookup_color(struct wl_client *client,
			   struct wl_resource *resource, const char *name)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);

	polo_backend_shell_ui_lookup_color(server->shell_ui, name);
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
	g_free(server->test_color_table);
	server->test_color_table =
		g_strdup_printf("color_table %g %g", values[0],
				values[size / sizeof(float) - 1]);
	broadcast_shell_ui_request(server, server->test_color_table);
}

static void
shell_ui_custom_color(PoloBackendShellUi *ui, const char *name, GBytes *rgba,
		      PoloBackendServer *server)
{
	const float *c = rgba ? g_bytes_get_data(rgba, NULL) : NULL;
	g_autofree char *text = c
		? g_strdup_printf("custom_color %s %g %g %g %g", name, c[0],
				  c[1], c[2], c[3])
		: g_strdup_printf("custom_color %s none", name);

	broadcast_shell_ui_request(server, text);
}

static void
test_output_device_changed(struct wl_client *client,
			   struct wl_resource *resource,
			   struct wl_resource *output_resource)
{
	struct weston_head *head = weston_head_from_resource(output_resource);

	if (head)
		weston_head_set_device_changed(head);
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
	.shell_ui_lookup_color = test_shell_ui_lookup_color,
	.output_device_changed = test_output_device_changed,
	.get_lamp_target = test_get_lamp_target,
	.bend_mirror = test_bend_mirror,
	.move_pointer_absolute = test_move_pointer_absolute,
	.add_output = test_add_output,
	.connect_output = test_connect_output,
	.remove_output = test_remove_output,
	.set_session_active = test_set_session_active,
	.get_outputs = test_get_outputs,
	.pointer_axis = test_pointer_axis,
	.get_pointer_position = test_get_pointer_position,
	.gpu_wedged = test_gpu_wedged,
	.trim_heap = test_trim_heap,
	.move_pointer_output = test_move_pointer_output,
	.get_resources = test_get_resources,
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
	if (version >= 4 && server->test_color_table)
		polo_test_v1_send_shell_ui_request(resource,
						   server->test_color_table);
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
	server->test_global =
		wl_global_create(server->display, &polo_test_v1_interface, 16,
				 server, test_bind);
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
	g_signal_connect(server->shell_ui, "custom-color",
			 G_CALLBACK(shell_ui_custom_color), server);
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
