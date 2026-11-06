/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwlr_gamma_control_manager_v1, for Night Light (rojo)
 */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "protocol-util.h"
#include "wlr-gamma-control-unstable-v1-server-protocol.h"

struct gamma_control {
	struct wl_list link;
	struct wl_resource *resource;
	PoloBackendServer *server;
	struct weston_output *output;
	struct wl_listener output_destroy;
	bool applied;
};

static void
control_detach(struct gamma_control *control)
{
	if (!control->output)
		return;
	wl_list_remove(&control->output_destroy.link);
	wl_list_remove(&control->link);
	wl_list_init(&control->link);
	control->output = NULL;
}

static void
control_output_destroyed(struct wl_listener *listener, void *data)
{
	struct gamma_control *control =
		wl_container_of(listener, control, output_destroy);

	control_detach(control);
	zwlr_gamma_control_v1_send_failed(control->resource);
}

static bool
read_table(int fd, uint16_t *table, size_t size)
{
	size_t total = 0;
	ssize_t n;

	n = pread(fd, table, size, 0);
	if (n < 0 && errno == ESPIPE) {
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
		while (total < size &&
		       (n = read(fd, (char *)table + total, size - total)) > 0)
			total += (size_t)n;
		return total == size;
	}
	return n == (ssize_t)size;
}

static void
control_set_gamma(struct wl_client *client, struct wl_resource *resource,
		  int32_t fd)
{
	struct gamma_control *control = wl_resource_get_user_data(resource);
	struct weston_output *output = control->output;
	uint32_t entries;
	uint16_t *table;
	bool ok;

	if (!output || !output->set_gamma) {
		close(fd);
		return;
	}
	entries = output->gamma_size;
	table = g_new(uint16_t, 3 * (size_t)entries);
	ok = read_table(fd, table, 3 * (size_t)entries * sizeof *table);
	close(fd);
	if (!ok) {
		g_free(table);
		wl_resource_post_error(
			resource, ZWLR_GAMMA_CONTROL_V1_ERROR_INVALID_GAMMA,
			"the gamma tables are not 3 x %u entries", entries);
		return;
	}
	output->set_gamma(output, entries, table, table + entries,
			  table + 2 * entries);
	control->applied = true;
	g_free(table);
}

static const struct zwlr_gamma_control_v1_interface control_impl = {
	.set_gamma = control_set_gamma,
	.destroy = polo_protocol_destroy,
};

static void
control_resource_destroyed(struct wl_resource *resource)
{
	struct gamma_control *control = wl_resource_get_user_data(resource);
	struct weston_output *output = control->output;

	if (output && control->applied && output->set_gamma)
		output->set_gamma(output, output->gamma_size, NULL, NULL, NULL);
	control_detach(control);
	g_free(control);
}

static bool
output_held(PoloBackendServer *server, struct weston_output *output)
{
	struct gamma_control *control;

	wl_list_for_each(control, &server->gamma_controls, link)
	{
		if (control->output == output)
			return true;
	}
	return false;
}

static void
manager_get_gamma_control(struct wl_client *client,
			  struct wl_resource *resource, uint32_t id,
			  struct wl_resource *output_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_head *head = weston_head_from_resource(output_resource);
	struct weston_output *output = head ? head->output : NULL;
	struct gamma_control *control = g_new0(struct gamma_control, 1);

	control->server = server;
	wl_list_init(&control->link);
	control->resource = polo_protocol_child_new(
		resource, &zwlr_gamma_control_v1_interface, id, &control_impl,
		control, control_resource_destroyed);
	if (!control->resource) {
		g_free(control);
		return;
	}
	if (!output || !output->enabled || !output->set_gamma ||
	    output->gamma_size == 0 || output_held(server, output)) {
		zwlr_gamma_control_v1_send_failed(control->resource);
		return;
	}
	control->output = output;
	control->output_destroy.notify = control_output_destroyed;
	wl_signal_add(&output->destroy_signal, &control->output_destroy);
	wl_list_insert(&server->gamma_controls, &control->link);
	zwlr_gamma_control_v1_send_gamma_size(control->resource,
					      output->gamma_size);
}

static const struct zwlr_gamma_control_manager_v1_interface manager_impl = {
	.get_gamma_control = manager_get_gamma_control,
	.destroy = polo_protocol_destroy,
};

POLO_PROTOCOL_BIND(manager_bind, &zwlr_gamma_control_manager_v1_interface, 1,
		   &manager_impl)

gboolean
polo_backend_gamma_control_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	wl_list_init(&server->gamma_controls);
	server->gamma_control_global = wl_global_create(
		server->display, &zwlr_gamma_control_manager_v1_interface, 1,
		server, manager_bind);
	return TRUE;
}

void
polo_backend_gamma_control_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->gamma_control_global, wl_global_destroy);
}
