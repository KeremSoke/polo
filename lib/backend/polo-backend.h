/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * libpolo-backend: the only part of Polo that talks to libweston.
 *
 * This header is the C side of vapi/polo-backend.vapi. It must never include
 * libweston or wayland-server headers: Vala-generated C includes it, and the
 * types it exposes are Polo concepts, not libweston ones.
 *
 * Division of labour: the C side keeps protocol mechanics
 * correct (serials, roles, buffer offsets, the event loop); every decision
 * about where windows go, which one is focused or raised, and what a click
 * means is made by the Vala caller.
 */

#ifndef POLO_BACKEND_H
#define POLO_BACKEND_H

#include <glib-object.h>

G_BEGIN_DECLS

/* Version of the libweston Polo is linked against, e.g. "16.0.1". */
const char *
polo_backend_libweston_version(void);

/* ---- Errors ------------------------------------------------------------- */

#define POLO_BACKEND_ERROR (polo_backend_error_quark())

typedef enum {
	POLO_BACKEND_ERROR_FAILED,
	POLO_BACKEND_ERROR_BACKEND, /* the libweston backend did not load */
	POLO_BACKEND_ERROR_SOCKET,  /* the Wayland socket could not be created */
} PoloBackendError;

GQuark
polo_backend_error_quark(void);

/* ---- Logging ----------------------------------------------------------- */

/*
 * A named log scope. Scopes are owned by the log context and live until
 * polo_backend_log_shutdown(); callers only ever hold unowned pointers.
 */
typedef struct _PoloLogScope PoloLogScope;

/*
 * Open the log. @log_file NULL means stderr. @debug_scopes is a
 * comma-separated list of scope names whose debug messages are printed;
 * it may name Polo scopes ("polo-policy") or libweston scopes
 * ("drm-backend"), including ones created later. Info and warning messages
 * of every Polo scope always go to the log.
 *
 * Calling any other log function first initialises the log on stderr with
 * no debug scopes. Raises G_FILE_ERROR.
 */
gboolean
polo_backend_log_init(const char *log_file, const char *debug_scopes,
		      GError **error);

void
polo_backend_log_shutdown(void);

/* Return the scope called @name, creating it if needed. Never NULL. */
PoloLogScope *
polo_backend_log_scope(const char *name);

const char *
polo_log_scope_get_name(PoloLogScope *scope);

/* Whether debug messages of @scope are printed. */
gboolean
polo_log_scope_is_enabled(PoloLogScope *scope);

void
polo_log_scope_debug(PoloLogScope *scope, const char *format, ...)
	G_GNUC_PRINTF(2, 3);

void
polo_log_scope_info(PoloLogScope *scope, const char *format, ...)
	G_GNUC_PRINTF(2, 3);

void
polo_log_scope_warning(PoloLogScope *scope, const char *format, ...)
	G_GNUC_PRINTF(2, 3);

/* ---- Configuration ------------------------------------------------------ */

typedef enum {
	POLO_BACKEND_KIND_DRM,
	POLO_BACKEND_KIND_WAYLAND,
	POLO_BACKEND_KIND_HEADLESS,
} PoloBackendKind;

typedef enum {
	POLO_BACKEND_RENDERER_AUTO,
	POLO_BACKEND_RENDERER_GL,
	POLO_BACKEND_RENDERER_PIXMAN,
} PoloBackendRenderer;

/*
 * Start-up parameters. Strings are borrowed for the duration of
 * polo_backend_server_start() only.
 */
typedef struct {
	PoloBackendKind kind;
	PoloBackendRenderer renderer;
	/* wayland and headless: size, scale and number of outputs */
	int width;
	int height;
	int scale;
	int output_count;
	/* wayland: open the nested output fullscreen */
	gboolean fullscreen;
	/* NULL: first free "wayland-N" */
	const char *socket_name;
	/* drm: logind seat; NULL means XDG_SEAT or "seat0" */
	const char *seat;
	/* XKB rule names; NULL fields fall back to XKB_DEFAULT_* and xkbcommon */
	const char *xkb_model;
	const char *xkb_layout;
	const char *xkb_variant;
	const char *xkb_options;
	/* register polo_test_v1 (only in -Dtest-protocol builds) */
	gboolean test_mode;
	gboolean debug_kill;
	/*
	 * Leave every new output disabled until the caller's layout enables
	 * it with polo_backend_output_apply (), as DRM always does. Test mode
	 * uses it to run the DRM start-up path headless.
	 */
	gboolean manage_outputs;
	const char *cursor_theme;
	int cursor_size;
} PoloBackendConfig;

/* ---- Layers ------------------------------------------------------------ */

typedef enum {
	POLO_BACKEND_LAYER_BACKGROUND,
	POLO_BACKEND_LAYER_BOTTOM,
	POLO_BACKEND_LAYER_BELOW,
	POLO_BACKEND_LAYER_NORMAL,
	POLO_BACKEND_LAYER_ABOVE,
	POLO_BACKEND_LAYER_DOCK,
	POLO_BACKEND_LAYER_FULLSCREEN,
	POLO_BACKEND_LAYER_OVERRIDE_REDIRECT,
	POLO_BACKEND_LAYER_OVERLAY,
	POLO_BACKEND_LAYER_LOCK,
	POLO_BACKEND_N_LAYERS,
} PoloBackendLayerKind;

/* A compositor layer. Owned by the server; valid until it stops. */
typedef struct _PoloBackendLayer PoloBackendLayer;

/* ---- Object types ------------------------------------------------------- */

#define POLO_BACKEND_TYPE_SERVER (polo_backend_server_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendServer, polo_backend_server, POLO_BACKEND,
		     SERVER, GObject)

#define POLO_BACKEND_TYPE_OUTPUT (polo_backend_output_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendOutput, polo_backend_output, POLO_BACKEND,
		     OUTPUT, GObject)

#define POLO_BACKEND_TYPE_SEAT (polo_backend_seat_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendSeat, polo_backend_seat, POLO_BACKEND, SEAT,
		     GObject)

#define POLO_BACKEND_TYPE_VIEW (polo_backend_view_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendView, polo_backend_view, POLO_BACKEND, VIEW,
		     GObject)

