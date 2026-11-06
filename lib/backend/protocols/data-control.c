/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_data_control_manager_v1 and zwlr_data_control_manager_v1 (version 2,
 * which added the primary selection), for mate-clipboard. The two protocols
 * have the same requests and events in
 * the same order, so one implementation serves both. A device sees every
 * change of the seat's clipboard (libweston's selection) and primary
 * selection (primary-selection.c) whatever has the keyboard focus, and can
 * set either. A control source becomes a weston_data_source with no
 * resource, as the XWM's are. Trust offers the globals to trusted clients
 * only.
 */

#include <unistd.h>

#include "ext-data-control-v1-server-protocol.h"
#include "protocol-util.h"
#include "wlr-data-control-unstable-v1-server-protocol.h"

struct dc_protocol {
	const struct wl_interface *manager;
	const struct wl_interface *device;
	const struct wl_interface *source;
	const struct wl_interface *offer;
	uint32_t version;
	uint32_t primary_since;
};

static const struct dc_protocol ext_protocol = {
	&ext_data_control_manager_v1_interface,
	&ext_data_control_device_v1_interface,
	&ext_data_control_source_v1_interface,
	&ext_data_control_offer_v1_interface,
	1,
	1,
};

static const struct dc_protocol wlr_protocol = {
	&zwlr_data_control_manager_v1_interface,
	&zwlr_data_control_device_v1_interface,
	&zwlr_data_control_source_v1_interface,
	&zwlr_data_control_offer_v1_interface,
	2,
	ZWLR_DATA_CONTROL_DEVICE_V1_PRIMARY_SELECTION_SINCE_VERSION,
};

struct dc_global {
	PoloBackendServer *server;
	const struct dc_protocol *protocol;
};

struct dc_device {
	struct wl_resource *resource;
	struct dc_global *global;
	struct weston_seat *seat;
	struct wl_list offers;
	struct wl_listener selection;
	struct wl_listener primary;
	struct wl_listener seat_destroy;
};

struct dc_offer {
	struct wl_list link;
	struct wl_resource *resource;
	struct weston_data_source *source;
	gboolean primary;
};

struct dc_source {
	struct weston_data_source base;
	struct wl_resource *resource;
	gboolean used;
};

/* ---- Offers -------------------------------------------------------------*/

static void
offer_receive(struct wl_client *client, struct wl_resource *resource,
	      const char *mime_type, int32_t fd)
{
	struct dc_offer *offer = wl_resource_get_user_data(resource);

	if (offer->source)
		offer->source->send(offer->source, mime_type, fd);
	else
		close(fd);
}

static const struct ext_data_control_offer_v1_interface offer_impl = {
	.receive = offer_receive,
	.destroy = polo_protocol_destroy,
};

static void
offer_resource_destroyed(struct wl_resource *resource)
{
	struct dc_offer *offer = wl_resource_get_user_data(resource);

	wl_list_remove(&offer->link);
	g_free(offer);
}

static gboolean
primary_supported(struct dc_device *device)
{
	return (uint32_t)wl_resource_get_version(device->resource) >=
		device->global->protocol->primary_since;
}

static struct weston_data_source *
primary_source(struct dc_device *device)
{
	PoloBackendSeat *seat = polo_backend_server_lookup_seat(
		device->global->server, device->seat);
	struct polo_primary *primary =
		seat ? polo_backend_seat_get_primary(seat) : NULL;

	return primary ? polo_primary_get_source(primary) : NULL;
}

static void
send_selection(struct dc_device *device, gboolean primary)
{
	struct weston_data_source *source = primary
		? primary_source(device)
		: device->seat->selection_data_source;
	struct wl_resource *resource = NULL;
	struct dc_offer *offer, *next;

	wl_list_for_each_safe(offer, next, &device->offers, link)
	{
		if (offer->primary == primary)
			offer->source = NULL;
	}
	if (source) {
		offer = g_new0(struct dc_offer, 1);
		resource = polo_protocol_resource_new(
			wl_resource_get_client(device->resource),
			device->global->protocol->offer, 1, 0, &offer_impl,
			offer, offer_resource_destroyed);
		if (!resource) {
			g_free(offer);
			return;
		}
		offer->resource = resource;
		offer->source = source;
		offer->primary = primary;
		wl_list_insert(&device->offers, &offer->link);
		ext_data_control_device_v1_send_data_offer(device->resource,
							   resource);

		char **mime;

		wl_array_for_each(mime, &source->mime_types)
			ext_data_control_offer_v1_send_offer(resource, *mime);
	}
	if (primary)
		ext_data_control_device_v1_send_primary_selection(
			device->resource, resource);
	else
		ext_data_control_device_v1_send_selection(device->resource,
							  resource);
}

