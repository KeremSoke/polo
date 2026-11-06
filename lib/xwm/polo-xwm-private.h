/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What the vendored XWM (weston/) and the GObject facade (xwm-object.c) call
 * in each other. Not visible to Vala.
 */

#ifndef POLO_XWM_PRIVATE_H
#define POLO_XWM_PRIVATE_H

#include <stdint.h>
#include <stdbool.h>
#include <glib.h>
#include <xcb/xcb.h>
#include <libweston/xwayland-api.h>

struct _PoloXWindow;
struct weston_xserver;
struct weston_wm;
struct weston_wm_window;

struct polo_wm_window_info {
	uint32_t id, frame_id;
	int pid;
	const char *machine, *wm_class, *title;
	uint32_t window_type;
	struct _PoloXWindow *transient_for;
	int override_redirect;
	int x, y, width, height;
	int fullscreen, maximized_vert, maximized_horz;
	int supports_delete, supports_take_focus;
	int decorations;
	uint32_t hints_flags;
	int min_width, min_height, max_width, max_height;
	int width_inc, height_inc;
	int min_aspect_x, min_aspect_y, max_aspect_x, max_aspect_y;
	int base_width, base_height;
	int win_gravity;
};

struct _PoloXWindow *
polo_xwindow_created(struct weston_wm_window *window);
void
polo_xwindow_destroyed(struct _PoloXWindow *self);
void
polo_xwindow_property_changed(struct _PoloXWindow *self, const char *atom);
void
polo_xwindow_geometry_changed(struct _PoloXWindow *self);
void
polo_xwindow_unmapped(struct _PoloXWindow *self);
void
polo_xwindow_client_message(struct _PoloXWindow *self, const char *type,
			    const uint32_t *data);
bool
polo_xwindow_configure_requested(struct _PoloXWindow *self, uint32_t mask,
				 int x, int y, int width, int height);
void
polo_xwindow_focus_in(struct _PoloXWindow *self);
void
polo_xwm_focus_lost(void);
void
polo_xwm_bell(struct _PoloXWindow *window);
void
polo_wm_clear_focus(struct weston_wm *wm);
void
polo_xwm_root_message(const char *type, const uint32_t *data);
void
polo_xwm_watched_property_changed(uint32_t xid, const char *atom);
void
polo_xwindow_pong(struct _PoloXWindow *self);
void
polo_xwm_root_property_changed(const char *atom);
void
polo_xserver_destroyed(struct weston_xserver *wxs);

struct weston_xserver *
polo_xserver_create(struct weston_compositor *compositor);
int
polo_xserver_listen(struct weston_xserver *wxs, void *user_data,
		    weston_xwayland_spawn_xserver_func_t spawn_func);
void
polo_xserver_loaded(struct weston_xserver *wxs, int wm_fd);
void
polo_xserver_exited(struct weston_xserver *wxs);
void
polo_xserver_spawn(struct weston_xserver *wxs);
void
polo_xserver_give_up(struct weston_xserver *wxs);
int
polo_xserver_get_display(struct weston_xserver *wxs);
struct weston_wm *
polo_xserver_get_wm(struct weston_xserver *wxs);

void
polo_wm_window_get_info(struct weston_wm_window *window,
			struct polo_wm_window_info *info);
struct weston_wm_window *
polo_wm_window_for_surface(struct weston_surface *surface);
struct _PoloXWindow *
polo_wm_window_get_polo(struct weston_wm_window *window);
char *
polo_wm_atom_name(struct weston_wm_window *window, uint32_t atom);
void
polo_wm_window_configure(struct weston_wm_window *window, int x, int y,
			 int width, int height);
void
polo_wm_window_set_input_focus(struct weston_wm_window *window, uint32_t time);
void
polo_wm_window_send_take_focus(struct weston_wm_window *window, uint32_t time);
void
polo_wm_window_send_delete(struct weston_wm_window *window, uint32_t time);
void
polo_wm_window_set_wm_state(struct weston_wm_window *window, int32_t state);
void
polo_wm_window_set_atoms(struct weston_wm_window *window, const char *property,
			 const char *const *names, int n);
void
polo_wm_window_set_frame_extents(struct weston_wm_window *window, uint32_t left,
				 uint32_t right, uint32_t top, uint32_t bottom);
bool
polo_wm_is_ping(struct weston_wm *wm, uint32_t atom);
void
polo_wm_window_send_ping(struct weston_wm_window *window, uint32_t time);
void
polo_wm_window_set_visible_name(struct weston_wm_window *window,
				const char *name);
xcb_connection_t *
polo_wm_window_get_conn(struct weston_wm_window *window);
gboolean
polo_wm_window_is_root(struct weston_wm_window *window, uint32_t id);
void
polo_wm_window_kill_client(struct weston_wm_window *window);
void
polo_wm_set_root_cardinals(struct weston_wm *wm, const char *property,
			   uint32_t type, const uint32_t *values, int n);
bool
polo_wm_has_queued_event(struct weston_wm *wm);
void
polo_wm_process_queued_events(struct weston_wm *wm);
const char *
polo_wm_atom_name_cached(struct weston_wm *wm, uint32_t atom);
void
polo_wm_window_set_frame_inset(struct weston_wm_window *window, int left,
			       int right, int top, int bottom);
uint32_t *
polo_wm_get_cardinals(struct weston_wm *wm, uint32_t xid, const char *property,
		      int *n);
char **
polo_wm_get_atom_names(struct weston_wm *wm, uint32_t xid, const char *property,
		       int *n);
char *
polo_wm_get_text(struct weston_wm *wm, uint32_t xid, const char *property);
void
polo_wm_watch_window(struct weston_wm *wm, uint32_t xid);
void
polo_wm_set_window_cardinals(struct weston_wm *wm, uint32_t xid,
			     const char *property, const uint32_t *values,
			     int n);
struct weston_wm *
polo_wm_window_get_wm(struct weston_wm_window *window);
void
polo_wm_set_root_atoms(struct weston_wm *wm, const char *property,
		       const char *const *names, int n);
uint32_t *
polo_wm_get_root_cardinals(struct weston_wm *wm, const char *property, int *n);
void
polo_wm_set_root_utf8(struct weston_wm *wm, const char *property,
		      const char *value, int length);

#endif /* POLO_XWM_PRIVATE_H */
