/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwp_primary_selection_device_manager_v1, which libweston does not have. A
 * selection is a weston_data_source, as for the clipboard, so the XWM can hold
 * one too. Like the clipboard, it goes to the client with keyboard focus;
 * unlike libweston's clipboard, only that client can set it.
 */

#include <string.h>
#include <unistd.h>

#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "primary-selection-unstable-v1-server-protocol.h"

struct polo_primary {
	PoloBackendServer *server;
	struct weston_seat *seat;
	struct wl_list devices;
	struct wl_list offers;
	struct weston_data_source *source;
	uint32_t serial;
	struct wl_listener source_destroy;
	struct weston_keyboard *keyboard;
	struct wl_listener focus;
	struct wl_client *focus_client;
};

struct primary_offer {
	struct wl_resource *resource;
	struct weston_data_source *source;
	struct wl_list link;
};

struct client_source {
	struct weston_data_source base;
};

static void
offer_receive(struct wl_client *client, struct wl_resource *resource,
	      const char *mime_type, int32_t fd)
{
	struct primary_offer *offer = wl_resource_get_user_data(resource);

	if (offer->source)
		offer->source->send(offer->source, mime_type, fd);
	else
		close(fd);
}

static void
offer_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_primary_selection_offer_v1_interface offer_impl = {
	.receive = offer_receive,
	.destroy = offer_destroy,
};

static void
offer_resource_destroyed(struct wl_resource *resource)
{
	struct primary_offer *offer = wl_resource_get_user_data(resource);

	wl_list_remove(&offer->link);
	g_free(offer);
}

static void
send_selection_to_device(struct polo_primary *primary,
			 struct wl_resource *device)
{
	struct weston_data_source *source = primary->source;
	struct wl_resource *resource = NULL;

	if (source) {
		struct primary_offer *offer = g_new0(struct primary_offer, 1);
		char **mime;

		resource = wl_resource_create(
			wl_resource_get_client(device),
			&zwp_primary_selection_offer_v1_interface, 1, 0);
		if (!resource) {
			g_free(offer);
			wl_client_post_no_memory(
				wl_resource_get_client(device));
			return;
		}
		offer->resource = resource;
		offer->source = source;
		wl_list_insert(&primary->offers, &offer->link);
		wl_resource_set_implementation(resource, &offer_impl, offer,
					       offer_resource_destroyed);
		zwp_primary_selection_device_v1_send_data_offer(device,
								resource);
		wl_array_for_each(mime, &source->mime_types)
			zwp_primary_selection_offer_v1_send_offer(resource,
								  *mime);
	}
	zwp_primary_selection_device_v1_send_selection(device, resource);
}

static void
send_selection_to_client(struct polo_primary *primary, struct wl_client *client)
{
	struct wl_resource *device;

	if (!client)
		return;
	wl_resource_for_each(device, &primary->devices)
	{
		if (wl_resource_get_client(device) == client)
			send_selection_to_device(primary, device);
	}
}

static void
selection_changed(struct polo_primary *primary)
{
	struct primary_offer *offer;

	wl_list_for_each(offer, &primary->offers, link) offer->source = NULL;
	send_selection_to_client(primary, primary->focus_client);
	wl_signal_emit(&primary->server->primary_selection_signal,
		       primary->seat);
}

static void
source_destroyed(struct wl_listener *listener, void *data)
{
	struct polo_primary *primary =
		wl_container_of(listener, primary, source_destroy);

	wl_list_remove(&primary->source_destroy.link);
	primary->source = NULL;
	selection_changed(primary);
}

static void
primary_set(struct polo_primary *primary, struct weston_data_source *source,
	    uint32_t serial)
{
	struct weston_data_source *old = primary->source;

	if (source == old)
		return;
	if (old) {
		wl_list_remove(&primary->source_destroy.link);
		primary->source = NULL;
		old->cancel(old);
	}
	primary->source = source;
	primary->serial = serial;
	if (source) {
		primary->source_destroy.notify = source_destroyed;
		wl_signal_add(&source->destroy_signal,
			      &primary->source_destroy);
	}
	selection_changed(primary);
}

static void
focus_changed(struct wl_listener *listener, void *data)
{
	struct polo_primary *primary =
		wl_container_of(listener, primary, focus);
	struct weston_surface *surface = primary->keyboard->focus;
	struct wl_client *client = surface && surface->resource
		? wl_resource_get_client(surface->resource)
		: NULL;

	if (client == primary->focus_client)
		return;
	primary->focus_client = client;
	send_selection_to_client(primary, client);
}

static void
device_set_selection(struct wl_client *client, struct wl_resource *resource,
		     struct wl_resource *source_resource, uint32_t serial)
{
	struct polo_primary *primary = wl_resource_get_user_data(resource);

	if (!primary || client != primary->focus_client)
		return;
	if (primary->source && serial - primary->serial > UINT32_MAX / 2)
		return;
	primary_set(primary,
		    source_resource ? wl_resource_get_user_data(source_resource)
				    : NULL,
		    serial);
}

static void
device_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_primary_selection_device_v1_interface device_impl = {
	.set_selection = device_set_selection,
	.destroy = device_destroy,
};

