/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwlr_output_manager_v1, version 4. Heads are the server's outputs, modes
 * are matched by size and refresh (libweston may rebuild an output's mode
 * list), and test and apply hand the whole configuration to the Vala
 * handler, which applies it or refuses it as one.
 */

#include <libweston/libweston.h>

#include "polo-backend-private.h"
#include "wlr-output-management-unstable-v1-server-protocol.h"

#define MANAGER_VERSION 4
#define MODE_VERSION 3

struct om_mode {
	struct wl_resource *resource;
	int width, height, refresh;
	gboolean preferred;
	gboolean gone;
};

struct om_head {
	struct wl_list link;
	struct om_manager *manager;
	struct wl_resource *resource;
	PoloBackendOutput *output;
	GPtrArray *modes;
};

struct om_manager {
	struct wl_list link;
	struct polo_output_management *om;
	struct wl_resource *resource;
	struct wl_list heads;
	struct wl_list configs;
};

struct om_config_head {
	struct wl_list link;
	struct om_config *config;
	struct om_head *head;
	struct wl_resource *resource;
	gboolean enabled;
	gboolean has_mode;
	int width, height, refresh;
	gboolean has_position;
	int x, y;
	gboolean has_transform;
	int transform;
	gboolean has_scale;
	double scale;
	gboolean has_adaptive_sync;
	gboolean adaptive_sync;
};

struct om_config {
	struct wl_list link;
	struct om_manager *manager;
	struct wl_resource *resource;
	uint32_t serial;
	gboolean used;
	struct wl_list heads;
};

struct polo_output_management {
	PoloBackendServer *server;
	struct wl_global *global;
	struct wl_list managers;
	uint32_t serial;
	PoloBackendOutputConfigHandler handler;
	gpointer handler_data;
	GDestroyNotify handler_destroy;
	gulong added_id, removed_id, changed_id;
};

static struct weston_output *
weston_output_of(PoloBackendOutput *output)
{
	return output ? polo_backend_output_get_weston_output(output) : NULL;
}

static struct weston_head *
weston_head_of(PoloBackendOutput *output)
{
	struct weston_output *wo = weston_output_of(output);

	return wo ? weston_output_get_first_head(wo) : NULL;
}

/* ---- Heads and modes -------------------------------------------------- */

static void
mode_resource_destroyed(struct wl_resource *resource)
{
	struct om_mode *mode = wl_resource_get_user_data(resource);

	if (mode)
		mode->resource = NULL;
}

static void
mode_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwlr_output_mode_v1_interface mode_impl = {
	.release = mode_release,
};

static void
om_mode_free(gpointer data)
{
	struct om_mode *mode = data;

	if (mode->resource)
		wl_resource_set_user_data(mode->resource, NULL);
	g_free(mode);
}

static struct om_mode *
find_mode(struct om_head *head, int width, int height, int refresh)
{
	for (guint i = 0; i < head->modes->len; i++) {
		struct om_mode *mode = head->modes->pdata[i];

		if (!mode->gone && mode->width == width &&
		    mode->height == height && mode->refresh == refresh)
			return mode;
	}
	return NULL;
}

