/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <sys/mman.h>
#include <unistd.h>

#include <libweston/libweston.h>

#include "foreign-toplevel-private.h"
#include "wlr-foreign-toplevel-management-unstable-v1-server-protocol.h"
#include "xfce-foreign-toplevel-management-private-v1-server-protocol.h"
#include "xfce-output-private-v1-server-protocol.h"

struct xfce_output {
	PoloBackendServer *server;
	struct wl_resource *resource;
	struct weston_head *head;
	struct wl_listener head_destroyed;
	struct wl_list link;
};

struct xfce_seat {
	PoloBackendServer *server;
	struct weston_seat *seat;
	struct weston_pointer *pointer;
	struct weston_output *output;
	struct wl_listener caps_changed;
	struct wl_listener seat_destroyed;
	struct wl_listener motion;
	struct wl_listener pointer_destroyed;
	struct wl_list link;
};

static const uint32_t extra_state_map[][2] = {
	{ POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_SHADED,
	  XFCE_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_SHADED },
	{ POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_SKIP_PAGER,
	  XFCE_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_SKIP_PAGER },
	{ POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_SKIP_TASKLIST,
	  XFCE_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_SKIP_TASKLIST },
	{ POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_STICKY,
	  XFCE_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_STICKY },
	{ POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_ABOVE,
	  XFCE_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ABOVE },
	{ POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_BELOW,
	  XFCE_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_BELOW },
	{ POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_DEMANDS_ATTENTION,
	  XFCE_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_DEMANDS_ATTENTION },
};

void
polo_backend_xfce_send_state(struct ft_handle_resource *hr)
{
	struct wl_array states;
	uint32_t *s;
	guint i;

	if (!hr->xfce || !hr->handle)
		return;
	wl_array_init(&states);
	for (i = 0; i < G_N_ELEMENTS(extra_state_map); i++) {
		if (!(hr->handle->extra_state & extra_state_map[i][0]))
			continue;
		s = wl_array_add(&states, sizeof *s);
		if (s)
			*s = extra_state_map[i][1];
	}
	xfce_foreign_toplevel_handle_v1_send_state(hr->xfce, &states);
	wl_array_release(&states);
}

void
polo_backend_xfce_handle_resource_destroyed(struct ft_handle_resource *hr)
{
	if (!hr->xfce)
		return;
	wl_resource_set_user_data(hr->xfce, NULL);
	hr->xfce = NULL;
}

static PoloBackendForeignToplevel *
xfce_handle_target(struct wl_resource *resource)
{
	struct ft_handle_resource *hr = wl_resource_get_user_data(resource);

	if (!hr || !hr->handle || hr->handle->closed)
		return NULL;
	return hr->handle;
}

static void
emit_bool(struct wl_resource *resource, const char *signal, gboolean on)
{
	PoloBackendForeignToplevel *toplevel = xfce_handle_target(resource);

	if (toplevel)
		g_signal_emit_by_name(toplevel, signal, on);
}

static void
handle_set_shaded(struct wl_client *client, struct wl_resource *resource)
{
	emit_bool(resource, "request-shade", TRUE);
}

static void
handle_unset_shaded(struct wl_client *client, struct wl_resource *resource)
{
	emit_bool(resource, "request-shade", FALSE);
}

static void
handle_set_sticky(struct wl_client *client, struct wl_resource *resource)
{
	emit_bool(resource, "request-sticky", TRUE);
}

static void
handle_unset_sticky(struct wl_client *client, struct wl_resource *resource)
{
	emit_bool(resource, "request-sticky", FALSE);
}

static void
handle_set_above(struct wl_client *client, struct wl_resource *resource)
{
	emit_bool(resource, "request-above", TRUE);
}

static void
handle_unset_above(struct wl_client *client, struct wl_resource *resource)
{
	emit_bool(resource, "request-above", FALSE);
}

static void
handle_set_below(struct wl_client *client, struct wl_resource *resource)
{
	emit_bool(resource, "request-below", TRUE);
}

