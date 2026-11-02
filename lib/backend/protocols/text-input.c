/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwp_text_input_v3 and zwp_input_method_v2, relayed between the focused client
 * and the seat's one input method. The keyboard grab is not a libweston grab:
 * keys reach the input method after Polo's own filter, so Alt+Tab and the other
 * shortcuts keep working during composition.
 */

#include <string.h>
#include <unistd.h>

#include <libweston/libweston.h>

#include "input-method-unstable-v2-server-protocol.h"
#include "polo-backend-private.h"
#include "text-input-unstable-v3-server-protocol.h"

#define N_HELD 16

struct text_state {
	gboolean enabled;
	gboolean has_surrounding;
	char *surrounding;
	int32_t cursor, anchor;
	uint32_t change_cause;
	gboolean has_content_type;
	uint32_t hint, purpose;
	gboolean has_rectangle;
	int32_t x, y, width, height;
};

struct text_input {
	struct wl_resource *resource;
	struct polo_text_seat *seat;
	struct wl_list link;
	struct weston_surface *entered;
	struct text_state pending, current;
	uint32_t commits;
};

struct input_popup {
	struct wl_resource *resource;
	struct polo_text_seat *seat;
	struct weston_surface *surface;
	struct weston_view *view;
	struct wl_listener surface_destroy;
	struct wl_list link;
};

struct polo_text_seat {
	PoloBackendServer *server;
	struct weston_seat *seat;
	struct weston_keyboard *keyboard;
	struct wl_listener focus;
	struct wl_list text_inputs;
	struct text_input *active;

	struct wl_resource *method;
	gboolean method_active;
	uint32_t method_dones;
	char *pending_commit;
	char *pending_preedit;
	int32_t preedit_begin, preedit_end;
	uint32_t delete_before, delete_after;
	struct wl_list popups;

	struct wl_resource *grab;
	uint32_t held[N_HELD];
	int n_held;
};

static void
place_popup(struct polo_text_seat *seat, struct input_popup *popup);

static void
state_clear(struct text_state *state)
{
	g_free(state->surrounding);
	memset(state, 0, sizeof *state);
}

static void
state_copy(struct text_state *to, const struct text_state *from)
{
	g_free(to->surrounding);
	*to = *from;
	to->surrounding = g_strdup(from->surrounding);
}

static void
hide_popup(struct input_popup *popup)
{
	if (popup->view && weston_view_is_mapped(popup->view))
		weston_view_move_to_layer(popup->view, NULL);
	if (popup->surface && weston_surface_is_mapped(popup->surface))
		weston_surface_unmap(popup->surface);
}

static void
send_method_state(struct polo_text_seat *seat)
{
	struct text_state *state = &seat->active->current;

	if (state->has_surrounding) {
		zwp_input_method_v2_send_surrounding_text(
			seat->method, state->surrounding, state->cursor,
			state->anchor);
		zwp_input_method_v2_send_text_change_cause(seat->method,
							   state->change_cause);
	}
	if (state->has_content_type)
		zwp_input_method_v2_send_content_type(seat->method, state->hint,
						      state->purpose);
	zwp_input_method_v2_send_done(seat->method);
	seat->method_dones++;
}

static void
clear_pending_text(struct polo_text_seat *seat)
{
	g_clear_pointer(&seat->pending_commit, g_free);
	g_clear_pointer(&seat->pending_preedit, g_free);
	seat->preedit_begin = seat->preedit_end = 0;
	seat->delete_before = seat->delete_after = 0;
}

static void
activate_method(struct polo_text_seat *seat)
{
	struct input_popup *popup;

	if (!seat->method || !seat->active || seat->method_active)
		return;
	seat->method_active = TRUE;
	clear_pending_text(seat);
	zwp_input_method_v2_send_activate(seat->method);
	send_method_state(seat);
	wl_list_for_each(popup, &seat->popups, link) place_popup(seat, popup);
}

static void
deactivate_method(struct polo_text_seat *seat)
{
	struct input_popup *popup;

	wl_list_for_each(popup, &seat->popups, link) hide_popup(popup);
	if (!seat->method || !seat->method_active)
		return;
	seat->method_active = FALSE;
	zwp_input_method_v2_send_deactivate(seat->method);
	zwp_input_method_v2_send_done(seat->method);
	seat->method_dones++;
}

