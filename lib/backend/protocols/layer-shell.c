/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwlr_layer_shell_v1 (v5) shim.
 *
 * Protocol mechanics live here: double-buffered state applied at
 * wl_surface.commit, configure serials and acks, role assignment,
 * validation errors, and get_popup. The Vala arranger decides placement,
 * exclusive zones, keyboard focus, and output assignment through the
 * PoloBackendLayerSurface handle.
 *
 * Layer surfaces are plain weston_surfaces with the role
 * "zwlr_layer_surface_v1"; they do not go through libweston-desktop. Their
 * xdg_popup children arrive through get_popup, which lets xdg_popups be
 * created with a NULL parent, placed at their raw positioner geometry in
 * global coordinates. Polo treats that geometry as an offset from the
 * layer surface's view and re-parents the popup view into the layer
 * surface's layer on every commit.
 */

#include <libweston/libweston.h>
#include <libweston/desktop.h>

#include "polo-backend-private.h"
#include "wlr-layer-shell-unstable-v1-server-protocol.h"

#define LAYER_SURFACE_ROLE "zwlr_layer_surface_v1"

#define ANCHOR_TOP ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
#define ANCHOR_BOTTOM ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
#define ANCHOR_LEFT ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
#define ANCHOR_RIGHT ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
#define ANCHOR_ALL (ANCHOR_TOP | ANCHOR_BOTTOM | ANCHOR_LEFT | ANCHOR_RIGHT)

/* An xdg_popup attached to a layer surface */
struct polo_layer_popup {
	struct polo_layer_surface *parent;
	struct weston_desktop_surface *desktop_surface;
	struct weston_surface *surface;
	struct weston_view *view;
	PoloBackendView *view_handle;

	struct wl_listener commit_listener;
	struct wl_listener surface_destroy_listener;
	struct wl_listener view_destroy_listener;

	struct wl_list link;
	double offset_x, offset_y; /* raw positioner geometry */
	bool released;
};

struct polo_layer_surface {
	PoloBackendServer *server;
	struct wl_resource *resource;
	struct weston_surface *surface;
	struct weston_view *view;
	PoloBackendView *view_handle;
	PoloBackendLayerSurface *handle; /* unowned */
	struct wl_listener surface_destroy_listener;
	struct wl_listener commit_listener;

	/* Pending, double-buffered state (applied at wl_surface.commit). */
	struct {
		bool layer_set;
		uint32_t layer;
		bool anchor_set;
		uint32_t anchor;
		bool exclusive_zone_set;
		int32_t exclusive_zone;
		bool exclusive_edge_set;
		uint32_t exclusive_edge;
		bool margin_set;
		int32_t margin[4]; /* top, right, bottom, left */
		bool keyboard_interactivity_set;
		uint32_t keyboard_interactivity;
		bool size_set;
		uint32_t width, height;
	} pending;

	/* Committed state, visible to Vala through the handle's properties. */
	uint32_t layer;
	uint32_t anchor;
	int32_t exclusive_zone;
	uint32_t exclusive_edge;
	int32_t margin[4];
	uint32_t keyboard_interactivity;
	uint32_t width, height; /* 0: unspecified, compositor decides */

	struct wl_list popup_list;
	bool initialized;
	bool configured;                /* a configure was acked */
	bool initial_configure_pending; /* initial commit not answered yet */
	struct wl_array serials; /* unacked configure serials, oldest first */
	uint32_t configured_width, configured_height; /* last configure sent */
	bool closed;
	bool dead;                 /* teardown has run */
	PoloBackendOutput *output; /* unowned, set by Vala */
	PoloBackendOutput
		*requested_output; /* unowned, from get_layer_surface */
};

enum {
	SIGNAL_COMMITTED,
	SIGNAL_NEW_POPUP,
	SIGNAL_DESTROYED,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

struct _PoloBackendLayerSurface {
	GObject parent_instance;