static void
handle_unset_below(struct wl_client *client, struct wl_resource *resource)
{
	emit_bool(resource, "request-below", FALSE);
}

static void
handle_highlight(struct wl_client *client, struct wl_resource *resource,
		 struct wl_resource *seat)
{
}

static void
handle_unhighlight(struct wl_client *client, struct wl_resource *resource)
{
}

static void
handle_move(struct wl_client *client, struct wl_resource *resource,
	    struct wl_resource *seat)
{
	PoloBackendForeignToplevel *toplevel = xfce_handle_target(resource);

	if (toplevel)
		g_signal_emit_by_name(toplevel, "request-move");
}

static void
handle_resize(struct wl_client *client, struct wl_resource *resource,
	      struct wl_resource *seat)
{
	PoloBackendForeignToplevel *toplevel = xfce_handle_target(resource);

	if (toplevel)
		g_signal_emit_by_name(toplevel, "request-resize");
}

static void
handle_move_to_output(struct wl_client *client, struct wl_resource *resource,
		      struct wl_resource *output_resource)
{
	PoloBackendForeignToplevel *toplevel = xfce_handle_target(resource);
	PoloBackendOutput *output;

	if (!toplevel)
		return;
	output = polo_backend_server_output_from_resource(toplevel->server,
							  output_resource);
	if (output)
		g_signal_emit_by_name(toplevel, "request-move-to-output",
				      output);
}

static void
handle_move_to_workspace(struct wl_client *client, struct wl_resource *resource,
			 struct wl_resource *workspace)
{
	struct ft_handle_resource *hr = wl_resource_get_user_data(resource);
	PoloBackendWorkspace *target =
		polo_backend_workspace_from_resource(workspace);

	if (hr && hr->handle && target)
		g_signal_emit_by_name(hr->handle, "request-move-to-workspace",
				      target);
}

/* Bring workspace_enter and workspace_leave in line; whether anything was sent.
 */
static gboolean
sync_workspaces(struct ft_handle_resource *hr)
{
	struct wl_client *client;
	GPtrArray *entered;
	gboolean changed = FALSE;
	guint i;

	if (!hr->xfce || !hr->handle)
		return FALSE;
	client = wl_resource_get_client(hr->xfce);
	entered = g_ptr_array_new();
	for (i = 0; i < hr->handle->workspaces->len; i++) {
		struct wl_resource *workspace =
			polo_backend_workspace_resource_for(
				g_ptr_array_index(hr->handle->workspaces, i),
				client);

		if (!workspace)
			continue;
		g_ptr_array_add(entered, workspace);
		if (!hr->entered_workspaces ||
		    !g_ptr_array_find(hr->entered_workspaces, workspace,
				      NULL)) {
			xfce_foreign_toplevel_handle_v1_send_workspace_enter(
				hr->xfce, workspace);
			changed = TRUE;
		}
	}
	for (i = 0; hr->entered_workspaces && i < hr->entered_workspaces->len;
	     i++) {
		struct wl_resource *workspace =
			g_ptr_array_index(hr->entered_workspaces, i);

		if (!g_ptr_array_find(entered, workspace, NULL)) {
			xfce_foreign_toplevel_handle_v1_send_workspace_leave(
				hr->xfce, workspace);
			changed = TRUE;
		}
	}
	if (hr->entered_workspaces)
		g_ptr_array_unref(hr->entered_workspaces);
	hr->entered_workspaces = entered;
	return changed;
}

static void
sync_toplevel_workspaces(PoloBackendForeignToplevel *toplevel)
{
	struct ft_handle_resource *hr;

	wl_list_for_each(hr, &toplevel->resources,
			 link) if (sync_workspaces(hr))
		zwlr_foreign_toplevel_handle_v1_send_done(hr->resource);
}

