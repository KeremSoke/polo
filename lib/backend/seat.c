/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PoloBackendSeat: keyboard focus, the button filter and validated pointer
 * grabs.
 *
 * The button filter wraps the pointer's default grab: when no grab is
 * active, every button event passes through our interface, is offered to
 * the filter as a value struct, and is forwarded to the original default
 * behaviour unless the filter consumes it.
 */

#include <linux/input.h>
#include <xkbcommon/xkbcommon.h>

#include <libweston/libweston.h>
#include <libweston/desktop.h>

#include "backend.h"
#include "polo-backend-private.h"

/* Exported by libweston, but declared only in its private
 * libweston-internal.h. */
void
weston_seat_update_keymap(struct weston_seat *seat, struct xkb_keymap *keymap);

#define X_KEYBOARD_GRAB_ACTION_USEC (5 * G_USEC_PER_SEC)

struct _PoloBackendSeat {
	GObject parent_instance;

	PoloBackendServer *server;
	struct weston_seat *seat;
	struct wl_listener destroy_listener;
	struct wl_listener caps_listener;

	/* The pointer whose default grab we wrap, and what we replaced. */
	struct weston_pointer *wrapped_pointer;
	const struct weston_pointer_grab_interface *default_interface;

	PoloBackendButtonFilter button_filter;
	gpointer button_filter_data;
	GDestroyNotify button_filter_destroy;
	gboolean filtering_pressed_button;

	struct weston_keyboard *wrapped_keyboard;
	const struct weston_keyboard_grab_interface *default_key_interface;
	struct wl_listener keyboard_focus_listener;
	gboolean dropping_focus;
	guint focus_lost_source;
	PoloBackendKeyFilter key_filter;
	gpointer key_filter_data;
	GDestroyNotify key_filter_destroy;
	struct wl_resource *x_keyboard_grab;
	/* A full-output X window's grab */
	struct wl_resource *x_pending_grab;
	struct weston_surface *x_grab_surface;
	struct wl_listener x_grab_surface_destroy;
	struct wl_listener x_grab_surface_commit;
	gboolean x_grab_covering;
	struct weston_surface *x_saved_focus;
	struct wl_listener x_saved_focus_destroy;
	struct polo_primary *primary;
	struct polo_text_seat *text;
	struct polo_shortcuts *shortcuts;
	struct polo_gestures *gestures;
	gint64 last_press_usec;
	guint32 consumed_keys[16];
	int n_consumed_keys;
	guint32 consumed_buttons[8];
	int n_consumed_buttons;
	/* A key Polo injects skips the server's key prefilter. */
	gboolean injecting;

	PoloBackendAxisFilter axis_filter;
	gpointer axis_filter_data;
	GDestroyNotify axis_filter_destroy;

	/* Applied to every keyboard the seat gets. */
	struct xkb_keymap *keymap;
	int pending_numlock;
	guint32 last_group;
	gboolean last_numlock;
};

