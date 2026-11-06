/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Internal to libpolo-backend: the handle structs and the glue between the
 * C files. Never included by Vala-generated code.
 *
 * Handle lifetime: the server keeps one reference on every handle it
 * creates. When the libweston object goes away, the handle becomes inert
 * (its libweston pointer is cleared), emits "destroyed", and the server drops
 * its reference. Vala may hold further references; every method checks for
 * an inert handle and does nothing.
 */

#ifndef POLO_BACKEND_PRIVATE_H
#define POLO_BACKEND_PRIVATE_H

#include <libweston/libweston.h>
#include <libweston/desktop.h>

#include "polo-backend.h"

struct _PoloBackendLayer {
	struct weston_layer layer;
	PoloBackendLayerKind kind;
};

struct polo_layer_surface;

struct _PoloBackendServer {
	GObject parent_instance;

	struct wl_display *display;
	struct weston_compositor *compositor;
	struct weston_backend *backend;
	struct weston_desktop *desktop;
	PoloBackendKind kind;
	gboolean manage_outputs; /* DRM, or config->manage_outputs */
	char *socket_name;
	GSource *source;
	gboolean started;
	gboolean stopped;
	struct polo_cursor *cursor;
	struct polo_internal_client *internal_client; /* pixel-buffer.c */
	char *cursor_theme;
	int cursor_size;

	PoloBackendLayer layers[POLO_BACKEND_N_LAYERS];

	GPtrArray *outputs; /* PoloBackendOutput, owned */
	gboolean animating;
	GPtrArray *seats; /* PoloBackendSeat, owned */
	GHashTable
		*input_devices; /* libinput_device -> PoloBackendInputDevice */
	GHashTable *clients;    /* wl_client -> PoloBackendClient, owned */
	GHashTable *views;      /* weston_view -> PoloBackendView */
	/* Server-decoration decisions received before their toplevel exists. */
	GHashTable *early_server_decorations;
	int output_width; /* windowed/headless outputs */
	int output_height;
	int output_scale;
	int output_scales[8];
	int n_output_scales;

	struct wl_listener heads_changed_listener;
	struct wl_listener seat_created_listener;
	struct wl_listener client_created_listener;

	PoloBackendGlobalFilter global_filter;
	gpointer global_filter_data;
	GDestroyNotify global_filter_destroy;

	PoloBackendTestInspector test_inspector;
	gpointer test_inspector_data;
	GDestroyNotify test_inspector_destroy;
	PoloBackendTestSettings test_settings;
	gpointer test_settings_data;
	GDestroyNotify test_settings_destroy;
	struct wl_global *test_global;
	struct wl_list test_resources; /* polo_test_v1 */
	char *test_color_table; /* Latest color table, cached for clients that
				   bind later. */
	struct wl_listener screenshot_authority; /* test mode */
	struct wl_global *layer_shell_global;
	struct wl_global *foreign_toplevel_global;
	struct wl_list foreign_toplevels; /* PoloBackendForeignToplevel */
	struct wl_list foreign_toplevel_managers; /* manager wl_resources */
	struct wl_list foreign_toplevel_clients;  /* struct ft_client */
	struct wl_global *ext_toplevel_list_global;
	struct wl_global *xfce_toplevel_global;
	struct wl_global *xfce_output_global;
	struct wl_list xfce_outputs;
	struct wl_list xfce_seats;
	struct wl_listener xfce_seat_created;
	struct wl_list ext_toplevel_lists; /* ext_foreign_toplevel_list_v1 */
	struct wl_global *activation_global;
	struct wl_global *xwayland_keyboard_grab_global;
	struct wl_global *primary_selection_global;
	struct wl_signal primary_selection_signal; /* struct weston_seat */
	struct wl_global *text_input_global;
	struct wl_global *input_method_global;
	struct wl_global *virtual_keyboard_global;
	struct wl_global *idle_inhibit_global;
	struct wl_list idle_inhibitors;
	struct wl_global *idle_notifier_global;
	struct polo_session_lock *session_lock;
	struct polo_output_management *output_management;
	struct wl_list idle_watches;
	struct wl_listener idle_wake_listener;
	struct wl_listener idle_idle_listener;
	gint64 last_input_usec;
	gboolean input_idle;
	gboolean session_idle_inhibited;
	gboolean idle_ready;
	struct wl_global *shortcuts_inhibit_global;
	struct wl_global *cursor_shape_global;
	struct wl_global *pointer_gestures_global;
	struct wl_global *xdg_exporter_global;
	struct wl_global *xdg_importer_global;
	struct wl_list xdg_exports;
	struct wl_global *xdg_dialog_global;
	struct wl_list xdg_dialogs;
	struct wl_global *toplevel_icon_global;
	struct wl_list toplevel_icons;
	struct wl_global *security_context_global;
	struct wl_list security_contexts;
	struct wl_global *system_bell_global;
	struct wl_global *gtk_shell_global;
	struct wl_list gtk_surfaces;
	struct wl_global *xdg_decoration_global;
	struct wl_global *kde_decoration_global;
	PoloBackendActivationMint activation_mint;
	gpointer activation_mint_data;
	GDestroyNotify activation_mint_destroy;
	PoloBackendActivationActivate activation_activate;
	gpointer activation_activate_data;
	GDestroyNotify activation_activate_destroy;
	PoloBackendShellUi *shell_ui;
	PoloBackendWorkspaceManager *workspace_manager;
};