static struct text_input *
find_enabled(struct polo_text_seat *seat)
{
	struct text_input *input;

	wl_list_for_each(input, &seat->text_inputs, link)
	{
		if (input->entered && input->current.enabled)
			return input;
	}
	return NULL;
}

static void
update_active(struct polo_text_seat *seat)
{
	struct text_input *next = find_enabled(seat);

	if (next == seat->active)
		return;
	if (seat->active)
		deactivate_method(seat);
	seat->active = next;
	activate_method(seat);
}

static void
enter_focus(struct polo_text_seat *seat, struct text_input *input)
{
	struct weston_surface *surface =
		seat->keyboard ? seat->keyboard->focus : NULL;

	if (surface && surface->resource &&
	    wl_resource_get_client(surface->resource) ==
		    wl_resource_get_client(input->resource)) {
		input->entered = surface;
		zwp_text_input_v3_send_enter(input->resource,
					     surface->resource);
	}
}

static void
focus_changed(struct wl_listener *listener, void *data)
{
	struct polo_text_seat *seat = wl_container_of(listener, seat, focus);
	struct text_input *input;

	wl_list_for_each(input, &seat->text_inputs, link)
	{
		if (input->entered && input->entered->resource &&
		    input->entered != seat->keyboard->focus)
			zwp_text_input_v3_send_leave(input->resource,
						     input->entered->resource);
		if (input->entered != seat->keyboard->focus)
			input->entered = NULL;
	}
	wl_list_for_each(input, &seat->text_inputs, link)
	{
		if (!input->entered)
			enter_focus(seat, input);
	}
	update_active(seat);
}

static void
text_input_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
text_input_enable(struct wl_client *client, struct wl_resource *resource)
{
	struct text_input *input = wl_resource_get_user_data(resource);

	state_clear(&input->pending);
	input->pending.enabled = TRUE;
}

static void
text_input_disable(struct wl_client *client, struct wl_resource *resource)
{
	struct text_input *input = wl_resource_get_user_data(resource);

	input->pending.enabled = FALSE;
}

static void
text_input_set_surrounding_text(struct wl_client *client,
				struct wl_resource *resource, const char *text,
				int32_t cursor, int32_t anchor)
{
	struct text_input *input = wl_resource_get_user_data(resource);

	g_free(input->pending.surrounding);
	input->pending.surrounding = g_strdup(text);
	input->pending.cursor = cursor;
	input->pending.anchor = anchor;
	input->pending.has_surrounding = TRUE;
}

static void
text_input_set_text_change_cause(struct wl_client *client,
				 struct wl_resource *resource, uint32_t cause)
{
	struct text_input *input = wl_resource_get_user_data(resource);

	input->pending.change_cause = cause;
}

static void
text_input_set_content_type(struct wl_client *client,
			    struct wl_resource *resource, uint32_t hint,
			    uint32_t purpose)
{
	struct text_input *input = wl_resource_get_user_data(resource);

	input->pending.has_content_type = TRUE;
	input->pending.hint = hint;
	input->pending.purpose = purpose;
}

static void
text_input_set_cursor_rectangle(struct wl_client *client,
				struct wl_resource *resource, int32_t x,
				int32_t y, int32_t width, int32_t height)
{
	struct text_input *input = wl_resource_get_user_data(resource);

	input->pending.has_rectangle = TRUE;
	input->pending.x = x;
	input->pending.y = y;
	input->pending.width = width;
	input->pending.height = height;
}

static void
text_input_commit(struct wl_client *client, struct wl_resource *resource)
{
	struct text_input *input = wl_resource_get_user_data(resource);
	struct polo_text_seat *seat = input->seat;
	struct input_popup *popup;

	input->commits++;
	state_copy(&input->current, &input->pending);
	if (!seat)
		return;
	if (input == seat->active && input->current.enabled) {
		if (seat->method_active)
			send_method_state(seat);
		wl_list_for_each(popup, &seat->popups, link)
			place_popup(seat, popup);
	}
	update_active(seat);
}