/* ---- Sources ------------------------------------------------------------*/

static void
source_send(struct weston_data_source *base, const char *mime_type, int32_t fd)
{
	struct dc_source *source = wl_container_of(base, source, base);

	ext_data_control_source_v1_send_send(source->resource, mime_type, fd);
	close(fd);
}

static void
source_cancel(struct weston_data_source *base)
{
	struct dc_source *source = wl_container_of(base, source, base);

	ext_data_control_source_v1_send_cancelled(source->resource);
}

static void
source_accept(struct weston_data_source *base, uint32_t serial,
	      const char *mime_type)
{
}

static void
source_offer(struct wl_client *client, struct wl_resource *resource,
	     const char *mime_type)
{
	struct dc_source *source = wl_resource_get_user_data(resource);
	char **mime;

	if (source->used) {
		wl_resource_post_error(
			resource,
			EXT_DATA_CONTROL_SOURCE_V1_ERROR_INVALID_OFFER,
			"offer after the source was used");
		return;
	}
	mime = wl_array_add(&source->base.mime_types, sizeof *mime);
	if (mime)
		*mime = g_strdup(mime_type);
}

static const struct ext_data_control_source_v1_interface source_impl = {
	.offer = source_offer,
	.destroy = polo_protocol_destroy,
};

static void
source_resource_destroyed(struct wl_resource *resource)
{
	struct dc_source *source = wl_resource_get_user_data(resource);
	char **mime;

	wl_signal_emit(&source->base.destroy_signal, &source->base);
	wl_array_for_each(mime, &source->base.mime_types) g_free(*mime);
	wl_array_release(&source->base.mime_types);
	g_free(source);
}

/* ---- Devices -----------------------------------------------------------*/

static struct dc_source *
take_source(struct dc_device *device, struct wl_resource *source_resource)
{
	struct dc_source *source;

	if (!source_resource)
		return NULL;
	source = wl_resource_get_user_data(source_resource);
	if (source->used) {
		wl_resource_post_error(
			device->resource,
			EXT_DATA_CONTROL_DEVICE_V1_ERROR_USED_SOURCE,
			"the source was already used");
		return NULL;
	}
	source->used = TRUE;
	return source;
}

static void
device_set_selection(struct wl_client *client, struct wl_resource *resource,
		     struct wl_resource *source_resource)
{
	struct dc_device *device = wl_resource_get_user_data(resource);
	struct dc_source *source;

	if (!device->seat)
		return;
	source = take_source(device, source_resource);
	if (source_resource && !source)
		return;
	weston_seat_set_selection(
		device->seat, source ? &source->base : NULL,
		wl_display_next_serial(device->global->server->display));
}

static void
device_set_primary_selection(struct wl_client *client,
			     struct wl_resource *resource,
			     struct wl_resource *source_resource)
{
	struct dc_device *device = wl_resource_get_user_data(resource);
	PoloBackendSeat *seat;
	struct polo_primary *primary;
	struct dc_source *source;

	if (!device->seat)
		return;
	source = take_source(device, source_resource);
	if (source_resource && !source)
		return;
	seat = polo_backend_server_lookup_seat(device->global->server,
					       device->seat);
	primary = seat ? polo_backend_seat_get_primary(seat) : NULL;
	if (primary)
		polo_primary_set_source(primary, source ? &source->base : NULL);
}

static const struct ext_data_control_device_v1_interface device_impl = {
	.set_selection = device_set_selection,
	.destroy = polo_protocol_destroy,
	.set_primary_selection = device_set_primary_selection,
};

static void
device_detach(struct dc_device *device)
{
	if (!device->seat)
		return;
	wl_list_remove(&device->selection.link);
	wl_list_remove(&device->primary.link);
	wl_list_remove(&device->seat_destroy.link);
	device->seat = NULL;
}

