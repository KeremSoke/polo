/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_session_lock_manager_v1, fail-safe: the black curtain goes up and
 * input stops reaching other surfaces as soon as lock is received, here,
 * without asking Vala. Only unlock_and_destroy, or Polo's force unlock,
 * takes it down; a locker that dies leaves the session locked.
 */

#include <libweston/libweston.h>

#include "ext-session-lock-v1-server-protocol.h"
#include "polo-backend-private.h"

#define LOCK_SURFACE_ROLE "ext_session_lock_surface_v1"

struct curtain {
	struct wl_list link;
	struct polo_session_lock *sl;
	struct weston_output *output;
	struct weston_surface *surface;
	struct weston_view *view;
	struct weston_buffer_reference *buffer;
	struct wl_listener frame;
	int frames;
};

struct lock {
	struct polo_session_lock *sl;
	struct wl_resource *resource;
	struct wl_list surfaces; 
	gboolean locked_sent;
	gboolean finished;
	gboolean cancelled;
};

struct configure {
	uint32_t serial;
	int32_t width, height;
};

struct lock_surface {
	struct wl_list link;
	struct lock *lock; /* NULL once the lock is gone */
	struct wl_resource *resource;
	struct weston_surface *surface;
	struct weston_view *view;
	struct weston_output *output;
	struct wl_listener surface_destroy;
	struct wl_listener commit;
	GArray *pending; 
	gboolean acked;
	int32_t width, height;
};

struct saved_focus {
	struct wl_list link;
	struct weston_keyboard *keyboard;
	struct weston_surface *surface;
	struct wl_listener surface_destroy;
};

struct polo_session_lock {
	PoloBackendServer *server;
	struct wl_list saved_focus;
	struct wl_global *global;
	PoloBackendLockState state;
	struct lock *lock;
	struct wl_list curtains;
	struct weston_view *prompt;
	struct wl_listener output_created;
	struct wl_listener output_destroyed;
	struct wl_listener output_changed;
	struct wl_listener output_resized;
};

static void
set_state(struct polo_session_lock *sl, PoloBackendLockState state)
{
	if (sl->state == state)
		return;
	sl->state = state;
	polo_backend_server_emit_lock_state(sl->server, state);
}

static struct weston_layer_entry *
lock_layer(struct polo_session_lock *sl)
{
	return &polo_backend_server_get_layer(sl->server,
					      POLO_BACKEND_LAYER_LOCK)
			->layer.view_list;
}

/* ---- Curtains ------------------------------------------------------------ */

static void
check_locked(struct polo_session_lock *sl);

static void
curtain_frame(struct wl_listener *listener, void *data)
{
	struct curtain *curtain = wl_container_of(listener, curtain, frame);

	/* A second frame starts only after the first flipped, so the
	 * curtain is on screen. */
	curtain->frames++;
	if (curtain->frames == 1)
		weston_output_schedule_repaint(curtain->output);
	else if (curtain->frames == 2)
		check_locked(curtain->sl);
}

static void
curtain_place(struct curtain *curtain)
{
	struct weston_output *wo = curtain->output;

	if (curtain->buffer)
		weston_buffer_destroy_solid(curtain->buffer);
	curtain->buffer = weston_buffer_create_solid_rgba(wo->compositor, 0.0,
							  0.0, 0.0, 1.0);
	weston_surface_attach_solid(curtain->surface, curtain->buffer,
				    wo->width, wo->height);
	weston_view_set_position(curtain->view, wo->pos);
	weston_view_geometry_dirty(curtain->view);
	weston_output_schedule_repaint(wo);
}

