/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate_input_device_manager_v1, version 1: a read-only description of every
 * input device, from libinput and udev (polo_backend_input_device_describe)
 * or a test-mode fake. The kind and id follow the protocol's rules; a
 * touchscreen's or tablet's output is the monitor id of the output it maps
 * to (its udev WL_OUTPUT, else the first enabled output, as libweston maps
 * it), resent when outputs change.
 */

#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "protocol-util.h"
#include "mate-input-device-v1-server-protocol.h"

#define MANAGER_VERSION 1

struct mi_manager {
	struct wl_list link;
	struct polo_mate_input *mi;
	struct wl_resource *resource;
	struct wl_list devices;
	gboolean stopped;
};

struct mi_device {
	struct wl_list link;
	struct mi_manager *manager;
	struct wl_resource *resource;
	PoloBackendInputDevice *device;
	gulong removed_id;
	char *output;
};

struct polo_mate_input {
	PoloBackendServer *server;
	struct wl_global *global;
	struct wl_list managers;
	gulong added_id;
	gulong outputs_id;
};

static guint
kind_of(const PoloBackendDeviceDescription *d)
{
	if (d->capabilities & MATE_INPUT_DEVICE_V1_CAPABILITY_TABLET_PAD)
		return MATE_INPUT_DEVICE_V1_KIND_TABLET_PAD;
	if (d->capabilities & MATE_INPUT_DEVICE_V1_CAPABILITY_TABLET_TOOL)
		return MATE_INPUT_DEVICE_V1_KIND_TABLET;
	if (d->capabilities & MATE_INPUT_DEVICE_V1_CAPABILITY_TOUCH)
		return MATE_INPUT_DEVICE_V1_KIND_TOUCHSCREEN;
	if (d->capabilities & MATE_INPUT_DEVICE_V1_CAPABILITY_POINTER) {
		if (d->tap_fingers > 0 || (d->tags & POLO_DEVICE_TAG_TOUCHPAD))
			return MATE_INPUT_DEVICE_V1_KIND_TOUCHPAD;
		if (d->tags & POLO_DEVICE_TAG_POINTING_STICK)
			return MATE_INPUT_DEVICE_V1_KIND_POINTING_STICK;
		if (d->tags & POLO_DEVICE_TAG_TRACKBALL)
			return MATE_INPUT_DEVICE_V1_KIND_TRACKBALL;
		return MATE_INPUT_DEVICE_V1_KIND_MOUSE;
	}
	if (d->capabilities & MATE_INPUT_DEVICE_V1_CAPABILITY_KEYBOARD)
		return MATE_INPUT_DEVICE_V1_KIND_KEYBOARD;
	if (d->capabilities & MATE_INPUT_DEVICE_V1_CAPABILITY_SWITCH)
		return MATE_INPUT_DEVICE_V1_KIND_SWITCH;
	return MATE_INPUT_DEVICE_V1_KIND_OTHER;
}

static char *
base_id(const PoloBackendDeviceDescription *d)
{
	GString *id = g_string_new(NULL);

	g_string_printf(id, "%04x:%04x:", d->vendor & 0xffff,
			d->product & 0xffff);
	for (const char *c = d->name; *c; c++)
		g_string_append_c(id,
				  g_ascii_isalnum(*c) || *c == '_' ||
						  *c == '.' || *c == '-'
					  ? *c
					  : '_');
	return g_string_free(id, FALSE);
}

static char *
device_id(struct polo_mate_input *mi, PoloBackendInputDevice *device,
	  const PoloBackendDeviceDescription *d)
{
	char *base = base_id(d);
	GHashTableIter iter;
	gpointer value;

	g_hash_table_iter_init(&iter, mi->server->input_devices);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		PoloBackendDeviceDescription other;
		g_autofree char *other_base = NULL;

		if (value == device ||
		    !polo_backend_input_device_describe(value, &other))
			continue;
		other_base = base_id(&other);
		if (strcmp(base, other_base) == 0) {
			char *id = g_strdup_printf("%s@%08x", base,
						   g_str_hash(d->phys));

			g_free(base);
			return id;
		}
	}
	return base;
}