enum {
	SIGNAL_DESTROYED,
	SIGNAL_MOTION,
	SIGNAL_GROUP_CHANGED,
	SIGNAL_NUMLOCK_CHANGED,
	SIGNAL_MODIFIERS_CHANGED,
	SIGNAL_KEYBOARD_FOCUS_LOST,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(PoloBackendSeat, polo_backend_seat, G_TYPE_OBJECT)

static guint32
real_modifiers(struct weston_keyboard *keyboard);

static gboolean
media_key(xkb_keysym_t sym)
{
	switch (sym) {
	case XKB_KEY_XF86AudioRaiseVolume:
	case XKB_KEY_XF86AudioLowerVolume:
	case XKB_KEY_XF86AudioMute:
	case XKB_KEY_XF86AudioMicMute:
	case XKB_KEY_XF86AudioPlay:
	case XKB_KEY_XF86AudioPause:
	case XKB_KEY_XF86AudioStop:
	case XKB_KEY_XF86AudioPrev:
	case XKB_KEY_XF86AudioNext:
	case XKB_KEY_XF86MonBrightnessUp:
	case XKB_KEY_XF86MonBrightnessDown:
	case XKB_KEY_XF86KbdBrightnessUp:
	case XKB_KEY_XF86KbdBrightnessDown:
		return TRUE;
	default:
		return FALSE;
	}
}

/* While locked, keys go straight to the focus (a lock surface or none); only
 * media keys still reach the bindings. Enter asks to restart a dead locker. */
static gboolean
locked_key(PoloBackendSeat *self, struct weston_keyboard_grab *grab,
	   const struct weston_key_event *event)
{
	struct weston_keyboard *keyboard = grab->keyboard;
	xkb_keysym_t sym;

	if (!polo_backend_session_lock_active(self->server))
		return FALSE;
	sym = keyboard->xkb_state.state
		? xkb_state_key_get_one_sym(keyboard->xkb_state.state,
					    event->key + 8)
		: XKB_KEY_NoSymbol;
	if (event->key_state == WL_KEYBOARD_KEY_STATE_PRESSED &&
	    (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) &&
	    polo_backend_session_lock_is_abandoned(self->server))
		polo_backend_server_emit_lock_restart(self->server);
	if (media_key(sym))
		return FALSE;
	self->default_key_interface->key(grab, event);
	return TRUE;
}

static PoloBackendSeat *
seat_from_pointer(struct weston_pointer *pointer)
{
	PoloBackendServer *server =
		weston_compositor_get_user_data(pointer->seat->compositor);

	return polo_backend_server_lookup_seat(server, pointer->seat);
}

static PoloBackendView *
main_view_at(PoloBackendServer *server, struct weston_coord_global pos)
{
	struct weston_view *view;
	struct weston_surface *main_surface;
	PoloBackendToplevel *toplevel;
	PoloBackendView *handle;

	view = weston_compositor_pick_view(server->compositor, pos);
	if (!view)
		return NULL;

	/* Report the toplevel, not the subsurface or popup under it. */
	main_surface = weston_surface_get_main_surface(view->surface);
	toplevel = polo_backend_toplevel_from_surface(main_surface);
	if (toplevel)
		return polo_backend_toplevel_get_view(toplevel);

	handle = polo_backend_server_lookup_view(server, view);
	if (handle)
		return handle;
	wl_list_for_each(view, &main_surface->views, surface_link)
	{
		handle = polo_backend_server_lookup_view(server, view);
		if (handle)
			return handle;
	}
	return NULL;
}

static void
filter_grab_focus(struct weston_pointer_grab *grab)
{
	PoloBackendSeat *self = seat_from_pointer(grab->pointer);

	self->default_interface->focus(grab);
}

static void
filter_grab_motion(struct weston_pointer_grab *grab,
		   const struct weston_pointer_motion_event *event)
{
	struct weston_pointer *pointer = grab->pointer;
	PoloBackendSeat *self = seat_from_pointer(pointer);

	polo_backend_stats_input(&event->base.ts);
	self->default_interface->motion(grab, event);

	g_signal_emit(self, signals[SIGNAL_MOTION], 0, pointer->pos.c.x,
		      pointer->pos.c.y,
		      (guint)(event->base.ts.tv_sec * 1000 +
			      event->base.ts.tv_nsec / 1000000),
		      main_view_at(self->server, pointer->pos));
}

static void
filter_grab_button(struct weston_pointer_grab *grab,
		   const struct weston_pointer_button_event *event)
{
	struct weston_pointer *pointer = grab->pointer;
	PoloBackendSeat *self = seat_from_pointer(pointer);

	if (event->button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
		polo_backend_stats_input(&event->base.ts);
		self->last_press_usec = g_get_monotonic_time();
	}
	if (self->button_filter &&
	    !polo_backend_session_lock_active(self->server)) {
		PoloBackendButtonEvent ev = {
			.time_msec =
				(guint32)(event->base.ts.tv_sec * 1000 +
					  event->base.ts.tv_nsec / 1000000),
			.button = event->button,
			.pressed = event->button_state ==
				WL_POINTER_BUTTON_STATE_PRESSED,
			.x = pointer->pos.c.x,
			.y = pointer->pos.c.y,
			.modifiers = real_modifiers(
				weston_seat_get_keyboard(pointer->seat)),
			.view = main_view_at(self->server, pointer->pos),
		};

		self->filtering_pressed_button = ev.pressed;
		PoloBackendFilterResult result = self->button_filter(
			self, &ev, self->button_filter_data);
		self->filtering_pressed_button = FALSE;
		if (!ev.pressed) {
			int i;

			for (i = 0; i < self->n_consumed_buttons; i++) {
				if (self->consumed_buttons[i] ==
				    event->button) {
					self->consumed_buttons[i] =
						self->consumed_buttons
							[--self->n_consumed_buttons];
					return;
				}
			}
		}
		if (result == POLO_BACKEND_FILTER_CONSUME) {
			if (ev.pressed &&
			    pointer->grab == &pointer->default_grab &&
			    self->n_consumed_buttons < 8)
				self->consumed_buttons
					[self->n_consumed_buttons++] =
					event->button;
			return;
		}
	}

	self->default_interface->button(grab, event);
}

static void
filter_grab_axis(struct weston_pointer_grab *grab,
		 const struct weston_pointer_axis_event *event)
{
	struct weston_pointer *pointer = grab->pointer;
	PoloBackendSeat *self = seat_from_pointer(pointer);

	if (self->axis_filter &&
	    !polo_backend_session_lock_active(self->server)) {
		PoloBackendAxisEvent ev = {
			.time_msec =
				(guint32)(event->base.ts.tv_sec * 1000 +
					  event->base.ts.tv_nsec / 1000000),
			.axis = event->axis,
			.value = event->value,
			.has_discrete = event->has_discrete,
			.discrete = event->discrete,
			.x = pointer->pos.c.x,
			.y = pointer->pos.c.y,
			.modifiers = real_modifiers(
				weston_seat_get_keyboard(pointer->seat)),
			.view = main_view_at(self->server, pointer->pos),
		};

		if (self->axis_filter(self, &ev, self->axis_filter_data) ==
		    POLO_BACKEND_FILTER_CONSUME)
			return;
	}

	self->default_interface->axis(grab, event);
}

static void
filter_grab_axis_source(struct weston_pointer_grab *grab, uint32_t source)
{
	PoloBackendSeat *self = seat_from_pointer(grab->pointer);

	self->default_interface->axis_source(grab, source);
}

static void
filter_grab_frame(struct weston_pointer_grab *grab)
{
	PoloBackendSeat *self = seat_from_pointer(grab->pointer);

	self->default_interface->frame(grab);
}

static void
filter_grab_cancel(struct weston_pointer_grab *grab)
{
	PoloBackendSeat *self = seat_from_pointer(grab->pointer);

	self->default_interface->cancel(grab);
}

static const struct weston_pointer_grab_interface filter_grab_interface = {
	filter_grab_focus,  filter_grab_motion,      filter_grab_button,
	filter_grab_axis,   filter_grab_axis_source, filter_grab_frame,
	filter_grab_cancel,
};

/* ---- Keyboard ------------------------------------------------------------ */

static void
real_indices(struct weston_xkb_info *info, xkb_mod_index_t indices[8])
{
	indices[0] = info->shift_mod;
	indices[1] = info->caps_mod;
	indices[2] = info->ctrl_mod;
	indices[3] = info->alt_mod;
	indices[4] = info->mod2_mod;
	indices[5] = info->mod3_mod;
	indices[6] = info->super_mod;
	indices[7] = info->mod5_mod;
}

static guint32
real_from_xkb(struct weston_xkb_info *info, xkb_mod_mask_t mods)
{
	xkb_mod_index_t indices[8];
	guint32 result = 0;

	real_indices(info, indices);
	for (int i = 0; i < 8; i++) {
		if (indices[i] != XKB_MOD_INVALID &&
		    (mods & (1u << indices[i])))
			result |= 1u << i;
	}
	return result;
}

static xkb_mod_mask_t
xkb_from_real(struct weston_xkb_info *info, guint32 real)
{
	xkb_mod_index_t indices[8];
	xkb_mod_mask_t result = 0;

	real_indices(info, indices);
	for (int i = 0; i < 8; i++) {
		if (indices[i] != XKB_MOD_INVALID && (real & (1u << i)))
			result |= 1u << indices[i];
	}
	return result;
}

static guint32
real_modifiers(struct weston_keyboard *keyboard)
{
	if (!keyboard || !keyboard->xkb_info || !keyboard->xkb_state.state)
		return 0;
	return real_from_xkb(
		keyboard->xkb_info,
		xkb_state_serialize_mods(keyboard->xkb_state.state,
					 XKB_STATE_MODS_EFFECTIVE));
}

static guint32
base_keysym(struct weston_keyboard *keyboard, xkb_keycode_t keycode)
{
	const xkb_keysym_t *syms;

	if (!keyboard->xkb_info ||
	    xkb_keymap_key_get_syms_by_level(keyboard->xkb_info->keymap,
					     keycode, 0, 0, &syms) < 1)
		return 0;
	return syms[0];
}

static gboolean
take_consumed(PoloBackendSeat *self, guint32 keycode)
{
	int i;

	for (i = 0; i < self->n_consumed_keys; i++) {
		if (self->consumed_keys[i] == keycode) {
			self->consumed_keys[i] =
				self->consumed_keys[--self->n_consumed_keys];
			return TRUE;
		}
	}
	return FALSE;
}

static void
forget_held_key(struct weston_keyboard *keyboard, guint32 key)
{
	guint32 *k, *end;

	end = (guint32 *)((char *)keyboard->keys.data + keyboard->keys.size);
	for (k = keyboard->keys.data; k < end; k++) {
		if (*k == key) {
			*k = *--end;
			break;
		}
	}
	keyboard->keys.size = (char *)end - (char *)keyboard->keys.data;
}

static gboolean
keyboard_focus_is_xserver(PoloBackendSeat *self,
			  struct weston_keyboard *keyboard)
{
	PoloBackendClient *client;