static void
curtain_add(struct polo_session_lock *sl, struct weston_output *output)
{
	struct curtain *curtain = g_new0(struct curtain, 1);

	curtain->sl = sl;
	curtain->output = output;
	curtain->surface = weston_surface_create(output->compositor, NULL);
	weston_surface_set_label(
		curtain->surface,
		g_strdup_printf("polo lock curtain %s", output->name));
	curtain->view = weston_view_create(curtain->surface);
	curtain_place(curtain);
	weston_view_move_to_layer(curtain->view, lock_layer(sl));
	weston_surface_map(curtain->surface);
	curtain->frame.notify = curtain_frame;
	wl_signal_add(&output->frame_signal, &curtain->frame);
	wl_list_insert(&sl->curtains, &curtain->link);
	weston_output_schedule_repaint(output);
}

static void
curtain_free(struct curtain *curtain)
{
	wl_list_remove(&curtain->frame.link);
	wl_list_remove(&curtain->link);
	weston_view_destroy(curtain->view);
	weston_surface_unref(curtain->surface);
	if (curtain->buffer)
		weston_buffer_destroy_solid(curtain->buffer);
	g_free(curtain);
}

static struct curtain *
curtain_for(struct polo_session_lock *sl, struct weston_output *output)
{
	struct curtain *curtain;

	wl_list_for_each(curtain, &sl->curtains, link)
	{
		if (curtain->output == output)
			return curtain;
	}
	return NULL;
}

/* ---- Input -------------------------------------------------------------- */

static void
saved_focus_free(struct saved_focus *saved)
{
	if (saved->surface)
		wl_list_remove(&saved->surface_destroy.link);
	wl_list_remove(&saved->link);
	g_free(saved);
}

static void
saved_focus_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct saved_focus *saved =
		wl_container_of(listener, saved, surface_destroy);

	wl_list_remove(&saved->surface_destroy.link);
	saved->surface = NULL;
}

static void
save_focus(struct polo_session_lock *sl, struct weston_keyboard *keyboard)
{
	struct saved_focus *saved;

	if (!keyboard->focus)
		return;
	saved = g_new0(struct saved_focus, 1);
	saved->keyboard = keyboard;
	saved->surface = keyboard->focus;
	saved->surface_destroy.notify = saved_focus_surface_destroyed;
	wl_signal_add(&saved->surface->destroy_signal, &saved->surface_destroy);
	wl_list_insert(&sl->saved_focus, &saved->link);
}

static void
restore_focus(struct polo_session_lock *sl)
{
	struct saved_focus *saved, *next;
	struct weston_seat *seat;

	wl_list_for_each_safe(saved, next, &sl->saved_focus, link)
	{
		wl_list_for_each(seat, &sl->server->compositor->seat_list, link)
		{
			if (weston_seat_get_keyboard(seat) == saved->keyboard &&
			    saved->surface &&
			    weston_surface_is_mapped(saved->surface))
				weston_keyboard_set_focus(saved->keyboard,
							  saved->surface);
		}
		saved_focus_free(saved);
	}
}

static void
cancel_grabs_and_focus(struct polo_session_lock *sl)
{
	struct weston_seat *seat;

	wl_list_for_each(seat, &sl->server->compositor->seat_list, link)
	{
		struct weston_pointer *pointer = weston_seat_get_pointer(seat);
		struct weston_keyboard *keyboard =
			weston_seat_get_keyboard(seat);
		struct weston_touch *touch = weston_seat_get_touch(seat);

		if (pointer) {
			if (pointer->grab != &pointer->default_grab)
				pointer->grab->interface->cancel(pointer->grab);
			weston_pointer_clear_focus(pointer);
		}
		if (keyboard) {
			if (keyboard->grab != &keyboard->default_grab)
				keyboard->grab->interface->cancel(
					keyboard->grab);
			save_focus(sl, keyboard);
			weston_keyboard_set_focus(keyboard, NULL);
		}
		if (touch && touch->grab != &touch->default_grab)
			touch->grab->interface->cancel(touch->grab);
	}
}

static struct lock_surface *
first_mapped_surface(struct lock *lock)
{
	struct lock_surface *ls;

	if (!lock)
		return NULL;
	wl_list_for_each_reverse(ls, &lock->surfaces, link)
	{
		if (ls->view && weston_surface_is_mapped(ls->surface))
			return ls;
	}
	return NULL;
}