static void
device_resource_destroyed(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
client_source_send(struct weston_data_source *source, const char *mime_type,
		   int32_t fd)
{
	zwp_primary_selection_source_v1_send_send(source->resource, mime_type,
						  fd);
	close(fd);
}

static void
client_source_cancel(struct weston_data_source *source)
{
	zwp_primary_selection_source_v1_send_cancelled(source->resource);
}

static void
client_source_accept(struct weston_data_source *source, uint32_t serial,
		     const char *mime_type)
{
}

static void
source_offer(struct wl_client *client, struct wl_resource *resource,
	     const char *mime_type)
{
	struct client_source *source = wl_resource_get_user_data(resource);
	char **mime = wl_array_add(&source->base.mime_types, sizeof *mime);

	if (mime)
		*mime = g_strdup(mime_type);
}

static void
source_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_primary_selection_source_v1_interface source_impl = {
	.offer = source_offer,
	.destroy = source_destroy,
};

static void
source_resource_destroyed(struct wl_resource *resource)
{
	struct client_source *source = wl_resource_get_user_data(resource);
	char **mime;

	wl_signal_emit(&source->base.destroy_signal, &source->base);
	wl_array_for_each(mime, &source->base.mime_types) g_free(*mime);
	wl_array_release(&source->base.mime_types);
	g_free(source);
}

static void
manager_create_source(struct wl_client *client, struct wl_resource *resource,
		      uint32_t id)
{
	struct client_source *source = g_new0(struct client_source, 1);

	source->base.resource = wl_resource_create(
		client, &zwp_primary_selection_source_v1_interface, 1, id);
	if (!source->base.resource) {
		g_free(source);
		wl_client_post_no_memory(client);
		return;
	}
	wl_signal_init(&source->base.destroy_signal);
	wl_array_init(&source->base.mime_types);
	source->base.accept = client_source_accept;
	source->base.send = client_source_send;
	source->base.cancel = client_source_cancel;
	wl_resource_set_implementation(source->base.resource, &source_impl,
				       source, source_resource_destroyed);
}

static void
manager_get_device(struct wl_client *client, struct wl_resource *resource,
		   uint32_t id, struct wl_resource *seat_resource)
{
	PoloBackendServer *server = wl_resource_get_user_data(resource);
	struct weston_seat *weston_seat =
		wl_resource_get_user_data(seat_resource);
	PoloBackendSeat *seat = weston_seat
		? polo_backend_server_lookup_seat(server, weston_seat)
		: NULL;
	struct polo_primary *primary =
		seat ? polo_backend_seat_get_primary(seat) : NULL;
	struct wl_resource *device = wl_resource_create(
		client, &zwp_primary_selection_device_v1_interface, 1, id);

	if (!device) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(device, &device_impl, primary,
				       device_resource_destroyed);
	if (!primary) {
		wl_list_init(wl_resource_get_link(device));
		return;
	}
	wl_list_insert(&primary->devices, wl_resource_get_link(device));
	if (client == primary->focus_client)
		send_selection_to_device(primary, device);
}

static void
manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_primary_selection_device_manager_v1_interface
	manager_impl = {
		.create_source = manager_create_source,
		.get_device = manager_get_device,
		.destroy = manager_destroy,
	};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &zwp_primary_selection_device_manager_v1_interface, 1,
		id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

struct polo_primary *
polo_primary_new(PoloBackendServer *server, struct weston_seat *seat)
{
	struct polo_primary *primary = g_new0(struct polo_primary, 1);

	primary->server = server;
	primary->seat = seat;
	wl_list_init(&primary->devices);
	wl_list_init(&primary->offers);
	primary->focus.notify = focus_changed;
	return primary;
}

void
polo_primary_attach_keyboard(struct polo_primary *primary,
			     struct weston_keyboard *keyboard)
{
	if (primary->keyboard)
		wl_list_remove(&primary->focus.link);
	primary->keyboard = keyboard;
	if (keyboard)
		wl_signal_add(&keyboard->focus_signal, &primary->focus);
}

void
polo_primary_free(struct polo_primary *primary)
{
	struct wl_resource *device, *next_device;
	struct primary_offer *offer, *next_offer;

	polo_primary_attach_keyboard(primary, NULL);
	if (primary->source)
		wl_list_remove(&primary->source_destroy.link);
	wl_list_for_each_safe(offer, next_offer, &primary->offers, link)
	{
		offer->source = NULL;
		wl_list_remove(&offer->link);
		wl_list_init(&offer->link);
	}
	wl_resource_for_each_safe(device, next_device, &primary->devices)
	{
		wl_resource_set_user_data(device, NULL);
		wl_list_remove(wl_resource_get_link(device));
		wl_list_init(wl_resource_get_link(device));
	}
	g_free(primary);
}

struct weston_data_source *
polo_primary_get_source(struct polo_primary *primary)
{
	return primary->source;
}

void
polo_primary_set_source(struct polo_primary *primary,
			struct weston_data_source *source)
{
	primary_set(primary, source, primary->serial);
}

gboolean
polo_backend_primary_selection_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	wl_signal_init(&server->primary_selection_signal);
	server->primary_selection_global = wl_global_create(
		server->display,
		&zwp_primary_selection_device_manager_v1_interface, 1, server,
		manager_bind);
	return TRUE;
}

void
polo_backend_primary_selection_fini(PoloBackendServer *server)
{
	if (server->primary_selection_global) {
		wl_global_destroy(server->primary_selection_global);
		server->primary_selection_global = NULL;
	}
}