/* input-device.c */
struct libinput_device;

PoloBackendInputDevice *
polo_backend_input_device_new(struct libinput_device *device);

/* Emits "removed" and lets the libinput device go. */
void
polo_backend_input_device_remove(PoloBackendInputDevice *device);

/* server.c: configure_device and device_removed. */
void
polo_backend_server_input_device_added(PoloBackendServer *server,
				       struct libinput_device *device);
void
polo_backend_server_input_device_removed(PoloBackendServer *server,
					 struct libinput_device *device);

/* loop-source.c: dispatches the Wayland event loop from GLib. */
GSource *
polo_backend_loop_source_new(struct wl_display *display);

/* log.c */
struct weston_log_context *
polo_backend_log_get_context(void);

/* server.c */
PoloBackendView *
polo_backend_server_lookup_view(PoloBackendServer *server,
				struct weston_view *view);

PoloBackendClient *
polo_backend_server_get_client(PoloBackendServer *server,
			       struct wl_client *wl_client);

/* Called by toplevel.c when libweston-desktop announces a toplevel. */
void
polo_backend_server_emit_new_toplevel(PoloBackendServer *server,
				      PoloBackendToplevel *toplevel);

void
polo_backend_server_remove_output(PoloBackendServer *server,
				  PoloBackendOutput *handle);

/* output.c */
PoloBackendOutput *
polo_backend_output_new(PoloBackendServer *server,
			struct weston_output *output);

struct weston_output *
polo_backend_output_get_weston_output(PoloBackendOutput *output);

void
polo_backend_output_destroy(PoloBackendOutput *output);

void
polo_backend_output_set_animating(PoloBackendOutput *output,
				  gboolean animating);

void
polo_backend_server_emit_frame(PoloBackendServer *server,
			       PoloBackendOutput *output, gint64 usec);

/* seat.c */
PoloBackendSeat *
polo_backend_seat_new(PoloBackendServer *server, struct weston_seat *seat);

struct weston_seat *
polo_backend_seat_get_weston_seat(PoloBackendSeat *seat);

void
polo_backend_seat_destroy(PoloBackendSeat *seat);

PoloBackendSeat *
polo_backend_server_lookup_seat(PoloBackendServer *server,
				struct weston_seat *seat);

/* view.c: a view handle does not own its weston_view. */
PoloBackendView *
polo_backend_view_new(PoloBackendServer *server, struct weston_view *view,
		      PoloBackendToplevel *toplevel);

struct weston_view *
polo_backend_view_get_weston_view(PoloBackendView *view);

void
polo_backend_view_destroy(PoloBackendView *view);

/* toplevel.c */
gboolean
polo_backend_desktop_init(PoloBackendServer *server, GError **error);

void
polo_backend_desktop_fini(PoloBackendServer *server);

PoloBackendToplevel *
polo_backend_toplevel_from_surface(struct weston_surface *surface);

/* Records the decoration mode agreed with the client. */
void
polo_backend_toplevel_set_server_decorated(struct weston_surface *surface,
					   gboolean server_decorated);