static void
focus_lock_surfaces(struct polo_session_lock *sl)
{
	struct lock_surface *target = first_mapped_surface(sl->lock);
	struct weston_seat *seat;

	wl_list_for_each(seat, &sl->server->compositor->seat_list, link)
	{
		struct weston_keyboard *keyboard =
			weston_seat_get_keyboard(seat);

		if (!keyboard)
			continue;
		if (keyboard->focus &&
		    polo_backend_session_lock_allows_focus(sl->server,
							   keyboard->focus))
			continue;
		weston_keyboard_set_focus(keyboard,
					  target ? target->surface : NULL);
	}
}

/* ---- State -------------------------------------------------------------- */

static void
check_locked(struct polo_session_lock *sl)
{
	struct curtain *curtain;

	if (sl->state != POLO_BACKEND_LOCK_LOCKING || !sl->lock)
		return;
	wl_list_for_each(curtain, &sl->curtains, link)
	{
		if (curtain->frames < 2)
			return;
	}
	sl->lock->locked_sent = TRUE;
	ext_session_lock_v1_send_locked(sl->lock->resource);
	set_state(sl, POLO_BACKEND_LOCK_LOCKED);
}

static void
start_locking(struct polo_session_lock *sl)
{
	struct weston_output *output;

	if (sl->state == POLO_BACKEND_LOCK_UNLOCKED) {
		wl_list_for_each(output, &sl->server->compositor->output_list,
				 link) curtain_add(sl, output);
		cancel_grabs_and_focus(sl);
	}
	set_state(sl, POLO_BACKEND_LOCK_LOCKING);
	check_locked(sl);
}

static void
lock_surface_unmap(struct lock_surface *ls)
{
	if (!ls->view)
		return;
	weston_view_destroy(ls->view);
	ls->view = NULL;
	if (weston_surface_is_mapped(ls->surface))
		weston_surface_unmap(ls->surface);
}

static void
detach_lock(struct lock *lock)
{
	struct lock_surface *ls, *next;

	wl_list_for_each_safe(ls, next, &lock->surfaces, link)
	{
		lock_surface_unmap(ls);
		ls->lock = NULL;
		wl_list_remove(&ls->link);
		wl_list_init(&ls->link);
	}
	if (lock->sl->lock == lock)
		lock->sl->lock = NULL;
}

static void
unlock(struct polo_session_lock *sl)
{
	struct curtain *curtain, *next;

	if (sl->lock)
		detach_lock(sl->lock);
	wl_list_for_each_safe(curtain, next, &sl->curtains, link)
		curtain_free(curtain);
	if (sl->prompt) {
		weston_view_move_to_layer(sl->prompt, NULL);
		sl->prompt = NULL;
	}
	set_state(sl, POLO_BACKEND_LOCK_UNLOCKED);
	restore_focus(sl);
	weston_compositor_schedule_repaint(sl->server->compositor);
}

/* ---- ext_session_lock_surface_v1 ---------------------------------------- */

static void
lock_surface_committed(struct weston_surface *surface,
		       struct weston_coord_surface new_origin)
{
	(void)surface;
	(void)new_origin;
}