#define POLO_BACKEND_TYPE_TOPLEVEL (polo_backend_toplevel_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendToplevel, polo_backend_toplevel,
		     POLO_BACKEND, TOPLEVEL, GObject)

#define POLO_BACKEND_TYPE_CLIENT (polo_backend_client_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendClient, polo_backend_client, POLO_BACKEND,
		     CLIENT, GObject)

#define POLO_BACKEND_TYPE_WORKSPACE_MANAGER                                    \
	(polo_backend_workspace_manager_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendWorkspaceManager,
		     polo_backend_workspace_manager, POLO_BACKEND,
		     WORKSPACE_MANAGER, GObject)

#define POLO_BACKEND_TYPE_WORKSPACE (polo_backend_workspace_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendWorkspace, polo_backend_workspace, POLO_BACKEND,
		     WORKSPACE, GObject)

#define POLO_BACKEND_TYPE_SHELL_UI (polo_backend_shell_ui_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendShellUi, polo_backend_shell_ui, POLO_BACKEND,
		     SHELL_UI, GObject)

#define POLO_BACKEND_TYPE_POINTER_GRAB (polo_backend_pointer_grab_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendPointerGrab, polo_backend_pointer_grab,
		     POLO_BACKEND, POINTER_GRAB, GObject)

#define POLO_BACKEND_TYPE_LAYER_SURFACE (polo_backend_layer_surface_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendLayerSurface, polo_backend_layer_surface,
		     POLO_BACKEND, LAYER_SURFACE, GObject)

#define POLO_BACKEND_TYPE_PIXEL_BUFFER (polo_backend_pixel_buffer_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendPixelBuffer, polo_backend_pixel_buffer,
		     POLO_BACKEND, PIXEL_BUFFER, GObject)

#define POLO_BACKEND_TYPE_FOREIGN_TOPLEVEL \
	(polo_backend_foreign_toplevel_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendForeignToplevel, polo_backend_foreign_toplevel,
		     POLO_BACKEND, FOREIGN_TOPLEVEL, GObject)

#define POLO_BACKEND_TYPE_INPUT_DEVICE (polo_backend_input_device_get_type())
G_DECLARE_FINAL_TYPE(PoloBackendInputDevice, polo_backend_input_device,
		     POLO_BACKEND, INPUT_DEVICE, GObject)

/* ---- Server ------------------------------------------------------------- */

/*
 * Decide whether @client may see the global @interface_name
 * (wl_display_set_global_filter). Called often; must not block.
 */
typedef gboolean (*PoloBackendGlobalFilter)(PoloBackendClient *client,
					    const char *interface_name,
					    gpointer user_data);

PoloBackendServer *
polo_backend_server_new(void);

/*
 * Create the display and compositor, load the backend and open the socket.
 * Signals for outputs and seats that exist at start-up are emitted before
 * this returns. Raises POLO_BACKEND_ERROR.
 */
gboolean
polo_backend_server_start(PoloBackendServer *server,
			  const PoloBackendConfig *config, GError **error);

/* Dispatch Wayland events from @context. */
void
polo_backend_server_attach(PoloBackendServer *server, GMainContext *context);

/*
 * Destroy clients, the compositor and the display. Emits "destroyed" on
 * every handle first. Safe to call twice.
 */
void
polo_backend_server_stop(PoloBackendServer *server);

const char *
polo_backend_server_get_socket_name(PoloBackendServer *server);

/* A zwp_idle_inhibitor_v1 is on a surface mapped on an output. */
gboolean
polo_backend_server_get_idle_inhibited(PoloBackendServer *server);

/* Valid until the server stops. */
PoloBackendLayer *
polo_backend_server_get_layer(PoloBackendServer *server,
			      PoloBackendLayerKind kind);

/* xdg_activation_v1: what a client asked for when committing a token. */
typedef struct {
	const char *app_id;
	gboolean has_surface;
	PoloBackendToplevel *toplevel;
	gboolean input_valid;
	guint32 input_time_msec;
	gboolean surface_focused;
} PoloBackendActivationRequest;

/* Returns the token name (g_free'd by the caller). */
typedef char *(*PoloBackendActivationMint)(
	const PoloBackendActivationRequest *request, gpointer user_data);

typedef void (*PoloBackendActivationActivate)(const char *token,
					      PoloBackendToplevel *toplevel,
					      gpointer user_data);

void
polo_backend_server_set_activation_mint(PoloBackendServer *server,
					PoloBackendActivationMint mint,
					gpointer user_data,
					GDestroyNotify destroy);

void
polo_backend_server_set_activation_activate(
	PoloBackendServer *server, PoloBackendActivationActivate activate,
	gpointer user_data, GDestroyNotify destroy);

/* The compositor clock input event times use, in milliseconds. */
guint32
polo_backend_server_get_time_msec(PoloBackendServer *server);

/*
 * wl_keyboard.repeat_info for every keyboard, bound now or later: rate in
 * keys per second (0 turns repeat off), delay in milliseconds.
 */
void
polo_backend_server_set_keyboard_repeat(PoloBackendServer *server, int rate,
					int delay);

/*
 * Signals: "output-added" (PoloBackendOutput), "output-removed",
 * "outputs-changed", "seat-added" (PoloBackendSeat), "system-bell" (PoloBackendToplevel* or NULL),
 * "new-toplevel"
 * (PoloBackendToplevel), "new-layer-surface" (PoloBackendLayerSurface), "shutdown".
 */
void
polo_backend_server_set_global_filter(PoloBackendServer *server,
				      PoloBackendGlobalFilter filter,
				      gpointer user_data,
				      GDestroyNotify destroy);

/*
 * Create a connected client socket pair. Returns the fd to hand
 * to the child as WAYLAND_SOCKET (the caller owns it) and stores the
 * server-side client in @client (unowned; valid until its "destroyed").
 * Returns -1 on error.
 */
int
polo_backend_server_create_client_socket(PoloBackendServer *server,
					 PoloBackendClient **client,
					 GError **error);

/* ---- Layer -------------------------------------------------------------- */

/*
 * Reorder @views (bottom to top) as the topmost views of @layer. Views not
 * yet in the layer are added.
 */
void
polo_backend_layer_restack(PoloBackendLayer *layer, PoloBackendView **views,
			   int n_views);