static void
sync_modes(struct om_head *head, struct weston_output *wo)
{
	struct wl_client *client = wl_resource_get_client(head->resource);
	struct weston_mode *wm;

	for (guint i = 0; i < head->modes->len; i++) {
		struct om_mode *mode = head->modes->pdata[i];
		gboolean present = FALSE;

		if (mode->gone)
			continue;
		wl_list_for_each(wm, &wo->mode_list, link)
		{
			if (wm->width == mode->width &&
			    wm->height == mode->height &&
			    (int)wm->refresh == mode->refresh)
				present = TRUE;
		}
		if (!present) {
			mode->gone = TRUE;
			if (mode->resource)
				zwlr_output_mode_v1_send_finished(
					mode->resource);
		}
	}
	wl_list_for_each(wm, &wo->mode_list, link)
	{
		struct om_mode *mode;

		if (find_mode(head, wm->width, wm->height, (int)wm->refresh))
			continue;
		mode = g_new0(struct om_mode, 1);
		mode->width = wm->width;
		mode->height = wm->height;
		mode->refresh = (int)wm->refresh;
		mode->preferred = (wm->flags & WL_OUTPUT_MODE_PREFERRED) != 0;
		mode->resource = wl_resource_create(
			client, &zwlr_output_mode_v1_interface,
			MIN(wl_resource_get_version(head->resource),
			    MODE_VERSION),
			0);
		g_ptr_array_add(head->modes, mode);
		if (!mode->resource)
			continue;
		wl_resource_set_implementation(mode->resource, &mode_impl, mode,
					       mode_resource_destroyed);
		zwlr_output_head_v1_send_mode(head->resource, mode->resource);
		zwlr_output_mode_v1_send_size(mode->resource, mode->width,
					      mode->height);
		if (mode->refresh > 0)
			zwlr_output_mode_v1_send_refresh(mode->resource,
							 mode->refresh);
		if (mode->preferred)
			zwlr_output_mode_v1_send_preferred(mode->resource);
	}
}

static void
send_head_state(struct om_head *head)
{
	struct weston_output *wo = weston_output_of(head->output);
	int version = wl_resource_get_version(head->resource);

	if (!wo)
		return;
	sync_modes(head, wo);
	zwlr_output_head_v1_send_enabled(head->resource, wo->enabled);
	if (wo->enabled) {
		struct om_mode *current = wo->current_mode
			? find_mode(head, wo->current_mode->width,
				    wo->current_mode->height,
				    (int)wo->current_mode->refresh)
			: NULL;

		if (current && current->resource)
			zwlr_output_head_v1_send_current_mode(
				head->resource, current->resource);
		zwlr_output_head_v1_send_position(head->resource,
						  (int32_t)wo->pos.c.x,
						  (int32_t)wo->pos.c.y);
		zwlr_output_head_v1_send_transform(head->resource,
						   (int32_t)wo->transform);
		zwlr_output_head_v1_send_scale(
			head->resource, wl_fixed_from_int(wo->current_scale));
	}
	if (version >= ZWLR_OUTPUT_HEAD_V1_ADAPTIVE_SYNC_SINCE_VERSION)
		zwlr_output_head_v1_send_adaptive_sync(
			head->resource,
			wo->vrr_mode != WESTON_VRR_MODE_NONE
				? ZWLR_OUTPUT_HEAD_V1_ADAPTIVE_SYNC_STATE_ENABLED
				: ZWLR_OUTPUT_HEAD_V1_ADAPTIVE_SYNC_STATE_DISABLED);
}

static void
head_finish(struct om_head *head)
{
	if (!head->output)
		return;
	head->output = NULL;
	for (guint i = 0; i < head->modes->len; i++) {
		struct om_mode *mode = head->modes->pdata[i];

		if (!mode->gone && mode->resource)
			zwlr_output_mode_v1_send_finished(mode->resource);
		mode->gone = TRUE;
	}
	if (head->resource)
		zwlr_output_head_v1_send_finished(head->resource);
}

static void
head_free(struct om_head *head)
{
	struct om_config *config;
	struct om_config_head *ch;

	wl_list_for_each(config, &head->manager->configs, link)
	{
		wl_list_for_each(ch, &config->heads, link)
		{
			if (ch->head == head)
				ch->head = NULL;
		}
	}
	wl_list_remove(&head->link);
	g_ptr_array_unref(head->modes);
	g_free(head);
}

static void
head_resource_destroyed(struct wl_resource *resource)
{
	struct om_head *head = wl_resource_get_user_data(resource);

	if (!head)
		return;
	head->resource = NULL;
	head->output = NULL;
	head_free(head);
}

static void
head_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwlr_output_head_v1_interface head_impl = {
	.release = head_release,
};

