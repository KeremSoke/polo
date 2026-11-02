/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "props.h"

#include <stdlib.h>
#include <string.h>

#define WM_HINTS_INPUT (1u << 0)
#define WM_HINTS_STATE (1u << 1)
#define WM_HINTS_WINDOW_GROUP (1u << 6)
#define WM_HINTS_URGENCY (1u << 8)
#define ICONIC_STATE 3

static xcb_atom_t
intern(xcb_connection_t *conn, const char *name)
{
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
		conn, xcb_intern_atom(conn, 0, strlen(name), name), NULL);
	xcb_atom_t atom = reply ? reply->atom : XCB_ATOM_NONE;

	free(reply);
	return atom;
}

void
polo_xprop_atoms_init(struct polo_xprop_atoms *atoms, xcb_connection_t *conn)
{
	atoms->net_wm_name = intern(conn, "_NET_WM_NAME");
	atoms->utf8_string = intern(conn, "UTF8_STRING");
	atoms->compound_text = intern(conn, "COMPOUND_TEXT");
	atoms->wm_protocols = intern(conn, "WM_PROTOCOLS");
	atoms->wm_delete_window = intern(conn, "WM_DELETE_WINDOW");
	atoms->wm_take_focus = intern(conn, "WM_TAKE_FOCUS");
	atoms->net_wm_ping = intern(conn, "_NET_WM_PING");
	atoms->net_wm_sync_request = intern(conn, "_NET_WM_SYNC_REQUEST");
	atoms->wm_client_leader = intern(conn, "WM_CLIENT_LEADER");
	atoms->sm_client_id = intern(conn, "SM_CLIENT_ID");
	atoms->motif_wm_hints = intern(conn, "_MOTIF_WM_HINTS");
}

char *
polo_xprops_decode_text(const char *data, int length, gboolean is_utf8)
{
	if (is_utf8) {
		if (!g_utf8_validate(data, length, NULL))
			return NULL;
		return g_strndup(data, length);
	}
	return g_convert(data, length, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
}

static xcb_get_property_reply_t *
get_property(xcb_connection_t *conn, xcb_window_t window, xcb_atom_t property)
{
	xcb_get_property_reply_t *reply = xcb_get_property_reply(
		conn,
		xcb_get_property(conn, 0, window, property, XCB_ATOM_ANY, 0,
				 4096),
		NULL);

	if (reply && reply->type == XCB_ATOM_NONE) {
		free(reply);
		return NULL;
	}
	return reply;
}

static guint32
get_window_id(xcb_connection_t *conn, xcb_window_t window, xcb_atom_t property)
{
	xcb_get_property_reply_t *reply = get_property(conn, window, property);
	guint32 value = 0;

	if (reply && reply->format == 32 && reply->value_len >= 1)
		value = *(guint32 *)xcb_get_property_value(reply);
	free(reply);
	return value;
}

static char *
get_latin1_text(xcb_connection_t *conn, xcb_window_t window,
		xcb_atom_t property)
{
	xcb_get_property_reply_t *reply = get_property(conn, window, property);
	char *text = NULL;

	if (reply && reply->format == 8)
		text = polo_xprops_decode_text(
			xcb_get_property_value(reply),
			xcb_get_property_value_length(reply), FALSE);
	free(reply);
	return text;
}

void
polo_xprops_load(struct polo_xprops *props, xcb_connection_t *conn,
		 const struct polo_xprop_atoms *atoms, xcb_window_t window)
{
	xcb_get_property_reply_t *reply;

	polo_xprops_clear(props);
	props->input = TRUE;

	reply = get_property(conn, window, atoms->net_wm_name);
	if (reply && reply->format == 8 && reply->type == atoms->utf8_string)
		props->net_wm_name = polo_xprops_decode_text(
			xcb_get_property_value(reply),
			xcb_get_property_value_length(reply), TRUE);
	free(reply);

	reply = get_property(conn, window, XCB_ATOM_WM_NAME);
	if (reply && reply->format == 8)
		props->wm_name = polo_xprops_decode_text(
			xcb_get_property_value(reply),
			xcb_get_property_value_length(reply),
			reply->type == atoms->utf8_string);
	free(reply);

	reply = get_property(conn, window, XCB_ATOM_WM_CLASS);
	if (reply && reply->format == 8) {
		const char *data = xcb_get_property_value(reply);
		int length = xcb_get_property_value_length(reply);
		int first = strnlen(data, length);

		props->res_name = polo_xprops_decode_text(data, first, FALSE);
		if (first + 1 < length)
			props->res_class = polo_xprops_decode_text(
				data + first + 1,
				strnlen(data + first + 1, length - first - 1),
				FALSE);
	}
	free(reply);

	reply = get_property(conn, window, atoms->wm_protocols);
	if (reply && reply->format == 32) {
		const xcb_atom_t *protocols = xcb_get_property_value(reply);

		for (uint32_t i = 0; i < reply->value_len; i++) {
			if (protocols[i] == atoms->wm_delete_window)
				props->supports_delete = TRUE;
			else if (protocols[i] == atoms->wm_take_focus)
				props->supports_take_focus = TRUE;
			else if (protocols[i] == atoms->net_wm_ping)
				props->supports_ping = TRUE;
			else if (protocols[i] == atoms->net_wm_sync_request)
				props->supports_sync_request = TRUE;
		}
	}
	free(reply);

	reply = get_property(conn, window, XCB_ATOM_WM_HINTS);
	if (reply && reply->format == 32 && reply->value_len >= 2) {
		const guint32 *hints = xcb_get_property_value(reply);
		guint32 flags = hints[0];

		if (flags & WM_HINTS_INPUT)
			props->input = hints[1] != 0;
		if ((flags & WM_HINTS_STATE) && reply->value_len >= 3)
			props->initially_iconic = hints[2] == ICONIC_STATE;
		if ((flags & WM_HINTS_WINDOW_GROUP) && reply->value_len >= 9)
			props->window_group = hints[8];
		props->urgent = (flags & WM_HINTS_URGENCY) != 0;
	}
	free(reply);

	reply = get_property(conn, window, atoms->motif_wm_hints);
	if (reply && reply->format == 32) {
		const guint32 *hints = xcb_get_property_value(reply);

		if (reply->value_len >= 1)
			props->mwm_flags = hints[0];
		if (reply->value_len >= 2)
			props->mwm_functions = hints[1];
		if (reply->value_len >= 3)
			props->mwm_decorations = hints[2];
	}
	free(reply);

	props->transient_for =
		get_window_id(conn, window, XCB_ATOM_WM_TRANSIENT_FOR);
	props->client_leader =
		get_window_id(conn, window, atoms->wm_client_leader);
	props->sm_client_id = get_latin1_text(
		conn, props->client_leader ? props->client_leader : window,
		atoms->sm_client_id);
}

void
polo_xprops_clear(struct polo_xprops *props)
{
	g_free(props->net_wm_name);
	g_free(props->wm_name);
	g_free(props->res_name);
	g_free(props->res_class);
	g_free(props->sm_client_id);
	memset(props, 0, sizeof *props);
}
