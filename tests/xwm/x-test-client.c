/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An X client for tests/vapi/xwm.vala. Maps a window with known ICCCM and EWMH
 * data and writes what the window manager does to it to the file in argv[1],
 * one line per event: "screen WxH" first, then "message NAME", "prop ATOM
 * VALUE...", "focus", "key N", "button N", "size WxH" or "at X Y" (the root
 * position the WM announced). X_TEST_DUMP_ROOT reports the WM check data,
 * X_TEST_LAYOUT sets a 2x2 _NET_DESKTOP_LAYOUT, X_TEST_NO_HINTS leaves
 * WM_NORMAL_HINTS and WM_HINTS out, X_TEST_ICONIC maps it iconic,
 * X_TEST_ICONIFY iconifies it on a key, X_TEST_NO_PONG ignores pings,
 * X_TEST_TYPE names the window type, X_TEST_CONVERT_ON_KEY asks for that
 * CLIPBOARD target on a key and never reads it, X_TEST_FOCUS_MODEL is passive,
 * locally, globally or none (ICCCM 4.1.7), X_TEST_TAKE_FOCUS_RESPOND takes
 * focus on WM_TAKE_FOCUS, "blur" is FocusOut, X_TEST_OVERRIDE makes it
 * override-redirect, X_TEST_MOTIF sets _MOTIF_WM_HINTS to
 * "FLAGS,FUNCTIONS,DECORATIONS" and X_TEST_MOTIF_ON_KEY does so on a key.
 * X_TEST_WM_PID sets _NET_WM_PID. X_TEST_POSITION="X,Y" moves the window there
 * before mapping it and marks the position as the user's, as Xt does.
 * X_TEST_GRAB_ON_KEY names a keycode: that key takes an active keyboard grab,
 * after X_TEST_GRAB_DELAY seconds, and logs "grabbed"; the next press of it
 * ungrabs.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

static xcb_connection_t *conn;
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
print_atom_name(xcb_atom_t value)
{
	xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(
		conn, xcb_get_atom_name(conn, value), NULL);

	if (reply) {
		fprintf(out, "%.*s", xcb_get_atom_name_name_length(reply),
			xcb_get_atom_name_name(reply));
		free(reply);
	}
}

static void
report_property(xcb_window_t window, xcb_atom_t property)
{
	xcb_get_property_reply_t *reply = xcb_get_property_reply(
		conn,
		xcb_get_property(conn, 0, window, property, XCB_ATOM_ANY, 0,
				 256),
		NULL);

	if (!reply)
		return;
	fprintf(out, "prop ");
	print_atom_name(property);
	if (reply->format == 32) {
		uint32_t *values = xcb_get_property_value(reply);

		for (uint32_t i = 0; i < reply->value_len; i++) {
			fputc(' ', out);
			if (reply->type == XCB_ATOM_ATOM)
				print_atom_name(values[i]);
			else
				fprintf(out, "%u", values[i]);
		}
	} else if (reply->format == 8) {
		char *text = xcb_get_property_value(reply);

		fputc(' ', out);
		for (uint32_t i = 0; i < reply->value_len; i++)
			fputc(text[i] ? text[i] : '|', out);
	}
	fputc('\n', out);
	fflush(out);
	free(reply);
}

static void
set_motif(xcb_window_t window, const char *spec)
{
	uint32_t hints[5] = { 0 };

	sscanf(spec, "%u,%u,%u", &hints[0], &hints[1], &hints[2]);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
			    atom("_MOTIF_WM_HINTS"), atom("_MOTIF_WM_HINTS"),
			    32, 5, hints);
}

