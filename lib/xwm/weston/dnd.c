/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <X11/Xcursor/Xcursor.h>

#include <libweston/libweston.h>
#include "xwayland.h"
#include "polo-xwm-private.h"

#include "shared/hash.h"
#include "shared/helpers.h"

struct dnd_data_source {
	struct weston_data_source base;
	struct weston_wm *wm;
	int version;
	uint32_t window;
	xcb_atom_t text_target;
	bool dropped;
};

static xcb_atom_t
intern(struct weston_wm *wm, const char *name)
{
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
		wm->conn, xcb_intern_atom(wm->conn, 0, strlen(name), name), NULL);
	xcb_atom_t atom = reply ? reply->atom : XCB_ATOM_NONE;

	free(reply);
	return atom;
}

static void
send_message(struct weston_wm *wm, xcb_window_t to, xcb_atom_t type,
	     uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4)
{
	xcb_client_message_event_t message;

	memset(&message, 0, sizeof message);
	message.response_type = XCB_CLIENT_MESSAGE;
	message.format = 32;
	message.window = to;
	message.type = type;
	message.data.data32[0] = d0;
	message.data.data32[1] = d1;
	message.data.data32[2] = d2;
	message.data.data32[3] = d3;
	message.data.data32[4] = d4;
	xcb_send_event(wm->conn, 0, to, XCB_EVENT_MASK_NO_EVENT, (char *)&message);
	xcb_flush(wm->conn);
}

/* ---- X source, native target ------------------------------------------- */

static void
send_status(struct weston_wm *wm)
{
	struct dnd_data_source *source = wm->x_source;

	send_message(wm, source->window, wm->atom.xdnd_status, wm->dnd_window,
		     source->base.accepted ? 1 : 0, 0, 0,
		     source->base.current_dnd_action ==
				     WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE ?
			     intern(wm, "XdndActionMove") :
			     wm->atom.xdnd_action_copy);
}

static void
data_source_accept(struct weston_data_source *base,
		   uint32_t serial, const char *mime_type)
{
	struct dnd_data_source *source = (struct dnd_data_source *) base;

	base->accepted = mime_type != NULL;
	if (source->wm->x_source == source)
		send_status(source->wm);
}

static void
data_source_send(struct weston_data_source *base,
		 const char *mime_type, int32_t fd)
{
	struct dnd_data_source *source = (struct dnd_data_source *) base;
	struct weston_wm *wm = source->wm;
	char *text = NULL;

	if (strcmp(mime_type, "text/plain;charset=utf-8") == 0 &&
	    source->text_target != wm->atom.utf8_string)
		text = strdup(get_atom_name(wm->conn, source->text_target));
	weston_wm_read_selection(wm, wm->atom.xdnd_selection,
				 text ? text : mime_type, fd);
	free(text);
}

static void
source_free(struct dnd_data_source *source)
{
	char **mime;

	wl_signal_emit(&source->base.destroy_signal, &source->base);
	wl_array_for_each(mime, &source->base.mime_types)
		free(*mime);
	wl_array_release(&source->base.mime_types);
	free(source);
}

static void
data_source_cancel(struct weston_data_source *source)
{
}

static void
end_x_drag(struct weston_wm *wm)
{
	if (wm->x_drag)
		weston_external_drag_end(wm->x_drag);
	wm->x_drag = NULL;
	if (wm->x_source && !wm->x_source->dropped)
		source_free(wm->x_source);
	wm->x_source = NULL;
}

static void
data_source_finished(struct weston_data_source *base)
{
	struct dnd_data_source *source = (struct dnd_data_source *) base;

	send_message(source->wm, source->window, source->wm->atom.xdnd_finished,
		     source->wm->dnd_window, 1,
		     base->current_dnd_action ==
				     WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE ?
			     intern(source->wm, "XdndActionMove") :
			     source->wm->atom.xdnd_action_copy,
		     0, 0);
	if (source->wm->x_source == source)
		source->wm->x_source = NULL;
	source_free(source);
}