	PoloBackendServer *server;
	struct polo_layer_surface *impl; /* NULL once gone */
	PoloBackendView *view;           /* the layer surface's own view */
	char *namespace_;
};

static void
layer_surface_teardown(struct polo_layer_surface *impl);

static void
layer_popup_try_release(struct polo_layer_popup *popup)
{
	if (!popup->released && popup->view == NULL &&
	    popup->desktop_surface == NULL) {
		popup->released = true;
		wl_list_remove(&popup->link);
		free(popup);
	}
}

static void
layer_popup_reposition(struct polo_layer_popup *popup)
{
	struct weston_view *view = popup->view;
	struct weston_view *parent_view =
		popup->parent ? popup->parent->view : NULL;
	struct weston_layer_entry *layer = NULL;
	struct weston_layer *menus;

	if (!view || !parent_view)
		return;

	weston_view_set_position(
		view,
		(struct weston_coord_global){
			.c = weston_coord(parent_view->geometry.pos_offset.x +
						  popup->offset_x,
					  parent_view->geometry.pos_offset.y +
						  popup->offset_y) });
	weston_view_update_transform(view);

	/* Above windows like an X menu, or in the parent's layer if higher */
	menus = &polo_backend_server_get_layer(
			 popup->parent->server,
			 POLO_BACKEND_LAYER_OVERRIDE_REDIRECT)
			 ->layer;
	if (parent_view->layer_link.layer)
		layer = parent_view->layer_link.layer->position >
				menus->position
			? &parent_view->layer_link.layer->view_list
			: &menus->view_list;
	weston_view_move_to_layer(view, layer);
}

/* Position as the offset from the layer surface and re-anchor the popup */
static void
layer_popup_committed(struct wl_listener *listener, void *data)
{
	struct polo_layer_popup *popup =
		wl_container_of(listener, popup, commit_listener);

	if (!popup->view || !popup->parent || !popup->parent->view)
		return;

	popup->offset_x = popup->view->geometry.pos_offset.x;
	popup->offset_y = popup->view->geometry.pos_offset.y;
	layer_popup_reposition(popup);
}

static void
layer_popup_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct polo_layer_popup *popup =
		wl_container_of(listener, popup, surface_destroy_listener);

	wl_list_remove(&popup->commit_listener.link);
	wl_list_remove(&popup->surface_destroy_listener.link);
	popup->surface = NULL;
	popup->desktop_surface = NULL;
	layer_popup_try_release(popup);
}

static void
layer_popup_view_destroyed(struct wl_listener *listener, void *data)
{
	struct polo_layer_popup *popup =
		wl_container_of(listener, popup, view_destroy_listener);

	wl_list_remove(&popup->view_destroy_listener.link);
	popup->view = NULL;
	g_clear_object(&popup->view_handle); /* already destroyed by view.c */
	layer_popup_try_release(popup);
}

static void
layer_popup_create(struct polo_layer_surface *impl,
		   struct weston_desktop_surface *desktop_surface)
{
	struct polo_layer_popup *popup = zalloc(sizeof *popup);
	struct weston_surface *surface =
		weston_desktop_surface_get_surface(desktop_surface);

	popup->parent = impl;
	popup->desktop_surface = desktop_surface;
	popup->surface = surface;
	popup->view = weston_desktop_surface_create_view(desktop_surface);
	if (popup->view)
		popup->view_handle =
			polo_backend_view_new(impl->server, popup->view, NULL);

	popup->commit_listener.notify = layer_popup_committed;
	wl_signal_add(&surface->commit_signal, &popup->commit_listener);
	popup->surface_destroy_listener.notify = layer_popup_surface_destroyed;
	wl_signal_add(&surface->destroy_signal,
		      &popup->surface_destroy_listener);
	if (popup->view) {
		popup->view_destroy_listener.notify =
			layer_popup_view_destroyed;
		wl_signal_add(&popup->view->destroy_signal,
			      &popup->view_destroy_listener);
	}

	wl_list_insert(&impl->popup_list, &popup->link);
	g_signal_emit(impl->handle, signals[SIGNAL_NEW_POPUP], 0,
		      popup->view_handle);
}

/* Sends popup_done and detaches every popup; each one cleans itself up
 * when the client destroys it. */
static void
layer_surface_dismiss_popups(struct polo_layer_surface *impl)
{
	struct polo_layer_popup *popup, *tmp;

	wl_list_for_each_safe(popup, tmp, &impl->popup_list, link)
	{
		popup->parent = NULL;
		wl_list_remove(&popup->link);
		wl_list_init(&popup->link);
		weston_desktop_surface_close(popup->desktop_surface);
		layer_popup_try_release(popup);
	}
}

