/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate_output_manager_v1, version 1: monitor identity and MATE semantics
 * for zwlr_output_head_v1 heads and wl_outputs. Vala decides the id, the
 * display name, mirroring, the primary, the work area and who draws the
 * background (Backend.Output setters); this file sends them, and the EDID
 * facts it reads itself, whenever they change. set_primary and
 * set_temporary ride on the wlr configuration (wlr-output-management.c).
 */

#include <stdlib.h>
#include <unistd.h>
#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "protocol-util.h"
#include "mate-output-v1-server-protocol.h"

#define MANAGER_VERSION 1

struct sent {
	gboolean valid;
	char *id;
	char *display_name;
	char *mirror_of;
	gboolean primary;
	struct weston_geometry workarea;
	guint background_owner;
};

struct mate_head {
	struct wl_list link;
	struct wl_resource *resource;
	PoloBackendOutput *output;
	struct sent sent;
};

struct mate_out {
	struct wl_list link;
	struct wl_resource *resource;
	struct weston_head *head;
	struct wl_listener head_destroyed;
	struct sent sent;
};

struct mate_config {
	struct wl_resource *resource;
	struct wl_resource *config;
	struct wl_listener config_destroyed;
};

struct polo_mate_output {
	PoloBackendServer *server;
	struct wl_global *global;
	struct wl_list managers;
	struct wl_list heads;
	struct wl_list outputs;
	gulong removed_id;
};

static void
sent_clear(struct sent *sent)
{
	g_free(sent->id);
	g_free(sent->display_name);
	g_free(sent->mirror_of);
	memset(sent, 0, sizeof *sent);
}

static gboolean
take_string(char **field, const char *value, gboolean all)
{
	if (!all && g_strcmp0(*field, value) == 0)
		return FALSE;
	g_free(*field);
	*field = g_strdup(value);
	return TRUE;
}

static guint
parse_hex(const char *text)
{
	return text && *text ? (guint)strtoul(text, NULL, 16) : 0;
}

struct identity {
	char *vendor;
	guint product;
	guint serial;
	const char *serial_string;
};

static void
identity_of(PoloBackendOutput *output, struct identity *identity)
{
	g_autofree char *product = polo_backend_output_get_product(output);
	g_autofree char *serial = polo_backend_output_get_edid_serial(output);

	identity->vendor = polo_backend_output_get_vendor(output);
	identity->product = parse_hex(product);
	identity->serial = parse_hex(serial);
	identity->serial_string = polo_backend_output_get_serial(output);
}

static struct weston_head *
weston_head_of(PoloBackendOutput *output)
{
	struct weston_output *wo =
		polo_backend_output_get_weston_output(output);

	return wo ? weston_output_get_first_head(wo) : NULL;
}

/* ---- Heads -------------------------------------------------------------- */

static gboolean
head_primary(PoloBackendOutput *output)
{
	struct weston_output *wo =
		polo_backend_output_get_weston_output(output);

	return wo && wo->enabled && polo_backend_output_get_primary(output);
}

static void
head_send(struct mate_head *mh, gboolean all)
{
	PoloBackendOutput *output = mh->output;
	struct sent *sent = &mh->sent;
	gboolean changed = all;
	gboolean primary;

	if (!output || !mh->resource)
		return;
	if (all) {
		struct identity identity;
		size_t size = 0;
		int fd = polo_backend_head_edid_fd(weston_head_of(output),
						   &size);

		identity_of(output, &identity);
		mate_output_head_v1_send_edid_identity(
			mh->resource, identity.vendor, identity.product,
			identity.serial, identity.serial_string);
		g_free(identity.vendor);
		if (fd >= 0) {
			mate_output_head_v1_send_edid(mh->resource, fd, size);
			close(fd);
		}
		mate_output_head_v1_send_internal(
			mh->resource, polo_backend_output_get_internal(output));
	}
	if (take_string(&sent->id, polo_backend_output_get_mate_id(output),
			all)) {
		mate_output_head_v1_send_id(mh->resource, sent->id);
		changed = TRUE;
	}
	if (take_string(&sent->display_name,
			polo_backend_output_get_display_name(output), all)) {
		mate_output_head_v1_send_display_name(mh->resource,
						      sent->display_name);
		changed = TRUE;
	}
	primary = head_primary(output);
	if (all || primary != sent->primary) {
		sent->primary = primary;
		mate_output_head_v1_send_primary(mh->resource, primary);
		changed = TRUE;
	}
	if (take_string(&sent->mirror_of,
			polo_backend_output_get_mirror_of(output), all)) {
		mate_output_head_v1_send_mirror_of(mh->resource,
						   sent->mirror_of);
		changed = TRUE;
	}
	if (changed)
		mate_output_head_v1_send_done(mh->resource);
}

