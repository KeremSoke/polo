/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * x-send WINDOW TYPE D0 D1 D2 D3 D4: an EWMH client message to the root window,
 * as a pager sends it. WINDOW 0 means the root window. A D that does not start
 * with a digit is an atom name. TYPE SET_INPUT_FOCUS instead has this connection
 * call SetInputFocus on WINDOW, 0 meaning None.
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <xcb/xcb.h>

static xcb_atom_t
atom(xcb_connection_t *conn, const char *name)
{
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
		conn, xcb_intern_atom(conn, 0, strlen(name), name), NULL);
	xcb_atom_t result = reply ? reply->atom : XCB_ATOM_NONE;

	free(reply);
	return result;
}

int
main(int argc, char **argv)
{
	xcb_connection_t *conn = xcb_connect(NULL, NULL);
	xcb_client_message_event_t message;

	if (argc < 8 || xcb_connection_has_error(conn))
		return 2;
	xcb_screen_t *screen =
		xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

	if (!strcmp(argv[2], "SET_INPUT_FOCUS")) {
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT,
				    strtoul(argv[1], NULL, 0),
				    XCB_TIME_CURRENT_TIME);
		free(xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn),
					       NULL));
		xcb_disconnect(conn);
		return 0;
	}

	memset(&message, 0, sizeof message);
	message.response_type = XCB_CLIENT_MESSAGE;
	message.format = 32;
	message.window = strtoul(argv[1], NULL, 0);
	if (message.window == 0)
		message.window = screen->root;
	message.type = atom(conn, argv[2]);
	for (int i = 0; i < 5; i++)
		message.data.data32[i] = isdigit((unsigned char)argv[3 + i][0])
			? strtoul(argv[3 + i], NULL, 0)
			: atom(conn, argv[3 + i]);
	xcb_send_event(conn, 0, screen->root,
		       XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
			       XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
		       (char *)&message);
	free(xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn), NULL));
	xcb_disconnect(conn);
	return 0;
}
