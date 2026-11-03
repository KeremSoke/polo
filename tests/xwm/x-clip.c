/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An X clipboard client; data is the alphabet repeated.
 *   x-clip copy TARGET SIZE: own CLIPBOARD until killed, with INCR past 128 KiB.
 *   x-clip paste TARGET SIZE: exit 0 on SIZE correct bytes, 3 when refused.
 *   x-clip targets TARGET: exit 0 if TARGETS names TARGET.
 *   x-clip loop TARGET: send CLIPBOARD's owner a request naming itself as the
 *   requestor.
 * X_CLIP_SELECTION names another selection than CLIPBOARD.
 * X_CLIP_CHILD makes the requestor a child window, which the WM does not watch.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

#define INCR_LIMIT (128 * 1024)
#define CHUNK (64 * 1024)

static xcb_connection_t *conn;
static xcb_window_t window;

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
fill(char *data, size_t offset, size_t n)
{
	for (size_t i = 0; i < n; i++)
		data[i] = 'a' + (offset + i) % 26;
}

static int
copy(const char *target_name, size_t size)
{
	xcb_atom_t target = atom(target_name), targets = atom("TARGETS");
	xcb_atom_t incr = atom("INCR");
	xcb_window_t requestor = XCB_NONE;
	xcb_atom_t property = XCB_NONE;
	size_t sent = 0;
	int ending = 0;
	static char chunk[CHUNK];
	xcb_generic_event_t *event;

	xcb_set_selection_owner(conn, window, atom(getenv("X_CLIP_SELECTION") ? getenv("X_CLIP_SELECTION") : "CLIPBOARD"),
				XCB_TIME_CURRENT_TIME);
	xcb_flush(conn);
	while ((event = xcb_wait_for_event(conn))) {
		switch (event->response_type & ~0x80) {
		case XCB_SELECTION_REQUEST: {
			xcb_selection_request_event_t *request = (void *)event;
			xcb_selection_notify_event_t notify = { 0 };

			notify.response_type = XCB_SELECTION_NOTIFY;
			notify.time = request->time;
			notify.requestor = request->requestor;
			notify.selection = request->selection;
			notify.target = request->target;
			notify.property = request->property;
			if (request->target == targets) {
				xcb_atom_t list[2] = { targets, target };

				xcb_change_property(conn, XCB_PROP_MODE_REPLACE,
						    request->requestor,
						    request->property,
						    XCB_ATOM_ATOM, 32, 2, list);
			} else if (request->target == target && size > INCR_LIMIT) {
				uint32_t hint = size;
				uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;

				xcb_change_window_attributes(
					conn, request->requestor,
					XCB_CW_EVENT_MASK, &mask);
				xcb_change_property(conn, XCB_PROP_MODE_REPLACE,
						    request->requestor,
						    request->property, incr, 32,
						    1, &hint);
				requestor = request->requestor;
				property = request->property;
				sent = 0;
				ending = 0;
			} else if (request->target == target) {
				char *data = malloc(size + 1);

				fill(data, 0, size);
				xcb_change_property(conn, XCB_PROP_MODE_REPLACE,
						    request->requestor,
						    request->property, target,
						    8, size, data);
				free(data);
			} else {
				notify.property = XCB_ATOM_NONE;
			}
			xcb_send_event(conn, 0, request->requestor,
				       XCB_EVENT_MASK_NO_EVENT, (char *)&notify);
			xcb_flush(conn);
			break;
		}
		case XCB_PROPERTY_NOTIFY: {
			xcb_property_notify_event_t *p = (void *)event;

			if (p->window != requestor || p->atom != property ||
			    p->state != XCB_PROPERTY_DELETE || ending)
				break;
			size_t n = size - sent < CHUNK ? size - sent : CHUNK;

			/* Long enough for a paste to land during another read. */
			usleep(10000);
			fill(chunk, sent, n);
			xcb_change_property(conn, XCB_PROP_MODE_REPLACE, requestor,
					    property, target, 8, n, chunk);
			xcb_flush(conn);
			sent += n;
			ending = n == 0;
			break;
		}
		case XCB_SELECTION_CLEAR:
			return 0;
		}
		free(event);
	}
	return 1;
}

static xcb_generic_event_t *
wait_for(int type)
{
	xcb_generic_event_t *event;

	while ((event = xcb_wait_for_event(conn))) {
		if ((event->response_type & ~0x80) == type)
			return event;
		free(event);
	}
	exit(1);
}

static xcb_get_property_reply_t *
take_property(xcb_atom_t property)
{
	return xcb_get_property_reply(
		conn,
		xcb_get_property(conn, 1, window, property, XCB_ATOM_ANY, 0,
				 0x1fffffff),
		NULL);
}