static void
layer_surface_reposition_popups(struct polo_layer_surface *impl)
{
	struct polo_layer_popup *popup;

	wl_list_for_each(popup, &impl->popup_list, link)
		layer_popup_reposition(popup);
}

static void
layer_surface_reset_configure(struct polo_layer_surface *impl)
{
	impl->initialized = false;
	impl->configured = false;
	impl->initial_configure_pending = false;
	impl->serials.size = 0;
}

static void
layer_surface_send_configure(struct polo_layer_surface *impl, uint32_t width,
			     uint32_t height)
{
	uint32_t serial = wl_display_next_serial(impl->server->display);
	uint32_t *slot = wl_array_add(&impl->serials, sizeof *slot);

	if (slot)
		*slot = serial;
	impl->initial_configure_pending = false;
	impl->configured_width = width;
	impl->configured_height = height;
	zwlr_layer_surface_v1_send_configure(impl->resource, serial, width,
					     height);
}

static void
layer_surface_apply_pending(struct polo_layer_surface *impl)
{
	uint32_t anchor =
		impl->pending.anchor_set ? impl->pending.anchor : impl->anchor;

	uint32_t width =
		impl->pending.size_set ? impl->pending.width : impl->width;
	uint32_t height =
		impl->pending.size_set ? impl->pending.height : impl->height;

	/* As wlroots does, on the state the commit results in: a zero
	 * width needs left and right anchors, a zero height top and
	 * bottom. */
	if ((width == 0 &&
	     (anchor & (ANCHOR_LEFT | ANCHOR_RIGHT)) !=
		     (ANCHOR_LEFT | ANCHOR_RIGHT)) ||
	    (height == 0 &&
	     (anchor & (ANCHOR_TOP | ANCHOR_BOTTOM)) !=
		     (ANCHOR_TOP | ANCHOR_BOTTOM))) {
		wl_resource_post_error(
			impl->resource,
			ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SIZE,
			"a zero size is only valid when anchored to both "
			"opposite edges on that axis");
		return;
	}

	if (impl->pending.exclusive_edge_set &&
	    !(anchor & impl->pending.exclusive_edge)) {
		wl_resource_post_error(
			impl->resource,
			ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_EXCLUSIVE_EDGE,
			"exclusive edge is not part of the anchor");
		return;
	}

	if (impl->pending.layer_set)
		impl->layer = impl->pending.layer;
	if (impl->pending.anchor_set)
		impl->anchor = impl->pending.anchor;
	if (impl->pending.exclusive_zone_set)
		impl->exclusive_zone = impl->pending.exclusive_zone;
	if (impl->pending.exclusive_edge_set)
		impl->exclusive_edge = impl->pending.exclusive_edge;
	if (impl->pending.margin_set)
		memcpy(impl->margin, impl->pending.margin, sizeof impl->margin);
	if (impl->pending.keyboard_interactivity_set)
		impl->keyboard_interactivity =
			impl->pending.keyboard_interactivity;
	if (impl->pending.size_set) {
		impl->width = impl->pending.width;
		impl->height = impl->pending.height;
	}

	memset(&impl->pending, 0, sizeof impl->pending);
}

static void
layer_surface_committed(struct weston_surface *surface,
			struct weston_coord_surface new_origin)
{
	(void)surface;
	(void)new_origin;
}

static void
layer_surface_commit_state(struct polo_layer_surface *impl)
{
	struct weston_surface *surface = impl->surface;

	if (impl->closed)
		return;

	if (surface->buffer_ref.buffer) {
		if (!impl->configured) {
			wl_resource_post_error(
				impl->resource,
				ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
				"committed a buffer before acking a configure");
			return;
		}
		layer_surface_apply_pending(impl);
		if (!weston_surface_is_mapped(surface))
			weston_surface_map(surface);
	} else if (weston_surface_is_unmapping(surface)) {
		layer_surface_dismiss_popups(impl);
		layer_surface_apply_pending(impl);
		layer_surface_reset_configure(impl);
	} else {
		layer_surface_apply_pending(impl);
		if (!impl->initialized) {
			impl->initialized = true;
			impl->initial_configure_pending = true;
		}
	}

	g_signal_emit(impl->handle, signals[SIGNAL_COMMITTED], 0);
	if (impl->initial_configure_pending && !impl->closed)
		layer_surface_send_configure(impl, impl->width, impl->height);
	layer_surface_reposition_popups(impl);
}

