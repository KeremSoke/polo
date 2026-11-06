/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_image_copy_capture_manager_v1 with output and foreign toplevel
 * sources (ext-image-capture-source-v1), for xdg-desktop-portal-wlr and
 * grim. Outputs are read back by the renderer on their next repaint
 * (libweston's output capture); toplevels and cursors are copied from the
 * surface's buffer. Only shm buffers are offered. Who may bind is decided
 * by the global filter.
 */

#include <string.h>
#include <time.h>

#include "ext-foreign-toplevel-list-v1-server-protocol.h"
#include "ext-image-capture-source-v1-server-protocol.h"
#include "ext-image-copy-capture-v1-server-protocol.h"
#include "foreign-toplevel-private.h"
#include "protocol-util.h"

#define FOURCC_ARGB8888 0x34325241u
#define FOURCC_XRGB8888 0x34325258u
#define IDLE_FRAME_MS 500

struct capture_target {
	struct weston_output *output;
	struct wl_listener output_destroy;
	struct polo_surface_ref surface;
	void (*gone)(struct capture_target *target);
};

struct capture_source {
	struct capture_target target;
};

enum session_kind {
	SESSION_OUTPUT,
	SESSION_SURFACE,
	SESSION_CURSOR,
};

struct cursor_session;
struct frame;

struct session {
	struct wl_resource *resource;
	PoloBackendServer *server;
	enum session_kind kind;
	struct capture_target target;
	struct wl_listener output_frame;
	struct weston_capture_source *csrc;
	struct polo_surface_ref sprite;
	struct wl_listener commit;
	bool watching;
	struct cursor_session *cursor;
	int width, height;
	uint32_t shm_format;
	pixman_region32_t damage;
	bool full_damage;
	bool dirty;
	bool stopped;
	struct frame *frame;
	struct wl_event_source *idle;
	struct wl_event_source *timer;
};

struct frame {
	struct wl_resource *resource;
	struct session *session;
	struct polo_resource_ref buffer;
	pixman_region32_t damage;
	bool captured;
	bool queued;
	bool served;
	bool completed;
	bool finished;
};

struct cursor_session {
	struct wl_resource *resource;
	PoloBackendServer *server;
	struct capture_target target;
	struct weston_pointer *pointer;
	struct wl_listener pointer_destroy;
	struct wl_listener motion;
	struct wl_listener focus;
	struct session *session;
	bool entered;
	int32_t x, y;
	int32_t hotspot_x, hotspot_y;
};

static void
target_output_destroyed(struct wl_listener *listener, void *data)
{
	struct capture_target *target =
		wl_container_of(listener, target, output_destroy);

	wl_list_remove(&target->output_destroy.link);
	target->output = NULL;
	if (target->gone)
		target->gone(target);
}

static void
target_surface_gone(struct polo_surface_ref *ref)
{
	struct capture_target *target = wl_container_of(ref, target, surface);

	ref->surface = NULL;
	if (target->gone)
		target->gone(target);
}

static void
target_set(struct capture_target *target, struct weston_output *output,
	   struct weston_surface *surface)
{
	if (output && output->enabled) {
		target->output = output;
		target->output_destroy.notify = target_output_destroyed;
		wl_signal_add(&output->destroy_signal, &target->output_destroy);
	}
	if (surface)
		polo_surface_ref_set(&target->surface, surface,
				     target_surface_gone);
}

static void
target_copy(struct capture_target *dst, const struct capture_target *src)
{
	target_set(dst, src->output, src->surface.surface);
}

static void
target_clear(struct capture_target *target)
{
	if (target->output) {
		wl_list_remove(&target->output_destroy.link);
		target->output = NULL;
	}
	polo_surface_ref_clear(&target->surface);
}

static bool
target_alive(const struct capture_target *target)
{
	return target->output || target->surface.surface;
}

static struct weston_view *
surface_first_view(struct weston_surface *surface)
{
	struct weston_view *view;

	if (wl_list_empty(&surface->views))
		return NULL;
	return wl_container_of(surface->views.next, view, surface_link);
}

static uint32_t
shm_format_from_fourcc(uint32_t fourcc)
{
	if (fourcc == FOURCC_ARGB8888)
		return WL_SHM_FORMAT_ARGB8888;
	if (fourcc == FOURCC_XRGB8888)
		return WL_SHM_FORMAT_XRGB8888;
	return fourcc;
}

static bool
read_shm(struct wl_shm_buffer *shm, uint32_t *pixels, int width, int height)
{
	uint32_t format = wl_shm_buffer_get_format(shm);
	int32_t stride = wl_shm_buffer_get_stride(shm);
	const uint8_t *in;

	if ((format != WL_SHM_FORMAT_ARGB8888 &&
	     format != WL_SHM_FORMAT_XRGB8888) ||
	    wl_shm_buffer_get_width(shm) != width ||
	    wl_shm_buffer_get_height(shm) != height)
		return false;
	wl_shm_buffer_begin_access(shm);
	in = wl_shm_buffer_get_data(shm);
	for (int y = 0; y < height; y++) {
		const uint32_t *row =
			(const uint32_t *)(in + (size_t)y * stride);
		uint32_t *out = pixels + (size_t)y * width;

		for (int x = 0; x < width; x++)
			out[x] = format == WL_SHM_FORMAT_XRGB8888
				? row[x] | 0xff000000u
				: row[x];
	}
	wl_shm_buffer_end_access(shm);
	return true;
}

static bool
read_surface(struct weston_surface *surface, uint32_t *pixels, int width,
	     int height)
{
	struct weston_buffer *src = surface->buffer_ref.buffer;
	size_t n = (size_t)width * height;

	if (src && src->type == WESTON_BUFFER_SHM && src->shm_buffer &&
	    read_shm(src->shm_buffer, pixels, width, height))
		return true;
	if (weston_surface_copy_content(surface, pixels, n * 4, 0, 0, width,
					height) < 0)
		return false;
	for (size_t i = 0; i < n; i++) {
		uint32_t v = pixels[i];

		pixels[i] = (v & 0xff00ff00u) | ((v & 0xffu) << 16) |
			((v >> 16) & 0xffu);
	}
	return true;
}

static bool
copy_surface(struct weston_surface *surface, struct wl_shm_buffer *dst,
	     int width, int height)
{
	uint32_t *pixels = g_new(uint32_t, (size_t)width *height);
	int32_t stride = wl_shm_buffer_get_stride(dst);
	uint8_t *out;
	bool ok = read_surface(surface, pixels, width, height);

	if (ok) {
		wl_shm_buffer_begin_access(dst);
		out = wl_shm_buffer_get_data(dst);
		for (int y = 0; y < height; y++)
			memcpy(out + (size_t)y * stride,
			       pixels + (size_t)y * width, (size_t)width * 4);
		wl_shm_buffer_end_access(dst);
	}
	g_free(pixels);
	return ok;
}

static void
clear_shm(struct wl_shm_buffer *dst, int height)
{
	wl_shm_buffer_begin_access(dst);
	memset(wl_shm_buffer_get_data(dst), 0,
	       (size_t)wl_shm_buffer_get_stride(dst) * height);
	wl_shm_buffer_end_access(dst);
}

static void
frame_release(struct frame *frame)
{
	frame->finished = true;
	if (frame->session && frame->session->frame == frame) {
		frame->session->frame = NULL;
		if (frame->session->timer)
			wl_event_source_timer_update(frame->session->timer, 0);
	}
	frame->session = NULL;
}

static int
session_timer_fired(void *data);

static void
session_arm_timer(struct session *session)
{
	if (!session->timer)
		session->timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(session->server->display),
			session_timer_fired, session);
	if (session->timer)
		wl_event_source_timer_update(session->timer, IDLE_FRAME_MS);
}