/* client.c */
PoloBackendClient *
polo_backend_client_new(PoloBackendServer *server, struct wl_client *client);

void
polo_backend_client_destroy(PoloBackendClient *client);

/* grab.c */
PoloBackendPointerGrab *
polo_backend_pointer_grab_start(struct weston_pointer *pointer);

/* test-protocol.c */
gboolean
polo_backend_test_protocol_init(PoloBackendServer *server, GError **error);

/* protocols/layer-shell.c */
gboolean
polo_backend_layer_shell_init(PoloBackendServer *server, GError **error);

void
polo_backend_layer_shell_fini(PoloBackendServer *server);

void
polo_backend_server_emit_new_layer_surface(PoloBackendServer *server,
					   PoloBackendLayerSurface *surface);

PoloBackendLayerSurface *
polo_backend_layer_surface_new(PoloBackendServer *server,
			       struct polo_layer_surface *impl,
			       const char *namespace_);

/* protocols/wlr-foreign-toplevel.c */
gboolean
polo_backend_foreign_toplevel_manager_init(PoloBackendServer *server,
					   GError **error);

void
polo_backend_foreign_toplevel_manager_fini(PoloBackendServer *server);

/* protocols/ext-foreign-toplevel-list.c */
gboolean
polo_backend_ext_toplevel_list_init(PoloBackendServer *server, GError **error);

void
polo_backend_ext_toplevel_list_fini(PoloBackendServer *server);

gboolean
polo_backend_output_get_work_area(PoloBackendOutput *output,
				  struct weston_geometry *area);

gboolean
polo_backend_output_get_primary(PoloBackendOutput *output);

PoloBackendServer *
polo_backend_output_get_server(PoloBackendOutput *output);

gboolean
polo_backend_xfce_private_init(PoloBackendServer *server, GError **error);

void
polo_backend_xfce_private_fini(PoloBackendServer *server);

gboolean
polo_backend_xdg_activation_init(PoloBackendServer *server, GError **error);

void
polo_backend_xdg_activation_fini(PoloBackendServer *server);

gboolean
polo_backend_idle_inhibit_init(PoloBackendServer *server, GError **error);

void
polo_backend_idle_inhibit_fini(PoloBackendServer *server);

void
polo_backend_idle_init(PoloBackendServer *server);

void
polo_backend_idle_fini(PoloBackendServer *server);

void
polo_backend_idle_inhibitors_changed(PoloBackendServer *server, gboolean added);

void
polo_backend_server_emit_activity(PoloBackendServer *server);

gboolean
polo_backend_idle_notify_init(PoloBackendServer *server, GError **error);

void
polo_backend_idle_notify_fini(PoloBackendServer *server);

gboolean
polo_backend_output_management_init(PoloBackendServer *server, GError **error);

void
polo_backend_output_management_fini(PoloBackendServer *server);

gboolean
polo_backend_session_lock_init(PoloBackendServer *server, GError **error);

void
polo_backend_session_lock_fini(PoloBackendServer *server);

gboolean
polo_backend_session_lock_active(PoloBackendServer *server);

gboolean
polo_backend_session_lock_is_abandoned(PoloBackendServer *server);

/* While locked, only the live lock may have focus. */
gboolean
polo_backend_session_lock_allows_focus(PoloBackendServer *server,
				       struct weston_surface *surface);

void
polo_backend_server_emit_lock_state(PoloBackendServer *server,
				    PoloBackendLockState state);

void
polo_backend_server_emit_lock_restart(PoloBackendServer *server);

gboolean
polo_backend_cursor_shape_init(PoloBackendServer *server, GError **error);

void
polo_backend_cursor_shape_fini(PoloBackendServer *server);

/* Valid shapes are below this. */
int
polo_backend_cursor_n_shapes(void);

void
polo_backend_cursor_set_shape(PoloBackendServer *server,
			      struct weston_pointer *pointer, uint32_t shape);

gboolean
polo_backend_xdg_foreign_init(PoloBackendServer *server, GError **error);

void
polo_backend_xdg_foreign_fini(PoloBackendServer *server);

void
polo_backend_xdg_foreign_surface_added(PoloBackendServer *server);

gboolean
polo_backend_xdg_dialog_init(PoloBackendServer *server, GError **error);