	if (!keyboard->focus || !keyboard->focus->resource)
		return FALSE;
	client = g_hash_table_lookup(
		self->server->clients,
		wl_resource_get_client(keyboard->focus->resource));
	return client && polo_backend_client_get_xserver(client);
}

static gboolean
is_escape_hatch(struct weston_keyboard *keyboard,
		const struct weston_key_event *event)
{
	struct xkb_state *state = keyboard->xkb_state.state;

	return state && event->key_state == WL_KEYBOARD_KEY_STATE_PRESSED &&
		event->key == KEY_ESC &&
		xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL,
					     XKB_STATE_MODS_EFFECTIVE) > 0 &&
		xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT,
					     XKB_STATE_MODS_EFFECTIVE) > 0 &&
		xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT,
					     XKB_STATE_MODS_EFFECTIVE) > 0;
}

static gboolean
surface_covers_output(PoloBackendServer *server, struct weston_surface *surface)
{
	struct weston_view *view;
	struct weston_output *output;

	if (!surface || !weston_surface_is_mapped(surface))
		return FALSE;
	wl_list_for_each(view, &surface->views, surface_link)
	{
		struct weston_coord_global pos;

		if (!weston_view_is_mapped(view))
			continue;
		pos = weston_view_get_pos_offset_global(view);
		wl_list_for_each(output, &server->compositor->output_list, link)
		{
			if (pos.c.x <= output->pos.c.x &&
			    pos.c.y <= output->pos.c.y &&
			    pos.c.x + surface->width >=
				    output->pos.c.x + output->width &&
			    pos.c.y + surface->height >=
				    output->pos.c.y + output->height)
				return TRUE;
		}
	}
	return FALSE;
}

static void
x_saved_focus_destroyed(struct wl_listener *listener, void *data)
{
	PoloBackendSeat *self =
		wl_container_of(listener, self, x_saved_focus_destroy);

	wl_list_remove(&self->x_saved_focus_destroy.link);
	self->x_saved_focus = NULL;
}

static void
x_forget_saved_focus(PoloBackendSeat *self)
{
	if (!self->x_saved_focus)
		return;
	wl_list_remove(&self->x_saved_focus_destroy.link);
	self->x_saved_focus = NULL;
}

static void
x_forget_grab_surface(PoloBackendSeat *self)
{
	if (!self->x_grab_surface)
		return;
	wl_list_remove(&self->x_grab_surface_destroy.link);
	wl_list_remove(&self->x_grab_surface_commit.link);
	self->x_grab_surface = NULL;
}

static gboolean
keyboard_focus_is_xserver(PoloBackendSeat *self,
			  struct weston_keyboard *keyboard);

static void
x_grant_covering(PoloBackendSeat *self, struct wl_resource *grab)
{
	struct weston_keyboard *keyboard = weston_seat_get_keyboard(self->seat);
	struct weston_surface *surface = self->x_grab_surface;

	x_forget_grab_surface(self);
	self->x_pending_grab = NULL;
	if (!keyboard || !surface ||
	    !polo_backend_session_lock_allows_focus(self->server, surface))
		return;
	x_forget_saved_focus(self);
	if (keyboard->focus && keyboard->focus != surface) {
		self->x_saved_focus = keyboard->focus;
		self->x_saved_focus_destroy.notify = x_saved_focus_destroyed;
		wl_signal_add(&self->x_saved_focus->destroy_signal,
			      &self->x_saved_focus_destroy);
	}
	if (self->x_keyboard_grab && self->x_keyboard_grab != grab)
		wl_resource_set_user_data(self->x_keyboard_grab, NULL);
	self->x_keyboard_grab = grab;
	self->x_grab_covering = TRUE;
	weston_keyboard_set_focus(keyboard, surface);
	weston_log("X keyboard grab granted to a full-output X window\n");
}

static void
x_grab_surface_committed(struct wl_listener *listener, void *data)
{
	PoloBackendSeat *self =
		wl_container_of(listener, self, x_grab_surface_commit);

	if (self->x_pending_grab &&
	    surface_covers_output(self->server, self->x_grab_surface))
		x_grant_covering(self, self->x_pending_grab);
}

static void
x_grab_surface_destroyed(struct wl_listener *listener, void *data)
{
	PoloBackendSeat *self =
		wl_container_of(listener, self, x_grab_surface_destroy);

	x_forget_grab_surface(self);
	self->x_pending_grab = NULL;
}

static void
x_end_covering(PoloBackendSeat *self)
{
	struct weston_keyboard *keyboard = weston_seat_get_keyboard(self->seat);

	self->x_grab_covering = FALSE;
	if (keyboard && self->x_saved_focus &&
	    weston_surface_is_mapped(self->x_saved_focus) &&
	    (!keyboard->focus || keyboard_focus_is_xserver(self, keyboard)) &&
	    polo_backend_session_lock_allows_focus(self->server,
						   self->x_saved_focus))
		weston_keyboard_set_focus(keyboard, self->x_saved_focus);
	x_forget_saved_focus(self);
}

