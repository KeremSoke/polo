/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protocol-util.h"

void
polo_protocol_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

struct wl_resource *
polo_protocol_resource_new(struct wl_client *client,
			   const struct wl_interface *interface,
			   uint32_t version, uint32_t id, const void *impl,
			   void *data, wl_resource_destroy_func_t destroy)
{
	struct wl_resource *resource =
		wl_resource_create(client, interface, (int)version, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return NULL;
	}
	wl_resource_set_implementation(resource, impl, data, destroy);
	return resource;
}

struct wl_resource *
polo_protocol_child_new(struct wl_resource *parent,
			const struct wl_interface *interface, uint32_t id,
			const void *impl, void *data,
			wl_resource_destroy_func_t destroy)
{
	return polo_protocol_resource_new(
		wl_resource_get_client(parent), interface,
		(uint32_t)wl_resource_get_version(parent), id, impl, data,
		destroy);
}

static void
surface_ref_destroyed(struct wl_listener *listener, void *data)
{
	struct polo_surface_ref *ref = wl_container_of(listener, ref, listener);

	wl_list_remove(&ref->listener.link);
	if (ref->gone)
		ref->gone(ref);
	ref->surface = NULL;
}

void
polo_surface_ref_set(struct polo_surface_ref *ref,
		     struct weston_surface *surface,
		     void (*gone)(struct polo_surface_ref *ref))
{
	polo_surface_ref_clear(ref);
	ref->gone = gone;
	ref->surface = surface;
	if (!surface)
		return;
	ref->listener.notify = surface_ref_destroyed;
	wl_signal_add(&surface->destroy_signal, &ref->listener);
}

void
polo_surface_ref_clear(struct polo_surface_ref *ref)
{
	if (ref->surface)
		wl_list_remove(&ref->listener.link);
	ref->surface = NULL;
}

static void
resource_ref_destroyed(struct wl_listener *listener, void *data)
{
	struct polo_resource_ref *ref =
		wl_container_of(listener, ref, listener);

	wl_list_remove(&ref->listener.link);
	if (ref->gone)
		ref->gone(ref);
	ref->resource = NULL;
}

void
polo_resource_ref_set(struct polo_resource_ref *ref,
		      struct wl_resource *resource,
		      void (*gone)(struct polo_resource_ref *ref))
{
	polo_resource_ref_clear(ref);
	ref->gone = gone;
	ref->resource = resource;
	if (!resource)
		return;
	ref->listener.notify = resource_ref_destroyed;
	wl_resource_add_destroy_listener(resource, &ref->listener);
}

void
polo_resource_ref_clear(struct polo_resource_ref *ref)
{
	if (ref->resource)
		wl_list_remove(&ref->listener.link);
	ref->resource = NULL;
}

PoloBackendToplevel *
polo_protocol_toplevel_from_surface(struct wl_resource *surface_resource)
{
	if (!surface_resource)
		return NULL;
	return polo_backend_toplevel_from_surface(
		weston_surface_get_main_surface(
			wl_resource_get_user_data(surface_resource)));
}