/* ---- Output ------------------------------------------------------------- */

const char *
polo_backend_output_get_name(PoloBackendOutput *output);

/* Logical (global) geometry. */
void
polo_backend_output_get_geometry(PoloBackendOutput *output, int *x, int *y,
				 int *width, int *height);

int
polo_backend_output_get_scale(PoloBackendOutput *output);

/*
 * Paint a solid background. The output is not repainted at all until it has
 * a background.
 */
void
polo_backend_output_set_work_area(PoloBackendOutput *output, int x, int y,
				  int width, int height);

void
polo_backend_output_set_primary(PoloBackendOutput *output, gboolean primary);

void
polo_backend_output_set_background_color(PoloBackendOutput *output,
					 double red, double green,
					 double blue);

/* ---- Input events and filters ------------------------------------------- */

typedef enum {
	POLO_BACKEND_FILTER_PASS,
	POLO_BACKEND_FILTER_CONSUME,
} PoloBackendFilterResult;

/*
 * A pointer button event, crossing the boundary by value. @view is the view
 * under the pointer (unowned, may be NULL); for subsurfaces it is the view
 * of the main surface.
 */
typedef struct {
	guint32 time_msec;
	guint32 button;    /* evdev code, e.g. BTN_LEFT */
	gboolean pressed;
	double x;          /* global logical coordinates */
	double y;
	guint32 modifiers; /* weston_keyboard_modifier mask */
	PoloBackendView *view;
} PoloBackendButtonEvent;

/*
 * Called for every button event before it reaches clients. CONSUME stops
 * delivery; a filter that consumes a press must also consume its release.
 */
/* A scroll: axis 0 is vertical, 1 horizontal (wl_pointer.axis). */
typedef struct {
	guint32 time_msec;
	guint32 axis;
	double value;
	gboolean has_discrete;
	gint32 discrete;
	double x;
	double y;
	guint32 modifiers;
	PoloBackendView *view;
} PoloBackendAxisEvent;

typedef PoloBackendFilterResult (*PoloBackendAxisFilter)(
	PoloBackendSeat *seat, const PoloBackendAxisEvent *event,
	gpointer user_data);

void
polo_backend_seat_set_axis_filter(PoloBackendSeat *seat,
				  PoloBackendAxisFilter filter,
				  gpointer user_data, GDestroyNotify destroy);

/* Real modifiers, in the X11 order. */
typedef enum {
	POLO_BACKEND_MOD_SHIFT = 1 << 0,
	POLO_BACKEND_MOD_LOCK = 1 << 1,
	POLO_BACKEND_MOD_CONTROL = 1 << 2,
	POLO_BACKEND_MOD_MOD1 = 1 << 3,
	POLO_BACKEND_MOD_MOD2 = 1 << 4,
	POLO_BACKEND_MOD_MOD3 = 1 << 5,
	POLO_BACKEND_MOD_MOD4 = 1 << 6,
	POLO_BACKEND_MOD_MOD5 = 1 << 7,
} PoloBackendModifiers;

/*
 * A key press or release. keysym is the symbol at the active layout and
 * level, base_keysym the key's symbol in the first layout at the first
 * level; modifiers are the effective ones before this key.
 */
typedef struct {
	guint32 time_msec;
	guint32 keycode;
	guint32 keysym;
	guint32 base_keysym;
	guint32 modifiers;
	gboolean pressed;
} PoloBackendKeyEvent;

/* A filter that consumes a press gets its release consumed too. */
typedef PoloBackendFilterResult (*PoloBackendKeyFilter)(
	PoloBackendSeat *seat, const PoloBackendKeyEvent *event,
	gpointer user_data);

void
polo_backend_seat_set_key_filter(PoloBackendSeat *seat,
				 PoloBackendKeyFilter filter,
				 gpointer user_data, GDestroyNotify destroy);

/* xkb_keysym_from_name (); 0 for an unknown name. */
guint32
polo_backend_keysym_from_name(const char *name);

guint32
polo_backend_keysym_to_lower(guint32 keysym);

typedef PoloBackendFilterResult (*PoloBackendButtonFilter)(
	PoloBackendSeat *seat, PoloBackendButtonEvent *event,
	gpointer user_data);

/* ---- Seat --------------------------------------------------------------- */

const char *
polo_backend_seat_get_name(PoloBackendSeat *seat);

void
polo_backend_seat_get_pointer_position(PoloBackendSeat *seat, double *x,
				       double *y);

void
polo_backend_seat_warp_pointer(PoloBackendSeat *seat, double x, double y);

/*
 * The keyboard's effective real modifiers, as in key events. Key events
 * carry the state before the key; "modifiers-changed" (guint32) gives the
 * state after each change.
 */
guint32
polo_backend_seat_get_modifiers(PoloBackendSeat *seat);

/*
 * Compiles the keymap from RMLVO names (NULL: xkbcommon's default) and gives
 * it to the seat's keyboard, now and whenever the seat gets one.
 */
gboolean
polo_backend_seat_set_keymap(PoloBackendSeat *seat, const char *rules,
			     const char *model, const char *layout,
			     const char *variant, const char *options,
			     GError **error);

/*
 * Whether the XKB registry (evdev rules, extras included) lists the layout,
 * or the layout's variant when variant is not NULL. TRUE when the registry
 * cannot be read.
 */
gboolean
polo_backend_keymap_has_layout(const char *layout, const char *variant);

/* ---- Input devices (DRM only) --------------------------------------------
 *
 * Server signal "input-device-added" (PoloBackendInputDevice); the device's
 * "removed" follows when it goes. Setters return whether libinput took the
 * value.
 */

typedef enum {
	POLO_BACKEND_DEVICE_KEYBOARD,
	POLO_BACKEND_DEVICE_POINTER,
	POLO_BACKEND_DEVICE_TOUCH,
	POLO_BACKEND_DEVICE_TABLET_TOOL,
} PoloBackendDeviceCapability;