static void
frame_finish_failed(struct frame *frame, uint32_t reason)
{
	if (frame->finished)
		return;
	frame_release(frame);
	ext_image_copy_capture_frame_v1_send_failed(frame->resource, reason);
}

static void
frame_finish_ready(struct frame *frame, pixman_region32_t *damage,
		   uint32_t transform, struct weston_compositor *compositor)
{
	struct timespec now;
	pixman_box32_t *rects;
	int n;

	if (frame->finished)
		return;
	frame_release(frame);
	ext_image_copy_capture_frame_v1_send_transform(frame->resource,
						       transform);
	rects = pixman_region32_rectangles(damage, &n);
	for (int i = 0; i < n; i++)
		ext_image_copy_capture_frame_v1_send_damage(
			frame->resource, rects[i].x1, rects[i].y1,
			rects[i].x2 - rects[i].x1, rects[i].y2 - rects[i].y1);
	clock_gettime(compositor->presentation_clock, &now);
	ext_image_copy_capture_frame_v1_send_presentation_time(
		frame->resource, (uint32_t)((uint64_t)now.tv_sec >> 32),
		(uint32_t)now.tv_sec, (uint32_t)now.tv_nsec);
	ext_image_copy_capture_frame_v1_send_ready(frame->resource);
}

static void
session_send_constraints(struct session *session)
{
	ext_image_copy_capture_session_v1_send_buffer_size(
		session->resource, session->width, session->height);
	ext_image_copy_capture_session_v1_send_shm_format(session->resource,
							  session->shm_format);
	ext_image_copy_capture_session_v1_send_done(session->resource);
}