static void
layer_surface_commit_notify(struct wl_listener *listener, void *data)
{
	struct polo_layer_surface *impl =
		wl_container_of(listener, impl, commit_listener);

	(void)data;
	layer_surface_commit_state(impl);
}

static void
layer_surface_protocol_set_size(struct wl_client *client,
				struct wl_resource *resource, uint32_t width,
				uint32_t height)
{
	struct polo_layer_surface *impl = wl_resource_get_user_data(resource);

	if (impl->closed)
		return;
	impl->pending.size_set = true;
	impl->pending.width = width;
	impl->pending.height = height;
}

static void
layer_surface_protocol_set_anchor(struct wl_client *client,
				  struct wl_resource *resource, uint32_t anchor)
{
	struct polo_layer_surface *impl = wl_resource_get_user_data(resource);

	if (impl->closed)
		return;
	if (anchor & ~ANCHOR_ALL) {
		wl_resource_post_error(
			resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_ANCHOR,
			"invalid anchor bits %x", anchor);
		return;
	}
	impl->pending.anchor_set = true;
	impl->pending.anchor = anchor;
}

static void
layer_surface_protocol_set_exclusive_zone(struct wl_client *client,
					  struct wl_resource *resource,
					  int32_t zone)
{
	struct polo_layer_surface *impl = wl_resource_get_user_data(resource);

	if (impl->closed)
		return;
	impl->pending.exclusive_zone_set = true;
	impl->pending.exclusive_zone = zone;
}

static void
layer_surface_protocol_set_margin(struct wl_client *client,
				  struct wl_resource *resource, int32_t top,
				  int32_t right, int32_t bottom, int32_t left)
{
	struct polo_layer_surface *impl = wl_resource_get_user_data(resource);

	if (impl->closed)
		return;
	impl->pending.margin_set = true;
	impl->pending.margin[0] = top;
	impl->pending.margin[1] = right;
	impl->pending.margin[2] = bottom;
	impl->pending.margin[3] = left;
}

static void
layer_surface_protocol_set_keyboard_interactivity(struct wl_client *client,
						  struct wl_resource *resource,
						  uint32_t interactivity)
{
	struct polo_layer_surface *impl = wl_resource_get_user_data(resource);

	if (impl->closed)
		return;
	if (interactivity >
	    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND) {
		wl_resource_post_error(
			resource,
			ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_KEYBOARD_INTERACTIVITY,
			"invalid keyboard interactivity %u", interactivity);
		return;
	}
	impl->pending.keyboard_interactivity_set = true;
	impl->pending.keyboard_interactivity = interactivity;
}

static void
layer_surface_protocol_get_popup(struct wl_client *client,
				 struct wl_resource *resource,
				 struct wl_resource *popup_resource)
{
	struct polo_layer_surface *impl = wl_resource_get_user_data(resource);
	struct weston_desktop_surface *desktop_surface;
	struct weston_surface *surface;
	struct polo_layer_popup *popup;

	if (impl->closed)
		return;

	desktop_surface = wl_resource_get_user_data(popup_resource);
	if (!desktop_surface)
		goto invalid;
	surface = weston_desktop_surface_get_surface(desktop_surface);
	if (!surface || !surface->role_name ||
	    strcmp(surface->role_name, "xdg_popup") != 0)
		goto invalid;
	/* The popup must have been created with a NULL parent and not
	 * committed yet, and it must not already belong to us. */
	if (weston_desktop_surface_get_parent(desktop_surface) != NULL ||
	    weston_surface_is_mapped(surface) ||
	    surface->buffer_ref.buffer != NULL)
		goto invalid;
	wl_list_for_each(popup, &impl->popup_list, link)
	{
		if (popup->desktop_surface == desktop_surface)
			goto invalid;
	}