/* What the device can configure. */
typedef enum {
	POLO_BACKEND_DEVICE_LEFT_HANDED = 1 << 0,
	POLO_BACKEND_DEVICE_ACCEL = 1 << 1,
	POLO_BACKEND_DEVICE_ACCEL_PROFILE = 1 << 2,
	POLO_BACKEND_DEVICE_MIDDLE_EMULATION = 1 << 3,
	POLO_BACKEND_DEVICE_DWT = 1 << 4,
	POLO_BACKEND_DEVICE_TAP = 1 << 5,
	POLO_BACKEND_DEVICE_CLICK_METHOD = 1 << 6,
	POLO_BACKEND_DEVICE_SCROLL_METHOD = 1 << 7,
	POLO_BACKEND_DEVICE_NATURAL_SCROLL = 1 << 8,
	POLO_BACKEND_DEVICE_SEND_EVENTS = 1 << 9,
} PoloBackendDeviceFeatures;

typedef enum {
	POLO_BACKEND_ACCEL_PROFILE_DEFAULT,
	POLO_BACKEND_ACCEL_PROFILE_ADAPTIVE,
	POLO_BACKEND_ACCEL_PROFILE_FLAT,
} PoloBackendAccelProfile;

typedef enum {
	POLO_BACKEND_CLICK_METHOD_NONE,
	POLO_BACKEND_CLICK_METHOD_BUTTON_AREAS,
	POLO_BACKEND_CLICK_METHOD_CLICKFINGER,
} PoloBackendClickMethod;

typedef enum {
	POLO_BACKEND_SCROLL_METHOD_NONE,
	POLO_BACKEND_SCROLL_METHOD_TWO_FINGER,
	POLO_BACKEND_SCROLL_METHOD_EDGE,
	POLO_BACKEND_SCROLL_METHOD_ON_BUTTON_DOWN,
} PoloBackendScrollMethod;

const char *
polo_backend_input_device_get_name(PoloBackendInputDevice *device);

gboolean
polo_backend_input_device_has_capability(PoloBackendInputDevice *device,
					 PoloBackendDeviceCapability cap);

/* Has tap fingers: what xf86-input-libinput makes an XI touchpad. */
gboolean
polo_backend_input_device_is_touchpad(PoloBackendInputDevice *device);

/* PoloBackendDeviceFeatures; 0 once removed. */
guint32
polo_backend_input_device_get_features(PoloBackendInputDevice *device);

gboolean
polo_backend_input_device_set_left_handed(PoloBackendInputDevice *device,
					  gboolean left_handed);
/* libinput's -1..1. */
gboolean
polo_backend_input_device_set_accel_speed(PoloBackendInputDevice *device,
					  double speed);
gboolean
polo_backend_input_device_set_accel_profile(PoloBackendInputDevice *device,
					    PoloBackendAccelProfile profile);
gboolean
polo_backend_input_device_set_middle_emulation(PoloBackendInputDevice *device,
					       gboolean enabled);
gboolean
polo_backend_input_device_set_dwt(PoloBackendInputDevice *device,
				  gboolean enabled);
gboolean
polo_backend_input_device_set_tap(PoloBackendInputDevice *device,
				  gboolean enabled);
gboolean
polo_backend_input_device_set_click_method(PoloBackendInputDevice *device,
					   PoloBackendClickMethod method);
gboolean
polo_backend_input_device_set_scroll_method(PoloBackendInputDevice *device,
					    PoloBackendScrollMethod method);
gboolean
polo_backend_input_device_set_natural_scroll(PoloBackendInputDevice *device,
					     gboolean enabled);
gboolean
polo_backend_input_device_set_send_events(PoloBackendInputDevice *device,
					  gboolean enabled);

/* Locks the keyboard's layout group; "group-changed" follows. */
void
polo_backend_seat_lock_group(PoloBackendSeat *seat, guint32 group);

guint32
polo_backend_seat_get_group(PoloBackendSeat *seat);

/* Also applied to a keyboard the seat gets later. */
void
polo_backend_seat_set_numlock(PoloBackendSeat *seat, gboolean locked);

/* Give keyboard focus to @view's surface, or to nothing. */
void
polo_backend_seat_set_keyboard_focus(PoloBackendSeat *seat,
				     PoloBackendView *view);

void
polo_backend_seat_set_button_filter(PoloBackendSeat *seat,
				    PoloBackendButtonFilter filter,
				    gpointer user_data,
				    GDestroyNotify destroy);

/*
 * Start a pointer grab for an interactive move or resize requested with
 * @serial on @view (xdg_toplevel.move/resize). Returns NULL, without
 * starting anything, unless a button is held, @serial is the serial of that
 * press and the pointer is over @view: protocol correctness stays here.
 * The grab holds its own reference until it ends.
 */
PoloBackendPointerGrab *
polo_backend_seat_start_pointer_grab(PoloBackendSeat *seat, guint32 serial,
				     PoloBackendView *view);

/*
 * A pointer grab for a compositor operation without a held button (the
 * keyboard move and resize). NULL while another grab is active.
 */
PoloBackendPointerGrab *
polo_backend_seat_begin_pointer_grab(PoloBackendSeat *seat);

/* Only valid during a filtered pressed-button callback; the press stays
 * compositor-owned and has not yet acquired a client-visible serial. */
PoloBackendPointerGrab *
polo_backend_seat_start_pointer_grab_for_binding(PoloBackendSeat *seat,
						 PoloBackendView *view);

/* ---- Pointer grab ------------------------------------------------------- */

/*
 * Signals: "motion" (double x, double y, guint time_msec),
 * "button" (guint button, gboolean pressed, guint buttons_held),
 * "ended" (gboolean cancelled). After "ended" the grab is inert.
 */
void
polo_backend_pointer_grab_end(PoloBackendPointerGrab *grab);

/* ---- View --------------------------------------------------------------- */

/* Position of the surface origin, global logical coordinates. */
void
polo_backend_view_set_position(PoloBackendView *view, double x, double y);

void
polo_backend_view_get_position(PoloBackendView *view, double *x, double *y);

/* Put the view on top of @layer (NULL: remove it from any layer). */
void
polo_backend_view_move_to_layer(PoloBackendView *view,
				PoloBackendLayer *layer);

/* The toplevel this view shows, or NULL (unowned). */
PoloBackendToplevel *
polo_backend_view_get_toplevel(PoloBackendView *view);