static const struct zwp_text_input_v3_interface text_input_impl = {
	.destroy = text_input_destroy,
	.enable = text_input_enable,
	.disable = text_input_disable,
	.set_surrounding_text = text_input_set_surrounding_text,
	.set_text_change_cause = text_input_set_text_change_cause,
	.set_content_type = text_input_set_content_type,
	.set_cursor_rectangle = text_input_set_cursor_rectangle,
	.commit = text_input_commit,
};

static void
text_input_resource_destroyed(struct wl_resource *resource)
{
	struct text_input *input = wl_resource_get_user_data(resource);
	struct polo_text_seat *seat = input->seat;

	wl_list_remove(&input->link);
	if (seat && seat->active == input) {
		deactivate_method(seat);
		seat->active = NULL;
		update_active(seat);
	}
	state_clear(&input->pending);
	state_clear(&input->current);
	g_free(input);
}

static void
text_manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static struct polo_text_seat *
seat_from_resource(PoloBackendServer *server, struct wl_resource *seat_resource)
{
	struct weston_seat *weston_seat =
		wl_resource_get_user_data(seat_resource);
	PoloBackendSeat *seat = weston_seat
		? polo_backend_server_lookup_seat(server, weston_seat)
		: NULL;

	return seat ? polo_backend_seat_get_text(seat) : NULL;
}

static void
text_manager_get_text_input(struct wl_client *client,
			    struct wl_resource *resource, uint32_t id,
			    struct wl_resource *seat_resource)
{
	struct polo_text_seat *seat = seat_from_resource(
		wl_resource_get_user_data(resource), seat_resource);
	struct text_input *input = g_new0(struct text_input, 1);

	input->resource =
		wl_resource_create(client, &zwp_text_input_v3_interface, 1, id);
	if (!input->resource) {
		g_free(input);
		wl_client_post_no_memory(client);
		return;
	}
	wl_list_init(&input->link);
	wl_resource_set_implementation(input->resource, &text_input_impl, input,
				       text_input_resource_destroyed);
	input->seat = seat;
	if (seat) {
		wl_list_insert(&seat->text_inputs, &input->link);
		enter_focus(seat, input);
	}
}

static const struct zwp_text_input_manager_v3_interface text_manager_impl = {
	.destroy = text_manager_destroy,
	.get_text_input = text_manager_get_text_input,
};

static void
text_manager_bind(struct wl_client *client, void *data, uint32_t version,
		  uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &zwp_text_input_manager_v3_interface, 1, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &text_manager_impl, data,
				       NULL);
}

/* ---- Popups ------------------------------------------------------------ */

static void
place_popup(struct polo_text_seat *seat, struct input_popup *popup)
{
	struct weston_surface *focus;
	struct weston_view *focus_view;
	struct text_state *state;
	struct weston_coord_global at;
	int width, height, x, y, rx = 0, ry = 0, rw = 0, rh = 0;

	if (!seat->method_active || !seat->active || !seat->active->entered)
		return;
	focus = seat->active->entered;
	if (wl_list_empty(&focus->views))
		return;
	focus_view =
		wl_container_of(focus->views.next, focus_view, surface_link);
	state = &seat->active->current;
	if (state->has_rectangle) {
		rx = state->x;
		ry = state->y;
		rw = state->width;
		rh = state->height;
	}
	at = weston_coord_surface_to_global(
		focus_view, weston_coord_surface(rx, ry, focus));
	width = popup->surface->width;
	height = popup->surface->height;
	x = (int)at.c.x;
	y = (int)at.c.y + rh;
	if (focus_view->output) {
		struct weston_output *output = focus_view->output;
		int right = (int)output->pos.c.x + output->width;
		int bottom = (int)output->pos.c.y + output->height;

		if (y + height > bottom &&
		    (int)at.c.y - height >= (int)output->pos.c.y)
			y = (int)at.c.y - height;
		if (x + width > right)
			x = right - width;
		if (x < (int)output->pos.c.x)
			x = (int)output->pos.c.x;
	}
	zwp_input_popup_surface_v2_send_text_input_rectangle(
		popup->resource, (int)at.c.x - x, (int)at.c.y - y, rw, rh);
	if (!popup->surface->buffer_ref.buffer)
		return;
	weston_view_set_position(
		popup->view,
		(struct weston_coord_global){ .c = weston_coord(x, y) });
	if (!weston_surface_is_mapped(popup->surface))
		weston_surface_map(popup->surface);
	if (!weston_view_is_mapped(popup->view)) {
		weston_view_move_to_layer(
			popup->view,
			&seat->server->layers[POLO_BACKEND_LAYER_OVERLAY]
				 .layer.view_list);
	}
	weston_view_update_transform(popup->view);
	weston_view_schedule_repaint(popup->view);
}