static void
handle_enter(struct weston_wm *wm, xcb_client_message_event_t *client_message)
{
	struct dnd_data_source *source;
	struct weston_seat *seat = weston_wm_pick_seat(wm);
	char **p;
	const char *name;
	uint32_t *types;
	int i, length;
	xcb_get_property_reply_t *reply = NULL;

	end_x_drag(wm);
	if (!seat)
		return;
	source = zalloc(sizeof *source);
	if (source == NULL)
		return;

	wl_signal_init(&source->base.destroy_signal);
	source->base.accept = data_source_accept;
	source->base.send = data_source_send;
	source->base.cancel = data_source_cancel;
	source->base.finished = data_source_finished;
	source->base.actions_set = true;
	source->base.dnd_actions = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
		WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
	source->wm = wm;
	source->window = client_message->data.data32[0];
	source->version = client_message->data.data32[1] >> 24;

	if (client_message->data.data32[1] & 1) {
		reply = xcb_get_property_reply(
			wm->conn,
			xcb_get_property(wm->conn, 0, source->window,
					 wm->atom.xdnd_type_list, XCB_ATOM_ANY,
					 0, 2048),
			NULL);
		if (reply && reply->format == 32) {
			types = xcb_get_property_value(reply);
			length = reply->value_len;
		} else {
			types = NULL;
			length = 0;
		}
	} else {
		types = &client_message->data.data32[2];
		length = 3;
	}

	wl_array_init(&source->base.mime_types);
	for (i = 0; i < length; i++) {
		if (types[i] == XCB_ATOM_NONE)
			continue;

		name = get_atom_name(wm->conn, types[i]);
		if (types[i] == wm->atom.utf8_string ||
		    types[i] == wm->atom.text_plain_utf8 ||
		    types[i] == wm->atom.text_plain) {
			if (source->text_target)
				continue;

			source->text_target = types[i];
			p = wl_array_add(&source->base.mime_types, sizeof *p);
			if (p)
				*p = strdup("text/plain;charset=utf-8");
		} else if (strchr(name, '/')) {
			p = wl_array_add(&source->base.mime_types, sizeof *p);
			if (p)
				*p = strdup(name);
		}
	}

	free(reply);
	wm->x_source = source;
	wm->x_drag = weston_seat_start_external_drag(seat, &source->base);
}

/* While an X client holds the pointer in a drag, X only sees X windows: a
 * window over everything stands in for the native ones under the pointer. */
static void
set_proxy_mapped(struct weston_wm *wm, bool mapped)
{
	uint32_t above = XCB_STACK_MODE_ABOVE;

	if (wm->dnd_mapped == mapped)
		return;
	wm->dnd_mapped = mapped;
	if (mapped) {
		xcb_configure_window(wm->conn, wm->dnd_window,
				     XCB_CONFIG_WINDOW_STACK_MODE, &above);
		xcb_map_window(wm->conn, wm->dnd_window);
	} else {
		xcb_unmap_window(wm->conn, wm->dnd_window);
	}
	xcb_flush(wm->conn);
}

static void
handle_pointer_motion(struct wl_listener *listener, void *data)
{
	struct weston_wm *wm =
		container_of(listener, struct weston_wm, dnd_motion_listener);
	struct weston_pointer *pointer = data;
	struct weston_view *view;

	if (wm->dnd_owner == XCB_WINDOW_NONE || pointer->button_count == 0 ||
	    !pointer->focus ||
	    !polo_wm_window_for_surface(pointer->focus->surface)) {
		set_proxy_mapped(wm, false);
		return;
	}
	view = weston_compositor_pick_view(wm->server->compositor, pointer->pos);
	set_proxy_mapped(wm, !view ||
			 !polo_wm_window_for_surface(view->surface));
}

static void
handle_position(struct weston_wm *wm)
{
	struct weston_seat *seat = weston_wm_pick_seat(wm);
	struct weston_pointer *pointer =
		seat ? weston_seat_get_pointer(seat) : NULL;

	if (!wm->x_source)
		return;
	if (wm->x_drag && pointer)
		weston_external_drag_update(wm->x_drag, seat, pointer->pos, 0);
	send_status(wm);
}

static void
handle_drop(struct weston_wm *wm)
{
	struct dnd_data_source *source = wm->x_source;

	if (!source)
		return;
	if (wm->x_drag && weston_external_drag_drop(wm->x_drag)) {
		source->dropped = true;
		weston_external_drag_end(wm->x_drag);
		wm->x_drag = NULL;
	} else {
		send_message(wm, source->window, wm->atom.xdnd_finished,
			     wm->dnd_window, 0, XCB_ATOM_NONE, 0, 0);
		end_x_drag(wm);
	}
	set_proxy_mapped(wm, false);
}

/* ---- Native source, X target ------------------------------------------- */

static uint32_t
x_action(struct weston_wm *wm, uint32_t actions)
{
	return actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY ||
			!(actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) ?
		wm->atom.xdnd_action_copy : intern(wm, "XdndActionMove");
}

static void
bridge_send_position(struct weston_wm *wm)
{
	send_message(wm, wm->bridge_target, intern(wm, "XdndPosition"),
		     wm->selection_window, 0,
		     (wm->bridge_x << 16) | (wm->bridge_y & 0xffff),
		     XCB_TIME_CURRENT_TIME,
		     x_action(wm, wm->bridge_source->dnd_actions));
	wm->bridge_waiting = true;
	wm->bridge_moved = false;
}