static void
session_drop_csrc(struct session *session)
{
	g_clear_pointer(&session->csrc, weston_capture_source_destroy);
}

static void
session_unwatch(struct session *session)
{
	if (!session->watching)
		return;
	wl_list_remove(&session->commit.link);
	session->watching = false;
}

static void
session_drop_target(struct session *session)
{
	session_drop_csrc(session);
	if (session->kind == SESSION_OUTPUT && session->target.output)
		wl_list_remove(&session->output_frame.link);
	session_unwatch(session);
	target_clear(&session->target);
	polo_surface_ref_clear(&session->sprite);
	g_clear_pointer(&session->idle, wl_event_source_remove);
	g_clear_pointer(&session->timer, wl_event_source_remove);
}

static void
session_stop(struct session *session)
{
	if (session->stopped)
		return;
	session->stopped = true;
	session_drop_target(session);
	if (session->frame && session->frame->captured)
		frame_finish_failed(
			session->frame,
			EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED);
	ext_image_copy_capture_session_v1_send_stopped(session->resource);
}

static void
session_target_gone(struct capture_target *target)
{
	struct session *session = wl_container_of(target, session, target);

	if (session->kind == SESSION_OUTPUT) {
		wl_list_remove(&session->output_frame.link);
		session_drop_csrc(session);
	}
	session_unwatch(session);
	session_stop(session);
}

static const struct weston_capture_callbacks output_callbacks;

static void
session_output_csrc(struct session *session)
{
	session_drop_csrc(session);
	session->csrc = weston_output_capture_source_create(
		session->target.output, &output_callbacks, session);
}

static bool
session_output_constraints(struct session *session)
{
	uint32_t fourcc;

	if (!weston_output_get_capture_info(session->target.output,
					    &session->width, &session->height,
					    &fourcc))
		return false;
	session->shm_format = shm_format_from_fourcc(fourcc);
	return true;
}

static void
output_capture_complete(void *data)
{
	struct session *session = data;
	struct frame *frame = session->frame;

	if (!frame)
		return;
	frame->completed = true;
	if (frame->served) {
		frame_finish_ready(frame, &frame->damage,
				   session->target.output->transform,
				   session->target.output->compositor);
		session->full_damage = false;
	}
}

static void
output_capture_failed(void *data, const char *msg)
{
	struct session *session = data;

	session->full_damage = true;
	if (session->frame)
		frame_finish_failed(
			session->frame,
			EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);
}

static void
output_capture_retry(void *data)
{
	struct session *session = data;

	session->full_damage = true;
	if (!session_output_constraints(session)) {
		session_stop(session);
		return;
	}
	session_send_constraints(session);
	if (session->frame)
		frame_finish_failed(
			session->frame,
			EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS);
}

static const struct weston_capture_callbacks output_callbacks = {
	.complete = output_capture_complete,
	.failed = output_capture_failed,
	.retry = output_capture_retry,
};

static void
global_to_buffer(pixman_region32_t *dst, struct weston_output *output,
		 pixman_region32_t *global, int width, int height)
{
	pixman_box32_t *rects;
	int n;

	rects = pixman_region32_rectangles(global, &n);
	for (int i = 0; i < n; i++) {
		struct weston_coord a = weston_matrix_transform_coord(
			&output->matrix,
			weston_coord(rects[i].x1, rects[i].y1));
		struct weston_coord b = weston_matrix_transform_coord(
			&output->matrix,
			weston_coord(rects[i].x2, rects[i].y2));

		pixman_region32_union_rect(
			dst, dst, (int)MIN(a.x, b.x), (int)MIN(a.y, b.y),
			(unsigned)ABS(b.x - a.x), (unsigned)ABS(b.y - a.y));
	}
	pixman_region32_intersect_rect(dst, dst, 0, 0, width, height);
}

