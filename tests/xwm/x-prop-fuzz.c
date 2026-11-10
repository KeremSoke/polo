/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An X client that feeds the window manager malformed data: properties of every
 * format and length (empty, short, long, a few megabytes, the wrong type, the
 * wrong format), transient_for chains, loops and bad ids, struts and icons made
 * of extreme numbers, client messages with arbitrary words, and windows that
 * come and go.
 *
 *   x-prop-fuzz LOG [SEED [ROUNDS]]
 *
 * The same SEED does the same thing. It writes "round N" lines to LOG and
 * "done" when finished, "lost" if the X connection went away. SEED "maps"
 * instead maps and unmaps windows back to back, faster than Xwayland pairs
 * them with their Wayland surfaces.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

#define WINDOWS 6
#define MAX_WORDS 200000

static xcb_connection_t *conn;
static xcb_screen_t *screen;
static uint64_t state;
static xcb_window_t windows[WINDOWS];
static uint32_t words[MAX_WORDS];

static const char *const PROPERTIES[] = {
	"WM_NORMAL_HINTS",
	"WM_HINTS",
	"WM_CLASS",
	"WM_NAME",
	"WM_ICON_NAME",
	"WM_TRANSIENT_FOR",
	"WM_PROTOCOLS",
	"WM_CLIENT_MACHINE",
	"WM_CLIENT_LEADER",
	"SM_CLIENT_ID",
	"_NET_WM_NAME",
	"_NET_WM_PID",
	"_NET_WM_WINDOW_TYPE",
	"_NET_WM_STATE",
	"_MOTIF_WM_HINTS",
	"_NET_WM_ICON",
	"_NET_WM_STRUT",
	"_NET_WM_STRUT_PARTIAL",
	"_GNOME_WM_STRUT_AREA",
	"_NET_WM_DESKTOP",
	"_NET_WM_USER_TIME",
	"_NET_WM_USER_TIME_WINDOW",
	"_NET_WM_ICON_GEOMETRY",
	"_GTK_FRAME_EXTENTS",
	"_GTK_APPLICATION_ID",
	"_BAMF_DESKTOP_FILE",
	"XdndAware",
	"XdndTypeList",
	"_NET_WM_SYNC_REQUEST_COUNTER",
	"_NET_WM_ALLOWED_ACTIONS",
	"_NET_WM_BYPASS_COMPOSITOR",
	"_NET_WM_OPAQUE_REGION",
	"_NET_WM_WINDOW_OPACITY",
};

static const char *const MESSAGES[] = {
	"_NET_WM_STATE",        "_NET_ACTIVE_WINDOW",
	"_NET_WM_DESKTOP",      "_NET_MOVERESIZE_WINDOW",
	"_NET_WM_MOVERESIZE",   "_NET_CURRENT_DESKTOP",
	"WM_CHANGE_STATE",      "_NET_REQUEST_FRAME_EXTENTS",
	"_NET_RESTACK_WINDOW",  "_NET_WM_FULLSCREEN_MONITORS",
	"_NET_SHOWING_DESKTOP", "_NET_NUMBER_OF_DESKTOPS",
};

static const char *const TYPES[] = {
	"STRING",          "UTF8_STRING",   "CARDINAL", "ATOM",
	"WINDOW",          "WM_SIZE_HINTS", "WM_HINTS", "INTEGER",
	"_MOTIF_WM_HINTS", "COMPOUND_TEXT",
};

static const uint32_t INTERESTING[] = {
	0,          1,          2,
	3,          7,          8,
	15,         16,         31,
	32,         63,         64,
	255,        256,        1023,
	1024,       1025,       32767,
	32768,      65535,      65536,
	0x7fffffff, 0x80000000, 0xfffffffe,
	0xffffffff, 1u << 20,   (1u << 20) + 1,
	0x01000000, 0xdeadbeef,
};

static const uint32_t LENGTHS[] = {
	0,  1,  2,  3,  4,  5,   8,   9,    11,   12,   13,    15,     16,
	17, 18, 19, 20, 24, 100, 256, 1000, 4096, 4097, 20000, 100000, 200000,
};