static void
head_create(struct om_manager *manager, PoloBackendOutput *output)
{
	struct weston_head *wh = weston_head_of(output);
	struct om_head *head;
	int version = wl_resource_get_version(manager->resource);

	if (!wh)
		return;
	head = g_new0(struct om_head, 1);
	head->manager = manager;
	head->output = output;
	head->modes = g_ptr_array_new_with_free_func(om_mode_free);
	head->resource =
		wl_resource_create(wl_resource_get_client(manager->resource),
				   &zwlr_output_head_v1_interface, version, 0);
	if (!head->resource) {
		g_ptr_array_unref(head->modes);
		g_free(head);
		return;
	}
	wl_list_insert(manager->heads.prev, &head->link);
	wl_resource_set_implementation(head->resource, &head_impl, head,
				       head_resource_destroyed);
	zwlr_output_manager_v1_send_head(manager->resource, head->resource);
	zwlr_output_head_v1_send_name(head->resource, wh->name ? wh->name : "");
	zwlr_output_head_v1_send_description(head->resource,
					     wh->model ? wh->model : "");
	if (wh->mm_width > 0 && wh->mm_height > 0)
		zwlr_output_head_v1_send_physical_size(
			head->resource, wh->mm_width, wh->mm_height);
	if (version >= ZWLR_OUTPUT_HEAD_V1_MAKE_SINCE_VERSION) {
		if (wh->make)
			zwlr_output_head_v1_send_make(head->resource, wh->make);
		if (wh->model)
			zwlr_output_head_v1_send_model(head->resource,
						       wh->model);
		if (wh->serial_number)
			zwlr_output_head_v1_send_serial_number(
				head->resource, wh->serial_number);
	}
	send_head_state(head);
}

static void
broadcast_done(struct polo_output_management *om)
{
	struct om_manager *manager;

	om->serial++;
	wl_list_for_each(manager, &om->managers, link)
		zwlr_output_manager_v1_send_done(manager->resource, om->serial);
}

/* ---- Configurations ---------------------------------------------------- */

static struct om_config_head *
config_head_for(struct om_config *config, struct om_head *head)
{
	struct om_config_head *ch;

	wl_list_for_each(ch, &config->heads, link)
	{
		if (ch->head == head)
			return ch;
	}
	return NULL;
}

static gboolean
config_head_check(struct wl_resource *resource, struct om_config_head **out)
{
	struct om_config_head *ch = wl_resource_get_user_data(resource);

	*out = ch;
	return ch != NULL && ch->config && ch->config->manager && ch->head &&
		!ch->config->used;
}

static void
config_head_set_mode(struct wl_client *client, struct wl_resource *resource,
		     struct wl_resource *mode_resource)
{
	struct om_config_head *ch;
	struct om_mode *mode = wl_resource_get_user_data(mode_resource);
	gboolean belongs = FALSE;

	if (!config_head_check(resource, &ch))
		return;
	if (ch->has_mode) {
		wl_resource_post_error(
			resource,
			ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_ERROR_ALREADY_SET,
			"mode already set");
		return;
	}
	for (guint i = 0; mode && i < ch->head->modes->len; i++)
		belongs |= ch->head->modes->pdata[i] == mode;
	if (!belongs) {
		wl_resource_post_error(
			resource,
			ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_ERROR_INVALID_MODE,
			"mode does not belong to the head");
		return;
	}
	ch->has_mode = TRUE;
	ch->width = mode->width;
	ch->height = mode->height;
	ch->refresh = mode->refresh;
}

static void
config_head_set_custom_mode(struct wl_client *client,
			    struct wl_resource *resource, int32_t width,
			    int32_t height, int32_t refresh)
{
	struct om_config_head *ch;

	if (!config_head_check(resource, &ch))
		return;
	if (ch->has_mode) {
		wl_resource_post_error(
			resource,
			ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_ERROR_ALREADY_SET,
			"mode already set");
		return;
	}
	if (width <= 0 || height <= 0 || refresh < 0) {
		wl_resource_post_error(
			resource,
			ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_ERROR_INVALID_CUSTOM_MODE,
			"invalid custom mode %dx%d@%d", width, height, refresh);
		return;
	}
	ch->has_mode = TRUE;
	ch->width = width;
	ch->height = height;
	ch->refresh = refresh;
}

