/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ICCCM and EWMH property decoding. What the values mean is Vala's business.
 */

#ifndef POLO_XWM_PROPS_H
#define POLO_XWM_PROPS_H

#include <glib.h>
#include <xcb/xcb.h>

struct polo_xprop_atoms {
	xcb_atom_t net_wm_name, utf8_string, compound_text;
	xcb_atom_t wm_protocols, wm_delete_window, wm_take_focus;
	xcb_atom_t net_wm_ping, net_wm_sync_request;
	xcb_atom_t wm_client_leader, sm_client_id;
	xcb_atom_t motif_wm_hints;
};

struct polo_xprops {
	char *net_wm_name; /* NULL when absent or not UTF-8 */
	char *wm_name;     /* converted to UTF-8 */
	char *res_name, *res_class;
	char *sm_client_id;
	gboolean supports_delete, supports_take_focus;
	gboolean supports_ping, supports_sync_request;
	gboolean input, initially_iconic, urgent;
	guint32 window_group; /* 0 for none */
	guint32 transient_for;
	guint32 client_leader;
	guint32 mwm_flags, mwm_functions, mwm_decorations;
};

void
polo_xprop_atoms_init(struct polo_xprop_atoms *atoms, xcb_connection_t *conn);

void
polo_xprops_load(struct polo_xprops *props, xcb_connection_t *conn,
		 const struct polo_xprop_atoms *atoms, xcb_window_t window);

void
polo_xprops_clear(struct polo_xprops *props);

/* Exposed for tests: the bytes of a text property as UTF-8, NULL if impossible.
 */
char *
polo_xprops_decode_text(const char *data, int length, gboolean is_utf8);

#endif /* POLO_XWM_PROPS_H */