void
polo_backend_xdg_dialog_fini(PoloBackendServer *server);

void
polo_backend_xdg_dialog_surface_added(PoloBackendServer *server);

/* FALSE when the child or parent has no toplevel yet. parent NULL unsets. */
gboolean
polo_backend_toplevel_foreign_parent(struct weston_desktop_surface *child,
				     struct weston_desktop_surface *parent);

/* FALSE when the surface has no toplevel yet. */
gboolean
polo_backend_toplevel_set_modal_hint(struct weston_desktop_surface *surface,
				     gboolean modal);

gboolean
polo_backend_toplevel_icon_init(PoloBackendServer *server, GError **error);

void
polo_backend_toplevel_icon_fini(PoloBackendServer *server);

void
polo_backend_toplevel_icon_commit(PoloBackendServer *server,
				  struct weston_desktop_surface *surface);

void
polo_backend_toplevel_set_icon(struct weston_desktop_surface *surface,
			       const char *name, GBytes *pixels, int size);

gboolean
polo_backend_security_context_init(PoloBackendServer *server, GError **error);

void
polo_backend_security_context_fini(PoloBackendServer *server);

gboolean
polo_backend_system_bell_init(PoloBackendServer *server, GError **error);

void
polo_backend_system_bell_fini(PoloBackendServer *server);

gboolean
polo_backend_gtk_shell_init(PoloBackendServer *server, GError **error);

void
polo_backend_gtk_shell_fini(PoloBackendServer *server);

void
polo_backend_gtk_shell_surface_added(PoloBackendServer *server);


void
polo_backend_server_emit_system_bell(PoloBackendServer *server,
				     PoloBackendToplevel *toplevel);

struct polo_gestures;

gboolean
polo_backend_pointer_gestures_init(PoloBackendServer *server, GError **error);

void
polo_backend_pointer_gestures_fini(PoloBackendServer *server);

struct polo_gestures *
polo_gestures_new(PoloBackendServer *server);

void
polo_gestures_free(struct polo_gestures *gestures);

void
polo_gestures_attach_pointer(struct polo_gestures *gestures,
			     struct weston_pointer *pointer);

struct polo_gestures *
polo_backend_seat_get_gestures(PoloBackendSeat *seat);

struct polo_shortcuts;

gboolean
polo_backend_shortcuts_inhibit_init(PoloBackendServer *server, GError **error);

void
polo_backend_shortcuts_inhibit_fini(PoloBackendServer *server);

struct polo_shortcuts *
polo_shortcuts_new(void);

void
polo_shortcuts_free(struct polo_shortcuts *seat);

void
polo_shortcuts_attach_keyboard(struct polo_shortcuts *seat,
			       struct weston_keyboard *keyboard);

/* TRUE when an active inhibitor keeps this key from Polo's bindings. */
gboolean
polo_shortcuts_takes_key(struct polo_shortcuts *seat, gboolean escape_hatch);

struct polo_shortcuts *
polo_backend_seat_get_shortcuts(PoloBackendSeat *seat);

struct polo_text_seat;

gboolean
polo_backend_text_input_init(PoloBackendServer *server, GError **error);

void
polo_backend_text_input_fini(PoloBackendServer *server);

gboolean
polo_backend_virtual_keyboard_init(PoloBackendServer *server, GError **error);

void
polo_backend_virtual_keyboard_fini(PoloBackendServer *server);

struct polo_text_seat *
polo_text_seat_new(PoloBackendServer *server, struct weston_seat *seat);

void
polo_text_seat_free(struct polo_text_seat *seat);

void
polo_text_seat_attach_keyboard(struct polo_text_seat *seat,
			       struct weston_keyboard *keyboard);

/* TRUE when the input method took the key. */
gboolean
polo_text_seat_key(struct polo_text_seat *seat,
		   const struct weston_key_event *event);

void
polo_text_seat_modifiers(struct polo_text_seat *seat, uint32_t serial,
			 uint32_t depressed, uint32_t latched, uint32_t locked,
			 uint32_t group);

struct polo_text_seat *
polo_backend_seat_get_text(PoloBackendSeat *seat);

struct polo_primary;

gboolean
polo_backend_primary_selection_init(PoloBackendServer *server, GError **error);