static void
head_resource_destroyed(struct wl_resource *resource)
{
	struct mate_head *mh = wl_resource_get_user_data(resource);

	if (!mh)
		return;
	wl_list_remove(&mh->link);
	sent_clear(&mh->sent);
	g_free(mh);
}

static const struct mate_output_head_v1_interface head_impl = {
	.destroy = polo_protocol_destroy,
};

/* ---- Outputs ------------------------------------------------------------ */

static PoloBackendOutput *
out_output(struct polo_mate_output *mo, struct mate_out *out)
{
	if (!out->head || !out->head->output)
		return NULL;
	return polo_backend_server_lookup_output(mo->server, out->head->output);
}

static struct weston_geometry
out_workarea(PoloBackendOutput *output, struct weston_output *wo)
{
	struct weston_geometry area = {
		.x = (int)wo->pos.c.x,
		.y = (int)wo->pos.c.y,
		.width = wo->width,
		.height = wo->height,
	};

	polo_backend_output_get_work_area(output, &area);
	area.x -= (int)wo->pos.c.x;
	area.y -= (int)wo->pos.c.y;
	return area;
}

static void
out_send(struct polo_mate_output *mo, struct mate_out *out, gboolean all)
{
	PoloBackendOutput *output = out_output(mo, out);
	struct sent *sent = &out->sent;
	struct weston_geometry area;
	gboolean changed = all;
	gboolean primary;
	guint owner;

	if (!output || !out->resource)
		return;
	if (all) {
		struct identity identity;
		size_t size = 0;
		int fd = polo_backend_head_edid_fd(out->head, &size);

		identity_of(output, &identity);
		mate_output_v1_send_edid_identity(
			out->resource, identity.vendor, identity.product,
			identity.serial, identity.serial_string);
		g_free(identity.vendor);
		if (fd >= 0) {
			mate_output_v1_send_edid(out->resource, fd, size);
			close(fd);
		}
		mate_output_v1_send_internal(
			out->resource,
			polo_backend_output_get_internal(output));
	}
	if (take_string(&sent->id, polo_backend_output_get_mate_id(output),
			all)) {
		mate_output_v1_send_id(out->resource, sent->id);
		changed = TRUE;
	}
	if (take_string(&sent->display_name,
			polo_backend_output_get_display_name(output), all)) {
		mate_output_v1_send_display_name(out->resource,
						 sent->display_name);
		changed = TRUE;
	}
	primary = head_primary(output);
	if (all || primary != sent->primary) {
		sent->primary = primary;
		mate_output_v1_send_primary(out->resource, primary);
		changed = TRUE;
	}
	if (take_string(&sent->mirror_of,
			polo_backend_output_get_mirror_of(output), all)) {
		mate_output_v1_send_mirror_of(out->resource, sent->mirror_of);
		changed = TRUE;
	}
	area = out_workarea(output, out->head->output);
	if (all || memcmp(&area, &sent->workarea, sizeof area) != 0) {
		sent->workarea = area;
		mate_output_v1_send_workarea(out->resource, area.x, area.y,
					     area.width, area.height);
		changed = TRUE;
	}
	owner = polo_backend_output_get_background_owner(output);
	if (all || owner != sent->background_owner) {
		sent->background_owner = owner;
		mate_output_v1_send_background_owner(out->resource, owner);
		changed = TRUE;
	}
	if (changed)
		mate_output_v1_send_done(out->resource);
}

static void
out_resource_destroyed(struct wl_resource *resource)
{
	struct mate_out *out = wl_resource_get_user_data(resource);

	if (!out)
		return;
	if (out->head)
		wl_list_remove(&out->head_destroyed.link);
	wl_list_remove(&out->link);
	sent_clear(&out->sent);
	g_free(out);
}

static void
out_head_destroyed(struct wl_listener *listener, void *data)
{
	struct mate_out *out = wl_container_of(listener, out, head_destroyed);

	wl_list_remove(&out->head_destroyed.link);
	out->head = NULL;
}

static const struct mate_output_v1_interface out_impl = {
	.destroy = polo_protocol_destroy,
};

/* ---- Configurations ----------------------------------------------------- */

static void
config_target_destroyed(struct wl_listener *listener, void *data)
{
	struct mate_config *mc =
		wl_container_of(listener, mc, config_destroyed);

	wl_list_remove(&mc->config_destroyed.link);
	wl_list_init(&mc->config_destroyed.link);
	mc->config = NULL;
}