static void
lock_surface_commit(struct wl_listener *listener, void *data)
{
	struct lock_surface *ls = wl_container_of(listener, ls, commit);
	struct weston_surface *surface = ls->surface;

	if (!ls->resource)
		return;
	if (!ls->acked) {
		wl_resource_post_error(
			ls->resource,
			EXT_SESSION_LOCK_SURFACE_V1_ERROR_COMMIT_BEFORE_FIRST_ACK,
			"committed before acking a configure");
		return;
	}
	if (!surface->buffer_ref.buffer) {
		wl_resource_post_error(
			ls->resource,
			EXT_SESSION_LOCK_SURFACE_V1_ERROR_NULL_BUFFER,
			"committed a null buffer");
		return;
	}
	if (surface->width != ls->width || surface->height != ls->height) {
		wl_resource_post_error(
			ls->resource,
			EXT_SESSION_LOCK_SURFACE_V1_ERROR_DIMENSIONS_MISMATCH,
			"buffer is %dx%d, configured %dx%d", surface->width,
			surface->height, ls->width, ls->height);
		return;
	}
	if (!ls->lock || !ls->view || !ls->output)
		return;
	if (!weston_surface_is_mapped(surface)) {
		weston_view_set_position(ls->view, ls->output->pos);
		weston_view_move_to_layer(ls->view, lock_layer(ls->lock->sl));
		if (ls->lock->sl->prompt)
			weston_view_move_to_layer(ls->lock->sl->prompt,
						  lock_layer(ls->lock->sl));
		weston_surface_map(surface);
		focus_lock_surfaces(ls->lock->sl);
	}
	weston_output_schedule_repaint(ls->output);
}

static void
lock_surface_send_configure(struct lock_surface *ls)
{
	struct configure configure = {
		.serial = wl_display_next_serial(wl_client_get_display(
			wl_resource_get_client(ls->resource))),
		.width = ls->output->width,
		.height = ls->output->height,
	};

	g_array_append_val(ls->pending, configure);
	ext_session_lock_surface_v1_send_configure(
		ls->resource, configure.serial, configure.width,
		configure.height);
}

static void
lock_surface_ack_configure(struct wl_client *client,
			   struct wl_resource *resource, uint32_t serial)
{
	struct lock_surface *ls = wl_resource_get_user_data(resource);

	for (guint i = 0; i < ls->pending->len; i++) {
		struct configure *c =
			&g_array_index(ls->pending, struct configure, i);

		if (c->serial != serial)
			continue;
		ls->acked = TRUE;
		ls->width = c->width;
		ls->height = c->height;
		g_array_remove_range(ls->pending, 0, i + 1);
		return;
	}
	wl_resource_post_error(resource,
			       EXT_SESSION_LOCK_SURFACE_V1_ERROR_INVALID_SERIAL,
			       "no configure with serial %u", serial);
}

static void
lock_surface_destroy_request(struct wl_client *client,
			     struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct ext_session_lock_surface_v1_interface lock_surface_impl = {
	.destroy = lock_surface_destroy_request,
	.ack_configure = lock_surface_ack_configure,
};

static void
lock_surface_free(struct lock_surface *ls)
{
	struct polo_session_lock *sl = ls->lock ? ls->lock->sl : NULL;

	lock_surface_unmap(ls);
	wl_list_remove(&ls->link);
	if (ls->surface) {
		wl_list_remove(&ls->surface_destroy.link);
		wl_list_remove(&ls->commit.link);
		ls->surface->committed = NULL;
		ls->surface->committed_private = NULL;
	}
	g_array_unref(ls->pending);
	g_free(ls);
	if (sl)
		focus_lock_surfaces(sl);
}

static void
lock_surface_resource_destroyed(struct wl_resource *resource)
{
	struct lock_surface *ls = wl_resource_get_user_data(resource);

	ls->resource = NULL;
	lock_surface_free(ls);
}

static void
lock_surface_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct lock_surface *ls =
		wl_container_of(listener, ls, surface_destroy);

	lock_surface_unmap(ls);
	wl_list_remove(&ls->surface_destroy.link);
	wl_list_remove(&ls->commit.link);
	wl_list_init(&ls->surface_destroy.link);
	wl_list_init(&ls->commit.link);
	ls->surface = NULL;
}

/* ---- ext_session_lock_v1 ------------------------------------------------ */

static void
lock_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
	struct lock *lock = wl_resource_get_user_data(resource);

	if (lock->locked_sent && !lock->finished) {
		wl_resource_post_error(
			resource, EXT_SESSION_LOCK_V1_ERROR_INVALID_DESTROY,
			"the session is locked; use "
			"unlock_and_destroy");
		return;
	}
	lock->cancelled = TRUE;
	wl_resource_destroy(resource);
}