static void
bridge_motion(void *data, int x, int y)
{
	struct weston_wm *wm = data;

	wm->bridge_x = x;
	wm->bridge_y = y;
	if (!wm->bridge_target)
		return;
	if (wm->bridge_waiting)
		wm->bridge_moved = true;
	else
		bridge_send_position(wm);
}

static void
bridge_leave(void *data)
{
	struct weston_wm *wm = data;

	if (wm->bridge_dropped)
		return;
	if (wm->bridge_target)
		send_message(wm, wm->bridge_target, wm->atom.xdnd_leave,
			     wm->selection_window, 0, 0, 0, 0);
	wm->bridge_target = XCB_WINDOW_NONE;
	wm->bridge_waiting = wm->bridge_moved = wm->bridge_accepted = false;
	if (wm->bridge_source)
		weston_data_source_bridge_status(wm->bridge_source, NULL, 0);
}

static void
bridge_clear_source(struct weston_wm *wm)
{
	if (!wm->bridge_source)
		return;
	wl_list_remove(&wm->bridge_source_listener.link);
	wm->bridge_source = NULL;
	wm->bridge_target = XCB_WINDOW_NONE;
	wm->bridge_dropped = false;
	wm->bridge_waiting = wm->bridge_moved = wm->bridge_accepted = false;
}

static void
bridge_source_destroyed(struct wl_listener *listener, void *data)
{
	struct weston_wm *wm =
		container_of(listener, struct weston_wm, bridge_source_listener);

	if (wm->bridge_target && !wm->bridge_dropped)
		send_message(wm, wm->bridge_target, wm->atom.xdnd_leave,
			     wm->selection_window, 0, 0, 0, 0);
	bridge_clear_source(wm);
}

static bool
bridge_wants_surface(void *data, struct weston_surface *surface)
{
	struct weston_wm_window *window = polo_wm_window_for_surface(surface);
	struct polo_wm_window_info info;

	if (!window)
		return false;
	polo_wm_window_get_info(window, &info);
	return !info.override_redirect;
}

static void
bridge_enter(void *data, struct weston_data_source *source,
	     struct weston_surface *surface, int x, int y)
{
	struct weston_wm *wm = data;
	struct polo_wm_window_info info;
	xcb_atom_t types[64];
	uint32_t n = 0, version = 0;
	const char **mime;
	xcb_get_property_reply_t *reply;

	polo_wm_window_get_info(polo_wm_window_for_surface(surface), &info);
	if (wm->bridge_source != source) {
		bridge_clear_source(wm);
		wm->bridge_source = source;
		wm->bridge_source_listener.notify = bridge_source_destroyed;
		wl_signal_add(&source->destroy_signal, &wm->bridge_source_listener);
		xcb_set_selection_owner(wm->conn, wm->selection_window,
					wm->atom.xdnd_selection,
					XCB_TIME_CURRENT_TIME);
	}
	wm->bridge_x = x;
	wm->bridge_y = y;

	reply = xcb_get_property_reply(
		wm->conn,
		xcb_get_property(wm->conn, 0, info.id, wm->atom.xdnd_aware,
				 XCB_ATOM_ATOM, 0, 1),
		NULL);
	if (reply && reply->format == 32 && reply->value_len == 1)
		version = *(uint32_t *)xcb_get_property_value(reply);
	free(reply);
	if (version < 3)
		return;

	wl_array_for_each(mime, &source->mime_types) {
		if (n >= ARRAY_LENGTH(types) - 1)
			break;
		if (strcmp(*mime, "text/plain;charset=utf-8") == 0)
			types[n++] = wm->atom.utf8_string;
		types[n++] = intern(wm, *mime);
	}
	xcb_change_property(wm->conn, XCB_PROP_MODE_REPLACE, wm->selection_window,
			    wm->atom.xdnd_type_list, XCB_ATOM_ATOM, 32, n, types);

	wm->bridge_target = info.id;
	wm->bridge_version = version > 5 ? 5 : version;
	send_message(wm, info.id, wm->atom.xdnd_enter, wm->selection_window,
		     ((uint32_t)wm->bridge_version << 24) | (n > 3 ? 1 : 0),
		     n > 0 ? types[0] : 0, n > 1 ? types[1] : 0,
		     n > 2 ? types[2] : 0);
	bridge_send_position(wm);
}

static bool
bridge_drop(void *data)
{
	struct weston_wm *wm = data;

	if (!wm->bridge_target || !wm->bridge_accepted)
		return false;
	send_message(wm, wm->bridge_target, wm->atom.xdnd_drop,
		     wm->selection_window, 0, XCB_TIME_CURRENT_TIME, 0, 0);
	wm->bridge_dropped = true;
	return true;
}

static const struct weston_drag_bridge drag_bridge = {
	bridge_wants_surface,
	bridge_enter,
	bridge_motion,
	bridge_leave,
	bridge_drop,
};