static const char *
output_id(struct polo_mate_input *mi, const PoloBackendDeviceDescription *d)
{
	GPtrArray *outputs = mi->server->outputs;

	if (!(d->capabilities &
	      (MATE_INPUT_DEVICE_V1_CAPABILITY_TOUCH |
	       MATE_INPUT_DEVICE_V1_CAPABILITY_TABLET_TOOL)))
		return "";
	for (guint i = 0; d->output_name[0] && i < outputs->len; i++) {
		PoloBackendOutput *output = outputs->pdata[i];

		if (g_strcmp0(polo_backend_output_get_name(output),
			      d->output_name) == 0)
			return polo_backend_output_get_mate_id(output);
	}
	for (guint i = 0; i < outputs->len; i++) {
		PoloBackendOutput *output = outputs->pdata[i];
		struct weston_output *wo =
			polo_backend_output_get_weston_output(output);

		if (wo && wo->enabled)
			return polo_backend_output_get_mate_id(output);
	}
	return "";
}

static void
device_send(struct polo_mate_input *mi, struct mi_device *md, gboolean all)
{
	PoloBackendDeviceDescription d;
	struct wl_resource *r = md->resource;
	const char *output;

	if (!r || !md->device ||
	    !polo_backend_input_device_describe(md->device, &d))
		return;
	output = output_id(mi, &d);
	if (!all && g_strcmp0(output, md->output) == 0)
		return;
	if (all) {
		g_autofree char *id = device_id(mi, md->device, &d);

		mate_input_device_v1_send_name(r, d.name);
		mate_input_device_v1_send_id(r, id);
		mate_input_device_v1_send_usb_ids(r, d.vendor, d.product);
		mate_input_device_v1_send_kind(r, kind_of(&d));
		mate_input_device_v1_send_capabilities(r, d.capabilities);
		mate_input_device_v1_send_buttons(r, d.buttons);
		mate_input_device_v1_send_config(r, d.config);
		mate_input_device_v1_send_tap_fingers(r, d.tap_fingers);
		mate_input_device_v1_send_accel_profiles(r, d.accel_profiles);
		mate_input_device_v1_send_scroll_methods(r, d.scroll_methods);
		mate_input_device_v1_send_click_methods(r, d.click_methods);
		mate_input_device_v1_send_send_events(r, d.send_events);
	}
	g_free(md->output);
	md->output = g_strdup(output);
	mate_input_device_v1_send_output(r, output);
	mate_input_device_v1_send_done(r);
}

static void
device_forget(struct mi_device *md)
{
	if (!md->device)
		return;
	g_signal_handler_disconnect(md->device, md->removed_id);
	g_clear_object(&md->device);
}

static void
device_free(struct mi_device *md)
{
	device_forget(md);
	wl_list_remove(&md->link);
	g_free(md->output);
	g_free(md);
}

static void
device_resource_destroyed(struct wl_resource *resource)
{
	struct mi_device *md = wl_resource_get_user_data(resource);

	if (md)
		device_free(md);
}

static const struct mate_input_device_v1_interface device_impl = {
	.destroy = polo_protocol_destroy,
};

static void
device_removed(PoloBackendInputDevice *device, gpointer data)
{
	struct mi_device *md = data;

	if (md->resource)
		mate_input_device_v1_send_removed(md->resource);
	device_forget(md);
}

static void
device_create(struct mi_manager *manager, PoloBackendInputDevice *device)
{
	struct mi_device *md = g_new0(struct mi_device, 1);

	md->manager = manager;
	md->resource = wl_resource_create(
		wl_resource_get_client(manager->resource),
		&mate_input_device_v1_interface,
		wl_resource_get_version(manager->resource), 0);
	if (!md->resource) {
		g_free(md);
		wl_client_post_no_memory(
			wl_resource_get_client(manager->resource));
		return;
	}
	wl_resource_set_implementation(md->resource, &device_impl, md,
				       device_resource_destroyed);
	wl_list_insert(manager->devices.prev, &md->link);
	md->device = g_object_ref(device);
	md->removed_id = g_signal_connect(device, "removed",
					  G_CALLBACK(device_removed), md);
	mate_input_device_manager_v1_send_device(manager->resource,
						 md->resource);
	device_send(manager->mi, md, TRUE);
}

/* ---- Manager ------------------------------------------------------------ */