static void
lock_unlock_and_destroy(struct wl_client *client, struct wl_resource *resource)
{
	struct lock *lock = wl_resource_get_user_data(resource);

	if (!lock->locked_sent) {
		wl_resource_post_error(resource,
				       EXT_SESSION_LOCK_V1_ERROR_INVALID_UNLOCK,
				       "the session was never locked");
		return;
	}
	if (lock->sl->lock == lock)
		unlock(lock->sl);
	lock->cancelled = TRUE;
	wl_resource_destroy(resource);
}

static void
lock_get_lock_surface(struct wl_client *client, struct wl_resource *resource,
		      uint32_t id, struct wl_resource *surface_resource,
		      struct wl_resource *output_resource)
{
	struct lock *lock = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct weston_head *head = weston_head_from_resource(output_resource);
	struct weston_output *output = head ? head->output : NULL;
	struct lock_surface *ls;

	if (surface->committed != NULL ||
	    weston_surface_set_role(surface, LOCK_SURFACE_ROLE, resource,
				    EXT_SESSION_LOCK_V1_ERROR_ROLE) < 0) {
		wl_resource_post_error(resource, EXT_SESSION_LOCK_V1_ERROR_ROLE,
				       "wl_surface@%d already has a role",
				       wl_resource_get_id(surface_resource));
		return;
	}
	if (surface->buffer_ref.buffer || surface->pending.buffer_ref.buffer) {
		wl_resource_post_error(
			resource, EXT_SESSION_LOCK_V1_ERROR_ALREADY_CONSTRUCTED,
			"wl_surface@%d has a buffer attached",
			wl_resource_get_id(surface_resource));
		return;
	}
	wl_list_for_each(ls, &lock->surfaces, link)
	{
		if (output && ls->output == output) {
			wl_resource_post_error(
				resource,
				EXT_SESSION_LOCK_V1_ERROR_DUPLICATE_OUTPUT,
				"output %s already has a lock surface",
				output->name);
			return;
		}
	}

	ls = g_new0(struct lock_surface, 1);
	ls->resource = wl_resource_create(
		client, &ext_session_lock_surface_v1_interface,
		wl_resource_get_version(resource), id);
	if (!ls->resource) {
		g_free(ls);
		wl_client_post_no_memory(client);
		return;
	}
	ls->pending = g_array_new(FALSE, FALSE, sizeof(struct configure));
	ls->surface = surface;
	ls->output = output;
	wl_resource_set_implementation(ls->resource, &lock_surface_impl, ls,
				       lock_surface_resource_destroyed);
	ls->surface_destroy.notify = lock_surface_surface_destroyed;
	wl_signal_add(&surface->destroy_signal, &ls->surface_destroy);
	ls->commit.notify = lock_surface_commit;
	wl_signal_add(&surface->commit_signal, &ls->commit);
	surface->committed = lock_surface_committed;
	surface->committed_private = ls;
	if (lock->sl->lock == lock && output) {
		ls->lock = lock;
		ls->view = weston_view_create(surface);
		wl_list_insert(&lock->surfaces, &ls->link);
	} else {
		wl_list_init(&ls->link);
	}
	if (output)
		lock_surface_send_configure(ls);
}

static const struct ext_session_lock_v1_interface lock_impl = {
	.destroy = lock_destroy_request,
	.get_lock_surface = lock_get_lock_surface,
	.unlock_and_destroy = lock_unlock_and_destroy,
};

static void
lock_resource_destroyed(struct wl_resource *resource)
{
	struct lock *lock = wl_resource_get_user_data(resource);
	struct polo_session_lock *sl = lock->sl;
	gboolean live = sl->lock == lock;

	detach_lock(lock);
	if (live) {
		if (lock->cancelled && !lock->locked_sent)
			unlock(sl);
		else
			set_state(sl, POLO_BACKEND_LOCK_ABANDONED);
	}
	g_free(lock);
}

/* ---- ext_session_lock_manager_v1 ---------------------------------------- */