static gboolean
config_result(struct wl_resource *resource, PoloOmExtensionResult result)
{
	switch (result) {
	case POLO_OM_EXTENSION_OK:
		return TRUE;
	case POLO_OM_EXTENSION_ALREADY_SET:
		wl_resource_post_error(
			resource,
			MATE_OUTPUT_CONFIGURATION_V1_ERROR_ALREADY_SET,
			"option already set");
		break;
	case POLO_OM_EXTENSION_INVALID_HEAD:
		wl_resource_post_error(
			resource,
			MATE_OUTPUT_CONFIGURATION_V1_ERROR_INVALID_HEAD,
			"head of another configuration");
		break;
	default:
		wl_resource_post_error(
			resource,
			MATE_OUTPUT_CONFIGURATION_V1_ERROR_ALREADY_USED,
			"configuration already applied, tested or destroyed");
		break;
	}
	return FALSE;
}

static void
config_set_primary(struct wl_client *client, struct wl_resource *resource,
		   struct wl_resource *head)
{
	struct mate_config *mc = wl_resource_get_user_data(resource);

	config_result(resource,
		      mc->config ? polo_output_management_set_primary(
					   mc->config, head)
				 : POLO_OM_EXTENSION_ALREADY_USED);
}

static void
config_set_temporary(struct wl_client *client, struct wl_resource *resource)
{
	struct mate_config *mc = wl_resource_get_user_data(resource);

	config_result(resource,
		      mc->config
			      ? polo_output_management_set_temporary(mc->config)
			      : POLO_OM_EXTENSION_ALREADY_USED);
}

static void
config_resource_destroyed(struct wl_resource *resource)
{
	struct mate_config *mc = wl_resource_get_user_data(resource);

	wl_list_remove(&mc->config_destroyed.link);
	g_free(mc);
}

static const struct mate_output_configuration_v1_interface config_impl = {
	.set_primary = config_set_primary,
	.set_temporary = config_set_temporary,
	.destroy = polo_protocol_destroy,
};

/* ---- Manager ------------------------------------------------------------ */