void
polo_backend_foreign_toplevel_set_workspaces(
	PoloBackendForeignToplevel *toplevel, PoloBackendWorkspace **workspaces,
	int n_workspaces)
{
	int i;

	g_return_if_fail(POLO_BACKEND_IS_FOREIGN_TOPLEVEL(toplevel));
	g_ptr_array_set_size(toplevel->workspaces, 0);
	for (i = 0; i < n_workspaces; i++)
		g_ptr_array_add(toplevel->workspaces,
				g_object_ref(workspaces[i]));
	sync_toplevel_workspaces(toplevel);
}

void
polo_backend_xfce_sync_all_workspaces(PoloBackendServer *server)
{
	PoloBackendForeignToplevel *toplevel;

	wl_list_for_each(toplevel, &server->foreign_toplevels, link)
		sync_toplevel_workspaces(toplevel);
}

void
polo_backend_xfce_workspace_removed(PoloBackendServer *server,
				    PoloBackendWorkspace *workspace)
{
	PoloBackendForeignToplevel *toplevel;

	wl_list_for_each(
		toplevel, &server->foreign_toplevels,
		link) if (g_ptr_array_remove(toplevel->workspaces, workspace))
		sync_toplevel_workspaces(toplevel);
}

void
polo_backend_xfce_workspace_handle_gone(PoloBackendServer *server,
					struct wl_resource *resource)
{
	PoloBackendForeignToplevel *toplevel;
	struct ft_handle_resource *hr;

	wl_list_for_each(toplevel, &server->foreign_toplevels, link)
		wl_list_for_each(hr, &toplevel->resources,
				 link) if (hr->entered_workspaces)
			g_ptr_array_remove(hr->entered_workspaces, resource);
}

static void
icon_pixels_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct xfce_foreign_toplevel_icon_pixels_v1_interface
	icon_pixels_implementation = {
		.destroy = icon_pixels_destroy,
	};

static void
handle_get_icon_pixels(struct wl_client *client, struct wl_resource *resource,
		       uint32_t id, uint32_t size, uint32_t scale)
{
	struct wl_resource *pixels = wl_resource_create(
		client, &xfce_foreign_toplevel_icon_pixels_v1_interface,
		wl_resource_get_version(resource), id);

	if (!pixels) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(pixels, &icon_pixels_implementation,
				       NULL, NULL);
	xfce_foreign_toplevel_icon_pixels_v1_send_failed(
		pixels,
		XFCE_FOREIGN_TOPLEVEL_ICON_PIXELS_V1_FAILURE_REASON_INVALID_ARGS);
}

static void
handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct xfce_foreign_toplevel_handle_v1_interface
	handle_implementation = {
		.set_shaded = handle_set_shaded,
		.unset_shaded = handle_unset_shaded,
		.set_sticky = handle_set_sticky,
		.unset_sticky = handle_unset_sticky,
		.set_above = handle_set_above,
		.unset_above = handle_unset_above,
		.set_below = handle_set_below,
		.unset_below = handle_unset_below,
		.highlight = handle_highlight,
		.unhighlight = handle_unhighlight,
		.move = handle_move,
		.resize = handle_resize,
		.move_to_output = handle_move_to_output,
		.move_to_workspace = handle_move_to_workspace,
		.get_icon_pixels = handle_get_icon_pixels,
		.destroy = handle_destroy,
	};

static void
handle_resource_destroyed(struct wl_resource *resource)
{
	struct ft_handle_resource *hr = wl_resource_get_user_data(resource);

	if (hr)
		hr->xfce = NULL;
}