static gboolean
x_keyboard_grab_takes_key(PoloBackendSeat *self,
			  struct weston_keyboard *keyboard,
			  const struct weston_key_event *event)
{
	if (!self->x_keyboard_grab)
		return FALSE;
	if (keyboard_focus_is_xserver(self, keyboard) &&
	    !is_escape_hatch(keyboard, event))
		return TRUE;
	wl_resource_set_user_data(self->x_keyboard_grab, NULL);
	self->x_keyboard_grab = NULL;
	if (self->x_grab_covering)
		x_end_covering(self);
	weston_log("X keyboard grab ended\n");
	return FALSE;
}

gboolean
polo_backend_seat_begin_x_keyboard_grab(PoloBackendSeat *seat,
					struct wl_resource *grab,
					struct weston_surface *surface)
{
	struct weston_keyboard *keyboard = weston_seat_get_keyboard(seat->seat);

	if (!keyboard || polo_backend_session_lock_active(seat->server)) {
		weston_log("X keyboard grab refused\n");
		return FALSE;
	}
	/* screensaver or lock window grabs without a key press*/
	if (surface && !seat->x_grab_covering) {
		x_forget_grab_surface(seat);
		seat->x_pending_grab = NULL;
		seat->x_grab_surface = surface;
		seat->x_grab_surface_destroy.notify = x_grab_surface_destroyed;
		wl_signal_add(&surface->destroy_signal,
			      &seat->x_grab_surface_destroy);
		seat->x_grab_surface_commit.notify = x_grab_surface_committed;
		wl_signal_add(&surface->commit_signal,
			      &seat->x_grab_surface_commit);
		if (surface_covers_output(seat->server, surface)) {
			x_grant_covering(seat, grab);
			return TRUE;
		}
		if (!keyboard_focus_is_xserver(seat, keyboard)) {
			seat->x_pending_grab = grab;
			weston_log("X keyboard grab waits for its window\n");
			return TRUE;
		}
		x_forget_grab_surface(seat);
	}
	if (!keyboard_focus_is_xserver(seat, keyboard) ||
	    !seat->last_press_usec ||
	    g_get_monotonic_time() - seat->last_press_usec >
		    X_KEYBOARD_GRAB_ACTION_USEC) {
		weston_log("X keyboard grab refused\n");
		return FALSE;
	}
	if (seat->x_keyboard_grab)
		wl_resource_set_user_data(seat->x_keyboard_grab, NULL);
	seat->x_keyboard_grab = grab;
	weston_log("X keyboard grab granted\n");
	return TRUE;
}

void
polo_backend_seat_end_x_keyboard_grab(PoloBackendSeat *seat,
				      struct wl_resource *grab)
{
	if (seat->x_pending_grab == grab) {
		seat->x_pending_grab = NULL;
		x_forget_grab_surface(seat);
	}
	if (seat->x_keyboard_grab == grab) {
		seat->x_keyboard_grab = NULL;
		if (seat->x_grab_covering)
			x_end_covering(seat);
		weston_log("X keyboard grab ended by its client\n");
	}
}

/* Pressed alone they change nothing on screen, so they say nothing about
 * how fast input reaches it. */
static gboolean
is_modifier_key(guint32 key)
{
	switch (key) {
	case KEY_LEFTCTRL:
	case KEY_RIGHTCTRL:
	case KEY_LEFTSHIFT:
	case KEY_RIGHTSHIFT:
	case KEY_LEFTALT:
	case KEY_RIGHTALT:
	case KEY_LEFTMETA:
	case KEY_RIGHTMETA:
	case KEY_CAPSLOCK:
		return TRUE;
	default:
		return FALSE;
	}
}

static void
filter_key(struct weston_keyboard_grab *grab,
	   const struct weston_key_event *event)
{
	struct weston_keyboard *keyboard = grab->keyboard;
	PoloBackendSeat *self = polo_backend_server_lookup_seat(
		weston_compositor_get_user_data(keyboard->seat->compositor),
		keyboard->seat);

	if (!self)
		return;
	if (event->key_state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (!is_modifier_key(event->key))
			polo_backend_stats_input(&event->base.ts);
		self->last_press_usec = g_get_monotonic_time();
	}
	if (locked_key(self, grab, event))
		return;
	if (x_keyboard_grab_takes_key(self, keyboard, event) ||
	    polo_shortcuts_takes_key(self->shortcuts,
				     is_escape_hatch(keyboard, event))) {
		if (event->key_state == WL_KEYBOARD_KEY_STATE_RELEASED &&
		    take_consumed(self, event->key))
			return;
		if (!polo_text_seat_key(self->text, event))
			self->default_key_interface->key(grab, event);
		return;
	}

	if (self->key_filter) {
		xkb_keycode_t keycode = event->key + 8;

		/* A popup grab took this key's release, so the record is stale.
		 */
		if (event->key_state == WL_KEYBOARD_KEY_STATE_PRESSED)
			take_consumed(self, event->key);

		PoloBackendKeyEvent ev = {
			.time_msec =
				(guint32)(event->base.ts.tv_sec * 1000 +
					  event->base.ts.tv_nsec / 1000000),
			.keycode = event->key,
			.keysym = keyboard->xkb_state.state
				? xkb_state_key_get_one_sym(
					  keyboard->xkb_state.state, keycode)
				: 0,
			.base_keysym = base_keysym(keyboard, keycode),
			.modifiers = real_modifiers(keyboard),
			.pressed = event->key_state ==
				WL_KEYBOARD_KEY_STATE_PRESSED,
		};
		PoloBackendFilterResult result =
			self->key_filter(self, &ev, self->key_filter_data);

		if (ev.pressed && result == POLO_BACKEND_FILTER_CONSUME) {
			if (self->n_consumed_keys < 16)
				self->consumed_keys[self->n_consumed_keys++] =
					event->key;
			forget_held_key(keyboard, event->key);
			return;
		}
		if (!ev.pressed && take_consumed(self, event->key))
			return;
	}

	if (polo_text_seat_key(self->text, event))
		return;
	self->default_key_interface->key(grab, event);
}

static gboolean
numlock_locked(struct weston_keyboard *keyboard)
{
	return keyboard->xkb_state.state &&
		xkb_state_mod_name_is_active(keyboard->xkb_state.state,
					     XKB_MOD_NAME_NUM,
					     XKB_STATE_MODS_LOCKED) > 0;
}