static void
session_output_frame(struct wl_listener *listener, void *data)
{
	struct session *session =
		wl_container_of(listener, session, output_frame);
	struct frame *frame = session->frame;
	pixman_region32_t region;

	pixman_region32_init(&region);
	global_to_buffer(&region, session->target.output, data, session->width,
			 session->height);
	if (frame && frame->queued && !frame->served && !frame->finished) {
		frame->served = true;
		if (session->full_damage)
			pixman_region32_init_rect(&frame->damage, 0, 0,
						  session->width,
						  session->height);
		else
			pixman_region32_union(&frame->damage, &session->damage,
					      &region);
		pixman_region32_clear(&session->damage);
		if (frame->completed)
			output_capture_complete(session);
	} else {
		pixman_region32_union(&session->damage, &session->damage,
				      &region);
	}
	pixman_region32_fini(&region);
}

static void
session_output_capture(struct session *session, struct frame *frame)
{
	struct weston_output *output = session->target.output;
	struct weston_buffer *buffer;

	buffer = weston_buffer_from_resource(output->compositor,
					     frame->buffer.resource);
	if (!buffer || buffer->type != WESTON_BUFFER_SHM) {
		frame_finish_failed(
			frame,
			EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS);
		return;
	}
	switch (weston_capture_source_capture(session->csrc, buffer)) {
	case WESTON_CAPTURE_QUEUED:
		frame->queued = true;
		if (session->full_damage ||
		    pixman_region32_not_empty(&session->damage))
			weston_output_schedule_repaint(output);
		else
			session_arm_timer(session);
		break;
	case WESTON_CAPTURE_INCOMPATIBLE:
		output_capture_retry(session);
		break;
	case WESTON_CAPTURE_BUSY:
	case WESTON_CAPTURE_UNAVAILABLE:
		frame_finish_failed(
			frame,
			EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);
		break;
	}
}

static struct weston_surface *
cursor_image(struct cursor_session *cursor, int *hotspot_x, int *hotspot_y)
{
	struct weston_surface *surface;
	int x, y;

	if (!cursor || !cursor->pointer ||
	    !polo_backend_cursor_get_image(cursor->server, cursor->pointer,
					   &surface, &x, &y))
		return NULL;
	if (hotspot_x) {
		*hotspot_x = x;
		*hotspot_y = y;
	}
	return surface;
}

static void
session_surface_size(struct session *session, int *width, int *height)
{
	struct weston_surface *surface = session->kind == SESSION_CURSOR
		? session->sprite.surface
		: session->target.surface.surface;

	*width = *height = 0;
	if (surface)
		weston_surface_get_content_size(surface, width, height);
	if (session->kind == SESSION_CURSOR && (*width <= 0 || *height <= 0))
		*width = *height = 1;
}

static void
session_surface_kick(struct session *session);

static void
session_surface_committed(struct wl_listener *listener, void *data)
{
	struct session *session = wl_container_of(listener, session, commit);
	int width, height;

	session_surface_size(session, &width, &height);
	if (width > 0 && height > 0 &&
	    (width != session->width || height != session->height)) {
		session->width = width;
		session->height = height;
		session->full_damage = true;
		g_clear_pointer(&session->idle, wl_event_source_remove);
		if (session->frame && session->frame->captured)
			frame_finish_failed(
				session->frame,
				EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS);
		session_send_constraints(session);
	}
	session->dirty = true;
	session_surface_kick(session);
}

static void
session_sprite_gone(struct polo_surface_ref *ref)
{
	struct session *session = wl_container_of(ref, session, sprite);

	ref->surface = NULL;
	session_unwatch(session);
	session->dirty = true;
}

static void
session_watch(struct session *session, struct weston_surface *surface)
{
	session->commit.notify = session_surface_committed;
	wl_signal_add(&surface->commit_signal, &session->commit);
	session->watching = true;
}

static void
session_cursor_retarget(struct session *session)
{
	struct weston_surface *sprite =
		cursor_image(session->cursor, NULL, NULL);

	if (sprite == session->sprite.surface)
		return;
	session_unwatch(session);
	polo_surface_ref_clear(&session->sprite);
	if (sprite) {
		polo_surface_ref_set(&session->sprite, sprite,
				     session_sprite_gone);
		session_watch(session, sprite);
	}
	session_surface_committed(&session->commit, NULL);
}