static void
manager_lock(struct wl_client *client, struct wl_resource *resource,
	     uint32_t id)
{
	struct polo_session_lock *sl = wl_resource_get_user_data(resource);
	struct lock *lock = g_new0(struct lock, 1);

	lock->sl = sl;
	wl_list_init(&lock->surfaces);
	lock->resource =
		wl_resource_create(client, &ext_session_lock_v1_interface,
				   wl_resource_get_version(resource), id);
	if (!lock->resource) {
		g_free(lock);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(lock->resource, &lock_impl, lock,
				       lock_resource_destroyed);
	if (sl->lock) {
		lock->finished = TRUE;
		ext_session_lock_v1_send_finished(lock->resource);
		return;
	}
	sl->lock = lock;
	start_locking(sl);
}

static void
manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct ext_session_lock_manager_v1_interface manager_impl = {
	.destroy = manager_destroy,
	.lock = manager_lock,
};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version,
	     uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &ext_session_lock_manager_v1_interface, 1, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

/* ---- Outputs ------------------------------------------------------------ */

static void
reconfigure_surfaces(struct polo_session_lock *sl, struct weston_output *output)
{
	struct lock_surface *ls;

	if (!sl->lock)
		return;
	wl_list_for_each(ls, &sl->lock->surfaces, link)
	{
		if (ls->output != output || !ls->resource)
			continue;
		if (ls->view)
			weston_view_set_position(ls->view, output->pos);
		lock_surface_send_configure(ls);
	}
}

static void
output_created(struct wl_listener *listener, void *data)
{
	struct polo_session_lock *sl =
		wl_container_of(listener, sl, output_created);

	if (sl->state != POLO_BACKEND_LOCK_UNLOCKED)
		curtain_add(sl, data);
}

static void
output_destroyed(struct wl_listener *listener, void *data)
{
	struct polo_session_lock *sl =
		wl_container_of(listener, sl, output_destroyed);
	struct weston_output *output = data;
	struct curtain *curtain = curtain_for(sl, output);
	struct lock_surface *ls;

	if (curtain)
		curtain_free(curtain);
	if (sl->lock) {
		wl_list_for_each(ls, &sl->lock->surfaces, link)
		{
			if (ls->output == output) {
				lock_surface_unmap(ls);
				ls->output = NULL;
			}
		}
	}
	check_locked(sl);
}

static void
output_changed(struct wl_listener *listener, void *data)
{
	struct polo_session_lock *sl =
		wl_container_of(listener, sl, output_changed);
	struct curtain *curtain = curtain_for(sl, data);

	if (curtain)
		curtain_place(curtain);
	reconfigure_surfaces(sl, data);
}

static void
output_resized(struct wl_listener *listener, void *data)
{
	struct polo_session_lock *sl =
		wl_container_of(listener, sl, output_resized);
	struct curtain *curtain = curtain_for(sl, data);

	if (curtain)
		curtain_place(curtain);
	reconfigure_surfaces(sl, data);
}

/* ---- Policy hooks ------------------------------------------------------- */

gboolean
polo_backend_session_lock_active(PoloBackendServer *server)
{
	return server->session_lock &&
		server->session_lock->state != POLO_BACKEND_LOCK_UNLOCKED;
}

gboolean
polo_backend_session_lock_allows_focus(PoloBackendServer *server,
				       struct weston_surface *surface)
{
	struct lock_surface *ls;

	if (!polo_backend_session_lock_active(server))
		return TRUE;
	if (!surface || !server->session_lock->lock)
		return FALSE;
	wl_list_for_each(ls, &server->session_lock->lock->surfaces, link)
	{
		if (ls->surface == surface && ls->view)
			return TRUE;
	}
	return FALSE;
}

gboolean
polo_backend_session_lock_is_abandoned(PoloBackendServer *server)
{
	return server->session_lock &&
		server->session_lock->state == POLO_BACKEND_LOCK_ABANDONED;
}

PoloBackendLockState
polo_backend_server_get_lock_state(PoloBackendServer *server)
{
	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server),
			     POLO_BACKEND_LOCK_UNLOCKED);
	return server->session_lock ? server->session_lock->state
				    : POLO_BACKEND_LOCK_UNLOCKED;
}