static int
check(const char *data, size_t offset, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (data[i] != (char)('a' + (offset + i) % 26))
			return 0;
	return 1;
}

static int
paste(const char *target_name, size_t size)
{
	xcb_atom_t property = atom("X_CLIP");
	xcb_atom_t incr = atom("INCR");

	xcb_convert_selection(conn, window, atom(getenv("X_CLIP_SELECTION") ? getenv("X_CLIP_SELECTION") : "CLIPBOARD"), atom(target_name),
			      property, XCB_TIME_CURRENT_TIME);
	xcb_flush(conn);
	xcb_selection_notify_event_t *notify =
		(void *)wait_for(XCB_SELECTION_NOTIFY);

	if (notify->property == XCB_ATOM_NONE)
		return 3;
	xcb_get_property_reply_t *reply = take_property(property);

	if (!reply)
		return 1;
	if (reply->type != incr) {
		size_t n = xcb_get_property_value_length(reply);

		return n == size && check(xcb_get_property_value(reply), 0, n) ? 0 : 1;
	}
	free(reply);
	xcb_flush(conn);
	size_t got = 0;

	for (;;) {
		xcb_property_notify_event_t *p =
			(void *)wait_for(XCB_PROPERTY_NOTIFY);

		if (p->atom != property || p->state != XCB_PROPERTY_NEW_VALUE)
			continue;
		reply = take_property(property);
		xcb_flush(conn);
		size_t n = reply ? xcb_get_property_value_length(reply) : 0;

		if (n == 0)
			return got == size ? 0 : 1;
		if (!check(xcb_get_property_value(reply), got, n))
			return 1;
		got += n;
		free(reply);
	}
}

static int
targets(const char *target_name)
{
	xcb_atom_t property = atom("X_CLIP");
	xcb_atom_t wanted = atom(target_name);

	xcb_convert_selection(conn, window, atom(getenv("X_CLIP_SELECTION") ? getenv("X_CLIP_SELECTION") : "CLIPBOARD"), atom("TARGETS"),
			      property, XCB_TIME_CURRENT_TIME);
	xcb_flush(conn);
	xcb_selection_notify_event_t *notify =
		(void *)wait_for(XCB_SELECTION_NOTIFY);

	if (notify->property == XCB_ATOM_NONE)
		return 3;
	xcb_get_property_reply_t *reply = take_property(property);

	if (!reply)
		return 1;
	xcb_atom_t *list = xcb_get_property_value(reply);

	for (uint32_t i = 0; i < reply->value_len; i++)
		if (list[i] == wanted)
			return 0;
	return 1;
}

static int
loop(const char *target)
{
	xcb_atom_t clipboard = atom("CLIPBOARD");
	xcb_get_selection_owner_reply_t *reply = xcb_get_selection_owner_reply(
		conn, xcb_get_selection_owner(conn, clipboard), NULL);
	xcb_window_t owner = reply ? reply->owner : XCB_WINDOW_NONE;
	xcb_selection_request_event_t request = {
		.response_type = XCB_SELECTION_REQUEST,
		.time = XCB_CURRENT_TIME,
		.owner = owner,
		.requestor = owner,
		.selection = clipboard,
		.target = atom(target),
		.property = atom("POLO_LOOP"),
	};

	free(reply);
	if (owner == XCB_WINDOW_NONE)
		return 1;
	xcb_send_event(conn, 0, owner, 0, (const char *)&request);
	xcb_flush(conn);
	return 0;
}

int
main(int argc, char **argv)
{
	uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;

	if (argc < 3)
		return 2;
	conn = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(conn))
		return 2;
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

	window = xcb_generate_id(conn);
	xcb_create_window(conn, XCB_COPY_FROM_PARENT, window, screen->root, -10,
			  -10, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
			  screen->root_visual, XCB_CW_EVENT_MASK, &mask);
	if (getenv("X_CLIP_CHILD")) {
		xcb_window_t child = xcb_generate_id(conn);

		xcb_create_window(conn, XCB_COPY_FROM_PARENT, child, window, 0, 0,
				  1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
				  screen->root_visual, XCB_CW_EVENT_MASK, &mask);
		window = child;
	}
	if (strcmp(argv[1], "copy") == 0)
		return copy(argv[2], strtoul(argv[3], NULL, 0));
	alarm(10);
	if (strcmp(argv[1], "paste") == 0)
		return paste(argv[2], strtoul(argv[3], NULL, 0));
	if (strcmp(argv[1], "loop") == 0)
		return loop(argv[2]);
	return targets(argv[2]);
}
