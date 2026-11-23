/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wp_fractional_scale_manager_v1. Outputs only have integer scales, so a
 * surface's preferred scale is the largest scale of the outputs it is on, or
 * of the first output before it is on any. It is sent when the object is
 * created and again after a commit that finds a different one.
 */

#include "fractional-scale-v1-server-protocol.h"
#include "protocol-util.h"

struct fractional_scale {
	struct wl_list link;
	struct wl_resource *resource;
	struct polo_surface_ref surface;
	struct wl_listener commit;
	uint32_t sent;
};

static uint32_t
preferred_scale(struct weston_surface *surface)
{
	struct weston_output *output;
	int32_t scale = 0;

	wl_list_for_each(output, &surface->compositor->output_list, link)
	{
		if (surface->output_mask & (1u << output->id))
			scale = MAX(scale, output->current_scale);
	}
	if (scale == 0 && !wl_list_empty(&surface->compositor->output_list)) {
		output = wl_container_of(surface->compositor->output_list.next,
					 output, link);
		scale = output->current_scale;
	}
	return (uint32_t)MAX(scale, 1) * 120;
}

static void
send_scale(struct fractional_scale *fs)
{
	uint32_t scale;

	if (!fs->surface.surface)
		return;
	scale = preferred_scale(fs->surface.surface);
	if (scale == fs->sent)
		return;
	fs->sent = scale;
	wp_fractional_scale_v1_send_preferred_scale(fs->resource, scale);
}

static void
surface_committed(struct wl_listener *listener, void *data)
{
	struct fractional_scale *fs = wl_container_of(listener, fs, commit);

	(void)data;
	send_scale(fs);
}

static void
stop_watching(struct fractional_scale *fs)
{
	if (fs->commit.notify) {
		wl_list_remove(&fs->commit.link);
		fs->commit.notify = NULL;
	}
}

static void
surface_gone(struct polo_surface_ref *ref)
{
	struct fractional_scale *fs = wl_container_of(ref, fs, surface);

	stop_watching(fs);
}

static const struct wp_fractional_scale_v1_interface fractional_scale_impl = {
	.destroy = polo_protocol_destroy,
};

static void
fractional_scale_resource_destroyed(struct wl_resource *resource)
{
	struct fractional_scale *fs = wl_resource_get_user_data(resource);

	stop_watching(fs);
	polo_surface_ref_clear(&fs->surface);
	wl_list_remove(&fs->link);
	g_free(fs);
}

static void
manager_get_fractional_scale(struct wl_client *client,
			     struct wl_resource *resource, uint32_t id,
			     struct wl_resource *surface_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct fractional_scale *fs;

	(void)client;
	wl_list_for_each(fs, &server->fractional_scales, link)
	{
		if (fs->surface.surface == surface) {
			wl_resource_post_error(
				resource,
				WP_FRACTIONAL_SCALE_MANAGER_V1_ERROR_FRACTIONAL_SCALE_EXISTS,
				"the surface already has a fractional scale "
				"object");
			return;
		}
	}

	fs = g_new0(struct fractional_scale, 1);
	fs->resource = polo_protocol_child_new(
		resource, &wp_fractional_scale_v1_interface, id,
		&fractional_scale_impl, fs,
		fractional_scale_resource_destroyed);
	if (!fs->resource) {
		g_free(fs);
		return;
	}
	wl_list_insert(&server->fractional_scales, &fs->link);
	polo_surface_ref_set(&fs->surface, surface, surface_gone);
	fs->commit.notify = surface_committed;
	wl_signal_add(&surface->commit_signal, &fs->commit);
	send_scale(fs);
}

static const struct wp_fractional_scale_manager_v1_interface manager_impl = {
	.destroy = polo_protocol_destroy,
	.get_fractional_scale = manager_get_fractional_scale,
};

POLO_PROTOCOL_BIND(manager_bind, &wp_fractional_scale_manager_v1_interface, 1,
		   &manager_impl)

gboolean
polo_backend_fractional_scale_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	wl_list_init(&server->fractional_scales);
	server->fractional_scale_global = wl_global_create(
		server->display, &wp_fractional_scale_manager_v1_interface, 1,
		server, manager_bind);
	return TRUE;
}

void
polo_backend_fractional_scale_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->fractional_scale_global, wl_global_destroy);
}