	layer_popup_create(impl, desktop_surface);
	weston_desktop_surface_set_popup_constraint_parent(desktop_surface,
							   impl->surface);
	return;

invalid:
	wl_resource_post_error(
		resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
		"get_popup expects an uncommitted xdg_popup created with a "
		"NULL parent");
}

static void
layer_surface_protocol_ack_configure(struct wl_client *client,
				     struct wl_resource *resource,
				     uint32_t serial)
{
	struct polo_layer_surface *impl = wl_resource_get_user_data(resource);

	uint32_t *serials = impl->serials.data;
	size_t n = impl->serials.size / sizeof *serials, i;

	if (impl->closed)
		return;

	for (i = 0; i < n; i++) {
		if (serials[i] == serial) {
			memmove(serials, serials + i + 1,
				(n - i - 1) * sizeof *serials);
			impl->serials.size = (n - i - 1) * sizeof *serials;
			impl->configured = true;
			return;
		}
	}
}

static void
layer_surface_protocol_set_layer(struct wl_client *client,
				 struct wl_resource *resource, uint32_t layer)
{
	struct polo_layer_surface *impl = wl_resource_get_user_data(resource);

	if (impl->closed)
		return;
	if (layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
		wl_resource_post_error(resource,
				       ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER,
				       "invalid layer %u", layer);
		return;
	}
	impl->pending.layer_set = true;
	impl->pending.layer = layer;
}

static void
layer_surface_protocol_set_exclusive_edge(struct wl_client *client,
					  struct wl_resource *resource,
					  uint32_t edge)
{
	struct polo_layer_surface *impl = wl_resource_get_user_data(resource);

	if (impl->closed)
		return;
	if (edge != ANCHOR_TOP && edge != ANCHOR_BOTTOM &&
	    edge != ANCHOR_LEFT && edge != ANCHOR_RIGHT) {
		wl_resource_post_error(
			resource,
			ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_EXCLUSIVE_EDGE,
			"exclusive edge must be a single anchor bit");
		return;
	}
	impl->pending.exclusive_edge_set = true;
	impl->pending.exclusive_edge = edge;
}

/* Client-side destroy: the resource's destructor runs the teardown */
static void
layer_surface_protocol_destroy(struct wl_client *client,
			       struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwlr_layer_surface_v1_interface
	layer_surface_implementation = {
		.set_size = layer_surface_protocol_set_size,
		.set_anchor = layer_surface_protocol_set_anchor,
		.set_exclusive_zone = layer_surface_protocol_set_exclusive_zone,
		.set_margin = layer_surface_protocol_set_margin,
		.set_keyboard_interactivity =
			layer_surface_protocol_set_keyboard_interactivity,
		.get_popup = layer_surface_protocol_get_popup,
		.ack_configure = layer_surface_protocol_ack_configure,
		.destroy = layer_surface_protocol_destroy,
		.set_layer = layer_surface_protocol_set_layer,
		.set_exclusive_edge = layer_surface_protocol_set_exclusive_edge,
	};

static void
layer_surface_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct polo_layer_surface *impl =
		wl_container_of(listener, impl, surface_destroy_listener);

	wl_list_remove(&impl->commit_listener.link);
	wl_list_remove(&impl->surface_destroy_listener.link);
	impl->surface = NULL;
	wl_resource_destroy(impl->resource);
}

static void
layer_surface_teardown(struct polo_layer_surface *impl)
{
	PoloBackendLayerSurface *handle = impl->handle;

	if (impl->dead)
		return;
	impl->dead = true;

	if (impl->surface) {
		wl_list_remove(&impl->commit_listener.link);
		wl_list_remove(&impl->surface_destroy_listener.link);
		impl->surface->committed = NULL;
		impl->surface->committed_private = NULL;
	}

	layer_surface_dismiss_popups(impl);

	if (impl->view_handle)
		polo_backend_view_destroy(impl->view_handle);
	if (impl->view)
		weston_view_destroy(impl->view);
	g_clear_object(&impl->view_handle);

	if (handle) {
		impl->handle = NULL;
		handle->impl = NULL;
		handle->view = NULL;
		g_signal_emit(handle, signals[SIGNAL_DESTROYED], 0);
		g_object_unref(handle); /* the server's reference */
	}
	wl_array_release(&impl->serials);
	free(impl);
}