static void
session_surface_copy(void *data)
{
	struct session *session = data;
	struct frame *frame = session->frame;
	struct weston_surface *surface;
	struct wl_shm_buffer *shm;
	pixman_region32_t damage;
	bool ok = true;

	session->idle = NULL;
	if (!frame || frame->finished)
		return;
	shm = frame->buffer.resource ? wl_shm_buffer_get(frame->buffer.resource)
				     : NULL;
	if (!shm || wl_shm_buffer_get_width(shm) != session->width ||
	    wl_shm_buffer_get_height(shm) != session->height ||
	    wl_shm_buffer_get_format(shm) != WL_SHM_FORMAT_ARGB8888) {
		frame_finish_failed(
			frame,
			EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS);
		return;
	}
	surface = session->kind == SESSION_CURSOR
		? session->sprite.surface
		: session->target.surface.surface;
	if (surface && surface->buffer_ref.buffer)
		ok = copy_surface(surface, shm, session->width,
				  session->height);
	else
		clear_shm(shm, session->height);
	if (!ok) {
		frame_finish_failed(
			frame,
			EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);
		return;
	}
	session->dirty = false;
	session->full_damage = false;
	pixman_region32_init_rect(&damage, 0, 0, session->width,
				  session->height);
	frame_finish_ready(frame, &damage, WL_OUTPUT_TRANSFORM_NORMAL,
			   session->server->compositor);
	pixman_region32_fini(&damage);
}

static void
session_surface_kick(struct session *session)
{
	struct frame *frame = session->frame;

	if (!frame || !frame->captured || frame->finished || session->idle)
		return;
	if (!session->dirty && !session->full_damage) {
		session_arm_timer(session);
		return;
	}
	session->idle = wl_event_loop_add_idle(
		wl_display_get_event_loop(session->server->display),
		session_surface_copy, session);
}

static int
session_timer_fired(void *data)
{
	struct session *session = data;
	struct frame *frame = session->frame;

	if (!frame || frame->finished || session->stopped)
		return 0;
	if (session->kind == SESSION_OUTPUT) {
		if (frame->queued && !frame->served && session->target.output)
			weston_output_schedule_repaint(session->target.output);
	} else {
		session->dirty = true;
		session_surface_kick(session);
	}
	return 0;
}

static void
frame_buffer_gone(struct polo_resource_ref *ref)
{
}

static void
frame_attach_buffer(struct wl_client *client, struct wl_resource *resource,
		    struct wl_resource *buffer)
{
	struct frame *frame = wl_resource_get_user_data(resource);

	if (frame->captured) {
		wl_resource_post_error(
			resource,
			EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_ALREADY_CAPTURED,
			"attach_buffer after capture");
		return;
	}
	polo_resource_ref_set(&frame->buffer, buffer, frame_buffer_gone);
}

static void
frame_damage_buffer(struct wl_client *client, struct wl_resource *resource,
		    int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct frame *frame = wl_resource_get_user_data(resource);

	if (frame->captured) {
		wl_resource_post_error(
			resource,
			EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_ALREADY_CAPTURED,
			"damage_buffer after capture");
		return;
	}
	if (x < 0 || y < 0 || width <= 0 || height <= 0)
		wl_resource_post_error(
			resource,
			EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_INVALID_BUFFER_DAMAGE,
			"invalid buffer damage");
}

static void
frame_capture(struct wl_client *client, struct wl_resource *resource)
{
	struct frame *frame = wl_resource_get_user_data(resource);
	struct session *session = frame->session;

	if (frame->captured) {
		wl_resource_post_error(
			resource,
			EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_ALREADY_CAPTURED,
			"capture sent twice");
		return;
	}
	if (!frame->buffer.resource) {
		wl_resource_post_error(
			resource,
			EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_NO_BUFFER,
			"capture without a buffer");
		return;
	}
	frame->captured = true;
	if (!session || session->stopped) {
		frame_finish_failed(
			frame,
			EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED);
		return;
	}
	if (session->kind == SESSION_OUTPUT)
		session_output_capture(session, frame);
	else
		session_surface_kick(session);
}

static const struct ext_image_copy_capture_frame_v1_interface frame_impl = {
	.destroy = polo_protocol_destroy,
	.attach_buffer = frame_attach_buffer,
	.damage_buffer = frame_damage_buffer,
	.capture = frame_capture,
};

static void
frame_resource_destroyed(struct wl_resource *resource)
{
	struct frame *frame = wl_resource_get_user_data(resource);
	struct session *session = frame->session;

	if (session) {
		session->frame = NULL;
		if (frame->queued && !frame->completed && session->csrc)
			session_output_csrc(session);
		if (frame->queued && !frame->finished)
			session->full_damage = true;
	}
	polo_resource_ref_clear(&frame->buffer);
	pixman_region32_fini(&frame->damage);
	g_free(frame);
}