int
main(int argc, char **argv)
{
	xcb_screen_t *screen;
	xcb_window_t window;
	xcb_gcontext_t gc;
	uint32_t mask[1] = { XCB_EVENT_MASK_PROPERTY_CHANGE |
			     XCB_EVENT_MASK_STRUCTURE_NOTIFY |
			     XCB_EVENT_MASK_FOCUS_CHANGE |
			     XCB_EVENT_MASK_KEY_PRESS |
			     XCB_EVENT_MASK_BUTTON_PRESS |
			     XCB_EVENT_MASK_EXPOSURE };
	uint32_t root_mask[1] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
	/* flags, x, y, w, h, min, max, inc, min aspect, max aspect, base,
	 * gravity */
	uint32_t hints[18] = { (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) |
				       (1 << 8) | (1 << 9),
			       0,
			       0,
			       0,
			       0,
			       100,
			       80,
			       900,
			       700,
			       7,
			       13,
			       1,
			       2,
			       3,
			       1,
			       10,
			       20,
			       5 };
	static const char class_name[] = "poloclient\0PoloClient";
	xcb_atom_t protocols[3];
	xcb_atom_t type;
	xcb_generic_event_t *event;

	if (argc < 2)
		return 2;
	out = fopen(argv[1], "a");
	conn = xcb_connect(NULL, NULL);
	if (!out || xcb_connection_has_error(conn))
		return 3;
	screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	fprintf(out, "screen %dx%d\n", screen->width_in_pixels,
		screen->height_in_pixels);
	fflush(out);
	window = xcb_generate_id(conn);
	xcb_create_window(
		conn, XCB_COPY_FROM_PARENT, window, screen->root, 10, 20, 300,
		200, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
		XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT |
			XCB_CW_EVENT_MASK,
		(uint32_t[]){ screen->white_pixel,
			      getenv("X_TEST_OVERRIDE") != NULL, mask[0] });
	xcb_change_window_attributes(conn, screen->root, XCB_CW_EVENT_MASK,
				     root_mask);

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
			    XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
			    strlen("old title"), "old title");
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
			    XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
			    sizeof class_name, class_name);
	if (!getenv("X_TEST_NO_HINTS"))
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
				    XCB_ATOM_WM_NORMAL_HINTS,
				    XCB_ATOM_WM_SIZE_HINTS, 32, 18, hints);
	protocols[0] = atom("WM_DELETE_WINDOW");
	protocols[1] = atom("WM_TAKE_FOCUS");
	protocols[2] = atom("_NET_WM_PING");
	static const char net_name[] = "\xc3\xbcn\xc3\xaf";
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
			    atom("_NET_WM_NAME"), atom("UTF8_STRING"), 8,
			    sizeof net_name - 1, net_name);
	uint32_t wm_hints[9] = {
		1 | (1 << 6) | (1 << 8), 0, 0, 0, 0, 0, 0, 0, 0x1234
	};

	if (getenv("X_TEST_ICONIC")) {
		wm_hints[0] |= 1 << 1;
		wm_hints[2] = 3;
	}
	if (!getenv("X_TEST_NO_HINTS"))
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
				    XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 32, 9,
				    wm_hints);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
			    atom("WM_CLIENT_LEADER"), XCB_ATOM_WINDOW, 32, 1,
			    &window);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
			    atom("SM_CLIENT_ID"), XCB_ATOM_STRING, 8,
			    strlen("session-1"), "session-1");
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
			    atom("WM_PROTOCOLS"), XCB_ATOM_ATOM, 32, 3,
			    protocols);
	if (getenv("X_TEST_FOCUS_MODEL")) {
		const char *model = getenv("X_TEST_FOCUS_MODEL");
		bool take_focus =
			!strcmp(model, "locally") || !strcmp(model, "globally");
		uint32_t input_hints[9] = { 1,
					    !strcmp(model, "passive") ||
						    !strcmp(model, "locally") };
		xcb_atom_t list[2] = { protocols[0], protocols[1] };

		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
				    XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 32, 9,
				    input_hints);
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
				    atom("WM_PROTOCOLS"), XCB_ATOM_ATOM, 32,
				    take_focus ? 2 : 1, list);
	}
	type = atom(getenv("X_TEST_TYPE") ? getenv("X_TEST_TYPE")
					  : "_NET_WM_WINDOW_TYPE_NORMAL");
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
			    atom("_NET_WM_WINDOW_TYPE"), XCB_ATOM_ATOM, 32, 1,
			    &type);
	if (getenv("X_TEST_WM_PID")) {
		uint32_t pid = getpid();
		char host[256] = "";

		gethostname(host, sizeof(host) - 1);
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
				    XCB_ATOM_WM_CLIENT_MACHINE, XCB_ATOM_STRING, 8,
				    strlen(host), host);
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
				    atom("_NET_WM_PID"), XCB_ATOM_CARDINAL, 32, 1,
				    &pid);
	}
	gc = xcb_generate_id(conn);
	xcb_create_gc(conn, gc, window, XCB_GC_FOREGROUND,
		      (uint32_t[]){ screen->white_pixel });
	if (getenv("X_TEST_INITIAL_STATE")) {
		xcb_atom_t states[1] = { atom(getenv("X_TEST_INITIAL_STATE")) };

		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
				    atom("_NET_WM_STATE"), XCB_ATOM_ATOM, 32, 1,
				    states);
	}
	if (getenv("X_TEST_INITIAL_DESKTOP")) {
		uint32_t desktop = atoi(getenv("X_TEST_INITIAL_DESKTOP"));

		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
				    atom("_NET_WM_DESKTOP"), XCB_ATOM_CARDINAL,
				    32, 1, &desktop);
	}
	if (getenv("X_TEST_USER_TIME")) {
		uint32_t time = atoi(getenv("X_TEST_USER_TIME"));

		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
				    atom("_NET_WM_USER_TIME"),
				    XCB_ATOM_CARDINAL, 32, 1, &time);
	}
	if (getenv("X_TEST_STRUT")) {
		uint32_t strut[4] = { 0, 0, atoi(getenv("X_TEST_STRUT")), 0 };

		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
				    atom("_NET_WM_STRUT"), XCB_ATOM_CARDINAL,
				    32, 4, strut);
	}
	if (getenv("X_TEST_FRAME_EXTENTS")) {
		uint32_t e = atoi(getenv("X_TEST_FRAME_EXTENTS"));
		uint32_t extents[4] = { e, e, e, e };

		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
				    atom("_GTK_FRAME_EXTENTS"),
				    XCB_ATOM_CARDINAL, 32, 4, extents);
	}
	if (getenv("X_TEST_MOTIF"))
		set_motif(window, getenv("X_TEST_MOTIF"));
	if (getenv("X_TEST_POSITION")) {
		uint32_t where[2] = { 0 };
		uint32_t user_position[18] = { 1 };

		sscanf(getenv("X_TEST_POSITION"), "%u,%u", &where[0],
		       &where[1]);
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
				    XCB_ATOM_WM_NORMAL_HINTS,
				    XCB_ATOM_WM_SIZE_HINTS, 32, 18,
				    user_position);
		xcb_configure_window(conn, window,
				     XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
				     where);
	}
	fprintf(out, "id %u\n", window);
	xcb_map_window(conn, window);
	xcb_flush(conn);
	if (getenv("X_TEST_DUMP_ROOT")) {
		static const char *const names[] = { "_NET_SUPPORTING_WM_CHECK",
						     "_NET_SUPPORTED" };

		for (size_t i = 0; i < 2; i++)
			report_property(screen->root, atom(names[i]));
		xcb_get_property_reply_t *check = xcb_get_property_reply(
			conn,
			xcb_get_property(conn, 0, screen->root,
					 atom("_NET_SUPPORTING_WM_CHECK"),
					 XCB_ATOM_WINDOW, 0, 1),
			NULL);

		if (check && check->value_len == 1)
			report_property(
				*(xcb_window_t *)xcb_get_property_value(check),
				atom("_NET_WM_NAME"));
		free(check);
	}
	if (getenv("X_TEST_LAYOUT")) {
		uint32_t layout[4] = { 0, 2, 2, 0 };

		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, screen->root,
				    atom("_NET_DESKTOP_LAYOUT"),
				    XCB_ATOM_CARDINAL, 32, 4, layout);
	}
	fprintf(out, "mapped\n");
	fflush(out);

	while ((event = xcb_wait_for_event(conn))) {
		switch (event->response_type & ~0x80) {
		case XCB_CLIENT_MESSAGE: {
			xcb_client_message_event_t *message = (void *)event;

			fprintf(out, "message ");
			print_atom_name(message->data.data32[0]);
			fputc('\n', out);
			fflush(out);
			if (message->data.data32[0] == protocols[1] &&
			    getenv("X_TEST_TAKE_FOCUS_RESPOND")) {
				xcb_set_input_focus(
					conn, XCB_INPUT_FOCUS_POINTER_ROOT,
					window, message->data.data32[1]);
				xcb_flush(conn);
			}
			if (message->data.data32[0] == protocols[2] &&
			    !getenv("X_TEST_NO_PONG")) {
				xcb_client_message_event_t pong = *message;

				pong.window = screen->root;
				xcb_send_event(
					conn, 0, screen->root,
					XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
						XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
					(char *)&pong);
				xcb_flush(conn);
			}
			break;
		}
		case XCB_PROPERTY_NOTIFY: {
			xcb_property_notify_event_t *notify = (void *)event;

			if (notify->atom != XCB_ATOM_WM_NAME)
				report_property(notify->window, notify->atom);
			break;
		}
		case XCB_EXPOSE: {
			xcb_rectangle_t all = { 0, 0, 2000, 2000 };

			xcb_poly_fill_rectangle(conn, window, gc, 1, &all);
			xcb_flush(conn);
			break;
		}
		case XCB_KEY_PRESS:
			if (getenv("X_TEST_GRAB_ON_KEY") &&
			    ((xcb_key_press_event_t *)event)->detail ==
				    atoi(getenv("X_TEST_GRAB_ON_KEY"))) {
				static bool grabbed;

				if (grabbed) {
					xcb_ungrab_keyboard(conn,
							    XCB_CURRENT_TIME);
				} else {
					if (getenv("X_TEST_GRAB_DELAY"))
						sleep(atoi(getenv(
							"X_TEST_GRAB_DELAY")));
					free(xcb_grab_keyboard_reply(
						conn,
						xcb_grab_keyboard(
							conn, 1, window,
							XCB_CURRENT_TIME,
							XCB_GRAB_MODE_ASYNC,
							XCB_GRAB_MODE_ASYNC),
						NULL));
					fprintf(out, "grabbed\n");
				}
				grabbed = !grabbed;
				xcb_flush(conn);
			}
			if (getenv("X_TEST_CONVERT_ON_KEY")) {
				xcb_convert_selection(
					conn, window, atom("CLIPBOARD"),
					atom(getenv("X_TEST_CONVERT_ON_KEY")),
					atom("X_TEST_TARGETS"),
					XCB_TIME_CURRENT_TIME);
				xcb_flush(conn);
			}
			if (getenv("X_TEST_MOTIF_ON_KEY")) {
				set_motif(window,
					  getenv("X_TEST_MOTIF_ON_KEY"));
				xcb_flush(conn);
			}
			if (getenv("X_TEST_CONFIGURE_ON_KEY")) {
				uint32_t values[2] = { 400, 300 };

				xcb_configure_window(
					conn, window,
					XCB_CONFIG_WINDOW_WIDTH |
						XCB_CONFIG_WINDOW_HEIGHT,
					values);
				xcb_flush(conn);
			}
			if (getenv("X_TEST_ICONIFY")) {
				xcb_client_message_event_t change;

				memset(&change, 0, sizeof change);
				change.response_type = XCB_CLIENT_MESSAGE;
				change.format = 32;
				change.window = window;
				change.type = atom("WM_CHANGE_STATE");
				change.data.data32[0] = 3;
				xcb_send_event(
					conn, 0, screen->root,
					XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
						XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
					(char *)&change);
				xcb_flush(conn);
			}
			fprintf(out, "key %u\n",
				((xcb_key_press_event_t *)event)->detail);
			fflush(out);
			break;
		case XCB_BUTTON_PRESS:
			fprintf(out, "button %u\n",
				((xcb_button_press_event_t *)event)->detail);
			fflush(out);
			break;
		case XCB_FOCUS_IN:
			fprintf(out, "focus\n");
			fflush(out);
			break;
		case XCB_FOCUS_OUT:
			fprintf(out, "blur\n");
			fflush(out);
			break;
		case XCB_CONFIGURE_NOTIFY: {
			xcb_configure_notify_event_t *notify = (void *)event;

			if (notify->window == window) {
				fprintf(out, "size %dx%d\n", notify->width,
					notify->height);
				if (notify->response_type & 0x80)
					fprintf(out, "at %d %d\n", notify->x,
						notify->y);
				fflush(out);
			}
			break;
		}
		default:
			break;
		}
		if ((event->response_type & ~0x80) == XCB_CLIENT_MESSAGE &&
		    getenv("X_TEST_RETITLE")) {
			xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
					    XCB_ATOM_WM_NAME, XCB_ATOM_STRING,
					    8, strlen("new title"),
					    "new title");
			xcb_flush(conn);
		}
		free(event);
	}
	fprintf(out, "disconnected\n");
	fclose(out);
	return 0;
}