void
polo_backend_primary_selection_fini(PoloBackendServer *server);

struct polo_primary *
polo_primary_new(PoloBackendServer *server, struct weston_seat *seat);

void
polo_primary_free(struct polo_primary *primary);

void
polo_primary_attach_keyboard(struct polo_primary *primary,
			     struct weston_keyboard *keyboard);

struct weston_data_source *
polo_primary_get_source(struct polo_primary *primary);

void
polo_primary_set_source(struct polo_primary *primary,
			struct weston_data_source *source);

struct polo_primary *
polo_backend_seat_get_primary(PoloBackendSeat *seat);

gboolean
polo_backend_xdg_decoration_init(PoloBackendServer *server, GError **error);

void
polo_backend_xdg_decoration_fini(PoloBackendServer *server);

gboolean
polo_backend_kde_decoration_init(PoloBackendServer *server, GError **error);

void
polo_backend_kde_decoration_fini(PoloBackendServer *server);

gboolean
polo_backend_xwayland_keyboard_grab_init(PoloBackendServer *server,
					 GError **error);

void
polo_backend_xwayland_keyboard_grab_fini(PoloBackendServer *server);

/* TRUE: grab belongs to seat until end; it may be granted later, once the
 * grabbing surface maps. */
gboolean
polo_backend_seat_begin_x_keyboard_grab(PoloBackendSeat *seat,
					struct wl_resource *grab,
					struct weston_surface *surface);

void
polo_backend_seat_end_x_keyboard_grab(PoloBackendSeat *seat,
				      struct wl_resource *grab);

/* protocols/ext-workspace.c */
gboolean
polo_backend_workspace_manager_init_global(PoloBackendServer *server,
					   GError **error);

void
polo_backend_workspace_manager_fini_global(PoloBackendServer *server);

struct wl_resource *
polo_backend_workspace_resource_for(PoloBackendWorkspace *workspace,
				    struct wl_client *client);

PoloBackendWorkspace *
polo_backend_workspace_from_resource(struct wl_resource *resource);

/* protocols/xfce-private.c */
void
polo_backend_xfce_sync_all_workspaces(PoloBackendServer *server);

void
polo_backend_xfce_workspace_removed(PoloBackendServer *server,
				    PoloBackendWorkspace *workspace);

void
polo_backend_xfce_workspace_handle_gone(PoloBackendServer *server,
					struct wl_resource *resource);

/* protocols/polo-shell-ui.c */
gboolean
polo_backend_shell_ui_global_init(PoloBackendServer *server, GError **error);

void
polo_backend_shell_ui_global_fini(PoloBackendServer *server);

void
polo_backend_xfce_output_changed(PoloBackendOutput *output,
				 gboolean became_primary);

PoloBackendOutput *
polo_backend_server_lookup_output(PoloBackendServer *server,
				  struct weston_output *output);

/* The Polo output for a client's wl_output resource, or NULL. */
PoloBackendOutput *
polo_backend_server_output_from_resource(PoloBackendServer *server,
					 struct wl_resource *resource);

/* The layer surface owning this wl_surface, or NULL (tests, teardown). */
PoloBackendLayerSurface *
polo_backend_layer_surface_from_surface(struct weston_surface *surface);

/* pixel-buffer.c: the in-process client behind PoloBackendPixelBuffer. */
void
polo_backend_internal_client_init(PoloBackendServer *server);

void
polo_backend_internal_client_fini(PoloBackendServer *server);

void
polo_backend_cursor_init(PoloBackendServer *server);
void
polo_backend_cursor_fini(PoloBackendServer *server);
void
polo_backend_cursor_add_pointer(PoloBackendServer *server,
				struct weston_pointer *pointer);
struct weston_backend *
polo_backend_drm_load(PoloBackendServer *server,
		      const PoloBackendConfig *config);
struct libinput_device;
void
polo_backend_drm_configure_device(struct weston_compositor *compositor,
				  struct libinput_device *device);
void
polo_backend_drm_install_bindings(PoloBackendServer *server,
				  gboolean debug_kill);
struct weston_surface *
polo_backend_toplevel_get_weston_surface(PoloBackendToplevel *toplevel);

#endif /* POLO_BACKEND_PRIVATE_H */