static void
manager_get_handle(struct wl_client *client, struct wl_resource *resource,
		   uint32_t id, struct wl_resource *wlr_handle)
{
	struct ft_handle_resource *hr = wl_resource_get_user_data(wlr_handle);
	struct wl_resource *xfce;

	if (hr && hr->xfce) {
		wl_resource_post_error(
			resource,
			XFCE_FOREIGN_TOPLEVEL_MANAGER_PRIVATE_V1_ERROR_ALREADY_BOUND,
			"toplevel handle already has an xfce handle");
		return;
	}
	xfce = wl_resource_create(client,
				  &xfce_foreign_toplevel_handle_v1_interface,
				  wl_resource_get_version(resource), id);
	if (!xfce) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(xfce, &handle_implementation, hr,
				       handle_resource_destroyed);
	if (!hr || !hr->handle || hr->handle->closed)
		return;
	hr->xfce = xfce;
	polo_backend_xfce_send_state(hr);
	xfce_foreign_toplevel_handle_v1_send_no_icon(xfce);
	sync_workspaces(hr);
	zwlr_foreign_toplevel_handle_v1_send_done(hr->resource);
}

static void
manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct xfce_foreign_toplevel_manager_private_v1_interface
	manager_implementation = {
		.get_xfce_foreign_toplevel_handle = manager_get_handle,
		.destroy = manager_destroy,
	};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &xfce_foreign_toplevel_manager_private_v1_interface,
		version, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_implementation, data,
				       NULL);
}

static PoloBackendOutput *
xfce_output_handle(struct xfce_output *xo)
{
	if (!xo->head || !xo->head->output)
		return NULL;
	return polo_backend_server_lookup_output(xo->server, xo->head->output);
}

static void
send_workarea(struct xfce_output *xo)
{
	PoloBackendOutput *handle = xfce_output_handle(xo);
	struct weston_output *output = xo->head->output;
	struct weston_geometry area = {
		.x = output->pos.c.x,
		.y = output->pos.c.y,
		.width = output->width,
		.height = output->height,
	};

	if (handle)
		polo_backend_output_get_work_area(handle, &area);
	xfce_output_v1_send_workarea(
		xo->resource, area.x - (int)output->pos.c.x,
		area.y - (int)output->pos.c.y, area.width, area.height);
}

static void
send_seat_event(struct xfce_output *xo, struct weston_seat *seat,
		gboolean enter)
{
	struct wl_client *client = wl_resource_get_client(xo->resource);
	struct wl_resource *seat_resource;

	wl_resource_for_each(seat_resource, &seat->base_resource_list)
	{
		if (wl_resource_get_client(seat_resource) != client)
			continue;
		if (enter)
			xfce_output_v1_send_pointer_enter(xo->resource,
							  seat_resource);
		else
			xfce_output_v1_send_pointer_leave(xo->resource,
							  seat_resource);
	}
}

static void
send_edid(struct xfce_output *xo)
{
	g_autofree char *pattern = NULL;
	g_autofree char *contents = NULL;
	GDir *dir;
	const char *name;
	gsize length = 0;
	int fd;

	dir = g_dir_open("/sys/class/drm", 0, NULL);
	if (!dir)
		return;
	pattern = g_strconcat("-", xo->head->name, NULL);
	while ((name = g_dir_read_name(dir))) {
		g_autofree char *path = NULL;

		if (!g_str_has_suffix(name, pattern))
			continue;
		path = g_build_filename("/sys/class/drm", name, "edid", NULL);
		if (g_file_get_contents(path, &contents, &length, NULL) &&
		    length > 0)
			break;
		g_clear_pointer(&contents, g_free);
	}
	g_dir_close(dir);
	if (!contents)
		return;
	fd = memfd_create("polo-edid", MFD_CLOEXEC);
	if (fd < 0)
		return;
	if (write(fd, contents, length) == (ssize_t)length)
		xfce_output_v1_send_edid(xo->resource, fd, length);
	close(fd);
}

static void
xfce_output_resource_destroyed(struct wl_resource *resource)
{
	struct xfce_output *xo = wl_resource_get_user_data(resource);

	if (xo->head)
		wl_list_remove(&xo->head_destroyed.link);
	wl_list_remove(&xo->link);
	g_free(xo);
}

static void
xfce_output_head_destroyed(struct wl_listener *listener, void *data)
{
	struct xfce_output *xo = wl_container_of(listener, xo, head_destroyed);

	wl_list_remove(&xo->head_destroyed.link);
	xo->head = NULL;
}

