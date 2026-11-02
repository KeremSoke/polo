/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PoloBackendOutput: an enabled weston_output, plus its solid background.
 */

#include <libweston/libweston.h>
#include <libweston/backend-drm.h>
#include <libdisplay-info/info.h>
#include <libdisplay-info/edid.h>
#include <stdlib.h>

#include "polo-backend-private.h"

struct _PoloBackendOutput {
	GObject parent_instance;

	PoloBackendServer *server;
	struct weston_output *output;
	char *name;
	struct wl_listener head_destroy;

	struct weston_surface *background_surface;
	struct weston_view *background_view;
	struct weston_buffer_reference *background_buffer;

	gboolean has_work_area;
	struct weston_geometry work_area;
	gboolean primary;
};

enum {
	SIGNAL_DESTROYED,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(PoloBackendOutput, polo_backend_output, G_TYPE_OBJECT)

static void
clear_background(PoloBackendOutput *self)
{
	if (!self->background_view)
		return;

	weston_view_destroy(self->background_view);
	weston_surface_unref(self->background_surface);
	weston_buffer_destroy_solid(self->background_buffer);
	self->background_view = NULL;
	self->background_surface = NULL;
	self->background_buffer = NULL;
}

static void
head_destroyed(struct wl_listener *listener, void *data)
{
	PoloBackendOutput *self = wl_container_of(listener, self, head_destroy);
	polo_backend_server_remove_output(self->server, self);
}

PoloBackendOutput *
polo_backend_output_new(PoloBackendServer *server, struct weston_output *output)
{
	PoloBackendOutput *self = g_object_new(POLO_BACKEND_TYPE_OUTPUT, NULL);

	self->server = server;
	self->output = output;
	self->name = g_strdup(output->name);
	self->head_destroy.notify = head_destroyed;
	weston_head_add_destroy_listener(
		weston_output_iterate_heads(output, NULL), &self->head_destroy);
	return self;
}

struct weston_output *
polo_backend_output_get_weston_output(PoloBackendOutput *output)
{
	return output->output;
}

void
polo_backend_output_destroy(PoloBackendOutput *self)
{
	if (!self->output)
		return;

	clear_background(self);
	wl_list_remove(&self->head_destroy.link);
	self->output = NULL;
	g_signal_emit(self, signals[SIGNAL_DESTROYED], 0);
}

const char *
polo_backend_output_get_name(PoloBackendOutput *output)
{
	g_return_val_if_fail(POLO_BACKEND_IS_OUTPUT(output), NULL);
	return output->name;
}

void
polo_backend_output_get_geometry(PoloBackendOutput *output, int *x, int *y,
				 int *width, int *height)
{
	g_return_if_fail(POLO_BACKEND_IS_OUTPUT(output));

	if (!output->output) {
		*x = *y = *width = *height = 0;
		return;
	}
	*x = (int)output->output->pos.c.x;
	*y = (int)output->output->pos.c.y;
	*width = output->output->width;
	*height = output->output->height;
}

int
polo_backend_output_get_scale(PoloBackendOutput *output)
{
	g_return_val_if_fail(POLO_BACKEND_IS_OUTPUT(output), 1);
	return output->output ? output->output->current_scale : 1;
}

void
polo_backend_output_set_work_area(PoloBackendOutput *output, int x, int y,
				  int width, int height)
{
	struct weston_geometry area = { x, y, width, height };
	gboolean has = width > 0 && height > 0;

	g_return_if_fail(POLO_BACKEND_IS_OUTPUT(output));
	if (has == output->has_work_area &&
	    memcmp(&area, &output->work_area, sizeof area) == 0)
		return;
	output->has_work_area = has;
	output->work_area = area;
	polo_backend_xfce_output_changed(output, FALSE);
}

void
polo_backend_output_set_primary(PoloBackendOutput *output, gboolean primary)
{
	g_return_if_fail(POLO_BACKEND_IS_OUTPUT(output));
	if (output->primary == !!primary)
		return;
	output->primary = !!primary;
	if (primary)
		polo_backend_xfce_output_changed(output, TRUE);
}

gboolean
polo_backend_output_get_primary(PoloBackendOutput *output)
{
	return output->primary;
}

PoloBackendServer *
polo_backend_output_get_server(PoloBackendOutput *output)
{
	return output->server;
}

gboolean
polo_backend_output_get_work_area(PoloBackendOutput *output,
				  struct weston_geometry *area)
{
	if (!output->has_work_area)
		return FALSE;
	*area = output->work_area;
	return TRUE;
}

void
polo_backend_output_set_background_color(PoloBackendOutput *output, double red,
					 double green, double blue)
{
	struct weston_compositor *compositor;
	struct weston_output *wo;
	PoloBackendLayer *layer;

	g_return_if_fail(POLO_BACKEND_IS_OUTPUT(output));
	if (!output->output)
		return;

	wo = output->output;
	compositor = wo->compositor;
	clear_background(output);

	output->background_surface = weston_surface_create(compositor, NULL);
	output->background_view =
		weston_view_create(output->background_surface);
	output->background_buffer = weston_buffer_create_solid_rgba(
		compositor, red, green, blue, 1.0);

	weston_surface_set_label(
		output->background_surface,
		g_strdup_printf("polo background %s", output->name));
	weston_surface_attach_solid(output->background_surface,
				    output->background_buffer, wo->width,
				    wo->height);
	/* The background never takes input: clicks on it stay unfocused. */
	pixman_region32_fini(&output->background_surface->input);
	pixman_region32_init(&output->background_surface->input);

	weston_view_set_position(output->background_view, wo->pos);
	layer = polo_backend_server_get_layer(output->server,
					      POLO_BACKEND_LAYER_BACKGROUND);
	weston_view_move_to_layer(output->background_view,
				  &layer->layer.view_list);
	weston_surface_map(output->background_surface);

	/* libweston holds back an output's first repaint until the shell
	 * says it has content, so no uninitialised buffer is scanned out. */
	weston_output_set_ready(wo);
	weston_compositor_schedule_repaint(compositor);
}

static void
polo_backend_output_finalize(GObject *object)
{
	PoloBackendOutput *self = POLO_BACKEND_OUTPUT(object);

	clear_background(self);
	g_free(self->name);
	G_OBJECT_CLASS(polo_backend_output_parent_class)->finalize(object);
}

static void
polo_backend_output_class_init(PoloBackendOutputClass *klass)
{
	G_OBJECT_CLASS(klass)->finalize = polo_backend_output_finalize;

	signals[SIGNAL_DESTROYED] = g_signal_new(
		"destroyed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_backend_output_init(PoloBackendOutput *self)
{
}

/* Head identity must use the EDID numeric product/serial for MateRR files;
 * wl_output's human-readable model and serial are different fields. */
static struct weston_head *
output_head(PoloBackendOutput *self)
{
	return self->output ? weston_output_iterate_heads(self->output, NULL)
			    : NULL;
}

const char *
polo_backend_output_get_make(PoloBackendOutput *self)
{
	struct weston_head *head = output_head(self);
	return head && head->make ? head->make : "";
}
const char *
polo_backend_output_get_model(PoloBackendOutput *self)
{
	struct weston_head *head = output_head(self);
	return head && head->model ? head->model : "";
}
const char *
polo_backend_output_get_serial(PoloBackendOutput *self)
{
	struct weston_head *head = output_head(self);
	return head && head->serial_number ? head->serial_number : "";
}

static const struct di_edid_vendor_product *
identity(PoloBackendOutput *self)
{
	struct weston_head *head = output_head(self);
	const struct di_edid *edid;
	if (!head || !head->display_info)
		return NULL;
	edid = di_info_get_edid(head->display_info);
	return edid ? di_edid_get_vendor_product(edid) : NULL;
}
char *
polo_backend_output_get_vendor(PoloBackendOutput *self)
{
	const struct di_edid_vendor_product *id = identity(self);
	return id ? g_strndup(id->manufacturer, 3) : g_strdup("");
}

char *
polo_backend_output_get_product(PoloBackendOutput *self)
{
	const struct di_edid_vendor_product *id = identity(self);
	return id ? g_strdup_printf("0x%04x", id->product) : g_strdup("");
}
char *
polo_backend_output_get_edid_serial(PoloBackendOutput *self)
{
	const struct di_edid_vendor_product *id = identity(self);
	return id ? g_strdup_printf("0x%08x", id->serial) : g_strdup("");
}
void
polo_backend_output_get_physical_size(PoloBackendOutput *self, int *width,
				      int *height)
{
	struct weston_head *head = output_head(self);
	*width = head ? head->mm_width : 0;
	*height = head ? head->mm_height : 0;
}
gboolean
polo_backend_output_get_internal(PoloBackendOutput *self)
{
	struct weston_head *head = output_head(self);
	return head && head->connection_internal;
}
gboolean
polo_backend_output_get_enabled(PoloBackendOutput *self)
{
	return self->output && self->output->enabled;
}
int
polo_backend_output_get_transform(PoloBackendOutput *self)
{
	return self->output ? self->output->transform : 0;
}
int
polo_backend_output_get_mode_count(PoloBackendOutput *self)
{
	return self->output ? wl_list_length(&self->output->mode_list) : 0;
}
gboolean
polo_backend_output_get_mode(PoloBackendOutput *self, int index,
			     PoloBackendMode *result)
{
	struct weston_mode *mode;
	if (!self->output || index < 0)
		return FALSE;
	wl_list_for_each(mode, &self->output->mode_list, link)
	{
		if (index-- != 0)
			continue;
		*result = (PoloBackendMode){
			mode->width, mode->height, mode->refresh,
			(mode->flags & WL_OUTPUT_MODE_PREFERRED) != 0,
			mode == self->output->current_mode
		};
		return TRUE;
	}
	return FALSE;
}

gboolean
polo_backend_output_apply(PoloBackendOutput *self, int width, int height,
			  int refresh, int scale, int transform, int x, int y,
			  gboolean enabled, GError **error)
{
	struct weston_output *output = self->output;
	struct weston_mode *mode, *chosen = NULL;
	if (!output || scale < 1 || scale > 8 || transform < 0 || transform > 7)
		goto invalid;
	wl_list_for_each(mode, &output->mode_list, link)
	{
		if (mode->width == width && mode->height == height &&
		    (refresh == 0 ||
		     abs((int)mode->refresh - refresh) <= 1000)) {
			if (!chosen ||
			    abs((int)mode->refresh - refresh) <
				    abs((int)chosen->refresh - refresh))
				chosen = mode;
		}
	}
	if (enabled && !chosen)
		goto invalid;
	if (!enabled) {
		clear_background(self);
		weston_output_disable(output);
		return TRUE;
	}
	if (output->enabled) {
		if (output->current_mode != chosen &&
		    weston_output_mode_set_native(output, chosen, scale) < 0)
			goto failed;
		weston_output_set_scale(output, scale);
		weston_output_set_transform(output, transform);
	} else {
		if (self->server->kind == POLO_BACKEND_KIND_DRM) {
			const struct weston_drm_output_api *api =
				weston_drm_output_get_api(output->compositor);
			char modeline[64];
			g_snprintf(modeline, sizeof(modeline), "%dx%d@%d",
				   width, height, (refresh + 500) / 1000);
			if (!api ||
			    api->set_mode(output,
					  WESTON_DRM_BACKEND_OUTPUT_PREFERRED,
					  modeline) < 0)
				goto failed;
		} else {
			output->current_mode = chosen;
		}
		weston_output_set_scale(output, scale);
		/* Before enabling, libweston only accepts a transform the first
		 * time: once one is set (by a previous enable, or a disable
		 * that kept it), set_transform takes the full path and touches
		 * repaint state that does not exist yet. Change it after
		 * enabling instead. */
		if (output->transform == UINT32_MAX)
			weston_output_set_transform(output, transform);
		if (weston_output_enable(output) < 0)
			goto failed;
		if (output->transform != (uint32_t)transform)
			weston_output_set_transform(output, transform);
	}
	weston_output_set_position(
		output,
		(struct weston_coord_global){ .c = weston_coord(x, y) });
	return TRUE;
invalid:
	g_set_error(error, POLO_BACKEND_ERROR, POLO_BACKEND_ERROR_FAILED,
		    "invalid mode, scale or transform for %s", self->name);
	return FALSE;
failed:
	g_set_error(error, POLO_BACKEND_ERROR, POLO_BACKEND_ERROR_BACKEND,
		    "cannot apply output %s", self->name);
	return FALSE;
}