static void
manager_get_head(struct wl_client *client, struct wl_resource *resource,
		 uint32_t id, struct wl_resource *wlr_head)
{
	struct polo_mate_output *mo = wl_resource_get_user_data(resource);
	struct mate_head *mh = g_new0(struct mate_head, 1);

	mh->resource =
		wl_resource_create(client, &mate_output_head_v1_interface,
				   wl_resource_get_version(resource), id);
	if (!mh->resource) {
		g_free(mh);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(mh->resource, &head_impl, mh,
				       head_resource_destroyed);
	wl_list_insert(&mo->heads, &mh->link);
	mh->output = polo_output_management_head_output(wlr_head);
	if (!mh->output) {
		mate_output_head_v1_send_finished(mh->resource);
		return;
	}
	head_send(mh, TRUE);
}

static void
manager_get_output(struct wl_client *client, struct wl_resource *resource,
		   uint32_t id, struct wl_resource *output_resource)
{
	struct polo_mate_output *mo = wl_resource_get_user_data(resource);
	struct weston_head *head = weston_head_from_resource(output_resource);
	struct mate_out *out = g_new0(struct mate_out, 1);

	out->resource =
		wl_resource_create(client, &mate_output_v1_interface,
				   wl_resource_get_version(resource), id);
	if (!out->resource) {
		g_free(out);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(out->resource, &out_impl, out,
				       out_resource_destroyed);
	wl_list_insert(&mo->outputs, &out->link);
	if (!head || !head->output)
		return;
	out->head = head;
	out->head_destroyed.notify = out_head_destroyed;
	weston_head_add_destroy_listener(head, &out->head_destroyed);
	out_send(mo, out, TRUE);
}

static void
manager_get_configuration(struct wl_client *client,
			  struct wl_resource *resource, uint32_t id,
			  struct wl_resource *config)
{
	struct mate_config *mc;

	switch (polo_output_management_attach(config)) {
	case POLO_OM_EXTENSION_OK:
		break;
	case POLO_OM_EXTENSION_ALREADY_CONSTRUCTED:
		wl_resource_post_error(
			resource,
			MATE_OUTPUT_MANAGER_V1_ERROR_ALREADY_CONSTRUCTED,
			"the configuration already has a mate configuration");
		return;
	default:
		wl_resource_post_error(
			resource, MATE_OUTPUT_MANAGER_V1_ERROR_ALREADY_USED,
			"the configuration was already used");
		return;
	}
	mc = g_new0(struct mate_config, 1);
	mc->config = config;
	mc->config_destroyed.notify = config_target_destroyed;
	wl_resource_add_destroy_listener(config, &mc->config_destroyed);
	mc->resource = wl_resource_create(
		client, &mate_output_configuration_v1_interface,
		wl_resource_get_version(resource), id);
	if (!mc->resource) {
		wl_list_remove(&mc->config_destroyed.link);
		g_free(mc);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(mc->resource, &config_impl, mc,
				       config_resource_destroyed);
}

static void
manager_stop(struct wl_client *client, struct wl_resource *resource)
{
	mate_output_manager_v1_send_finished(resource);
	wl_list_remove(wl_resource_get_link(resource));
	wl_list_init(wl_resource_get_link(resource));
}

static void
manager_resource_destroyed(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static const struct mate_output_manager_v1_interface manager_impl = {
	.get_head = manager_get_head,
	.get_output = manager_get_output,
	.get_configuration = manager_get_configuration,
	.stop = manager_stop,
	.destroy = polo_protocol_destroy,
};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct polo_mate_output *mo = data;
	struct wl_resource *resource =
		wl_resource_create(client, &mate_output_manager_v1_interface,
				   MIN(version, MANAGER_VERSION), id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, mo,
				       manager_resource_destroyed);
	wl_list_insert(&mo->managers, wl_resource_get_link(resource));
	mate_output_manager_v1_send_capabilities(
		resource, mo->server->wallpaper_capabilities);
}

/* ---- Server ------------------------------------------------------------- */

void
polo_backend_mate_output_changed(PoloBackendOutput *output)
{
	PoloBackendServer *server = polo_backend_output_get_server(output);
	struct polo_mate_output *mo = server ? server->mate_output : NULL;
	struct mate_head *mh;
	struct mate_out *out;

	if (server)
		polo_backend_mate_input_outputs_changed(server);
	if (!mo)
		return;
	wl_list_for_each(mh, &mo->heads, link)
	{
		if (mh->output == output)
			head_send(mh, FALSE);
	}
	wl_list_for_each(out, &mo->outputs, link)
	{
		if (out_output(mo, out) == output)
			out_send(mo, out, FALSE);
	}
}

void
polo_backend_server_set_wallpaper_capabilities(
	PoloBackendServer *server, PoloBackendWallpaperCapabilities caps)
{
	struct polo_mate_output *mo;
	struct wl_resource *resource;

	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	if (server->wallpaper_capabilities == (guint)caps)
		return;
	server->wallpaper_capabilities = caps;
	mo = server->mate_output;
	if (!mo)
		return;
	wl_resource_for_each(resource, &mo->managers)
		mate_output_manager_v1_send_capabilities(resource, caps);
}

static void
on_output_removed(PoloBackendServer *server, PoloBackendOutput *output,
		  gpointer data)
{
	struct polo_mate_output *mo = data;
	struct mate_head *mh;

	wl_list_for_each(mh, &mo->heads, link)
	{
		if (mh->output != output)
			continue;
		mh->output = NULL;
		if (mh->resource)
			mate_output_head_v1_send_finished(mh->resource);
	}
}

gboolean
polo_backend_mate_output_init(PoloBackendServer *server, GError **error)
{
	struct polo_mate_output *mo = g_new0(struct polo_mate_output, 1);

	(void)error;
	mo->server = server;
	wl_list_init(&mo->managers);
	wl_list_init(&mo->heads);
	wl_list_init(&mo->outputs);
	mo->global = wl_global_create(server->display,
				      &mate_output_manager_v1_interface,
				      MANAGER_VERSION, mo, manager_bind);
	mo->removed_id = g_signal_connect(server, "output-removed",
					  G_CALLBACK(on_output_removed), mo);
	server->mate_output = mo;
	return TRUE;
}

void
polo_backend_mate_output_fini(PoloBackendServer *server)
{
	struct polo_mate_output *mo = server->mate_output;
	struct mate_head *mh, *mh_next;
	struct mate_out *out, *out_next;
	struct wl_resource *resource, *resource_next;

	if (!mo)
		return;
	g_signal_handler_disconnect(server, mo->removed_id);
	g_clear_pointer(&mo->global, wl_global_destroy);
	wl_list_for_each_safe(mh, mh_next, &mo->heads, link)
	{
		wl_resource_set_user_data(mh->resource, NULL);
		wl_list_remove(&mh->link);
		sent_clear(&mh->sent);
		g_free(mh);
	}
	wl_list_for_each_safe(out, out_next, &mo->outputs, link)
	{
		wl_resource_set_user_data(out->resource, NULL);
		if (out->head)
			wl_list_remove(&out->head_destroyed.link);
		wl_list_remove(&out->link);
		sent_clear(&out->sent);
		g_free(out);
	}
	wl_resource_for_each_safe(resource, resource_next, &mo->managers)
	{
		wl_list_remove(wl_resource_get_link(resource));
		wl_list_init(wl_resource_get_link(resource));
	}
	server->mate_output = NULL;
	g_free(mo);
}