static void
xfce_output_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct xfce_output_v1_interface xfce_output_implementation = {
	.destroy = xfce_output_destroy,
};

static void
output_manager_get_output(struct wl_client *client,
			  struct wl_resource *resource, uint32_t id,
			  struct wl_resource *output_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_head *head = weston_head_from_resource(output_resource);
	struct xfce_output *xo = g_new0(struct xfce_output, 1);
	PoloBackendOutput *handle;
	struct xfce_seat *xs;

	xo->server = server;
	xo->resource =
		wl_resource_create(client, &xfce_output_v1_interface,
				   wl_resource_get_version(resource), id);
	if (!xo->resource) {
		g_free(xo);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(xo->resource,
				       &xfce_output_implementation, xo,
				       xfce_output_resource_destroyed);
	wl_list_insert(&server->xfce_outputs, &xo->link);
	if (!head || !head->output)
		return;
	xo->head = head;
	xo->head_destroyed.notify = xfce_output_head_destroyed;
	weston_head_add_destroy_listener(head, &xo->head_destroyed);

	if (head->serial_number && *head->serial_number)
		xfce_output_v1_send_serial(xo->resource, head->serial_number);
	send_edid(xo);
	handle = xfce_output_handle(xo);
	if (handle && polo_backend_output_get_primary(handle))
		xfce_output_v1_send_primary(xo->resource);
	send_workarea(xo);
	xfce_output_v1_send_done(xo->resource);

	wl_list_for_each(xs, &server->xfce_seats, link)
	{
		if (xs->output == head->output)
			send_seat_event(xo, xs->seat, TRUE);
	}
}

static void
output_manager_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct xfce_output_manager_private_v1_interface
	output_manager_implementation = {
		.get_xfce_output = output_manager_get_output,
		.release = output_manager_release,
	};

static void
output_manager_bind(struct wl_client *client, void *data, uint32_t version,
		    uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &xfce_output_manager_private_v1_interface, version, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &output_manager_implementation,
				       data, NULL);
}

void
polo_backend_xfce_output_changed(PoloBackendOutput *output,
				 gboolean became_primary)
{
	struct weston_output *woutput =
		polo_backend_output_get_weston_output(output);
	PoloBackendServer *server = polo_backend_output_get_server(output);
	struct xfce_output *xo;

	if (!server || !server->xfce_output_global)
		return;
	wl_list_for_each(xo, &server->xfce_outputs, link)
	{
		if (!xo->head || xo->head->output != woutput)
			continue;
		if (became_primary)
			xfce_output_v1_send_primary(xo->resource);
		send_workarea(xo);
		xfce_output_v1_send_done(xo->resource);
	}
}

static struct weston_output *
output_at(PoloBackendServer *server, struct weston_coord_global pos)
{
	struct weston_output *output;

	wl_list_for_each(output, &server->compositor->output_list, link)
	{
		if (pixman_region32_contains_point(&output->region, pos.c.x,
						   pos.c.y, NULL))
			return output;
	}
	return NULL;
}

static void
seat_pointer_moved(struct xfce_seat *xs)
{
	struct weston_output *output = output_at(xs->server, xs->pointer->pos);
	struct xfce_output *xo;

	if (output == xs->output)
		return;
	wl_list_for_each(xo, &xs->server->xfce_outputs, link)
	{
		if (!xo->head)
			continue;
		if (xs->output && xo->head->output == xs->output)
			send_seat_event(xo, xs->seat, FALSE);
		if (output && xo->head->output == output)
			send_seat_event(xo, xs->seat, TRUE);
	}
	xs->output = output;
}

static void
seat_motion(struct wl_listener *listener, void *data)
{
	struct xfce_seat *xs = wl_container_of(listener, xs, motion);

	seat_pointer_moved(xs);
}