static void
popup_committed(struct weston_surface *surface,
		struct weston_coord_surface new_origin)
{
	struct input_popup *popup = surface->committed_private;

	if (!popup || !popup->seat)
		return;
	if (surface->buffer_ref.buffer)
		place_popup(popup->seat, popup);
	else
		hide_popup(popup);
}

static void
popup_forget_surface(struct input_popup *popup)
{
	if (!popup->surface)
		return;
	hide_popup(popup);
	popup->surface->committed = NULL;
	popup->surface->committed_private = NULL;
	wl_list_remove(&popup->surface_destroy.link);
	if (popup->view)
		weston_view_destroy(popup->view);
	popup->view = NULL;
	popup->surface = NULL;
}

static void
popup_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct input_popup *popup =
		wl_container_of(listener, popup, surface_destroy);

	wl_list_remove(&popup->surface_destroy.link);
	wl_list_init(&popup->surface_destroy.link);
	if (popup->view)
		weston_view_destroy(popup->view);
	popup->view = NULL;
	popup->surface = NULL;
}

static void
popup_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_input_popup_surface_v2_interface popup_impl = {
	.destroy = popup_destroy,
};

static void
popup_resource_destroyed(struct wl_resource *resource)
{
	struct input_popup *popup = wl_resource_get_user_data(resource);

	popup_forget_surface(popup);
	wl_list_remove(&popup->link);
	g_free(popup);
}

/* ---- The input method -------------------------------------------------------
 */

static void
method_commit_string(struct wl_client *client, struct wl_resource *resource,
		     const char *text)
{
	struct polo_text_seat *seat = wl_resource_get_user_data(resource);

	if (!seat)
		return;
	g_free(seat->pending_commit);
	seat->pending_commit = g_strdup(text);
}

static void
method_set_preedit_string(struct wl_client *client,
			  struct wl_resource *resource, const char *text,
			  int32_t begin, int32_t end)
{
	struct polo_text_seat *seat = wl_resource_get_user_data(resource);

	if (!seat)
		return;
	g_free(seat->pending_preedit);
	seat->pending_preedit = g_strdup(text);
	seat->preedit_begin = begin;
	seat->preedit_end = end;
}

static void
method_delete_surrounding_text(struct wl_client *client,
			       struct wl_resource *resource, uint32_t before,
			       uint32_t after)
{
	struct polo_text_seat *seat = wl_resource_get_user_data(resource);

	if (!seat)
		return;
	seat->delete_before = before;
	seat->delete_after = after;
}

static void
method_commit(struct wl_client *client, struct wl_resource *resource,
	      uint32_t serial)
{
	struct polo_text_seat *seat = wl_resource_get_user_data(resource);
	struct wl_resource *target;

	if (!seat || !seat->method_active || !seat->active) {
		if (seat)
			clear_pending_text(seat);
		return;
	}
	target = seat->active->resource;
	if (seat->pending_preedit)
		zwp_text_input_v3_send_preedit_string(
			target, seat->pending_preedit, seat->preedit_begin,
			seat->preedit_end);
	if (seat->pending_commit)
		zwp_text_input_v3_send_commit_string(target,
						     seat->pending_commit);
	if (seat->delete_before || seat->delete_after)
		zwp_text_input_v3_send_delete_surrounding_text(
			target, seat->delete_before, seat->delete_after);
	zwp_text_input_v3_send_done(target, seat->active->commits);
	clear_pending_text(seat);
}

static void
method_get_input_popup_surface(struct wl_client *client,
			       struct wl_resource *resource, uint32_t id,
			       struct wl_resource *surface_resource)
{
	struct polo_text_seat *seat = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct input_popup *popup = g_new0(struct input_popup, 1);