static void
config_head_set_position(struct wl_client *client, struct wl_resource *resource,
			 int32_t x, int32_t y)
{
	struct om_config_head *ch;

	if (!config_head_check(resource, &ch))
		return;
	if (ch->has_position) {
		wl_resource_post_error(
			resource,
			ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_ERROR_ALREADY_SET,
			"position already set");
		return;
	}
	ch->has_position = TRUE;
	ch->x = x;
	ch->y = y;
}

static void
config_head_set_transform(struct wl_client *client,
			  struct wl_resource *resource, int32_t transform)
{
	struct om_config_head *ch;

	if (!config_head_check(resource, &ch))
		return;
	if (ch->has_transform) {
		wl_resource_post_error(
			resource,
			ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_ERROR_ALREADY_SET,
			"transform already set");
		return;
	}
	if (transform < WL_OUTPUT_TRANSFORM_NORMAL ||
	    transform > WL_OUTPUT_TRANSFORM_FLIPPED_270) {
		wl_resource_post_error(
			resource,
			ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_ERROR_INVALID_TRANSFORM,
			"invalid transform %d", transform);
		return;
	}
	ch->has_transform = TRUE;
	ch->transform = transform;
}

static void
config_head_set_scale(struct wl_client *client, struct wl_resource *resource,
		      wl_fixed_t scale)
{
	struct om_config_head *ch;

	if (!config_head_check(resource, &ch))
		return;
	if (ch->has_scale) {
		wl_resource_post_error(
			resource,
			ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_ERROR_ALREADY_SET,
			"scale already set");
		return;
	}
	if (scale <= 0) {
		wl_resource_post_error(
			resource,
			ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_ERROR_INVALID_SCALE,
			"scale must be positive");
		return;
	}
	ch->has_scale = TRUE;
	ch->scale = wl_fixed_to_double(scale);
}

static void
config_head_set_adaptive_sync(struct wl_client *client,
			      struct wl_resource *resource, uint32_t state)
{
	struct om_config_head *ch;

	if (!config_head_check(resource, &ch))
		return;
	if (ch->has_adaptive_sync) {
		wl_resource_post_error(
			resource,
			ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_ERROR_ALREADY_SET,
			"adaptive sync already set");
		return;
	}
	if (state != ZWLR_OUTPUT_HEAD_V1_ADAPTIVE_SYNC_STATE_DISABLED &&
	    state != ZWLR_OUTPUT_HEAD_V1_ADAPTIVE_SYNC_STATE_ENABLED) {
		wl_resource_post_error(
			resource,
			ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_ERROR_INVALID_ADAPTIVE_SYNC_STATE,
			"invalid adaptive sync state %u", state);
		return;
	}
	ch->has_adaptive_sync = TRUE;
	ch->adaptive_sync =
		state == ZWLR_OUTPUT_HEAD_V1_ADAPTIVE_SYNC_STATE_ENABLED;
}

static void
config_head_resource_destroyed(struct wl_resource *resource)
{
	struct om_config_head *ch = wl_resource_get_user_data(resource);

	if (ch)
		ch->resource = NULL;
}

static const struct zwlr_output_configuration_head_v1_interface
	config_head_impl = {
		.set_mode = config_head_set_mode,
		.set_custom_mode = config_head_set_custom_mode,
		.set_position = config_head_set_position,
		.set_transform = config_head_set_transform,
		.set_scale = config_head_set_scale,
		.set_adaptive_sync = config_head_set_adaptive_sync,
	};