static void
layer_surface_resource_destroyed(struct wl_resource *resource)
{
	struct polo_layer_surface *impl = wl_resource_get_user_data(resource);

	impl->resource = NULL;
	layer_surface_teardown(impl);
}

static void
layer_shell_protocol_get_layer_surface(struct wl_client *client,
				       struct wl_resource *resource,
				       uint32_t id,
				       struct wl_resource *surface_resource,
				       struct wl_resource *output_resource,
				       uint32_t layer, const char *namespace_)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_surface *surface;
	struct polo_layer_surface *impl;
	PoloBackendLayerSurface *handle;

	if (layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
		wl_resource_post_error(resource,
				       ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER,
				       "invalid layer %u", layer);
		return;
	}

	surface = wl_resource_get_user_data(surface_resource);
	if (!surface) {
		wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_ROLE,
				       "wl_surface is gone");
		return;
	}
	if (surface->pending.buffer_ref.buffer || surface->buffer_ref.buffer) {
		wl_resource_post_error(
			resource, ZWLR_LAYER_SHELL_V1_ERROR_ALREADY_CONSTRUCTED,
			"wl_surface@%d has a buffer attached",
			wl_resource_get_id(surface_resource));
		return;
	}
	if (surface->committed != NULL ||
	    weston_surface_set_role(surface, LAYER_SURFACE_ROLE, resource,
				    ZWLR_LAYER_SHELL_V1_ERROR_ROLE) < 0) {
		wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_ROLE,
				       "wl_surface@%d already has a role",
				       wl_resource_get_id(surface_resource));
		return;
	}

	impl = zalloc(sizeof *impl);
	impl->server = server;
	impl->layer = layer;
	impl->anchor = 0;
	impl->exclusive_zone = 0;
	impl->exclusive_edge = 0;
	impl->keyboard_interactivity = 0;
	wl_list_init(&impl->popup_list);
	wl_array_init(&impl->serials);

	impl->resource =
		wl_resource_create(client, &zwlr_layer_surface_v1_interface,
				   wl_resource_get_version(resource), id);
	if (!impl->resource) {
		wl_array_release(&impl->serials);
		free(impl);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(impl->resource,
				       &layer_surface_implementation, impl,
				       layer_surface_resource_destroyed);
	impl->surface = surface;
	impl->surface_destroy_listener.notify = layer_surface_surface_destroyed;
	wl_signal_add(&surface->destroy_signal,
		      &impl->surface_destroy_listener);
	impl->commit_listener.notify = layer_surface_commit_notify;
	wl_signal_add(&surface->commit_signal, &impl->commit_listener);
	surface->committed = layer_surface_committed;
	surface->committed_private = impl;

	impl->view = weston_view_create(surface);
	if (!impl->view) {
		wl_list_remove(&impl->commit_listener.link);
		wl_list_remove(&impl->surface_destroy_listener.link);
		impl->surface = NULL; /* teardown must not unhook again */
		surface->committed = NULL;
		surface->committed_private = NULL;
		wl_resource_destroy(impl->resource);
		wl_client_post_no_memory(client);
		return;
	}
	impl->view_handle = polo_backend_view_new(server, impl->view, NULL);

	impl->requested_output = output_resource
		? polo_backend_server_output_from_resource(server,
							   output_resource)
		: NULL;

	handle = polo_backend_layer_surface_new(server, impl, namespace_);
	impl->handle = handle;
	polo_backend_server_emit_new_layer_surface(server, handle);
}