/* The client whose surface this view shows (unowned), or NULL. */
PoloBackendClient *
polo_backend_view_get_client(PoloBackendView *view);

/* ---- Toplevel (xdg_toplevel via libweston-desktop) ---------------------- */

/* Matches enum weston_desktop_surface_edge. */
typedef enum {
	POLO_BACKEND_EDGE_NONE = 0,
	POLO_BACKEND_EDGE_TOP = 1,
	POLO_BACKEND_EDGE_BOTTOM = 2,
	POLO_BACKEND_EDGE_LEFT = 4,
	POLO_BACKEND_EDGE_RIGHT = 8,
} PoloBackendEdges;

/*
 * Signals: "committed", "request-move" (PoloBackendSeat*, guint serial),
 * "request-resize" (PoloBackendSeat*, guint serial, PoloBackendEdges),
 * "request-maximize" (gboolean), "request-fullscreen" (gboolean),
 * "request-minimize", "modal-hint-changed" (gboolean, xdg_dialog_v1),
 * "parent-changed" (PoloBackendToplevel* or NULL,
 * from xdg_toplevel.set_parent),
 * "request-window-menu" (PoloBackendSeat*, double x,
 * double y), "ping-timeout", "pong", "destroyed".
 * Properties "title" and "app-id" notify when the client changes them.
 */

const char *
polo_backend_toplevel_get_title(PoloBackendToplevel *toplevel);

const char *
polo_backend_toplevel_get_app_id(PoloBackendToplevel *toplevel);

int
polo_backend_toplevel_get_pid(PoloBackendToplevel *toplevel);

/* The parent toplevel (xdg_toplevel.set_parent), or NULL (unowned). */
PoloBackendToplevel *
polo_backend_toplevel_get_parent(PoloBackendToplevel *toplevel);

/* Window geometry relative to the surface origin (xdg_surface). */
void
polo_backend_toplevel_get_window_geometry(PoloBackendToplevel *toplevel,
					  int *x, int *y, int *width,
					  int *height);

/* 0 means unconstrained. */
void
polo_backend_toplevel_get_min_size(PoloBackendToplevel *toplevel,
				   int *width, int *height);

void
polo_backend_toplevel_get_max_size(PoloBackendToplevel *toplevel,
				   int *width, int *height);

/* Whether the surface has a buffer and can be shown. */
gboolean
polo_backend_toplevel_has_content(PoloBackendToplevel *toplevel);

gboolean
polo_backend_toplevel_is_mapped(PoloBackendToplevel *toplevel);

/* Map the surface (its view must be placed in a layer first). */
void
polo_backend_toplevel_map(PoloBackendToplevel *toplevel);

/* Valid until "destroyed". */
PoloBackendView *
polo_backend_toplevel_get_view(PoloBackendToplevel *toplevel);

PoloBackendClient *
polo_backend_toplevel_get_client(PoloBackendToplevel *toplevel);

/* Send a configure with this window-geometry size (0x0: client chooses). */
void
polo_backend_toplevel_configure_size(PoloBackendToplevel *toplevel, int width,
				     int height);

void
polo_backend_toplevel_set_activated(PoloBackendToplevel *toplevel,
				    gboolean activated);

void
polo_backend_toplevel_set_maximized(PoloBackendToplevel *toplevel,
				    gboolean maximized);

void
polo_backend_toplevel_set_fullscreen(PoloBackendToplevel *toplevel,
				     gboolean fullscreen);

void
polo_backend_toplevel_set_resizing(PoloBackendToplevel *toplevel,
				   gboolean resizing);

void
polo_backend_toplevel_set_tiled(PoloBackendToplevel *toplevel,
				PoloBackendEdges edges);

/* Ask the client to close (xdg_toplevel.close). */
void
polo_backend_toplevel_close(PoloBackendToplevel *toplevel);

void
polo_backend_toplevel_ping(PoloBackendToplevel *toplevel);

/* ---- Layer surface (zwlr_layer_shell_v1 v5) ---------------------------- */

/*
 * A zwlr_layer_surface_v1. The C side owns the protocol mechanics:
 * double-buffered state applied at wl_surface.commit, configure serials and
 * acks, validation errors, get_popup plumbing. Placement, exclusive-zone
 * struts, keyboard focus and output assignment are the Vala arranger's job
 * (src/shell/layer-shell.vala), which observes "committed" and acts through
 * configure(), set_output() and the view.
 *
 * Signals: "committed" (state was applied at a wl_surface.commit; the
 * arranger must (re)position the view, send a configure when the assigned
 * size changed and update exclusive-zone struts), "new-popup"
 * (PoloBackendView of an xdg_popup attached through get_popup), "destroyed".
 * Properties: "namespace", "layer" (zwlr_layer_shell_v1.layer),
 * "anchor" (zwlr_layer_surface_v1.anchor bits), "exclusive-zone",
 * "exclusive-edge" (anchor bit, 0 when unset), "margin-top/right/bottom/left",
 * "keyboard-interactivity", "desired-width/height" (0: compositor decides),
 * "mapped", "output" (read-write; the arranger assigns it),
 * "requested-output" (from get_layer_surface, may be NULL).
 *
 * Layers map onto PoloBackendLayerKind: background ->
 * POLO_BACKEND_LAYER_BACKGROUND, bottom -> BOTTOM, top -> DOCK, overlay ->
 * OVERLAY.
 */

/* Valid until "destroyed"; the surface's own view. */
PoloBackendView *
polo_backend_layer_surface_get_view(PoloBackendLayerSurface *surface);

const char *
polo_backend_layer_surface_get_namespace(PoloBackendLayerSurface *surface);

/* The output the arranger assigned, or NULL until it does. */
PoloBackendOutput *
polo_backend_layer_surface_get_output(PoloBackendLayerSurface *surface);

void
polo_backend_layer_surface_set_output(PoloBackendLayerSurface *surface,
				      PoloBackendOutput *output);

/* Send a configure with this desired size (0: client decides). Sending the
 * same size as the last pending configure is a no-op. */
void
polo_backend_layer_surface_configure(PoloBackendLayerSurface *surface,
				     guint32 width, guint32 height);

/* Send zwlr_layer_surface_v1.closed and ignore the surface from then on;
 * the client is expected to destroy it. */
