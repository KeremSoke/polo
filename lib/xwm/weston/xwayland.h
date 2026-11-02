/*
 * Copyright © 2012 Intel Corporation
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

#include <stdio.h>
#include <wayland-server.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>
#include <xcb/composite.h>
#include <xcb/render.h>

#include <libweston/libweston.h>
#include <libweston/xwayland-api.h>
#include <libweston/weston-log.h>
#include "shared/xcb-xwayland.h"

struct weston_xserver {
	struct wl_display *wl_display;
	struct wl_event_loop *loop;
	int abstract_fd;
	struct wl_event_source *abstract_source;
	int unix_fd;
	struct wl_event_source *unix_source;
	int display;
	struct wl_client *client;
	struct wl_listener client_destroy_listener;
	struct weston_compositor *compositor;
	struct wl_listener compositor_destroy_listener;
	struct weston_wm *wm;
	weston_xwayland_spawn_xserver_func_t spawn_func;
	void *user_data;

	struct weston_log_scope *wm_debug;
};

struct weston_wm {
	xcb_connection_t *conn;
	const xcb_query_extension_reply_t *xfixes;
	struct wl_event_source *source;
	xcb_screen_t *screen;
	struct hash_table *window_hash;
	struct weston_xserver *server;
	struct wl_global *xwayland_shell_global;
	xcb_window_t wm_window;
	xcb_window_t no_focus_window;
	struct weston_wm_window *focus_window;
	uint32_t last_focus_request;
	struct theme *theme;
	xcb_cursor_t *cursors;
	int last_cursor;
	xcb_render_pictforminfo_t format_rgb, format_rgba;
	xcb_visualid_t visual_id;
	xcb_colormap_t colormap;
	struct wl_listener create_surface_listener;
	struct wl_listener activate_listener;
	struct wl_listener kill_listener;
	struct wl_list unpaired_window_list;

	xcb_window_t selection_window;
	xcb_window_t selection_owner;
	xcb_window_t primary_owner;
	xcb_timestamp_t primary_timestamp;
	struct wl_listener primary_listener;
	int incr;
	int data_source_fd;
	int read_active;
	struct wl_list read_queue;
	struct wl_event_source *property_source;
	xcb_get_property_reply_t *property_reply;
	int property_start;
	struct wl_array source_data;
	xcb_selection_request_event_t selection_request;
	xcb_atom_t selection_target;
	xcb_timestamp_t selection_timestamp;
	int selection_property_set;
	int flush_property_on_delete;
	struct wl_listener selection_listener;
	struct wl_listener seat_create_listener;
	struct wl_listener seat_destroy_listener;

	xcb_window_t dnd_window;
	xcb_window_t dnd_owner;
	bool dnd_mapped;
	struct dnd_data_source *x_source;
	struct weston_drag *x_drag;
	struct wl_listener dnd_motion_listener;
	struct weston_data_source *bridge_source;
	struct wl_listener bridge_source_listener;
	xcb_window_t bridge_target;
	int bridge_version;
	bool bridge_waiting, bridge_moved, bridge_accepted, bridge_dropped;
	int bridge_x, bridge_y;
	uint32_t bridge_action;

	struct wl_list unpaired_surface_list;
	bool shell_bound;

	struct atom_x11 atom;
	xcb_generic_event_t *polo_held_event;
};

void
dump_property(FILE *fp, struct weston_wm *wm, xcb_atom_t property,
	      xcb_get_property_reply_t *reply);

void
weston_wm_selection_init(struct weston_wm *wm);
void
weston_wm_selection_fini(struct weston_wm *wm);
void
weston_wm_read_selection(struct weston_wm *wm, xcb_atom_t selection,
			 const char *mime_type, int32_t fd);
void
weston_wm_dnd_fini(struct weston_wm *wm);
int
weston_wm_handle_selection_event(struct weston_wm *wm,
				 xcb_generic_event_t *event);

struct weston_wm *
weston_wm_create(struct weston_xserver *wxs, int fd);
void
weston_wm_destroy(struct weston_wm *wm);

struct weston_seat *
weston_wm_pick_seat(struct weston_wm *wm);

int
weston_wm_handle_dnd_event(struct weston_wm *wm,
			   xcb_generic_event_t *event);
void
weston_wm_dnd_init(struct weston_wm *wm);