static gboolean
config_ready(struct wl_resource *resource, struct om_config **out)
{
	struct om_config *config = wl_resource_get_user_data(resource);

	*out = config;
	if (config->used) {
		wl_resource_post_error(
			resource,
			ZWLR_OUTPUT_CONFIGURATION_V1_ERROR_ALREADY_USED,
			"configuration already applied or tested");
		return FALSE;
	}
	return TRUE;
}

static struct om_config_head *
config_add_head(struct om_config *config, struct wl_resource *head_resource)
{
	struct om_head *head = config->manager
		? wl_resource_get_user_data(head_resource)
		: NULL;
	struct om_config_head *ch;

	if (head && config_head_for(config, head)) {
		wl_resource_post_error(
			config->resource,
			ZWLR_OUTPUT_CONFIGURATION_V1_ERROR_ALREADY_CONFIGURED_HEAD,
			"head configured twice");
		return NULL;
	}
	ch = g_new0(struct om_config_head, 1);
	ch->config = config;
	ch->head = head;
	wl_list_insert(config->heads.prev, &ch->link);
	return ch;
}

static void
config_enable_head(struct wl_client *client, struct wl_resource *resource,
		   uint32_t id, struct wl_resource *head_resource)
{
	struct om_config *config;
	struct om_config_head *ch;

	if (!config_ready(resource, &config) ||
	    !(ch = config_add_head(config, head_resource)))
		return;
	ch->enabled = TRUE;
	ch->resource = wl_resource_create(
		client, &zwlr_output_configuration_head_v1_interface,
		wl_resource_get_version(resource), id);
	if (!ch->resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(ch->resource, &config_head_impl, ch,
				       config_head_resource_destroyed);
}

static void
config_disable_head(struct wl_client *client, struct wl_resource *resource,
		    struct wl_resource *head_resource)
{
	struct om_config *config;

	if (config_ready(resource, &config))
		config_add_head(config, head_resource);
}

static void
fill_request(PoloBackendOutputRequest *request, struct om_config_head *ch)
{
	struct weston_output *wo = weston_output_of(ch->head->output);
	struct weston_mode *mode = wo->current_mode;
	struct weston_mode *wm;

	request->output = ch->head->output;
	request->enabled = ch->enabled;
	if (!wo->enabled || !mode) {
		mode = NULL;
		wl_list_for_each(wm, &wo->mode_list, link)
		{
			if (!mode || (wm->flags & WL_OUTPUT_MODE_PREFERRED))
				mode = wm;
		}
	}
	request->width = ch->has_mode ? ch->width : mode ? mode->width : 0;
	request->height = ch->has_mode ? ch->height : mode ? mode->height : 0;
	request->refresh = ch->has_mode ? ch->refresh
		: mode                  ? (int)mode->refresh
					: 0;
	request->x = ch->has_position ? ch->x : (int)wo->pos.c.x;
	request->y = ch->has_position ? ch->y : (int)wo->pos.c.y;
	request->transform = ch->has_transform
		? ch->transform
		: (wo->transform == UINT32_MAX ? 0 : (int)wo->transform);
	request->scale = ch->has_scale
		? ch->scale
		: (wo->current_scale > 0 ? wo->current_scale : 1);
	request->adaptive_sync = ch->has_adaptive_sync
		? ch->adaptive_sync
		: wo->vrr_mode != WESTON_VRR_MODE_NONE;
}

static void
config_finish(struct wl_resource *resource, gboolean test)
{
	struct om_config *config;
	struct polo_output_management *om;
	struct om_head *head;
	struct om_config_head *ch;
	GArray *requests;
	gboolean ok;

	if (!config_ready(resource, &config))
		return;
	config->used = TRUE;
	if (!config->manager || config->serial != config->manager->om->serial) {
		zwlr_output_configuration_v1_send_cancelled(resource);
		return;
	}
	om = config->manager->om;
	wl_list_for_each(head, &config->manager->heads, link)
	{
		if (head->output && !config_head_for(config, head)) {
			wl_resource_post_error(
				resource,
				ZWLR_OUTPUT_CONFIGURATION_V1_ERROR_UNCONFIGURED_HEAD,
				"a head was left out of the configuration");
			return;
		}
	}
	requests = g_array_new(FALSE, TRUE, sizeof(PoloBackendOutputRequest));
	wl_list_for_each(ch, &config->heads, link)
	{
		PoloBackendOutputRequest request = { 0 };

		if (!ch->head || !ch->head->output) {
			g_array_free(requests, TRUE);
			zwlr_output_configuration_v1_send_cancelled(resource);
			return;
		}
		fill_request(&request, ch);
		g_array_append_val(requests, request);
	}
	ok = om->handler &&
		om->handler((const PoloBackendOutputRequest *)requests->data,
			    requests->len, test, om->handler_data);
	g_array_free(requests, TRUE);
	if (ok)
		zwlr_output_configuration_v1_send_succeeded(resource);
	else
		zwlr_output_configuration_v1_send_failed(resource);
}

static void
config_apply(struct wl_client *client, struct wl_resource *resource)
{
	config_finish(resource, FALSE);
}

static void
config_test(struct wl_client *client, struct wl_resource *resource)
{
	config_finish(resource, TRUE);
}

static void
config_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwlr_output_configuration_v1_interface config_impl = {
	.enable_head = config_enable_head,
	.disable_head = config_disable_head,
	.apply = config_apply,
	.test = config_test,
	.destroy = config_destroy_request,
};

static void
config_resource_destroyed(struct wl_resource *resource)
{
	struct om_config *config = wl_resource_get_user_data(resource);
	struct om_config_head *ch, *next;

	wl_list_remove(&config->link);
	wl_list_for_each_safe(ch, next, &config->heads, link)
	{
		if (ch->resource)
			wl_resource_set_user_data(ch->resource, NULL);
		wl_list_remove(&ch->link);
		g_free(ch);
	}
	g_free(config);
}

/* ---- Manager ----------------------------------------------------------- */

static void
manager_create_configuration(struct wl_client *client,
			     struct wl_resource *resource, uint32_t id,
			     uint32_t serial)
{
	struct om_manager *manager = wl_resource_get_user_data(resource);
	struct om_config *config = g_new0(struct om_config, 1);

	config->manager = manager;
	config->serial = serial;
	wl_list_init(&config->heads);
	wl_list_insert(&manager->configs, &config->link);
	config->resource = wl_resource_create(
		client, &zwlr_output_configuration_v1_interface,
		wl_resource_get_version(resource), id);
	if (!config->resource) {
		g_free(config);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(config->resource, &config_impl, config,
				       config_resource_destroyed);
}

static void
manager_stop(struct wl_client *client, struct wl_resource *resource)
{
	zwlr_output_manager_v1_send_finished(resource);
	wl_resource_destroy(resource);
}

static const struct zwlr_output_manager_v1_interface manager_impl = {
	.create_configuration = manager_create_configuration,
	.stop = manager_stop,
};

static void
manager_resource_destroyed(struct wl_resource *resource)
{
	struct om_manager *manager = wl_resource_get_user_data(resource);
	struct om_head *head, *next;
	struct om_config *config, *config_next;

	wl_list_for_each_safe(config, config_next, &manager->configs, link)
	{
		struct om_config_head *ch;

		wl_list_for_each(ch, &config->heads, link) ch->head = NULL;
		config->manager = NULL;
		wl_list_remove(&config->link);
		wl_list_init(&config->link);
	}
	wl_list_for_each_safe(head, next, &manager->heads, link)
	{
		if (head->resource) {
			wl_resource_set_user_data(head->resource, NULL);
			head->resource = NULL;
		}
		head_free(head);
	}
	wl_list_remove(&manager->link);
	g_free(manager);
}

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct polo_output_management *om = data;
	struct om_manager *manager = g_new0(struct om_manager, 1);

	manager->om = om;
	wl_list_init(&manager->heads);
	wl_list_init(&manager->configs);
	manager->resource =
		wl_resource_create(client, &zwlr_output_manager_v1_interface,
				   MIN(version, MANAGER_VERSION), id);
	if (!manager->resource) {
		g_free(manager);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(manager->resource, &manager_impl,
				       manager, manager_resource_destroyed);
	wl_list_insert(&om->managers, &manager->link);
	for (guint i = 0; i < om->server->outputs->len; i++)
		head_create(manager, om->server->outputs->pdata[i]);
	zwlr_output_manager_v1_send_done(manager->resource, om->serial);
}

/* ---- Server events ----------------------------------------------------- */

static void
on_output_added(PoloBackendServer *server, PoloBackendOutput *output,
		gpointer data)
{
	struct polo_output_management *om = data;
	struct om_manager *manager;

	wl_list_for_each(manager, &om->managers, link)
		head_create(manager, output);
	broadcast_done(om);
}

static void
on_output_removed(PoloBackendServer *server, PoloBackendOutput *output,
		  gpointer data)
{
	struct polo_output_management *om = data;
	struct om_manager *manager;
	struct om_head *head;

	wl_list_for_each(manager, &om->managers, link)
	{
		wl_list_for_each(head, &manager->heads, link)
		{
			if (head->output == output)
				head_finish(head);
		}
	}
	broadcast_done(om);
}

void
polo_backend_server_set_monitor_count(PoloBackendServer *server, guint count)
{
	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	server->monitor_count = count;
}

void
polo_backend_server_output_state_changed(PoloBackendServer *server)
{
	struct polo_output_management *om;
	struct om_manager *manager;
	struct om_head *head;

	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	om = server->output_management;
	if (!om)
		return;
	wl_list_for_each(manager, &om->managers, link)
	{
		wl_list_for_each(head, &manager->heads, link)
		{
			if (head->output && head->resource)
				send_head_state(head);
		}
	}
	broadcast_done(om);
}

static void
on_outputs_changed(PoloBackendServer *server, gpointer data)
{
	polo_backend_server_output_state_changed(server);
}

void
polo_backend_server_set_output_config_handler(
	PoloBackendServer *server, PoloBackendOutputConfigHandler handler,
	gpointer user_data, GDestroyNotify destroy)
{
	struct polo_output_management *om;

	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	om = server->output_management;
	if (!om) {
		if (destroy)
			destroy(user_data);
		return;
	}
	if (om->handler_destroy)
		om->handler_destroy(om->handler_data);
	om->handler = handler;
	om->handler_data = user_data;
	om->handler_destroy = destroy;
}

gboolean
polo_backend_output_management_init(PoloBackendServer *server, GError **error)
{
	struct polo_output_management *om =
		g_new0(struct polo_output_management, 1);

	(void)error;
	om->server = server;
	wl_list_init(&om->managers);
	om->global = wl_global_create(server->display,
				      &zwlr_output_manager_v1_interface,
				      MANAGER_VERSION, om, manager_bind);
	om->added_id = g_signal_connect(server, "output-added",
					G_CALLBACK(on_output_added), om);
	om->removed_id = g_signal_connect(server, "output-removed",
					  G_CALLBACK(on_output_removed), om);
	om->changed_id = g_signal_connect(server, "outputs-changed",
					  G_CALLBACK(on_outputs_changed), om);
	server->output_management = om;
	return TRUE;
}

void
polo_backend_output_management_fini(PoloBackendServer *server)
{
	struct polo_output_management *om = server->output_management;

	if (!om)
		return;
	g_signal_handler_disconnect(server, om->added_id);
	g_signal_handler_disconnect(server, om->removed_id);
	g_signal_handler_disconnect(server, om->changed_id);
	g_clear_pointer(&om->global, wl_global_destroy);
	if (om->handler_destroy)
		om->handler_destroy(om->handler_data);
	server->output_management = NULL;
	g_free(om);
}