void
polo_backend_layer_surface_close(PoloBackendLayerSurface *surface);

gboolean
polo_backend_layer_surface_is_mapped(PoloBackendLayerSurface *surface);

/* The actual surface size in surface-local coordinates. */
void
polo_backend_layer_surface_get_size(PoloBackendLayerSurface *surface,
				    int *width, int *height);

/* Property getters (the Vala vapi points the properties at these). */

guint
polo_backend_layer_surface_get_layer(PoloBackendLayerSurface *surface);

guint
polo_backend_layer_surface_get_anchor(PoloBackendLayerSurface *surface);

int
polo_backend_layer_surface_get_exclusive_zone(PoloBackendLayerSurface *surface);

guint
polo_backend_layer_surface_get_exclusive_edge(PoloBackendLayerSurface *surface);

int
polo_backend_layer_surface_get_margin_top(PoloBackendLayerSurface *surface);

int
polo_backend_layer_surface_get_margin_right(PoloBackendLayerSurface *surface);

int
polo_backend_layer_surface_get_margin_bottom(PoloBackendLayerSurface *surface);

int
polo_backend_layer_surface_get_margin_left(PoloBackendLayerSurface *surface);

guint
polo_backend_layer_surface_get_keyboard_interactivity(
	PoloBackendLayerSurface *surface);

guint
polo_backend_layer_surface_get_desired_width(PoloBackendLayerSurface *surface);

guint
polo_backend_layer_surface_get_desired_height(PoloBackendLayerSurface *surface);

gboolean
polo_backend_layer_surface_get_mapped(PoloBackendLayerSurface *surface);

PoloBackendOutput *
polo_backend_layer_surface_get_requested_output(
	PoloBackendLayerSurface *surface);

/* ---- Pixel buffer ------------------------------------------------------ */

/*
 * A surface Polo draws itself: width × height buffer pixels at a buffer
 * scale, ARGB32 premultiplied (cairo's CAIRO_FORMAT_ARGB32). Draw into
 * begin ()'s memory (from any thread), then commit () on the main thread.
 * The view exists once "ready" has been emitted; it takes no input.
 */
PoloBackendPixelBuffer *
polo_backend_pixel_buffer_new(PoloBackendServer *server, int width,
			      int height, int scale);

gboolean
polo_backend_pixel_buffer_is_ready(PoloBackendPixelBuffer *buffer);

/* NULL until "ready" (the view exists and has its first frame). */
PoloBackendView *
polo_backend_pixel_buffer_get_view(PoloBackendPixelBuffer *buffer);

void
polo_backend_pixel_buffer_get_size(PoloBackendPixelBuffer *buffer,
				   int *width, int *height, int *scale);

guint8 *
polo_backend_pixel_buffer_begin(PoloBackendPixelBuffer *buffer, int *length,
				int *stride);

void
polo_backend_pixel_buffer_commit(PoloBackendPixelBuffer *buffer);

void
polo_backend_pixel_buffer_commit_damage(PoloBackendPixelBuffer *buffer, int x,
					int y, int width, int height);

/* ---- Foreign toplevel (zwlr_foreign_toplevel_manager_v1 v3) ------------ */

/*
 * One window as task bars see it. Vala creates one per listed window
 * (src/protocols/foreign-toplevel.vala), sets its title, app id, state,
 * outputs and parent, ends each batch with done () and calls close () when
 * the window goes away. The C side sends that to every manager resource,
 * including ones bound later, and turns client requests into signals:
 * "request-maximize" (gboolean), "request-minimize" (gboolean),
 * "request-activate", "request-close" and "request-fullscreen" (gboolean,
 * PoloBackendOutput or NULL).
 */
typedef enum {
	POLO_BACKEND_FOREIGN_TOPLEVEL_STATE_NONE = 0,
	POLO_BACKEND_FOREIGN_TOPLEVEL_STATE_MAXIMIZED = 1 << 0,
	POLO_BACKEND_FOREIGN_TOPLEVEL_STATE_MINIMIZED = 1 << 1,
	POLO_BACKEND_FOREIGN_TOPLEVEL_STATE_ACTIVATED = 1 << 2,
	POLO_BACKEND_FOREIGN_TOPLEVEL_STATE_FULLSCREEN = 1 << 3,
} PoloBackendForeignToplevelState;

typedef enum {
	POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_NONE = 0,
	POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_SHADED = 1 << 0,
	POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_SKIP_PAGER = 1 << 1,
	POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_SKIP_TASKLIST = 1 << 2,
	POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_STICKY = 1 << 3,
	POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_ABOVE = 1 << 4,
	POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_BELOW = 1 << 5,
	POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_DEMANDS_ATTENTION = 1 << 6,
} PoloBackendForeignToplevelExtraState;

PoloBackendForeignToplevel *
polo_backend_foreign_toplevel_new(PoloBackendServer *server);

void
polo_backend_foreign_toplevel_set_title(PoloBackendForeignToplevel *toplevel,
					const char *title);

void
polo_backend_foreign_toplevel_set_app_id(PoloBackendForeignToplevel *toplevel,
					 const char *app_id);

void
polo_backend_foreign_toplevel_set_state(PoloBackendForeignToplevel *toplevel,
					PoloBackendForeignToplevelState state);

void
polo_backend_foreign_toplevel_set_extra_state(
	PoloBackendForeignToplevel *toplevel,
	PoloBackendForeignToplevelExtraState state);

/* The outputs the window is on; the array is copied. */
void
polo_backend_foreign_toplevel_set_outputs(PoloBackendForeignToplevel *toplevel,
					  PoloBackendOutput **outputs,
					  int n_outputs);

void
polo_backend_foreign_toplevel_set_parent(PoloBackendForeignToplevel *toplevel,
					 PoloBackendForeignToplevel *parent);

void
polo_backend_foreign_toplevel_done(PoloBackendForeignToplevel *toplevel);

void
polo_backend_foreign_toplevel_close(PoloBackendForeignToplevel *toplevel);

/* The workspaces it is on; task bars see workspace_enter and workspace_leave. */
void
polo_backend_foreign_toplevel_set_workspaces(
	PoloBackendForeignToplevel *toplevel, PoloBackendWorkspace **workspaces,
	int n_workspaces);

