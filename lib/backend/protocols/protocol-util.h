/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What every protocol shim does the same way: destructor requests, creating
 * resources (no_memory on failure), manager binds clamped to the version
 * Polo implements, references to a surface or resource that clear
 * themselves when it goes away, and the toplevel behind a wl_surface.
 */

#ifndef POLO_PROTOCOL_UTIL_H
#define POLO_PROTOCOL_UTIL_H

#include <libweston/libweston.h>

#include "polo-backend-private.h"

void
polo_protocol_destroy(struct wl_client *client, struct wl_resource *resource);

struct wl_resource *
polo_protocol_resource_new(struct wl_client *client,
			   const struct wl_interface *interface,
			   uint32_t version, uint32_t id, const void *impl,
			   void *data, wl_resource_destroy_func_t destroy);

struct wl_resource *
polo_protocol_child_new(struct wl_resource *parent,
			const struct wl_interface *interface, uint32_t id,
			const void *impl, void *data,
			wl_resource_destroy_func_t destroy);

#define POLO_PROTOCOL_BIND(name, interface, max_version, impl)                 \
	static void name(struct wl_client *client, void *data,                 \
			 uint32_t version, uint32_t id)                        \
	{                                                                      \
		polo_protocol_resource_new(client, (interface),                \
					   MIN(version, (max_version)), id,    \
					   (impl), data, NULL);                \
	}

struct polo_surface_ref {
	struct weston_surface *surface;
	struct wl_listener listener;
	void (*gone)(struct polo_surface_ref *ref);
};

void
polo_surface_ref_set(struct polo_surface_ref *ref,
		     struct weston_surface *surface,
		     void (*gone)(struct polo_surface_ref *ref));

void
polo_surface_ref_clear(struct polo_surface_ref *ref);

struct polo_resource_ref {
	struct wl_resource *resource;
	struct wl_listener listener;
	void (*gone)(struct polo_resource_ref *ref);
};

void
polo_resource_ref_set(struct polo_resource_ref *ref,
		      struct wl_resource *resource,
		      void (*gone)(struct polo_resource_ref *ref));

void
polo_resource_ref_clear(struct polo_resource_ref *ref);

PoloBackendToplevel *
polo_protocol_toplevel_from_surface(struct wl_resource *surface_resource);

#endif