static void
handle_status(struct weston_wm *wm, xcb_client_message_event_t *message)
{
	struct weston_data_source *source = wm->bridge_source;
	const char **mime;

	if (!source || message->data.data32[0] != wm->bridge_target ||
	    wm->bridge_dropped)
		return;
	wm->bridge_waiting = false;
	wm->bridge_accepted = message->data.data32[1] & 1;
	mime = source->mime_types.data;
	weston_data_source_bridge_status(
		source,
		wm->bridge_accepted && source->mime_types.size ? *mime : NULL,
		!wm->bridge_accepted ? 0 :
		message->data.data32[4] == wm->atom.xdnd_action_copy ||
				!(source->dnd_actions &
				  WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) ?
			WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY :
			WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);
	if (wm->bridge_moved)
		bridge_send_position(wm);
}

static void
handle_finished(struct weston_wm *wm)
{
	struct weston_data_source *source = wm->bridge_source;

	if (!source || !wm->bridge_dropped)
		return;
	bridge_clear_source(wm);
	weston_data_source_bridge_finished(source);
}

int
weston_wm_handle_dnd_event(struct weston_wm *wm,
			   xcb_generic_event_t *event)
{
	xcb_xfixes_selection_notify_event_t *xfixes_selection_notify =
		(xcb_xfixes_selection_notify_event_t *) event;
	xcb_client_message_event_t *client_message =
		(xcb_client_message_event_t *) event;

	switch (event->response_type - wm->xfixes->first_event) {
	case XCB_XFIXES_SELECTION_NOTIFY:
		if (xfixes_selection_notify->selection != wm->atom.xdnd_selection)
			return 0;

		wm->dnd_owner = xfixes_selection_notify->owner == wm->selection_window ?
			XCB_WINDOW_NONE : xfixes_selection_notify->owner;
		if (wm->dnd_owner == XCB_WINDOW_NONE)
			set_proxy_mapped(wm, false);
		return 1;
	}

	switch (EVENT_TYPE(event)) {
	case XCB_CLIENT_MESSAGE:
		if (client_message->window == wm->dnd_window) {
			if (client_message->type == wm->atom.xdnd_enter) {
				handle_enter(wm, client_message);
				return 1;
			} else if (client_message->type == intern(wm, "XdndPosition")) {
				handle_position(wm);
				return 1;
			} else if (client_message->type == wm->atom.xdnd_leave) {
				end_x_drag(wm);
				return 1;
			} else if (client_message->type == wm->atom.xdnd_drop) {
				handle_drop(wm);
				return 1;
			}
		} else if (client_message->window == wm->selection_window) {
			if (client_message->type == wm->atom.xdnd_status) {
				handle_status(wm, client_message);
				return 1;
			} else if (client_message->type == wm->atom.xdnd_finished) {
				handle_finished(wm);
				return 1;
			}
		}
		return 0;
	}

	return 0;
}

void
weston_wm_dnd_init(struct weston_wm *wm)
{
	uint32_t values[2], version = 5, mask;
	struct weston_seat *seat = weston_wm_pick_seat(wm);
	struct weston_pointer *pointer =
		seat ? weston_seat_get_pointer(seat) : NULL;

	mask =
		XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
	xcb_xfixes_select_selection_input(wm->conn, wm->selection_window,
					  wm->atom.xdnd_selection, mask);

	wm->dnd_window = xcb_generate_id(wm->conn);
	values[0] = 1;
	values[1] =
		XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
		XCB_EVENT_MASK_PROPERTY_CHANGE;

	xcb_create_window(wm->conn,
			  XCB_COPY_FROM_PARENT,
			  wm->dnd_window,
			  wm->screen->root,
			  0, 0,
			  8192, 8192,
			  0,
			  XCB_WINDOW_CLASS_INPUT_ONLY,
			  wm->screen->root_visual,
			  XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, values);
	xcb_change_property(wm->conn,
			    XCB_PROP_MODE_REPLACE,
			    wm->dnd_window,
			    wm->atom.xdnd_aware,
			    XCB_ATOM_ATOM,
			    32, /* format */
			    1, &version);

	wl_list_init(&wm->dnd_motion_listener.link);
	wl_list_init(&wm->bridge_source_listener.link);
	wm->dnd_motion_listener.notify = handle_pointer_motion;
	if (pointer)
		wl_signal_add(&pointer->motion_signal, &wm->dnd_motion_listener);
	wm->server->compositor->drag_bridge = &drag_bridge;
	wm->server->compositor->drag_bridge_data = wm;
}

void
weston_wm_dnd_fini(struct weston_wm *wm)
{
	wm->server->compositor->drag_bridge = NULL;
	wm->server->compositor->drag_bridge_data = NULL;
	wl_list_remove(&wm->dnd_motion_listener.link);
	bridge_clear_source(wm);
	end_x_drag(wm);
}