/* The latest set_rectangle in global coordinates (the minimize target). */
gboolean
polo_backend_foreign_toplevel_get_rectangle(
	PoloBackendForeignToplevel *toplevel, int *x, int *y, int *width,
	int *height);

/* ---- Client ------------------------------------------------------------- */

/* Signal: "destroyed". */

int
polo_backend_client_get_pid(PoloBackendClient *client);

guint
polo_backend_client_get_uid(PoloBackendClient *client);

/* Path of the client executable, or NULL if unknown. */
const char *
polo_backend_client_get_executable(PoloBackendClient *client);

gboolean
polo_backend_client_get_trusted(PoloBackendClient *client);

gboolean
polo_backend_client_get_xserver(PoloBackendClient *client);

/* wp_security_context_v1. The engine is NULL for a client outside any context. */
void
polo_backend_client_set_sandbox(PoloBackendClient *client, const char *engine,
				const char *app_id, const char *instance_id);

const char *
polo_backend_client_get_sandbox_engine(PoloBackendClient *client);

const char *
polo_backend_client_get_sandbox_app_id(PoloBackendClient *client);

const char *
polo_backend_client_get_sandbox_instance_id(PoloBackendClient *client);

void
polo_backend_client_set_xserver(PoloBackendClient *client);

void
polo_backend_client_set_trusted(PoloBackendClient *client, gboolean trusted);

/*
 * The process at the other end of a socket pair: its credentials name the
 * process that made the pair, not the child it was handed to.
 */
void
polo_backend_client_set_process(PoloBackendClient *client, int pid);

/* ---- ext_workspace_manager_v1 ------------------------------------------- */

/* Signal: "create-workspace" (name). Requests arrive at the client's commit. */
PoloBackendWorkspaceManager *
polo_backend_server_get_workspace_manager(PoloBackendServer *server);

/* Owned by the manager until polo_backend_workspace_remove (). */
PoloBackendWorkspace *
polo_backend_workspace_manager_add(PoloBackendWorkspaceManager *manager,
				   const char *id, const char *name, guint col,
				   guint row, guint state, guint capabilities);

void
polo_backend_workspace_manager_done(PoloBackendWorkspaceManager *manager);

/* Signals: "activate", "remove". */
void
polo_backend_workspace_set_name(PoloBackendWorkspace *workspace,
				const char *name);

void
polo_backend_workspace_set_coordinates(PoloBackendWorkspace *workspace,
				       guint col, guint row);

void
polo_backend_workspace_set_state(PoloBackendWorkspace *workspace, guint state);

void
polo_backend_workspace_remove(PoloBackendWorkspace *workspace);

/* ---- polo_shell_ui_v1 ---------------------------------------------------- */

/*
 * The polo-shell-ui end of the protocol. Events are dropped while nothing is
 * bound. Signals: "bound" (PoloBackendClient), "unbound", "switcher-select"
 * (entry id), "menu-action"
 * (window id, PoloBackendMenuOp, workspace), "dialog-response" (dialog id,
 * response, text or NULL), "color-table" (GBytes of 160 floats).
 */

typedef enum {
	POLO_BACKEND_SWITCHER_ENTRY_BLANK = 1 << 0,
	POLO_BACKEND_SWITCHER_ENTRY_HIDDEN = 1 << 1,
	POLO_BACKEND_SWITCHER_ENTRY_DEMANDS_ATTENTION = 1 << 2,
} PoloBackendSwitcherEntryFlags;

/* Marco's MetaMenuOp. */
typedef enum {
	POLO_BACKEND_MENU_OP_NONE = 0,
	POLO_BACKEND_MENU_OP_DELETE = 1 << 0,
	POLO_BACKEND_MENU_OP_MINIMIZE = 1 << 1,
	POLO_BACKEND_MENU_OP_UNMAXIMIZE = 1 << 2,
	POLO_BACKEND_MENU_OP_MAXIMIZE = 1 << 3,
	POLO_BACKEND_MENU_OP_UNSHADE = 1 << 4,
	POLO_BACKEND_MENU_OP_SHADE = 1 << 5,
	POLO_BACKEND_MENU_OP_UNSTICK = 1 << 6,
	POLO_BACKEND_MENU_OP_STICK = 1 << 7,
	POLO_BACKEND_MENU_OP_WORKSPACES = 1 << 8,
	POLO_BACKEND_MENU_OP_MOVE = 1 << 9,
	POLO_BACKEND_MENU_OP_RESIZE = 1 << 10,
	POLO_BACKEND_MENU_OP_ABOVE = 1 << 11,
	POLO_BACKEND_MENU_OP_UNABOVE = 1 << 12,
	POLO_BACKEND_MENU_OP_MOVE_LEFT = 1 << 13,
	POLO_BACKEND_MENU_OP_MOVE_RIGHT = 1 << 14,
	POLO_BACKEND_MENU_OP_MOVE_UP = 1 << 15,
	POLO_BACKEND_MENU_OP_MOVE_DOWN = 1 << 16,
	POLO_BACKEND_MENU_OP_RECOVER = 1 << 17,
} PoloBackendMenuOp;

typedef enum {
	POLO_BACKEND_OSD_WORKSPACE,
	POLO_BACKEND_OSD_VOLUME,
	POLO_BACKEND_OSD_BRIGHTNESS,
	POLO_BACKEND_OSD_MESSAGE,
} PoloBackendOsdKind;

typedef enum {
	POLO_BACKEND_DIALOG_NOT_RESPONDING,
	POLO_BACKEND_DIALOG_RENAME_WORKSPACE,
	POLO_BACKEND_DIALOG_END_SESSION,
	POLO_BACKEND_DIALOG_MESSAGE,
} PoloBackendDialogKind;

PoloBackendShellUi *
polo_backend_server_get_shell_ui(PoloBackendServer *server);

gboolean
polo_backend_shell_ui_is_bound(PoloBackendShellUi *shell_ui);

/* The bound client, or NULL. */
PoloBackendClient *
polo_backend_shell_ui_get_client(PoloBackendShellUi *shell_ui);