void
polo_backend_server_force_unlock(PoloBackendServer *server)
{
	struct polo_session_lock *sl;

	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	sl = server->session_lock;
	if (!sl || sl->state == POLO_BACKEND_LOCK_UNLOCKED)
		return;
	if (sl->lock && !sl->lock->finished) {
		sl->lock->finished = TRUE;
		ext_session_lock_v1_send_finished(sl->lock->resource);
	}
	unlock(sl);
}

void
polo_backend_server_set_lock_prompt(PoloBackendServer *server,
				    PoloBackendPixelBuffer *buffer)
{
	struct polo_session_lock *sl;
	PoloBackendView *handle;
	struct weston_view *view;
	struct weston_output *output;

	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	sl = server->session_lock;
	if (!sl)
		return;
	if (sl->prompt) {
		weston_view_move_to_layer(sl->prompt, NULL);
		sl->prompt = NULL;
	}
	if (!buffer || sl->state == POLO_BACKEND_LOCK_UNLOCKED)
		goto out;
	handle = polo_backend_pixel_buffer_get_view(buffer);
	view = handle ? polo_backend_view_get_weston_view(handle) : NULL;
	if (!view || wl_list_empty(&server->compositor->output_list))
		goto out;
	output = wl_container_of(server->compositor->output_list.next, output,
				 link);
	weston_view_set_position(
		view,
		weston_coord_global_add(
			output->pos,
			(struct weston_coord_global){
				.c = weston_coord(
					(output->width - view->surface->width) /
						2,
					(output->height -
					 view->surface->height) /
						2) }));
	weston_view_move_to_layer(view, lock_layer(sl));
	sl->prompt = view;
out:
	weston_compositor_schedule_repaint(server->compositor);
}

gboolean
polo_backend_session_lock_init(PoloBackendServer *server, GError **error)
{
	struct polo_session_lock *sl = g_new0(struct polo_session_lock, 1);
	struct weston_compositor *compositor = server->compositor;

	(void)error;
	sl->server = server;
	sl->state = POLO_BACKEND_LOCK_UNLOCKED;
	wl_list_init(&sl->curtains);
	wl_list_init(&sl->saved_focus);
	sl->output_created.notify = output_created;
	wl_signal_add(&compositor->output_created_signal, &sl->output_created);
	sl->output_destroyed.notify = output_destroyed;
	wl_signal_add(&compositor->output_destroyed_signal,
		      &sl->output_destroyed);
	sl->output_changed.notify = output_changed;
	wl_signal_add(&compositor->output_moved_signal, &sl->output_changed);
	sl->output_resized.notify = output_resized;
	wl_signal_add(&compositor->output_resized_signal, &sl->output_resized);
	sl->global = wl_global_create(server->display,
				      &ext_session_lock_manager_v1_interface, 1,
				      sl, manager_bind);
	server->session_lock = sl;
	return TRUE;
}

void
polo_backend_session_lock_fini(PoloBackendServer *server)
{
	struct polo_session_lock *sl = server->session_lock;
	struct curtain *curtain, *next;
	struct saved_focus *saved, *saved_next;

	if (!sl)
		return;
	g_clear_pointer(&sl->global, wl_global_destroy);
	wl_list_for_each_safe(curtain, next, &sl->curtains, link)
		curtain_free(curtain);
	wl_list_for_each_safe(saved, saved_next, &sl->saved_focus, link)
		saved_focus_free(saved);
	wl_list_remove(&sl->output_created.link);
	wl_list_remove(&sl->output_destroyed.link);
	wl_list_remove(&sl->output_changed.link);
	wl_list_remove(&sl->output_resized.link);
	server->session_lock = NULL;
	g_free(sl);
}