static void
seat_untrack_pointer(struct xfce_seat *xs)
{
	if (!xs->pointer)
		return;
	wl_list_remove(&xs->motion.link);
	wl_list_remove(&xs->pointer_destroyed.link);
	xs->pointer = NULL;
}

static void
seat_pointer_destroyed(struct wl_listener *listener, void *data)
{
	struct xfce_seat *xs = wl_container_of(listener, xs, pointer_destroyed);

	seat_untrack_pointer(xs);
}

static void
seat_track_pointer(struct xfce_seat *xs)
{
	struct weston_pointer *pointer = weston_seat_get_pointer(xs->seat);

	if (!pointer || pointer == xs->pointer)
		return;
	seat_untrack_pointer(xs);
	xs->pointer = pointer;
	xs->motion.notify = seat_motion;
	wl_signal_add(&pointer->motion_signal, &xs->motion);
	xs->pointer_destroyed.notify = seat_pointer_destroyed;
	wl_signal_add(&pointer->destroy_signal, &xs->pointer_destroyed);
	seat_pointer_moved(xs);
}

static void
seat_caps_changed(struct wl_listener *listener, void *data)
{
	struct xfce_seat *xs = wl_container_of(listener, xs, caps_changed);

	seat_track_pointer(xs);
}

static void
xfce_seat_free(struct xfce_seat *xs)
{
	seat_untrack_pointer(xs);
	wl_list_remove(&xs->caps_changed.link);
	wl_list_remove(&xs->seat_destroyed.link);
	wl_list_remove(&xs->link);
	g_free(xs);
}

static void
seat_destroyed(struct wl_listener *listener, void *data)
{
	struct xfce_seat *xs = wl_container_of(listener, xs, seat_destroyed);

	xfce_seat_free(xs);
}

static void
xfce_seat_new(PoloBackendServer *server, struct weston_seat *seat)
{
	struct xfce_seat *xs = g_new0(struct xfce_seat, 1);

	xs->server = server;
	xs->seat = seat;
	xs->caps_changed.notify = seat_caps_changed;
	wl_signal_add(&seat->updated_caps_signal, &xs->caps_changed);
	xs->seat_destroyed.notify = seat_destroyed;
	wl_signal_add(&seat->destroy_signal, &xs->seat_destroyed);
	wl_list_insert(&server->xfce_seats, &xs->link);
	seat_track_pointer(xs);
}

static void
xfce_seat_created(struct wl_listener *listener, void *data)
{
	PoloBackendServer *server =
		wl_container_of(listener, server, xfce_seat_created);

	xfce_seat_new(server, data);
}

gboolean
polo_backend_xfce_private_init(PoloBackendServer *server, GError **error)
{
	struct weston_seat *seat;

	(void)error;
	server->xfce_toplevel_global = wl_global_create(
		server->display,
		&xfce_foreign_toplevel_manager_private_v1_interface, 1, server,
		manager_bind);
	server->xfce_output_global = wl_global_create(
		server->display, &xfce_output_manager_private_v1_interface, 1,
		server, output_manager_bind);
	wl_list_for_each(seat, &server->compositor->seat_list, link)
		xfce_seat_new(server, seat);
	server->xfce_seat_created.notify = xfce_seat_created;
	wl_signal_add(&server->compositor->seat_created_signal,
		      &server->xfce_seat_created);
	return TRUE;
}

void
polo_backend_xfce_private_fini(PoloBackendServer *server)
{
	if (server->xfce_toplevel_global) {
		wl_global_destroy(server->xfce_toplevel_global);
		server->xfce_toplevel_global = NULL;
	}
	if (server->xfce_output_global) {
		wl_global_destroy(server->xfce_output_global);
		server->xfce_output_global = NULL;
		wl_list_remove(&server->xfce_seat_created.link);
	}
	while (!wl_list_empty(&server->xfce_seats)) {
		struct xfce_seat *xs;

		xs = wl_container_of(server->xfce_seats.next, xs, link);
		xfce_seat_free(xs);
	}
}