	popup->resource = wl_resource_create(
		client, &zwp_input_popup_surface_v2_interface, 1, id);
	if (!popup->resource) {
		g_free(popup);
		wl_client_post_no_memory(client);
		return;
	}
	wl_list_init(&popup->link);
	wl_resource_set_implementation(popup->resource, &popup_impl, popup,
				       popup_resource_destroyed);
	if (!seat)
		return;
	if (weston_surface_set_role(surface, "zwp_input_popup_surface_v2",
				    resource, 0) < 0)
		return;
	popup->seat = seat;
	popup->surface = surface;
	popup->view = weston_view_create(surface);
	surface->committed = popup_committed;
	surface->committed_private = popup;
	popup->surface_destroy.notify = popup_surface_destroyed;
	wl_signal_add(&surface->destroy_signal, &popup->surface_destroy);
	wl_list_insert(&seat->popups, &popup->link);
}

static void
grab_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_input_method_keyboard_grab_v2_interface grab_impl = {
	.release = grab_release,
};

static void
grab_resource_destroyed(struct wl_resource *resource)
{
	struct polo_text_seat *seat = wl_resource_get_user_data(resource);

	if (seat && seat->grab == resource)
		seat->grab = NULL;
}

static void
method_grab_keyboard(struct wl_client *client, struct wl_resource *resource,
		     uint32_t id)
{
	struct polo_text_seat *seat = wl_resource_get_user_data(resource);
	struct wl_resource *grab = wl_resource_create(
		client, &zwp_input_method_keyboard_grab_v2_interface, 1, id);

	if (!grab) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(grab, &grab_impl, seat,
				       grab_resource_destroyed);
	if (!seat || !seat->keyboard)
		return;
	if (seat->grab)
		wl_resource_set_user_data(seat->grab, NULL);
	seat->grab = grab;

	uint32_t *key;

	seat->n_held = 0;
	wl_array_for_each(key, &seat->keyboard->keys)
	{
		if (seat->n_held < N_HELD)
			seat->held[seat->n_held++] = *key;
	}
	/* Same wire format as wl_keyboard.keymap, opcode included. */
	weston_keyboard_send_keymap(seat->keyboard, grab);
	zwp_input_method_keyboard_grab_v2_send_repeat_info(
		grab, seat->seat->compositor->kb_repeat_rate,
		seat->seat->compositor->kb_repeat_delay);
	zwp_input_method_keyboard_grab_v2_send_modifiers(
		grab, wl_display_next_serial(seat->server->display),
		seat->keyboard->modifiers.mods_depressed,
		seat->keyboard->modifiers.mods_latched,
		seat->keyboard->modifiers.mods_locked,
		seat->keyboard->modifiers.group);
}

static void
method_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_input_method_v2_interface method_impl = {
	.commit_string = method_commit_string,
	.set_preedit_string = method_set_preedit_string,
	.delete_surrounding_text = method_delete_surrounding_text,
	.commit = method_commit,
	.get_input_popup_surface = method_get_input_popup_surface,
	.grab_keyboard = method_grab_keyboard,
	.destroy = method_destroy,
};

static void
method_resource_destroyed(struct wl_resource *resource)
{
	struct polo_text_seat *seat = wl_resource_get_user_data(resource);
	struct input_popup *popup;

	if (!seat || seat->method != resource)
		return;
	wl_list_for_each(popup, &seat->popups, link)
	{
		hide_popup(popup);
	}
	if (seat->active && seat->method_active) {
		zwp_text_input_v3_send_preedit_string(seat->active->resource,
						      NULL, 0, 0);
		zwp_text_input_v3_send_done(seat->active->resource,
					    seat->active->commits);
	}
	seat->method = NULL;
	seat->method_active = FALSE;
	clear_pending_text(seat);
	if (seat->grab) {
		wl_resource_set_user_data(seat->grab, NULL);
		seat->grab = NULL;
	}
}

static void
method_manager_get_input_method(struct wl_client *client,
				struct wl_resource *resource,
				struct wl_resource *seat_resource, uint32_t id)
{
	struct polo_text_seat *seat = seat_from_resource(
		wl_resource_get_user_data(resource), seat_resource);
	struct wl_resource *method = wl_resource_create(
		client, &zwp_input_method_v2_interface, 1, id);