static void
manager_stop(struct wl_client *client, struct wl_resource *resource)
{
	struct mi_manager *manager = wl_resource_get_user_data(resource);

	if (!manager || manager->stopped)
		return;
	manager->stopped = TRUE;
	mate_input_device_manager_v1_send_finished(resource);
}

static void
manager_resource_destroyed(struct wl_resource *resource)
{
	struct mi_manager *manager = wl_resource_get_user_data(resource);
	struct mi_device *md, *next;

	if (!manager)
		return;
	wl_list_for_each_safe(md, next, &manager->devices, link)
	{
		wl_list_remove(&md->link);
		wl_list_init(&md->link);
		md->manager = NULL;
	}
	wl_list_remove(&manager->link);
	g_free(manager);
}

static const struct mate_input_device_manager_v1_interface manager_impl = {
	.stop = manager_stop,
	.destroy = polo_protocol_destroy,
};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct polo_mate_input *mi = data;
	struct mi_manager *manager = g_new0(struct mi_manager, 1);
	GHashTableIter iter;
	gpointer value;

	manager->mi = mi;
	wl_list_init(&manager->devices);
	manager->resource = wl_resource_create(
		client, &mate_input_device_manager_v1_interface,
		MIN(version, MANAGER_VERSION), id);
	if (!manager->resource) {
		g_free(manager);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(manager->resource, &manager_impl,
				       manager, manager_resource_destroyed);
	wl_list_insert(&mi->managers, &manager->link);
	g_hash_table_iter_init(&iter, mi->server->input_devices);
	while (g_hash_table_iter_next(&iter, NULL, &value))
		device_create(manager, value);
	mate_input_device_manager_v1_send_done(manager->resource);
}

/* ---- Server ------------------------------------------------------------- */

static void
on_device_added(PoloBackendServer *server, PoloBackendInputDevice *device,
		gpointer data)
{
	struct polo_mate_input *mi = data;
	struct mi_manager *manager;

	wl_list_for_each(manager, &mi->managers, link)
	{
		if (manager->stopped)
			continue;
		device_create(manager, device);
		mate_input_device_manager_v1_send_done(manager->resource);
	}
}

void
polo_backend_mate_input_outputs_changed(PoloBackendServer *server)
{
	struct polo_mate_input *mi = server->mate_input;
	struct mi_manager *manager;
	struct mi_device *md;

	if (!mi)
		return;
	wl_list_for_each(manager, &mi->managers, link)
	{
		if (manager->stopped)
			continue;
		wl_list_for_each(md, &manager->devices, link)
			device_send(mi, md, FALSE);
	}
}

static void
on_outputs_changed(PoloBackendServer *server, gpointer data)
{
	polo_backend_mate_input_outputs_changed(server);
}

gboolean
polo_backend_mate_input_device_init(PoloBackendServer *server, GError **error)
{
	struct polo_mate_input *mi = g_new0(struct polo_mate_input, 1);

	(void)error;
	mi->server = server;
	wl_list_init(&mi->managers);
	mi->global = wl_global_create(server->display,
				      &mate_input_device_manager_v1_interface,
				      MANAGER_VERSION, mi, manager_bind);
	mi->added_id = g_signal_connect(server, "input-device-added",
					G_CALLBACK(on_device_added), mi);
	mi->outputs_id = g_signal_connect(server, "outputs-changed",
					  G_CALLBACK(on_outputs_changed), mi);
	server->mate_input = mi;
	return TRUE;
}

void
polo_backend_mate_input_device_fini(PoloBackendServer *server)
{
	struct polo_mate_input *mi = server->mate_input;
	struct mi_manager *manager, *next;

	if (!mi)
		return;
	g_signal_handler_disconnect(server, mi->added_id);
	g_signal_handler_disconnect(server, mi->outputs_id);
	g_clear_pointer(&mi->global, wl_global_destroy);
	wl_list_for_each_safe(manager, next, &mi->managers, link)
	{
		struct mi_device *md, *md_next;

		wl_list_for_each_safe(md, md_next, &manager->devices, link)
		{
			wl_resource_set_user_data(md->resource, NULL);
			device_free(md);
		}
		wl_resource_set_user_data(manager->resource, NULL);
		wl_list_remove(&manager->link);
		g_free(manager);
	}
	server->mate_input = NULL;
	g_free(mi);
}
