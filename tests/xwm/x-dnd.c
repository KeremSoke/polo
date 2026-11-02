/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An X client that does XDND, data being the alphabet repeated, 1000 bytes of
 * UTF8_STRING. It maps a white window at a fixed place, the source at the left
 * and the target at the right, and logs to LOG:
 *   x-dnd source LOG: drags on button press. "at X Y", "enter", "status N",
 *                     "drop", "finished N".
 *   x-dnd target LOG: "at X Y", "enter", "position", "leave", "drop N" with
 *                     the bytes received. X_DND_REFUSE refuses everything.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

#define SIZE 1000

static xcb_connection_t *conn;
static xcb_screen_t *screen;
static xcb_window_t window;
static FILE *out;

static xcb_atom_t
atom(const char *name)
{
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
		conn, xcb_intern_atom(conn, 0, strlen(name), name), NULL);
	xcb_atom_t result = reply ? reply->atom : XCB_ATOM_NONE;

	free(reply);
	return result;
}

static void
send_message(xcb_window_t to, const char *type, uint32_t d0, uint32_t d1,
	     uint32_t d2, uint32_t d3, uint32_t d4)
{
	xcb_client_message_event_t message;

	memset(&message, 0, sizeof message);
	message.response_type = XCB_CLIENT_MESSAGE;
	message.format = 32;
	message.window = to;
	message.type = atom(type);
	message.data.data32[0] = d0;
	message.data.data32[1] = d1;
	message.data.data32[2] = d2;
	message.data.data32[3] = d3;
	message.data.data32[4] = d4;
	xcb_send_event(conn, 0, to, XCB_EVENT_MASK_NO_EVENT, (char *)&message);
	xcb_flush(conn);
}

static void
report_position(void)
{
	xcb_translate_coordinates_reply_t *at = xcb_translate_coordinates_reply(
		conn, xcb_translate_coordinates(conn, window, screen->root, 0, 0),
		NULL);

	if (at)
		fprintf(out, "at %d %d\n", at->dst_x, at->dst_y);
	fflush(out);
	free(at);
}

/* The topmost mapped XdndAware window under the pointer, descending through
 * unaware windows as a toolkit's source does. */
static xcb_window_t
aware_window_in(xcb_window_t parent, int x, int y)
{
	xcb_query_tree_reply_t *tree =
		xcb_query_tree_reply(conn, xcb_query_tree(conn, parent), NULL);
	xcb_atom_t aware = atom("XdndAware");
	xcb_window_t found = XCB_NONE;

	if (!tree)
		return XCB_NONE;
	xcb_window_t *children = xcb_query_tree_children(tree);

	for (int i = xcb_query_tree_children_length(tree) - 1; i >= 0; i--) {
		xcb_get_window_attributes_reply_t *attributes =
			xcb_get_window_attributes_reply(
				conn,
				xcb_get_window_attributes(conn, children[i]), NULL);
		xcb_get_geometry_reply_t *g = xcb_get_geometry_reply(
			conn, xcb_get_geometry(conn, children[i]), NULL);
		int inside = attributes && g &&
			attributes->map_state == XCB_MAP_STATE_VIEWABLE &&
			x >= g->x && y >= g->y && x < g->x + g->width &&
			y < g->y + g->height;
		int child_x = g ? x - g->x : 0, child_y = g ? y - g->y : 0;

		free(attributes);
		free(g);
		if (!inside)
			continue;
		xcb_get_property_reply_t *p = xcb_get_property_reply(
			conn,
			xcb_get_property(conn, 0, children[i], aware, XCB_ATOM_ATOM,
					 0, 1),
			NULL);

		found = p && p->value_len == 1 ?
			children[i] : aware_window_in(children[i], child_x, child_y);
		free(p);
		break;
	}
	free(tree);
	return found;
}

static xcb_window_t
aware_window_at(int x, int y)
{
	return aware_window_in(screen->root, x, y);
}

static void
serve(xcb_selection_request_event_t *request)
{
	xcb_selection_notify_event_t notify = { 0 };
	char data[SIZE];

	for (int i = 0; i < SIZE; i++)
		data[i] = 'a' + i % 26;
	notify.response_type = XCB_SELECTION_NOTIFY;
	notify.time = request->time;
	notify.requestor = request->requestor;
	notify.selection = request->selection;
	notify.target = request->target;
	notify.property = XCB_ATOM_NONE;
	if (request->target == atom("UTF8_STRING")) {
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, request->requestor,
				    request->property, request->target, 8, SIZE,
				    data);
		notify.property = request->property;
	}
	xcb_send_event(conn, 0, request->requestor, XCB_EVENT_MASK_NO_EVENT,
		       (char *)&notify);
	xcb_flush(conn);
}