void
polo_backend_shell_ui_switcher_entry(PoloBackendShellUi *shell_ui, guint id,
				     const char *title, const char *app_id,
				     PoloBackendSwitcherEntryFlags flags);

/*
 * The picture of @view's surface, scaled to @landscape_size wide if it is
 * wider than tall, else @portrait_size high. FALSE if nothing was sent.
 */
gboolean
polo_backend_shell_ui_switcher_thumbnail(PoloBackendShellUi *shell_ui,
					 guint id, PoloBackendView *view,
					 double landscape_size,
					 double portrait_size);

/* The icon from xdg_toplevel_icon_v1 for entry @id, if the toplevel has one. */
void
polo_backend_shell_ui_switcher_icon(PoloBackendShellUi *shell_ui, guint id,
				    PoloBackendToplevel *toplevel);

/* @output may be NULL. */
void
polo_backend_shell_ui_show_switcher(PoloBackendShellUi *shell_ui,
				    PoloBackendOutput *output, guint selected,
				    guint columns, gboolean expand_to_fit,
				    guint icon_size);

void
polo_backend_shell_ui_update_selected(PoloBackendShellUi *shell_ui,
				      guint selected);

void
polo_backend_shell_ui_hide_switcher(PoloBackendShellUi *shell_ui);

void
polo_backend_shell_ui_workspace_switcher_entry(PoloBackendShellUi *shell_ui,
					       guint id, const char *title,
					       gboolean blank);

void
polo_backend_shell_ui_workspace_switcher_window(PoloBackendShellUi *shell_ui,
						guint id, int x, int y,
						int width, int height,
						gboolean focused,
						const char *app_id);

void
polo_backend_shell_ui_show_workspace_switcher(
	PoloBackendShellUi *shell_ui, PoloBackendOutput *output, guint selected,
	guint columns, guint active, int screen_width, int screen_height,
	int output_width, int output_height, guint icon_size);

void
polo_backend_shell_ui_update_workspace_switcher(PoloBackendShellUi *shell_ui,
						guint selected, guint active);

void
polo_backend_shell_ui_window_menu_workspace(PoloBackendShellUi *shell_ui,
					    guint index, const char *name);

/* @x, @y are relative to @output. */
void
polo_backend_shell_ui_show_window_menu(PoloBackendShellUi *shell_ui,
				       guint window_id,
				       PoloBackendOutput *output, int x, int y,
				       guint ops, guint insensitive,
				       guint active_workspace,
				       guint n_workspaces);

void
polo_backend_shell_ui_hide_window_menu(PoloBackendShellUi *shell_ui,
				       guint window_id);

/* @value: 0..100, or -1. */
void
polo_backend_shell_ui_show_osd(PoloBackendShellUi *shell_ui,
			       PoloBackendOsdKind kind,
			       PoloBackendOutput *output, const char *text,
			       int value);

void
polo_backend_shell_ui_show_dialog(PoloBackendShellUi *shell_ui,
				  guint dialog_id, PoloBackendDialogKind kind,
				  PoloBackendOutput *output,
				  const char *payload);

void
polo_backend_shell_ui_hide_dialog(PoloBackendShellUi *shell_ui,
				  guint dialog_id);

/* The shell UI replies through custom-color with four RGBA floats or NULL. */
void
polo_backend_shell_ui_lookup_color(PoloBackendShellUi *shell_ui,
				   const char *name);

/* The dialog id polo-shell-ui gave @toplevel's surface (dialog_surface), or 0. */
guint
polo_backend_shell_ui_get_dialog(PoloBackendShellUi *shell_ui,
				 PoloBackendToplevel *toplevel);

/* ---- Test protocol (-Dtest-protocol builds only) ----------------------- */

/* What tests can learn about a window: the Vala model, not libweston. */
typedef struct {
	int x;
	int y;
	int width;
	int height;
	int layer;        /* PoloBackendLayerKind */
	int stack_index;  /* 0 = bottom of its layer */
	int workspace;    /* -1: none yet */
	guint32 state;    /* Polo.WindowState flags */
	gboolean focused;
} PoloBackendTestWindowInfo;

typedef gboolean (*PoloBackendTestInspector)(PoloBackendToplevel *toplevel,
					     PoloBackendTestWindowInfo *info,
					     gpointer user_data);

/* Whether this build has the test protocol. */
gboolean
polo_backend_server_has_test_protocol(void);

void
polo_backend_server_set_test_inspector(PoloBackendServer *server,
				       PoloBackendTestInspector inspector,
				       gpointer user_data,
				       GDestroyNotify destroy);

/*
 * polo_test_v1.set_setting (value set) and get_setting (value NULL): the
 * key's value afterwards, newly allocated, or NULL.
 */
typedef char *(*PoloBackendTestSettings)(const char *schema, const char *key,
					 const char *value, gpointer user_data);

void
polo_backend_server_set_test_settings(PoloBackendServer *server,
				      PoloBackendTestSettings settings,
				      gpointer user_data,
				      GDestroyNotify destroy);

typedef struct {
 int width, height, refresh;
 gboolean preferred, current;
} PoloBackendMode;
const char *polo_backend_output_get_make(PoloBackendOutput *self);
const char *polo_backend_output_get_model(PoloBackendOutput *self);
const char *polo_backend_output_get_serial(PoloBackendOutput *self);
char *polo_backend_output_get_vendor(PoloBackendOutput *self);
char *polo_backend_output_get_product(PoloBackendOutput *self);
char *polo_backend_output_get_edid_serial(PoloBackendOutput *self);
void polo_backend_output_get_physical_size(PoloBackendOutput *self, int *width, int *height);
gboolean polo_backend_output_get_internal(PoloBackendOutput *self);
gboolean polo_backend_output_get_enabled(PoloBackendOutput *self);
int polo_backend_output_get_transform(PoloBackendOutput *self);
int polo_backend_output_get_mode_count(PoloBackendOutput *self);
gboolean polo_backend_output_get_mode(PoloBackendOutput *self, int index, PoloBackendMode *mode);
gboolean polo_backend_output_apply(PoloBackendOutput *self, int width, int height, int refresh,
 int scale, int transform, int x, int y, gboolean enabled, GError **error);

G_END_DECLS

#endif /* POLO_BACKEND_H */