static uint32_t
rnd(void)
{
	state ^= state >> 12;
	state ^= state << 25;
	state ^= state >> 27;
	return (uint32_t)((state * 0x2545F4914F6CDD1DULL) >> 32);
}

static uint32_t
below(uint32_t n)
{
	return rnd() % n;
}

static xcb_atom_t
atom(const char *name)
{
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
		conn, xcb_intern_atom(conn, 0, strlen(name), name), NULL);
	xcb_atom_t result = reply ? reply->atom : XCB_ATOM_NONE;

	free(reply);
	return result;
}

static xcb_atom_t
cached(const char *const *names, xcb_atom_t *cache, uint32_t count)
{
	uint32_t i = below(count);

	if (!cache[i])
		cache[i] = atom(names[i]);
	return cache[i];
}

static xcb_atom_t
property_atom(void)
{
	static xcb_atom_t cache[sizeof PROPERTIES / sizeof PROPERTIES[0]];

	return cached(PROPERTIES, cache,
		      sizeof PROPERTIES / sizeof PROPERTIES[0]);
}

static xcb_atom_t
type_atom(void)
{
	static xcb_atom_t cache[sizeof TYPES / sizeof TYPES[0]];

	return cached(TYPES, cache, sizeof TYPES / sizeof TYPES[0]);
}

static xcb_atom_t
message_atom(void)
{
	static xcb_atom_t cache[sizeof MESSAGES / sizeof MESSAGES[0]];

	return cached(MESSAGES, cache, sizeof MESSAGES / sizeof MESSAGES[0]);
}

static void
drain(void)
{
	xcb_generic_event_t *event;

	while ((event = xcb_poll_for_event(conn)))
		free(event);
}

static void
make_window(int i)
{
	uint32_t mask[2] = { i == WINDOWS - 1,
			     XCB_EVENT_MASK_PROPERTY_CHANGE |
				     XCB_EVENT_MASK_STRUCTURE_NOTIFY };
	xcb_window_t window = xcb_generate_id(conn);

	xcb_create_window(conn, XCB_COPY_FROM_PARENT, window, screen->root,
			  60 + i * 40, 60 + i * 30, 200, 150, 0,
			  XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
			  XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, mask);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
			    XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, 9,
			    "fuzz\0Fuzz");
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window,
			    XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, 4, "fuzz");
	xcb_map_window(conn, window);
	windows[i] = window;
}

static xcb_window_t
some_window(void)
{
	switch (below(8)) {
	case 0:
		return 0;
	case 1:
		return screen->root;
	case 2:
		return 0x12345;
	case 3:
		return 0xffffffff;
	default:
		return windows[below(WINDOWS)];
	}
}

static uint32_t
word(void)
{
	switch (below(6)) {
	case 0:
	case 1:
		return INTERESTING[below(sizeof INTERESTING /
					 sizeof INTERESTING[0])];
	case 2:
		return below(2000);
	case 3:
		return some_window();
	case 4:
		return property_atom();
	default:
		return rnd();
	}
}

static void
set_property(void)
{
	xcb_window_t window = windows[below(WINDOWS)];
	xcb_atom_t property = atom(
		PROPERTIES[below(sizeof PROPERTIES / sizeof PROPERTIES[0])]);
	xcb_atom_t type = below(5) == 0 ? word() : type_atom();
	uint8_t format = (uint8_t[]){ 8, 16, 32, 32, 32 }[below(5)];
	uint32_t count = LENGTHS[below(sizeof LENGTHS / sizeof LENGTHS[0])];
	uint32_t limit = MAX_WORDS * 4u / (format / 8);
	uint32_t fill;

	if (count > 4096 && below(8) != 0)
		count = below(4096);
	if (count > limit)
		count = limit;
	fill = (count * (format / 8) + 3) / 4;
	if (below(3) == 0) {
		uint32_t pattern = word();

		for (uint32_t i = 0; i < fill; i++)
			words[i] = pattern;
	} else {
		for (uint32_t i = 0; i < fill; i++)
			words[i] = word();
	}
	if (type == XCB_ATOM_NONE)
		type = XCB_ATOM_CARDINAL;
	xcb_change_property(conn,
			    below(4) == 0 && count <= 4096
				    ? XCB_PROP_MODE_APPEND
				    : XCB_PROP_MODE_REPLACE,
			    window, property, type, format, count, words);
}