static void
session_create_frame(struct wl_client *client, struct wl_resource *resource,
		     uint32_t id)
{
	struct session *session = wl_resource_get_user_data(resource);
	struct frame *frame;

	if (session->frame) {
		wl_resource_post_error(
			resource,
			EXT_IMAGE_COPY_CAPTURE_SESSION_V1_ERROR_DUPLICATE_FRAME,
			"a frame already exists");
		return;
	}
	frame = g_new0(struct frame, 1);
	pixman_region32_init(&frame->damage);
	frame->resource = polo_protocol_child_new(
		resource, &ext_image_copy_capture_frame_v1_interface, id,
		&frame_impl, frame, frame_resource_destroyed);
	if (!frame->resource) {
		pixman_region32_fini(&frame->damage);
		g_free(frame);
		return;
	}
	frame->session = session;
	session->frame = frame;
}

static const struct ext_image_copy_capture_session_v1_interface session_impl = {
	.create_frame = session_create_frame,
	.destroy = polo_protocol_destroy,
};

static void
session_resource_destroyed(struct wl_resource *resource)
{
	struct session *session = wl_resource_get_user_data(resource);

	if (session->frame)
		session->frame->session = NULL;
	if (!session->stopped)
		session_drop_target(session);
	if (session->cursor)
		session->cursor->session = NULL;
	pixman_region32_fini(&session->damage);
	g_free(session);
}

static struct session *
session_new(struct wl_resource *parent, uint32_t id, enum session_kind kind)
{
	struct session *session = g_new0(struct session, 1);

	session->resource = polo_protocol_child_new(
		parent, &ext_image_copy_capture_session_v1_interface, id,
		&session_impl, session, session_resource_destroyed);
	if (!session->resource) {
		g_free(session);
		return NULL;
	}
	session->kind = kind;
	session->full_damage = true;
	session->shm_format = WL_SHM_FORMAT_ARGB8888;
	session->target.gone = session_target_gone;
	pixman_region32_init(&session->damage);
	return session;
}

static void
session_start(struct session *session, const struct capture_target *source)
{
	target_copy(&session->target, source);
	if (session->kind == SESSION_OUTPUT && session->target.output) {
		if (!session_output_constraints(session)) {
			session_stop(session);
			return;
		}
		session->output_frame.notify = session_output_frame;
		wl_signal_add(&session->target.output->frame_signal,
			      &session->output_frame);
		session_output_csrc(session);
	} else if (session->kind == SESSION_SURFACE &&
		   session->target.surface.surface) {
		session_surface_size(session, &session->width,
				     &session->height);
		if (session->width <= 0 || session->height <= 0) {
			session_stop(session);
			return;
		}
		session_watch(session, session->target.surface.surface);
	} else {
		session_stop(session);
		return;
	}
	session_send_constraints(session);
}

static struct capture_source *
source_from_resource(struct wl_resource *resource)
{
	return wl_resource_get_user_data(resource);
}

static bool
check_options(struct wl_resource *resource, uint32_t options)
{
	if (options &
	    ~EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS) {
		wl_resource_post_error(
			resource,
			EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_ERROR_INVALID_OPTION,
			"unknown options 0x%x", options);
		return false;
	}
	return true;
}

static void
manager_create_session(struct wl_client *client, struct wl_resource *resource,
		       uint32_t id, struct wl_resource *source_resource,
		       uint32_t options)
{
	struct capture_source *source = source_from_resource(source_resource);
	struct session *session;

	if (!check_options(resource, options))
		return;
	session = session_new(resource, id,
			      source->target.output ? SESSION_OUTPUT
						    : SESSION_SURFACE);
	if (!session)
		return;
	session->server = wl_resource_get_user_data(resource);
	session_start(session, &source->target);
}