static void
filter_modifiers(struct weston_keyboard_grab *grab, uint32_t serial,
		 uint32_t mods_depressed, uint32_t mods_latched,
		 uint32_t mods_locked, uint32_t group)
{
	PoloBackendSeat *self = polo_backend_server_lookup_seat(
		weston_compositor_get_user_data(
			grab->keyboard->seat->compositor),
		grab->keyboard->seat);
	gboolean numlock = numlock_locked(grab->keyboard);

	self->default_key_interface->modifiers(
		grab, serial, mods_depressed, mods_latched, mods_locked, group);
	polo_text_seat_modifiers(self->text, serial, mods_depressed,
				 mods_latched, mods_locked, group);

	if (group != self->last_group) {
		self->last_group = group;
		g_signal_emit(self, signals[SIGNAL_GROUP_CHANGED], 0, group);
	}
	if (numlock != self->last_numlock) {
		self->last_numlock = numlock;
		g_signal_emit(self, signals[SIGNAL_NUMLOCK_CHANGED], 0,
			      numlock);
	}
	g_signal_emit(self, signals[SIGNAL_MODIFIERS_CHANGED], 0,
		      real_modifiers(grab->keyboard));
}

static void
filter_key_cancel(struct weston_keyboard_grab *grab)
{
	PoloBackendSeat *self = polo_backend_server_lookup_seat(
		weston_compositor_get_user_data(
			grab->keyboard->seat->compositor),
		grab->keyboard->seat);

	self->default_key_interface->cancel(grab);
}

static const struct weston_keyboard_grab_interface filter_key_interface = {
	filter_key,
	filter_modifiers,
	filter_key_cancel,
};

static void
lock_numlock(struct weston_keyboard *keyboard, gboolean locked)
{
	struct xkb_state *state = keyboard->xkb_state.state;
	xkb_mod_mask_t num = 1u << keyboard->xkb_info->mod2_mod;
	xkb_mod_mask_t mods_locked;

	if (!state)
		return;
	mods_locked = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
	mods_locked = locked ? mods_locked | num : mods_locked & ~num;
	xkb_state_update_mask(
		state,
		xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED),
		xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED),
		mods_locked,
		xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_DEPRESSED),
		xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_LATCHED),
		xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_LOCKED));
	notify_modifiers(
		keyboard->seat,
		wl_display_next_serial(keyboard->seat->compositor->wl_display));
}

static gboolean
focus_lost_idle(gpointer data)
{
	PoloBackendSeat *self = data;
	struct weston_keyboard *keyboard;

	self->focus_lost_source = 0;
	keyboard = self->seat ? weston_seat_get_keyboard(self->seat) : NULL;
	if (keyboard && !keyboard->focus)
		g_signal_emit(self, signals[SIGNAL_KEYBOARD_FOCUS_LOST], 0);
	return G_SOURCE_REMOVE;
}

static void
keyboard_focus_changed(struct wl_listener *listener, void *data)
{
	PoloBackendSeat *self =
		wl_container_of(listener, self, keyboard_focus_listener);
	struct weston_keyboard *keyboard = data;

	if (keyboard->focus || self->dropping_focus || self->focus_lost_source)
		return;
	self->focus_lost_source = g_idle_add(focus_lost_idle, self);
}

static void
wrap_keyboard(PoloBackendSeat *self)
{
	struct weston_keyboard *keyboard = weston_seat_get_keyboard(self->seat);

	if (!keyboard || keyboard == self->wrapped_keyboard)
		return;

	self->wrapped_keyboard = keyboard;
	self->keyboard_focus_listener.notify = keyboard_focus_changed;
	wl_signal_add(&keyboard->focus_signal, &self->keyboard_focus_listener);
	polo_primary_attach_keyboard(self->primary, keyboard);
	polo_text_seat_attach_keyboard(self->text, keyboard);
	polo_shortcuts_attach_keyboard(self->shortcuts, keyboard);
	self->default_key_interface = keyboard->default_grab.interface;
	keyboard->default_grab.interface = &filter_key_interface;
	self->last_group = keyboard->modifiers.group;
	self->last_numlock = numlock_locked(keyboard);

	if (self->keymap)
		weston_seat_update_keymap(self->seat, self->keymap);
	if (self->pending_numlock >= 0)
		lock_numlock(keyboard, self->pending_numlock);
}

static void
unwrap_keyboard(PoloBackendSeat *self)
{
	struct weston_keyboard *keyboard;

	if (!self->wrapped_keyboard || !self->seat)
		return;

	keyboard = weston_seat_get_keyboard(self->seat);
	if (keyboard == self->wrapped_keyboard &&
	    keyboard->default_grab.interface == &filter_key_interface)
		keyboard->default_grab.interface = self->default_key_interface;
	polo_primary_attach_keyboard(self->primary, NULL);
	polo_text_seat_attach_keyboard(self->text, NULL);
	polo_shortcuts_attach_keyboard(self->shortcuts, NULL);
	wl_list_remove(&self->keyboard_focus_listener.link);
	if (self->focus_lost_source)
		g_source_remove(self->focus_lost_source);
	self->focus_lost_source = 0;
	self->wrapped_keyboard = NULL;
}

void
polo_backend_seat_set_key_filter(PoloBackendSeat *seat,
				 PoloBackendKeyFilter filter,
				 gpointer user_data, GDestroyNotify destroy)
{
	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));

	if (seat->key_filter_destroy)
		seat->key_filter_destroy(seat->key_filter_data);
	seat->key_filter = filter;
	seat->key_filter_data = user_data;
	seat->key_filter_destroy = destroy;
	seat->n_consumed_keys = 0;
}

guint32
polo_backend_keysym_from_name(const char *name)
{
	g_return_val_if_fail(name != NULL, 0);
	return xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS);
}

guint32
polo_backend_keysym_to_lower(guint32 keysym)
{
	return xkb_keysym_to_lower(keysym);
}

char *
polo_backend_keysym_name(guint32 keysym)
{
	char name[64];

	if (keysym == 0 || xkb_keysym_get_name(keysym, name, sizeof name) < 0)
		return g_strdup("");
	return g_strdup(name);
}

/* ---- Pointer --------------------------------------------------------------
 */