static void
set_transient(void)
{
	int i = below(WINDOWS);
	xcb_window_t target;

	switch (below(4)) {
	case 0:
		target = windows[(i + 1) % WINDOWS];
		break;
	case 1:
		target = windows[i];
		break;
	default:
		target = some_window();
		break;
	}
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, windows[i],
			    XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32, 1,
			    &target);
}

static void
loop_transients(void)
{
	for (int i = 0; i < WINDOWS; i++) {
		xcb_window_t target = windows[(i + 1) % WINDOWS];

		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, windows[i],
				    XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW,
				    32, 1, &target);
	}
}

static void
send_message(void)
{
	xcb_client_message_event_t event = { 0 };

	event.response_type = XCB_CLIENT_MESSAGE;
	event.format = 32;
	event.window = windows[below(WINDOWS)];
	event.type = message_atom();
	for (int i = 0; i < 5; i++)
		event.data.data32[i] = word();
	xcb_send_event(conn, 0, screen->root,
		       XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
			       XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
		       (const char *)&event);
}

static void
churn_window(void)
{
	int i = below(WINDOWS);

	switch (below(4)) {
	case 0:
		xcb_unmap_window(conn, windows[i]);
		break;
	case 1:
		xcb_map_window(conn, windows[i]);
		break;
	case 2: {
		uint32_t geometry[4] = { below(1500), below(900),
					 1 + below(1200), 1 + below(800) };

		xcb_configure_window(conn, windows[i],
				     XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
					     XCB_CONFIG_WINDOW_WIDTH |
					     XCB_CONFIG_WINDOW_HEIGHT,
				     geometry);
		break;
	}
	default:
		xcb_destroy_window(conn, windows[i]);
		make_window(i);
		break;
	}
}

static int
map_storm(FILE *out, unsigned long rounds)
{
	for (unsigned long round = 1; round <= rounds; round++) {
		xcb_window_t window = windows[round % WINDOWS];

		xcb_map_window(conn, window);
		xcb_unmap_window(conn, window);
		xcb_map_window(conn, window);
		if (round % 50 == 0) {
			xcb_flush(conn);
			drain();
		}
		if (xcb_connection_has_error(conn)) {
			fprintf(out, "lost\n");
			return 1;
		}
	}
	free(xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn), NULL));
	fprintf(out, "done\n");
	return 0;
}

int
main(int argc, char **argv)
{
	FILE *out;
	unsigned long seed = argc > 2 ? strtoul(argv[2], NULL, 10) : 1;
	unsigned long rounds = argc > 3 ? strtoul(argv[3], NULL, 10) : 3000;

	if (argc < 2)
		return 2;
	out = fopen(argv[1], "a");
	if (!out)
		return 2;
	setvbuf(out, NULL, _IOLBF, 0);
	state = seed * 0x9E3779B97F4A7C15ULL + 1;

	conn = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(conn)) {
		fprintf(out, "lost\n");
		return 1;
	}
	screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	for (int i = 0; i < WINDOWS; i++)
		make_window(i);
	xcb_flush(conn);
	if (argc > 2 && strcmp(argv[2], "maps") == 0)
		return map_storm(out, rounds);

	for (unsigned long round = 1; round <= rounds; round++) {
		uint32_t pick = below(100);

		if (pick < 60)
			set_property();
		else if (pick < 68)
			xcb_delete_property(conn, windows[below(WINDOWS)],
					    property_atom());
		else if (pick < 78)
			set_transient();
		else if (pick < 80)
			loop_transients();
		else if (pick < 90)
			send_message();
		else
			churn_window();

		if (round % 20 == 0) {
			free(xcb_get_input_focus_reply(
				conn, xcb_get_input_focus(conn), NULL));
			drain();
		}
		if (round % 100 == 0)
			fprintf(out, "round %lu\n", round);
		if (xcb_connection_has_error(conn)) {
			fprintf(out, "lost\n");
			return 1;
		}
	}
	free(xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn), NULL));
	fprintf(out, "done\n");
	return 0;
}