static void
cursor_update(struct cursor_session *cursor)
{
	struct weston_pointer *pointer = cursor->pointer;
	struct weston_output *output = cursor->target.output;
	struct weston_surface *surface = cursor->target.surface.surface;
	struct weston_coord pos = { 0 };
	int hotspot_x = 0, hotspot_y = 0;
	bool inside = false;

	if (!cursor_image(cursor, &hotspot_x, &hotspot_y)) {
		inside = false;
	} else if (output && output->current_mode) {
		pos = weston_matrix_transform_coord(&output->matrix,
						    pointer->pos.c);
		inside = pos.x >= 0 && pos.y >= 0 &&
			pos.x < output->current_mode->width &&
			pos.y < output->current_mode->height;
	} else if (surface) {
		struct weston_view *view = surface_first_view(surface);
		int width, height;

		weston_surface_get_content_size(surface, &width, &height);
		if (view) {
			pos = weston_coord_global_to_surface(view, pointer->pos)
				      .c;
			inside = pos.x >= 0 && pos.y >= 0 && pos.x < width &&
				pos.y < height;
		}
	}
	if (!inside) {
		if (cursor->entered)
			ext_image_copy_capture_cursor_session_v1_send_leave(
				cursor->resource);
		cursor->entered = false;
		return;
	}
	if (!cursor->entered) {
		cursor->entered = true;
		ext_image_copy_capture_cursor_session_v1_send_enter(
			cursor->resource);
		cursor->x = INT32_MIN;
		cursor->hotspot_x = INT32_MIN;
	}
	if ((int32_t)pos.x != cursor->x || (int32_t)pos.y != cursor->y) {
		cursor->x = (int32_t)pos.x;
		cursor->y = (int32_t)pos.y;
		ext_image_copy_capture_cursor_session_v1_send_position(
			cursor->resource, cursor->x, cursor->y);
	}
	if (hotspot_x != cursor->hotspot_x || hotspot_y != cursor->hotspot_y) {
		cursor->hotspot_x = hotspot_x;
		cursor->hotspot_y = hotspot_y;
		ext_image_copy_capture_cursor_session_v1_send_hotspot(
			cursor->resource, cursor->hotspot_x, cursor->hotspot_y);
	}
}

static void
cursor_changed(struct cursor_session *cursor)
{
	cursor_update(cursor);
	if (cursor->session && !cursor->session->stopped)
		session_cursor_retarget(cursor->session);
}

static void
cursor_motion(struct wl_listener *listener, void *data)
{
	struct cursor_session *cursor =
		wl_container_of(listener, cursor, motion);

	cursor_changed(cursor);
}

static void
cursor_motion_focus(struct wl_listener *listener, void *data)
{
	struct cursor_session *cursor =
		wl_container_of(listener, cursor, focus);

	cursor_changed(cursor);
}

static void
cursor_drop_pointer(struct cursor_session *cursor)
{
	if (!cursor->pointer)
		return;
	wl_list_remove(&cursor->motion.link);
	wl_list_remove(&cursor->focus.link);
	wl_list_remove(&cursor->pointer_destroy.link);
	cursor->pointer = NULL;
}

static void
cursor_pointer_destroyed(struct wl_listener *listener, void *data)
{
	struct cursor_session *cursor =
		wl_container_of(listener, cursor, pointer_destroy);

	cursor_drop_pointer(cursor);
	cursor_changed(cursor);
}

static void
cursor_target_gone(struct capture_target *target)
{
	struct cursor_session *cursor = wl_container_of(target, cursor, target);

	cursor_drop_pointer(cursor);
	cursor_update(cursor);
	if (cursor->session)
		session_stop(cursor->session);
}

static void
cursor_get_capture_session(struct wl_client *client,
			   struct wl_resource *resource, uint32_t id)
{
	struct cursor_session *cursor = wl_resource_get_user_data(resource);
	struct session *session;

	if (cursor->session) {
		wl_resource_post_error(
			resource,
			EXT_IMAGE_COPY_CAPTURE_CURSOR_SESSION_V1_ERROR_DUPLICATE_SESSION,
			"the cursor session already has a capture session");
		return;
	}
	session = session_new(resource, id, SESSION_CURSOR);
	if (!session)
		return;
	session->server = cursor->server;
	session->cursor = cursor;
	cursor->session = session;
	if (!target_alive(&cursor->target)) {
		session_stop(session);
		return;
	}
	session_cursor_retarget(session);
	if (!session->width)
		session_surface_committed(&session->commit, NULL);
}

static const struct ext_image_copy_capture_cursor_session_v1_interface
	cursor_impl = {
		.destroy = polo_protocol_destroy,
		.get_capture_session = cursor_get_capture_session,
	};

static void
cursor_resource_destroyed(struct wl_resource *resource)
{
	struct cursor_session *cursor = wl_resource_get_user_data(resource);

	if (cursor->session) {
		cursor->session->cursor = NULL;
		session_stop(cursor->session);
	}
	cursor_drop_pointer(cursor);
	target_clear(&cursor->target);
	g_free(cursor);
}

static void
manager_create_pointer_cursor_session(struct wl_client *client,
				      struct wl_resource *resource, uint32_t id,
				      struct wl_resource *source_resource,
				      struct wl_resource *pointer_resource)
{
	struct capture_source *source = source_from_resource(source_resource);
	struct cursor_session *cursor = g_new0(struct cursor_session, 1);