static void
layer_shell_protocol_destroy(struct wl_client *client,
			     struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwlr_layer_shell_v1_interface layer_shell_implementation = {
	.get_layer_surface = layer_shell_protocol_get_layer_surface,
	.destroy = layer_shell_protocol_destroy,
};

static void
layer_shell_bind(struct wl_client *client, void *data, uint32_t version,
		 uint32_t id)
{
	struct wl_resource *resource;

	resource = wl_resource_create(client, &zwlr_layer_shell_v1_interface,
				      version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &layer_shell_implementation,
				       data, NULL);
}

gboolean
polo_backend_layer_shell_init(PoloBackendServer *server, GError **error)
{
	server->layer_shell_global = wl_global_create(
		server->display, &zwlr_layer_shell_v1_interface, 5, server,
		layer_shell_bind);
	return TRUE;
}

void
polo_backend_layer_shell_fini(PoloBackendServer *server)
{
	if (server->layer_shell_global) {
		wl_global_destroy(server->layer_shell_global);
		server->layer_shell_global = NULL;
	}
}

G_DEFINE_FINAL_TYPE(PoloBackendLayerSurface, polo_backend_layer_surface,
		    G_TYPE_OBJECT)

PoloBackendLayerSurface *
polo_backend_layer_surface_new(PoloBackendServer *server,
			       struct polo_layer_surface *impl,
			       const char *namespace_)
{
	PoloBackendLayerSurface *self =
		g_object_new(POLO_BACKEND_TYPE_LAYER_SURFACE, NULL);

	self->server = server;
	self->impl = impl;
	self->view = impl->view_handle;
	self->namespace_ = g_strdup(namespace_ ? namespace_ : "");
	return self;
}

static void
polo_backend_layer_surface_dispose(GObject *object)
{
	PoloBackendLayerSurface *self = POLO_BACKEND_LAYER_SURFACE(object);

	if (self->impl)
		self->impl->handle = NULL;
	G_OBJECT_CLASS(polo_backend_layer_surface_parent_class)
		->dispose(object);
}

static void
polo_backend_layer_surface_finalize(GObject *object)
{
	PoloBackendLayerSurface *self = POLO_BACKEND_LAYER_SURFACE(object);

	g_free(self->namespace_);
	G_OBJECT_CLASS(polo_backend_layer_surface_parent_class)
		->finalize(object);
}

/* State is read through the explicit getters below (the vapi binds each
 * property to one, as with PoloBackendToplevel) there are no GObject
 * properties */
static void
polo_backend_layer_surface_class_init(PoloBackendLayerSurfaceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->dispose = polo_backend_layer_surface_dispose;
	object_class->finalize = polo_backend_layer_surface_finalize;

	signals[SIGNAL_COMMITTED] = g_signal_new(
		"committed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_NEW_POPUP] = g_signal_new(
		"new-popup", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 1, POLO_BACKEND_TYPE_VIEW);
	signals[SIGNAL_DESTROYED] = g_signal_new(
		"destroyed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_backend_layer_surface_init(PoloBackendLayerSurface *self)
{
}

PoloBackendLayerSurface *
polo_backend_layer_surface_from_surface(struct weston_surface *surface)
{
	struct polo_layer_surface *impl;

	if (!surface || surface->committed != layer_surface_committed)
		return NULL;
	impl = surface->committed_private;
	return impl ? impl->handle : NULL;
}

PoloBackendView *
polo_backend_layer_surface_get_view(PoloBackendLayerSurface *self)
{
	g_return_val_if_fail(POLO_BACKEND_IS_LAYER_SURFACE(self), NULL);
	return self->view;
}

const char *
polo_backend_layer_surface_get_namespace(PoloBackendLayerSurface *self)
{
	g_return_val_if_fail(POLO_BACKEND_IS_LAYER_SURFACE(self), NULL);
	return self->namespace_;
}

PoloBackendOutput *
polo_backend_layer_surface_get_output(PoloBackendLayerSurface *self)
{
	g_return_val_if_fail(POLO_BACKEND_IS_LAYER_SURFACE(self), NULL);
	return self->impl ? self->impl->output : NULL;
}

void
polo_backend_layer_surface_set_output(PoloBackendLayerSurface *self,
				      PoloBackendOutput *output)
{
	g_return_if_fail(POLO_BACKEND_IS_LAYER_SURFACE(self));
	if (!self->impl)
		return;
	self->impl->output = output;
}

void
polo_backend_layer_surface_configure(PoloBackendLayerSurface *self,
				     guint32 width, guint32 height)
{
	struct polo_layer_surface *impl;

	g_return_if_fail(POLO_BACKEND_IS_LAYER_SURFACE(self));
	impl = self->impl;
	if (!impl || impl->closed)
		return;

	if (!impl->initialized)
		return;
	if (!impl->initial_configure_pending &&
	    width == impl->configured_width &&
	    height == impl->configured_height)
		return;

	layer_surface_send_configure(impl, width, height);
}

void
polo_backend_layer_surface_close(PoloBackendLayerSurface *self)
{
	struct polo_layer_surface *impl;

	g_return_if_fail(POLO_BACKEND_IS_LAYER_SURFACE(self));
	impl = self->impl;
	if (!impl || impl->closed)
		return;

	impl->closed = true;
	if (weston_surface_is_mapped(impl->surface))
		weston_surface_unmap(impl->surface);
	layer_surface_dismiss_popups(impl);
	zwlr_layer_surface_v1_send_closed(impl->resource);
}

#define LAYER_SURFACE_GETTER(ctype, name, field, default_)                     \
	ctype polo_backend_layer_surface_get_##name(                           \
		PoloBackendLayerSurface *self)                                 \
	{                                                                      \
		g_return_val_if_fail(POLO_BACKEND_IS_LAYER_SURFACE(self),      \
				     default_);                                \
		return (ctype)(self->impl ? self->impl->field : (default_));   \
	}

LAYER_SURFACE_GETTER(guint, layer, layer, 0)
LAYER_SURFACE_GETTER(guint, anchor, anchor, 0)
LAYER_SURFACE_GETTER(int, exclusive_zone, exclusive_zone, 0)
LAYER_SURFACE_GETTER(guint, exclusive_edge, exclusive_edge, 0)
LAYER_SURFACE_GETTER(int, margin_top, margin[0], 0)
LAYER_SURFACE_GETTER(int, margin_right, margin[1], 0)
LAYER_SURFACE_GETTER(int, margin_bottom, margin[2], 0)
LAYER_SURFACE_GETTER(int, margin_left, margin[3], 0)
LAYER_SURFACE_GETTER(guint, keyboard_interactivity, keyboard_interactivity, 0)
LAYER_SURFACE_GETTER(guint, desired_width, width, 0)
LAYER_SURFACE_GETTER(guint, desired_height, height, 0)

gboolean
polo_backend_layer_surface_get_mapped(PoloBackendLayerSurface *self)
{
	g_return_val_if_fail(POLO_BACKEND_IS_LAYER_SURFACE(self), FALSE);
	return self->impl && self->impl->surface &&
		weston_surface_is_mapped(self->impl->surface);
}

PoloBackendOutput *
polo_backend_layer_surface_get_requested_output(PoloBackendLayerSurface *self)
{
	g_return_val_if_fail(POLO_BACKEND_IS_LAYER_SURFACE(self), NULL);
	return self->impl ? self->impl->requested_output : NULL;
}

gboolean
polo_backend_layer_surface_is_mapped(PoloBackendLayerSurface *self)
{
	return polo_backend_layer_surface_get_mapped(self);
}

gboolean
polo_backend_layer_surface_owns_view(PoloBackendLayerSurface *self,
				     PoloBackendView *view)
{
	struct weston_view *wv;
	struct weston_surface *surface;
	struct weston_desktop_surface *dsurface;
	struct polo_layer_popup *popup;

	g_return_val_if_fail(POLO_BACKEND_IS_LAYER_SURFACE(self), FALSE);
	if (!view || !self->impl || !self->impl->surface)
		return FALSE;
	wv = polo_backend_view_get_weston_view(view);
	if (!wv)
		return FALSE;
	surface = weston_surface_get_main_surface(wv->surface);
	if (surface == self->impl->surface)
		return TRUE;
	dsurface = weston_surface_get_desktop_surface(surface);
	while (dsurface && weston_desktop_surface_get_parent(dsurface) != NULL)
		dsurface = weston_desktop_surface_get_parent(dsurface);
	if (!dsurface)
		return FALSE;
	wl_list_for_each(popup, &self->impl->popup_list, link)
	{
		if (popup->desktop_surface == dsurface)
			return TRUE;
	}
	return FALSE;
}

void
polo_backend_layer_surface_get_size(PoloBackendLayerSurface *self, int *width,
				    int *height)
{
	g_return_if_fail(POLO_BACKEND_IS_LAYER_SURFACE(self));
	if (!self->impl || !self->impl->surface) {
		*width = *height = 0;
		return;
	}
	*width = self->impl->surface->width;
	*height = self->impl->surface->height;
}