static void
wrap_pointer(PoloBackendSeat *self)
{
	struct weston_pointer *pointer = weston_seat_get_pointer(self->seat);

	if (!pointer || pointer == self->wrapped_pointer)
		return;

	polo_backend_cursor_add_pointer(self->server, pointer);
	polo_backend_zoom_add_pointer(self->server, pointer);
	self->wrapped_pointer = pointer;
	polo_gestures_attach_pointer(self->gestures, pointer);
	self->default_interface = pointer->default_grab.interface;
	pointer->default_grab.interface = &filter_grab_interface;
}

static void
unwrap_pointer(PoloBackendSeat *self)
{
	struct weston_pointer *pointer;

	if (!self->wrapped_pointer || !self->seat)
		return;

	pointer = weston_seat_get_pointer(self->seat);
	if (pointer == self->wrapped_pointer &&
	    pointer->default_grab.interface == &filter_grab_interface)
		pointer->default_grab.interface = self->default_interface;
	polo_gestures_attach_pointer(self->gestures, NULL);
	self->wrapped_pointer = NULL;
}

static void
seat_caps_changed(struct wl_listener *listener, void *data)
{
	PoloBackendSeat *self = wl_container_of(listener, self, caps_listener);

	wrap_pointer(self);
	wrap_keyboard(self);
}

struct polo_gestures *
polo_backend_seat_get_gestures(PoloBackendSeat *seat)
{
	return seat->gestures;
}

struct polo_shortcuts *
polo_backend_seat_get_shortcuts(PoloBackendSeat *seat)
{
	return seat->shortcuts;
}

struct polo_text_seat *
polo_backend_seat_get_text(PoloBackendSeat *seat)
{
	return seat->text;
}

struct polo_primary *
polo_backend_seat_get_primary(PoloBackendSeat *seat)
{
	return seat->primary;
}

static void
seat_destroyed(struct wl_listener *listener, void *data)
{
	PoloBackendSeat *self =
		wl_container_of(listener, self, destroy_listener);

	polo_backend_seat_destroy(self);
}

PoloBackendSeat *
polo_backend_seat_new(PoloBackendServer *server, struct weston_seat *seat)
{
	PoloBackendSeat *self = g_object_new(POLO_BACKEND_TYPE_SEAT, NULL);

	self->server = server;
	self->seat = seat;
	self->primary = polo_primary_new(server, seat);
	self->text = polo_text_seat_new(server, seat);
	self->shortcuts = polo_shortcuts_new();
	self->gestures = polo_gestures_new(server);
	self->destroy_listener.notify = seat_destroyed;
	wl_signal_add(&seat->destroy_signal, &self->destroy_listener);
	self->caps_listener.notify = seat_caps_changed;
	wl_signal_add(&seat->updated_caps_signal, &self->caps_listener);
	wrap_pointer(self);
	wrap_keyboard(self);

	return self;
}

struct weston_seat *
polo_backend_seat_get_weston_seat(PoloBackendSeat *seat)
{
	return seat->seat;
}

void
polo_backend_seat_destroy(PoloBackendSeat *self)
{
	if (!self->seat)
		return;

	struct weston_keyboard *keyboard = weston_seat_get_keyboard(self->seat);
	if (keyboard)
		keyboard->grab->interface->cancel(keyboard->grab);
	unwrap_pointer(self);
	unwrap_keyboard(self);
	g_clear_pointer(&self->primary, polo_primary_free);
	g_clear_pointer(&self->text, polo_text_seat_free);
	g_clear_pointer(&self->shortcuts, polo_shortcuts_free);
	g_clear_pointer(&self->gestures, polo_gestures_free);
	wl_list_remove(&self->destroy_listener.link);
	wl_list_remove(&self->caps_listener.link);
	self->seat = NULL;
	g_signal_emit(self, signals[SIGNAL_DESTROYED], 0);
}

const char *
polo_backend_seat_get_name(PoloBackendSeat *seat)
{
	g_return_val_if_fail(POLO_BACKEND_IS_SEAT(seat), NULL);
	return seat->seat ? seat->seat->seat_name : "";
}

void
polo_backend_seat_get_pointer_position(PoloBackendSeat *seat, double *x,
				       double *y)
{
	struct weston_pointer *pointer;

	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));

	pointer = seat->seat ? weston_seat_get_pointer(seat->seat) : NULL;
	*x = pointer ? pointer->pos.c.x : 0;
	*y = pointer ? pointer->pos.c.y : 0;
}

guint32
polo_backend_seat_get_modifiers(PoloBackendSeat *seat)
{
	g_return_val_if_fail(POLO_BACKEND_IS_SEAT(seat), 0);

	return seat->seat ? real_modifiers(weston_seat_get_keyboard(seat->seat))
			  : 0;
}

void
polo_backend_seat_set_keyboard_focus(PoloBackendSeat *seat,
				     PoloBackendView *view)
{
	struct weston_keyboard *keyboard;
	struct weston_view *wv;

	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));
	if (!seat->seat)
		return;

	wv = view ? polo_backend_view_get_weston_view(view) : NULL;
	if (polo_backend_session_lock_active(seat->server) &&
	    (!wv ||
	     !polo_backend_session_lock_allows_focus(seat->server,
						     wv->surface)))
		return;
	if (seat->x_grab_covering)
		return;
	if (wv) {
		weston_view_activate_input(wv, seat->seat,
					   WESTON_ACTIVATE_FLAG_CLICKED);
		return;
	}

	keyboard = weston_seat_get_keyboard(seat->seat);
	if (keyboard) {
		seat->dropping_focus = TRUE;
		weston_keyboard_set_focus(keyboard, NULL);
		seat->dropping_focus = FALSE;
	}
}

void
polo_backend_seat_set_button_filter(PoloBackendSeat *seat,
				    PoloBackendButtonFilter filter,
				    gpointer user_data, GDestroyNotify destroy)
{
	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));

	if (seat->button_filter_destroy)
		seat->button_filter_destroy(seat->button_filter_data);
	seat->button_filter = filter;
	seat->button_filter_data = user_data;
	seat->button_filter_destroy = destroy;
}

PoloBackendPointerGrab *
polo_backend_seat_start_pointer_grab(PoloBackendSeat *seat, guint32 serial,
				     PoloBackendView *view)
{
	struct weston_pointer *pointer;
	struct weston_view *wv;
	struct weston_surface *focus;

	g_return_val_if_fail(POLO_BACKEND_IS_SEAT(seat), NULL);
	g_return_val_if_fail(POLO_BACKEND_IS_VIEW(view), NULL);

	if (!seat->seat)
		return NULL;
	wv = polo_backend_view_get_weston_view(view);
	pointer = weston_seat_get_pointer(seat->seat);
	if (!wv || !pointer || !pointer->focus || pointer->button_count == 0 ||
	    pointer->grab_serial != serial ||
	    pointer->grab != &pointer->default_grab)
		return NULL;

	focus = weston_surface_get_main_surface(pointer->focus->surface);
	if (focus != wv->surface)
		return NULL;

	return polo_backend_pointer_grab_start(pointer);
}