	cursor->resource = polo_protocol_child_new(
		resource, &ext_image_copy_capture_cursor_session_v1_interface,
		id, &cursor_impl, cursor, cursor_resource_destroyed);
	if (!cursor->resource) {
		g_free(cursor);
		return;
	}
	cursor->server = wl_resource_get_user_data(resource);
	cursor->target.gone = cursor_target_gone;
	target_copy(&cursor->target, &source->target);
	cursor->pointer = wl_resource_get_user_data(pointer_resource);
	if (cursor->pointer && target_alive(&cursor->target)) {
		cursor->motion.notify = cursor_motion;
		wl_signal_add(&cursor->pointer->motion_signal, &cursor->motion);
		cursor->focus.notify = cursor_motion_focus;
		wl_signal_add(&cursor->pointer->focus_signal, &cursor->focus);
		cursor->pointer_destroy.notify = cursor_pointer_destroyed;
		wl_signal_add(&cursor->pointer->destroy_signal,
			      &cursor->pointer_destroy);
	} else {
		cursor->pointer = NULL;
	}
	cursor_update(cursor);
}

static const struct ext_image_copy_capture_manager_v1_interface manager_impl = {
	.create_session = manager_create_session,
	.create_pointer_cursor_session = manager_create_pointer_cursor_session,
	.destroy = polo_protocol_destroy,
};

POLO_PROTOCOL_BIND(manager_bind, &ext_image_copy_capture_manager_v1_interface,
		   1, &manager_impl)

static void
source_resource_destroyed(struct wl_resource *resource)
{
	struct capture_source *source = wl_resource_get_user_data(resource);

	target_clear(&source->target);
	g_free(source);
}

static const struct ext_image_capture_source_v1_interface source_impl = {
	.destroy = polo_protocol_destroy,
};

static void
source_new(struct wl_resource *parent, uint32_t id,
	   struct weston_output *output, struct weston_surface *surface)
{
	struct capture_source *source = g_new0(struct capture_source, 1);

	if (!polo_protocol_child_new(
		    parent, &ext_image_capture_source_v1_interface, id,
		    &source_impl, source, source_resource_destroyed)) {
		g_free(source);
		return;
	}
	target_set(&source->target, output, surface);
}

static void
output_source_create(struct wl_client *client, struct wl_resource *resource,
		     uint32_t id, struct wl_resource *output_resource)
{
	struct weston_head *head = weston_head_from_resource(output_resource);

	source_new(resource, id, head ? head->output : NULL, NULL);
}

static const struct ext_output_image_capture_source_manager_v1_interface
	output_source_impl = {
		.create_source = output_source_create,
		.destroy = polo_protocol_destroy,
	};

POLO_PROTOCOL_BIND(output_source_bind,
		   &ext_output_image_capture_source_manager_v1_interface, 1,
		   &output_source_impl)

static void
toplevel_source_create(struct wl_client *client, struct wl_resource *resource,
		       uint32_t id, struct wl_resource *handle)
{
	PoloBackendForeignToplevel *toplevel =
		wl_resource_get_user_data(handle);
	struct weston_surface *surface = NULL;

	if (toplevel && !toplevel->closed && toplevel->window)
		surface = polo_backend_toplevel_get_weston_surface(
			toplevel->window);
	source_new(resource, id, NULL, surface);
}

static const struct
	ext_foreign_toplevel_image_capture_source_manager_v1_interface
		toplevel_source_impl = {
			.create_source = toplevel_source_create,
			.destroy = polo_protocol_destroy,
		};

POLO_PROTOCOL_BIND(
	toplevel_source_bind,
	&ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1,
	&toplevel_source_impl)

gboolean
polo_backend_image_capture_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	server->image_copy_capture_global = wl_global_create(
		server->display, &ext_image_copy_capture_manager_v1_interface,
		1, server, manager_bind);
	server->output_capture_source_global = wl_global_create(
		server->display,
		&ext_output_image_capture_source_manager_v1_interface, 1,
		server, output_source_bind);
	server->toplevel_capture_source_global = wl_global_create(
		server->display,
		&ext_foreign_toplevel_image_capture_source_manager_v1_interface,
		1, server, toplevel_source_bind);
	return TRUE;
}

void
polo_backend_image_capture_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->image_copy_capture_global, wl_global_destroy);
	g_clear_pointer(&server->output_capture_source_global,
			wl_global_destroy);
	g_clear_pointer(&server->toplevel_capture_source_global,
			wl_global_destroy);
}