	if (!method) {
		wl_client_post_no_memory(client);
		return;
	}
	if (!seat || seat->method) {
		wl_resource_set_implementation(method, &method_impl, NULL,
					       method_resource_destroyed);
		zwp_input_method_v2_send_unavailable(method);
		return;
	}
	wl_resource_set_implementation(method, &method_impl, seat,
				       method_resource_destroyed);
	seat->method = method;
	seat->method_dones = 0;
	activate_method(seat);
}

static void
method_manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_input_method_manager_v2_interface
	method_manager_impl = {
		.get_input_method = method_manager_get_input_method,
		.destroy = method_manager_destroy,
	};

static void
method_manager_bind(struct wl_client *client, void *data, uint32_t version,
		    uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(
		client, &zwp_input_method_manager_v2_interface, 1, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &method_manager_impl, data,
				       NULL);
}

/* ---- The seat ---------------------------------------------------------------
 */

gboolean
polo_text_seat_key(struct polo_text_seat *seat,
		   const struct weston_key_event *event)
{
	if (!seat->grab || !seat->method_active)
		return FALSE;
	if (event->key_state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		for (int i = 0; i < seat->n_held; i++) {
			if (seat->held[i] == event->key) {
				seat->held[i] = seat->held[--seat->n_held];
				return FALSE;
			}
		}
	}
	zwp_input_method_keyboard_grab_v2_send_key(
		seat->grab, wl_display_next_serial(seat->server->display),
		(uint32_t)(event->base.ts.tv_sec * 1000 +
			   event->base.ts.tv_nsec / 1000000),
		event->key, event->key_state);
	return TRUE;
}

void
polo_text_seat_modifiers(struct polo_text_seat *seat, uint32_t serial,
			 uint32_t depressed, uint32_t latched, uint32_t locked,
			 uint32_t group)
{
	if (seat->grab)
		zwp_input_method_keyboard_grab_v2_send_modifiers(
			seat->grab, serial, depressed, latched, locked, group);
}

struct polo_text_seat *
polo_text_seat_new(PoloBackendServer *server, struct weston_seat *seat)
{
	struct polo_text_seat *text = g_new0(struct polo_text_seat, 1);

	text->server = server;
	text->seat = seat;
	wl_list_init(&text->text_inputs);
	wl_list_init(&text->popups);
	text->focus.notify = focus_changed;
	return text;
}

void
polo_text_seat_attach_keyboard(struct polo_text_seat *seat,
			       struct weston_keyboard *keyboard)
{
	if (seat->keyboard)
		wl_list_remove(&seat->focus.link);
	seat->keyboard = keyboard;
	if (keyboard)
		wl_signal_add(&keyboard->focus_signal, &seat->focus);
}

void
polo_text_seat_free(struct polo_text_seat *seat)
{
	struct text_input *input, *next_input;
	struct input_popup *popup, *next_popup;

	polo_text_seat_attach_keyboard(seat, NULL);
	wl_list_for_each_safe(input, next_input, &seat->text_inputs, link)
	{
		input->seat = NULL;
		input->entered = NULL;
		wl_list_remove(&input->link);
		wl_list_init(&input->link);
	}
	wl_list_for_each_safe(popup, next_popup, &seat->popups, link)
	{
		popup_forget_surface(popup);
		popup->seat = NULL;
		wl_list_remove(&popup->link);
		wl_list_init(&popup->link);
	}
	if (seat->method)
		wl_resource_set_user_data(seat->method, NULL);
	if (seat->grab)
		wl_resource_set_user_data(seat->grab, NULL);
	clear_pending_text(seat);
	g_free(seat);
}

gboolean
polo_backend_text_input_init(PoloBackendServer *server, GError **error)
{
	(void)error;
	server->text_input_global = wl_global_create(
		server->display, &zwp_text_input_manager_v3_interface, 1,
		server, text_manager_bind);
	server->input_method_global = wl_global_create(
		server->display, &zwp_input_method_manager_v2_interface, 1,
		server, method_manager_bind);
	return TRUE;
}

void
polo_backend_text_input_fini(PoloBackendServer *server)
{
	g_clear_pointer(&server->text_input_global, wl_global_destroy);
	g_clear_pointer(&server->input_method_global, wl_global_destroy);
}