PoloBackendPointerGrab *
polo_backend_seat_start_pointer_grab_for_binding(PoloBackendSeat *seat,
						 PoloBackendView *view)
{
	struct weston_pointer *pointer;
	struct weston_view *wv;
	struct weston_surface *focus;

	g_return_val_if_fail(POLO_BACKEND_IS_SEAT(seat), NULL);
	g_return_val_if_fail(POLO_BACKEND_IS_VIEW(view), NULL);

	if (!seat->seat || !seat->filtering_pressed_button)
		return NULL;
	wv = polo_backend_view_get_weston_view(view);
	pointer = weston_seat_get_pointer(seat->seat);
	if (!wv || !pointer || !pointer->focus || pointer->button_count != 1 ||
	    pointer->grab != &pointer->default_grab)
		return NULL;
	focus = weston_surface_get_main_surface(pointer->focus->surface);
	if (focus != wv->surface)
		return NULL;
	return polo_backend_pointer_grab_start(pointer);
}

void
polo_backend_seat_set_axis_filter(PoloBackendSeat *seat,
				  PoloBackendAxisFilter filter,
				  gpointer user_data, GDestroyNotify destroy)
{
	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));

	if (seat->axis_filter_destroy)
		seat->axis_filter_destroy(seat->axis_filter_data);
	seat->axis_filter = filter;
	seat->axis_filter_data = user_data;
	seat->axis_filter_destroy = destroy;
}

gboolean
polo_backend_seat_set_keymap(PoloBackendSeat *seat, const char *rules,
			     const char *model, const char *layout,
			     const char *variant, const char *options,
			     GError **error)
{
	struct xkb_rule_names names = {
		.rules = rules,
		.model = model,
		.layout = layout,
		.variant = variant,
		.options = options,
	};
	struct xkb_keymap *keymap;

	g_return_val_if_fail(POLO_BACKEND_IS_SEAT(seat), FALSE);

	if (!seat->seat)
		return TRUE;
	keymap = xkb_keymap_new_from_names(seat->seat->compositor->xkb_context,
					   &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!keymap) {
		g_set_error(error, POLO_BACKEND_ERROR,
			    POLO_BACKEND_ERROR_FAILED,
			    "cannot compile the keymap for layout \"%s\"",
			    layout ? layout : "");
		return FALSE;
	}
	if (seat->keymap)
		xkb_keymap_unref(seat->keymap);
	seat->keymap = keymap;
	weston_seat_update_keymap(seat->seat, keymap);
	return TRUE;
}

void
polo_backend_seat_lock_group(PoloBackendSeat *seat, guint32 group)
{
	struct weston_keyboard *keyboard;
	struct xkb_state *state;

	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));

	keyboard = seat->seat ? weston_seat_get_keyboard(seat->seat) : NULL;
	if (!keyboard || !keyboard->xkb_state.state)
		return;
	state = keyboard->xkb_state.state;
	xkb_state_update_mask(
		state,
		xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED),
		xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED),
		xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED),
		xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_DEPRESSED),
		xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_LATCHED),
		group);
	notify_modifiers(
		seat->seat,
		wl_display_next_serial(seat->seat->compositor->wl_display));
}

guint32
polo_backend_seat_get_group(PoloBackendSeat *seat)
{
	struct weston_keyboard *keyboard;

	g_return_val_if_fail(POLO_BACKEND_IS_SEAT(seat), 0);

	keyboard = seat->seat ? weston_seat_get_keyboard(seat->seat) : NULL;
	return keyboard ? keyboard->modifiers.group : 0;
}

void
polo_backend_seat_set_numlock(PoloBackendSeat *seat, gboolean locked)
{
	struct weston_keyboard *keyboard;

	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));

	seat->pending_numlock = locked ? 1 : 0;
	keyboard = seat->seat ? weston_seat_get_keyboard(seat->seat) : NULL;
	if (keyboard)
		lock_numlock(keyboard, locked);
}

PoloBackendPointerGrab *
polo_backend_seat_begin_pointer_grab(PoloBackendSeat *seat)
{
	struct weston_pointer *pointer;

	g_return_val_if_fail(POLO_BACKEND_IS_SEAT(seat), NULL);

	if (!seat->seat)
		return NULL;
	pointer = weston_seat_get_pointer(seat->seat);
	if (!pointer || pointer->grab != &pointer->default_grab)
		return NULL;
	return polo_backend_pointer_grab_start(pointer);
}

static void
polo_backend_seat_dispose(GObject *object)
{
	PoloBackendSeat *self = POLO_BACKEND_SEAT(object);

	if (self->x_keyboard_grab) {
		wl_resource_set_user_data(self->x_keyboard_grab, NULL);
		self->x_keyboard_grab = NULL;
	}
	if (self->x_pending_grab) {
		wl_resource_set_user_data(self->x_pending_grab, NULL);
		self->x_pending_grab = NULL;
	}
	x_forget_grab_surface(self);
	x_forget_saved_focus(self);
	self->x_grab_covering = FALSE;
	polo_backend_seat_set_button_filter(self, NULL, NULL, NULL);
	polo_backend_seat_set_key_filter(self, NULL, NULL, NULL);
	polo_backend_seat_set_axis_filter(self, NULL, NULL, NULL);
	g_clear_pointer(&self->keymap, xkb_keymap_unref);
	G_OBJECT_CLASS(polo_backend_seat_parent_class)->dispose(object);
}