static void
source_loop(void)
{
	xcb_window_t target = XCB_NONE;
	int accepted = 0, dragging = 0, waiting = 0, moved = 0, x = 0, y = 0;
	xcb_generic_event_t *event;

	while ((event = xcb_wait_for_event(conn))) {
		switch (event->response_type & ~0x80) {
		case XCB_EXPOSE: {
			xcb_rectangle_t all = { 0, 0, 2000, 2000 };
			xcb_gcontext_t gc = xcb_generate_id(conn);

			xcb_create_gc(conn, gc, window, XCB_GC_FOREGROUND,
				      &screen->white_pixel);
			xcb_poly_fill_rectangle(conn, window, gc, 1, &all);
			xcb_free_gc(conn, gc);
			xcb_flush(conn);
			break;
		}
		case XCB_CONFIGURE_NOTIFY:
			report_position();
			break;
		case XCB_BUTTON_PRESS:
			xcb_set_selection_owner(conn, window, atom("XdndSelection"),
						XCB_TIME_CURRENT_TIME);
			xcb_flush(conn);
			dragging = 1;
			break;
		case XCB_MOTION_NOTIFY: {
			xcb_motion_notify_event_t *m = (void *)event;

			if (!dragging)
				break;
			x = m->root_x;
			y = m->root_y;
			xcb_window_t under = aware_window_at(x, y);

			if (under == window)
				under = XCB_NONE;
			if (under != target) {
				if (target)
					send_message(target, "XdndLeave", window,
						     0, 0, 0, 0);
				target = under;
				accepted = waiting = moved = 0;
				if (target) {
					fprintf(out, "enter\n");
					fflush(out);
					send_message(target, "XdndEnter", window,
						     5u << 24, atom("UTF8_STRING"),
						     0, 0);
				}
			}
			if (!target)
				break;
			if (waiting) {
				moved = 1;
				break;
			}
			send_message(target, "XdndPosition", window, 0,
				     (x << 16) | y, 0, atom("XdndActionCopy"));
			waiting = 1;
			break;
		}
		case XCB_BUTTON_RELEASE:
			dragging = 0;
			if (target && accepted) {
				fprintf(out, "drop\n");
				fflush(out);
				send_message(target, "XdndDrop", window, 0, 0, 0, 0);
			} else if (target) {
				send_message(target, "XdndLeave", window, 0, 0, 0, 0);
			}
			target = XCB_NONE;
			break;
		case XCB_SELECTION_REQUEST:
			serve((void *)event);
			break;
		case XCB_CLIENT_MESSAGE: {
			xcb_client_message_event_t *m = (void *)event;

			if (m->type == atom("XdndStatus")) {
				accepted = m->data.data32[1] & 1;
				waiting = 0;
				fprintf(out, "status %d\n", accepted);
				fflush(out);
				if (moved && target) {
					moved = 0;
					waiting = 1;
					send_message(target, "XdndPosition", window, 0,
						     (x << 16) | y, 0,
						     atom("XdndActionCopy"));
				}
			} else if (m->type == atom("XdndFinished")) {
				fprintf(out, "finished %d\n", m->data.data32[1] & 1);
				fflush(out);
			}
			break;
		}
		}
		free(event);
	}
}

static void
target_loop(void)
{
	xcb_window_t source = XCB_NONE;
	xcb_atom_t property = atom("X_DND");
	xcb_generic_event_t *event;

	while ((event = xcb_wait_for_event(conn))) {
		switch (event->response_type & ~0x80) {
		case XCB_EXPOSE: {
			xcb_rectangle_t all = { 0, 0, 2000, 2000 };
			xcb_gcontext_t gc = xcb_generate_id(conn);

			xcb_create_gc(conn, gc, window, XCB_GC_FOREGROUND,
				      &screen->white_pixel);
			xcb_poly_fill_rectangle(conn, window, gc, 1, &all);
			xcb_free_gc(conn, gc);
			xcb_flush(conn);
			break;
		}
		case XCB_CONFIGURE_NOTIFY:
			report_position();
			break;
		case XCB_CLIENT_MESSAGE: {
			xcb_client_message_event_t *m = (void *)event;

			if (m->type == atom("XdndEnter")) {
				source = m->data.data32[0];
				fprintf(out, "enter\n");
			} else if (m->type == atom("XdndPosition")) {
				fprintf(out, "position\n");
				send_message(source, "XdndStatus", window,
					     getenv("X_DND_REFUSE") ? 0 : 1, 0, 0,
					     atom("XdndActionCopy"));
			} else if (m->type == atom("XdndLeave")) {
				fprintf(out, "leave\n");
			} else if (m->type == atom("XdndDrop")) {
				xcb_convert_selection(conn, window,
						      atom("XdndSelection"),
						      atom("UTF8_STRING"), property,
						      XCB_TIME_CURRENT_TIME);
				xcb_flush(conn);
			}
			fflush(out);
			break;
		}
		case XCB_SELECTION_NOTIFY: {
			xcb_get_property_reply_t *reply = xcb_get_property_reply(
				conn,
				xcb_get_property(conn, 1, window, property,
						 XCB_ATOM_ANY, 0, 0x1fffffff),
				NULL);
			int n = reply ? xcb_get_property_value_length(reply) : 0;
			const char *data = reply ? xcb_get_property_value(reply) : NULL;

			for (int i = 0; i < n; i++)
				if (data[i] != 'a' + i % 26)
					n = -1;
			fprintf(out, "drop %d\n", n);
			fflush(out);
			free(reply);
			send_message(source, "XdndFinished", window, 1,
				     atom("XdndActionCopy"), 0, 0);
			break;
		}
		}
		free(event);
	}
}

int
main(int argc, char **argv)
{
	if (argc < 3)
		return 2;
	out = fopen(argv[2], "a");
	conn = xcb_connect(NULL, NULL);
	if (!out || xcb_connection_has_error(conn))
		return 2;
	screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	uint32_t mask = XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION;
	uint32_t version = 5;

	window = xcb_generate_id(conn);
	int source = strcmp(argv[1], "source") == 0;
	int left = source ? 20 : 690;
	uint32_t hints[18] = { 1, (uint32_t)left, 20, 300, 200 };

	xcb_create_window(conn, XCB_COPY_FROM_PARENT, window, screen->root, left, 20,
			  300, 200, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
			  screen->root_visual, XCB_CW_EVENT_MASK, &mask);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window, atom("XdndAware"),
			    XCB_ATOM_ATOM, 32, 1, &version);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
			    XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32,
			    18, hints);
	xcb_map_window(conn, window);
	xcb_flush(conn);
	if (source)
		source_loop();
	else
		target_loop();
	return 0;
}