static void
device_selection_changed(struct wl_listener *listener, void *data)
{
	struct dc_device *device = wl_container_of(listener, device, selection);

	send_selection(device, FALSE);
}

static void
device_primary_changed(struct wl_listener *listener, void *data)
{
	struct dc_device *device = wl_container_of(listener, device, primary);

	if (data == device->seat && primary_supported(device))
		send_selection(device, TRUE);
}

static void
device_seat_destroyed(struct wl_listener *listener, void *data)
{
	struct dc_device *device =
		wl_container_of(listener, device, seat_destroy);

	device_detach(device);
	ext_data_control_device_v1_send_finished(device->resource);
}

static void
device_resource_destroyed(struct wl_resource *resource)
{
	struct dc_device *device = wl_resource_get_user_data(resource);
	struct dc_offer *offer, *next;

	device_detach(device);
	wl_list_for_each_safe(offer, next, &device->offers, link)
	{
		offer->source = NULL;
		wl_list_remove(&offer->link);
		wl_list_init(&offer->link);
	}
	g_free(device);
}

/* ---- Manager ------------------------------------------------------------*/

static void
manager_create_data_source(struct wl_client *client,
			   struct wl_resource *resource, uint32_t id)
{
	struct dc_global *global = wl_resource_get_user_data(resource);
	struct dc_source *source = g_new0(struct dc_source, 1);

	wl_signal_init(&source->base.destroy_signal);
	wl_array_init(&source->base.mime_types);
	source->base.accept = source_accept;
	source->base.send = source_send;
	source->base.cancel = source_cancel;
	source->resource = polo_protocol_child_new(
		resource, global->protocol->source, id, &source_impl, source,
		source_resource_destroyed);
	if (!source->resource) {
		wl_array_release(&source->base.mime_types);
		g_free(source);
	}
}

static void
manager_get_data_device(struct wl_client *client, struct wl_resource *resource,
			uint32_t id, struct wl_resource *seat_resource)
{
	struct dc_global *global = wl_resource_get_user_data(resource);
	struct dc_device *device = g_new0(struct dc_device, 1);

	device->global = global;
	wl_list_init(&device->offers);
	device->resource = polo_protocol_child_new(
		resource, global->protocol->device, id, &device_impl, device,
		device_resource_destroyed);
	if (!device->resource) {
		g_free(device);
		return;
	}
	device->seat = wl_resource_get_user_data(seat_resource);
	if (!device->seat) {
		ext_data_control_device_v1_send_finished(device->resource);
		return;
	}
	device->selection.notify = device_selection_changed;
	wl_signal_add(&device->seat->selection_signal, &device->selection);
	device->primary.notify = device_primary_changed;
	wl_signal_add(&global->server->primary_selection_signal,
		      &device->primary);
	device->seat_destroy.notify = device_seat_destroyed;
	wl_signal_add(&device->seat->destroy_signal, &device->seat_destroy);
	send_selection(device, FALSE);
	if (primary_supported(device))
		send_selection(device, TRUE);
}

static const struct ext_data_control_manager_v1_interface manager_impl = {
	.create_data_source = manager_create_data_source,
	.get_data_device = manager_get_data_device,
	.destroy = polo_protocol_destroy,
};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct dc_global *global = data;

	polo_protocol_resource_new(client, global->protocol->manager,
				   MIN(version, global->protocol->version), id,
				   &manager_impl, global, NULL);
}

static struct wl_global *
global_create(PoloBackendServer *server, struct dc_global **slot,
	      const struct dc_protocol *protocol)
{
	*slot = g_new0(struct dc_global, 1);
	(*slot)->server = server;
	(*slot)->protocol = protocol;
	return wl_global_create(server->display, protocol->manager,
				(int)protocol->version, *slot, manager_bind);
}

gboolean
polo_backend_data_control_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	server->ext_data_control_global =
		global_create(server, &server->ext_data_control, &ext_protocol);
	server->wlr_data_control_global =
		global_create(server, &server->wlr_data_control, &wlr_protocol);
	return TRUE;
}

void
polo_backend_data_control_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->ext_data_control_global, wl_global_destroy);
	g_clear_pointer(&server->wlr_data_control_global, wl_global_destroy);
	g_clear_pointer(&server->ext_data_control, g_free);
	g_clear_pointer(&server->wlr_data_control, g_free);
}