static void
polo_backend_seat_class_init(PoloBackendSeatClass *klass)
{
	G_OBJECT_CLASS(klass)->dispose = polo_backend_seat_dispose;

	signals[SIGNAL_DESTROYED] = g_signal_new(
		"destroyed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_MOTION] = g_signal_new(
		"motion", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 4, G_TYPE_DOUBLE, G_TYPE_DOUBLE,
		G_TYPE_UINT, POLO_BACKEND_TYPE_VIEW);
	signals[SIGNAL_GROUP_CHANGED] = g_signal_new(
		"group-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_NUMLOCK_CHANGED] = g_signal_new(
		"numlock-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals[SIGNAL_MODIFIERS_CHANGED] =
		g_signal_new("modifiers-changed", G_TYPE_FROM_CLASS(klass),
			     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			     G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_KEYBOARD_FOCUS_LOST] = g_signal_new(
		"keyboard-focus-lost", G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_backend_seat_init(PoloBackendSeat *self)
{
	self->pending_numlock = -1;
}

void
polo_backend_seat_warp_pointer(PoloBackendSeat *seat, double x, double y)
{
	struct weston_pointer *pointer;
	struct weston_coord_global pos;

	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));

	pointer = weston_seat_get_pointer(seat->seat);
	if (!pointer)
		return;
	pos.c = weston_coord(x, y);
	weston_pointer_move_to(pointer, pos);
}

void
polo_backend_seat_set_cursor(PoloBackendSeat *seat, PoloBackendCursor cursor)
{
	struct weston_pointer *pointer;

	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));

	pointer = seat->seat ? weston_seat_get_pointer(seat->seat) : NULL;
	if (pointer && pointer->focus)
		polo_backend_cursor_set_shape(seat->server, pointer, cursor);
}

void
polo_backend_seat_set_pointer_hidden(PoloBackendSeat *seat, gboolean hidden)
{
	struct weston_pointer *pointer;

	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));
	pointer = seat->seat ? weston_seat_get_pointer(seat->seat) : NULL;
	if (pointer)
		polo_backend_cursor_set_hidden(seat->server, pointer, hidden);
}

/* ---- AccessX ------------------------------------------------------------- */

bool
polo_backend_seat_prefilter_key(const struct weston_key_event *event,
				void *data)
{
	PoloBackendServer *server = data;
	struct weston_keyboard *keyboard =
		weston_seat_get_keyboard(event->base.seat);
	PoloBackendSeat *self =
		polo_backend_server_lookup_seat(server, event->base.seat);

	if (!server->key_prefilter || !self || self->injecting || !keyboard)
		return false;

	xkb_keycode_t keycode = event->key + 8;
	PoloBackendKeyEvent ev = {
		.time_msec = (guint32)(event->base.ts.tv_sec * 1000 +
				       event->base.ts.tv_nsec / 1000000),
		.keycode = event->key,
		.keysym = keyboard->xkb_state.state
			? xkb_state_key_get_one_sym(keyboard->xkb_state.state,
						    keycode)
			: 0,
		.base_keysym = base_keysym(keyboard, keycode),
		.modifiers = real_modifiers(keyboard),
		.pressed = event->key_state == WL_KEYBOARD_KEY_STATE_PRESSED,
	};

	return server->key_prefilter(self, &ev, server->key_prefilter_data) ==
		POLO_BACKEND_FILTER_CONSUME;
}

void
polo_backend_seat_inject_key(PoloBackendSeat *seat, guint32 key,
			     gboolean pressed)
{
	struct weston_key_event event;
	struct timespec time;

	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));
	if (!seat->seat || !weston_seat_get_keyboard(seat->seat))
		return;
	weston_compositor_get_time(&time);
	weston_key_event_init(&event, &time, seat->seat, key,
			      pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
				      : WL_KEYBOARD_KEY_STATE_RELEASED,
			      STATE_UPDATE_AUTOMATIC);
	seat->injecting = TRUE;
	notify_key(&event);
	seat->injecting = FALSE;
}

void
polo_backend_seat_get_modifier_state(PoloBackendSeat *seat,
				     PoloBackendModifiers *depressed,
				     PoloBackendModifiers *latched,
				     PoloBackendModifiers *locked)
{
	struct weston_keyboard *keyboard;
	struct xkb_state *state;

	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));
	*depressed = *latched = *locked = 0;
	keyboard = seat->seat ? weston_seat_get_keyboard(seat->seat) : NULL;
	if (!keyboard || !keyboard->xkb_info || !keyboard->xkb_state.state)
		return;
	state = keyboard->xkb_state.state;
	*depressed = real_from_xkb(
		keyboard->xkb_info,
		xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED));
	*latched = real_from_xkb(
		keyboard->xkb_info,
		xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED));
	*locked = real_from_xkb(
		keyboard->xkb_info,
		xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED));
}

void
polo_backend_seat_set_latched_locked(PoloBackendSeat *seat,
				     PoloBackendModifiers latched,
				     PoloBackendModifiers locked)
{
	struct weston_keyboard *keyboard;
	struct xkb_state *state;

	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));
	keyboard = seat->seat ? weston_seat_get_keyboard(seat->seat) : NULL;
	if (!keyboard || !keyboard->xkb_info || !keyboard->xkb_state.state)
		return;
	state = keyboard->xkb_state.state;
	xkb_state_update_mask(
		state,
		xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED),
		xkb_from_real(keyboard->xkb_info, latched),
		xkb_from_real(keyboard->xkb_info, locked),
		xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_DEPRESSED),
		xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_LATCHED),
		xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_LOCKED));
	notify_modifiers(seat->seat,
			 wl_display_next_serial(seat->server->display));
}

void
polo_backend_seat_move_pointer(PoloBackendSeat *seat, double dx, double dy)
{
	struct weston_pointer_motion_event event;
	struct weston_coord rel = weston_coord(dx, dy);
	struct timespec time;

	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));
	if (!seat->seat || !weston_seat_get_pointer(seat->seat))
		return;
	weston_compositor_get_time(&time);
	weston_pointer_motion_event_init(&event, &time, seat->seat,
					 WESTON_POINTER_MOTION_REL, NULL, &rel,
					 NULL);
	notify_motion(&event);
	notify_pointer_frame(seat->seat);
}

void
polo_backend_seat_inject_button(PoloBackendSeat *seat, guint32 button,
				gboolean pressed)
{
	struct weston_pointer_button_event event;
	struct timespec time;

	g_return_if_fail(POLO_BACKEND_IS_SEAT(seat));
	if (!seat->seat || !weston_seat_get_pointer(seat->seat))
		return;
	weston_compositor_get_time(&time);
	weston_pointer_button_event_init(
		&event, &time, seat->seat, button,
		pressed ? WL_POINTER_BUTTON_STATE_PRESSED
			: WL_POINTER_BUTTON_STATE_RELEASED);
	notify_button(&event);
	notify_pointer_frame(seat->seat);
}
