/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bindings for libpolo-backend (lib/backend/polo-backend.h).
 *
 * Handles (Server, Output, Seat, View, Toplevel, Client, PointerGrab,
 * LayerSurface) are GObjects. Each emits "destroyed" when its libweston
 * object goes away and is inert afterwards.
 */

[CCode (cheader_filename = "polo-backend.h")]
namespace Polo {
    /* Log scope owned by the log context. Valid until Backend.Log.shutdown (); use unowned. */
    [Compact]
    [CCode (cname = "PoloLogScope", free_function = "", has_type_id = false)]
    public class LogScope {
        public unowned string name {
            [CCode (cname = "polo_log_scope_get_name")] get;
        }

        /* Whether debug () output of this scope is printed. */
        public bool enabled {
            [CCode (cname = "polo_log_scope_is_enabled")] get;
        }

        [PrintfFormat]
        [CCode (cname = "polo_log_scope_debug")]
        public void debug (string format, ...);

        [PrintfFormat]
        [CCode (cname = "polo_log_scope_info")]
        public void info (string format, ...);

        [PrintfFormat]
        [CCode (cname = "polo_log_scope_warning")]
        public void warning (string format, ...);
    }

    namespace Backend {
        [CCode (cname = "polo_backend_libweston_version")]
        public unowned string libweston_version ();

        namespace Log {
            /* A null file logs to stderr. debug_scopes takes comma-separated
             * Polo or libweston scope names. */
            [CCode (cname = "polo_backend_log_init")]
            public bool init (string? file, string? debug_scopes) throws GLib.Error;

            /* Invalidates every LogScope. */
            [CCode (cname = "polo_backend_log_shutdown")]
            public void shutdown ();

            /* Creates the named scope on first use; never null. */
            [CCode (cname = "polo_backend_log_scope")]
            public unowned Polo.LogScope scope (string name);
        }

        /* Logs main-loop stalls, aborts after POLO_WATCHDOG_SECONDS (15 when unset, 0 never)
         * without progress. Without by_default, only when that variable is set. */
        [CCode (cname = "polo_backend_watchdog_start")]
        public void start_watchdog (bool by_default);

        [CCode (cname = "polo_backend_stats_effects")]
        public void stats_effects (int64 duration_usec, uint vertices);

        /* Tests only: blocks this thread in the kernel (state D). */
        [CCode (cname = "polo_backend_watchdog_test_block_in_kernel")]
        public void block_in_kernel_for_tests (uint msec);

        [CCode (cname = "PoloBackendError", cprefix = "POLO_BACKEND_ERROR_")]
        public errordomain Error {
            FAILED,
            BACKEND,
            SOCKET;

            [CCode (cname = "polo_backend_error_quark")]
            public static GLib.Quark quark ();
        }

        [CCode (cname = "PoloBackendKind", cprefix = "POLO_BACKEND_KIND_", has_type_id = false)]
        public enum Kind {
            DRM,
            WAYLAND,
            HEADLESS
        }

        [CCode (cname = "PoloBackendRenderer", cprefix = "POLO_BACKEND_RENDERER_", has_type_id = false)]
        public enum Renderer {
            AUTO,
            GL,
            PIXMAN
        }

        [CCode (cname = "PoloBackendLayerKind", cprefix = "POLO_BACKEND_LAYER_", has_type_id = false)]
        public enum LayerKind {
            BACKGROUND,
            BOTTOM,
            BELOW,
            NORMAL,
            ABOVE,
            DOCK,
            FULLSCREEN,
            OVERRIDE_REDIRECT,
            OVERLAY,
            LOCK
        }

        [CCode (cname = "PoloBackendCursor", cprefix = "POLO_BACKEND_CURSOR_", has_type_id = false)]
        public enum Cursor {
            DEFAULT,
            E_RESIZE,
            N_RESIZE,
            NE_RESIZE,
            NW_RESIZE,
            S_RESIZE,
            SE_RESIZE,
            SW_RESIZE,
            W_RESIZE
        }

        [CCode (cname = "PoloBackendFilterResult", cprefix = "POLO_BACKEND_FILTER_", has_type_id = false)]
        public enum FilterResult {
            PASS,
            CONSUME
        }

        /* Values from zwlr_foreign_toplevel_handle_v1.state. */
        [Flags]
        [CCode (cname = "PoloBackendForeignToplevelState", cprefix = "POLO_BACKEND_FOREIGN_TOPLEVEL_STATE_",
                has_type_id = false)]
        public enum ForeignToplevelState {
            NONE,
            MAXIMIZED,
            MINIMIZED,
            ACTIVATED,
            FULLSCREEN
        }

        [Flags]
        [CCode (cname = "PoloBackendForeignToplevelExtraState",
                cprefix = "POLO_BACKEND_FOREIGN_TOPLEVEL_EXTRA_", has_type_id = false)]
        public enum ForeignToplevelExtraState {
            NONE,
            SHADED,
            SKIP_PAGER,
            SKIP_TASKLIST,
            STICKY,
            ABOVE,
            BELOW,
            DEMANDS_ATTENTION
        }

        [Flags]
        [CCode (cname = "PoloBackendEdges", cprefix = "POLO_BACKEND_EDGE_", has_type_id = false)]
        public enum Edges {
            NONE,
            TOP,
            BOTTOM,
            LEFT,
            RIGHT
        }

        /* Start-up settings. Strings are borrowed only by Server.start (). */
        [CCode (cname = "PoloBackendConfig", has_type_id = false, has_copy_function = false,
                has_destroy_function = false)]
        public struct Config {
            public Kind kind;
            public Renderer renderer;
            public int width;
            public int height;
            public int scale;
            public int output_count;
            public unowned string? scales;
            public bool fullscreen;
            public unowned string? socket_name;
            public unowned string? seat;
            public unowned string? xkb_model;
            public unowned string? xkb_layout;
            public unowned string? xkb_variant;
            public unowned string? xkb_options;
            public bool test_mode;
            public bool debug_kill;
            /* New outputs remain disabled until Output.apply (), as on DRM. */
            public bool manage_outputs;
            public unowned string? cursor_theme;
            public int cursor_size;
        }

        [CCode (cname = "PoloBackendButtonEvent", has_type_id = false, has_copy_function = false,
                has_destroy_function = false)]
        public struct ButtonEvent {
            public uint32 time_msec;
            public uint32 button;
            public bool pressed;
            public double x;
            public double y;
            public uint32 modifiers;
            /* Main-surface view under the pointer, if any. */
            public unowned View? view;
        }

        /* Window state exposed through the test protocol. */
        [CCode (cname = "PoloBackendTestWindowInfo", has_type_id = false,
                has_copy_function = false, has_destroy_function = false)]
        public struct TestWindowInfo {
            public int x;
            public int y;
            public int width;
            public int height;
            public int layer;
            public int stack_index;
            public int workspace;
            public uint32 state;
            public bool focused;
            public bool lamp_known;
            public int lamp_x;
            public int lamp_y;
            public int lamp_width;
            public int lamp_height;
            public uint lamp_edge;
            public uint lamp_source;
        }

        [CCode (cname = "PoloBackendGlobalFilter", has_target = true)]
        public delegate bool GlobalFilter (Client client, string interface_name);

        /* Pointer-axis event. Axis 0 is vertical; 1 is horizontal. */
        [CCode (cname = "PoloBackendAxisEvent", has_type_id = false, has_copy_function = false,
                has_destroy_function = false)]
        public struct AxisEvent {
            public uint32 time_msec;
            public uint32 axis;
            public double value;
            public bool has_discrete;
            public int32 discrete;
            public double x;
            public double y;
            public uint32 modifiers;
            public unowned View? view;
        }

        [CCode (cname = "PoloBackendAxisFilter", has_target = true)]
        public delegate FilterResult AxisFilter (Seat seat, AxisEvent event);

        /* X11 modifier mask order. */
        [Flags]
        [CCode (cname = "PoloBackendModifiers", cprefix = "POLO_BACKEND_MOD_", has_type_id = false)]
        public enum Modifiers {
            SHIFT,
            LOCK,
            CONTROL,
            MOD1,
            MOD2,
            MOD3,
            MOD4,
            MOD5
        }

        [CCode (cname = "PoloBackendKeyEvent", has_type_id = false, has_copy_function = false,
                has_destroy_function = false)]
        public struct KeyEvent {
            public uint32 time_msec;
            public uint32 keycode;
            /* Resolved at the active layout and level. */
            public uint32 keysym;
            /* Resolved at the first layout and level. */
            public uint32 base_keysym;
            public Modifiers modifiers;
            public bool pressed;
        }

        [CCode (cname = "PoloBackendKeyFilter", has_target = true)]
        public delegate FilterResult KeyFilter (Seat seat, KeyEvent event);

        namespace Keysym {
            /* Returns 0 if name is unknown. */
            [CCode (cname = "polo_backend_keysym_from_name")]
            public uint32 from_name (string name);

            [CCode (cname = "polo_backend_keysym_to_lower")]
            public uint32 to_lower (uint32 keysym);

            [CCode (cname = "polo_backend_keysym_name")]
            public string name (uint32 keysym);
        }

        [CCode (cname = "PoloBackendButtonFilter", has_target = true)]
        public delegate FilterResult ButtonFilter (Seat seat, ButtonEvent event);

        /* Activation-token request from xdg_activation_v1. */
        [CCode (cname = "PoloBackendActivationRequest", has_type_id = false, has_copy_function = false,
                has_destroy_function = false)]
        public struct ActivationRequest {
            public unowned string? app_id;
            public bool has_surface;
            public unowned Toplevel? toplevel;
            public bool input_valid;
            public uint32 input_time_msec;
            public bool surface_focused;
        }

        /* One head of a zwlr_output_manager_v1 configuration, unset properties filled in. */
        [CCode (cname = "PoloBackendOutputRequest", has_type_id = false, has_copy_function = false,
                has_destroy_function = false)]
        public struct OutputRequest {
            public unowned Output output;
            public bool enabled;
            public int width;
            public int height;
            public int refresh;
            public int x;
            public int y;
            public int transform;
            public double scale;
            public bool adaptive_sync;
        }

        /* test: validate only. An apply that fails must have reverted what it changed. */
        [CCode (cname = "PoloBackendOutputConfigHandler", has_target = true)]
        public delegate bool OutputConfigHandler ([CCode (array_length_type = "guint")] OutputRequest[] requests,
                                                  bool test);

        [CCode (cname = "PoloBackendActivationMint", has_target = true)]
        public delegate string? ActivationMint (ActivationRequest request);

        [CCode (cname = "PoloBackendActivationActivate", has_target = true)]
        public delegate void ActivationActivate (string token, Toplevel toplevel);

        [CCode (cname = "PoloBackendTestInspector", has_target = true)]
        public delegate bool TestInspector (Toplevel toplevel, ref TestWindowInfo info);

        namespace Keymap {
            [CCode (cname = "polo_backend_keymap_has_layout")]
            public bool has_layout (string layout, string? variant);
        }

        /* Passing null reads the setting and returns its value. */
        [CCode (cname = "PoloBackendTestSettings", has_target = true)]
        public delegate string? TestSettings (string schema, string key, string? value);

        /* Server-owned compositor layer, valid until Server.stop (). */
        [Compact]
        [CCode (cname = "PoloBackendLayer", free_function = "", has_type_id = false)]
        public class Layer {
            /* Moves views to the top of this layer, preserving bottom-to-top order. */
            [CCode (cname = "polo_backend_layer_restack")]
            public void restack ([CCode (array_length_type = "int")] View[] views);
        }

        [CCode (cname = "PoloBackendServer", type_id = "polo_backend_server_get_type ()")]
        public class Server : GLib.Object {
            [CCode (cname = "polo_backend_server_new", has_construct_function = false)]
            public Server ();

            /* Start-up output and seat signals may fire before this returns. */
            [CCode (cname = "polo_backend_server_start")]
            public bool start (Config config) throws GLib.Error;

            /* Frame, input and effect timings in the log every interval. After start (). */
            [CCode (cname = "polo_backend_stats_start")]
            public void start_stats (uint interval_msec);

            [CCode (cname = "polo_backend_server_attach")]
            public void attach (GLib.MainContext? context);

            /* Idempotently destroys clients, the compositor, and the display. */
            [CCode (cname = "polo_backend_server_stop")]
            public void stop ();

            [CCode (cname = "polo_backend_server_get_socket_name")]
            public unowned string? get_socket_name ();

            [CCode (cname = "polo_backend_server_get_idle_inhibited")]
            public bool get_idle_inhibited ();

            [CCode (cname = "polo_backend_server_set_session_idle_inhibited")]
            public void set_session_idle_inhibited (bool inhibited);

            /* Monotonic; within a second of now while input keeps coming. */
            [CCode (cname = "polo_backend_server_get_last_input_usec")]
            public int64 get_last_input_usec ();

            /* Every output off until the next input. */
            [CCode (cname = "polo_backend_server_blank_outputs")]
            public void blank_outputs ();

            /* White over every output for one frame. */
            [CCode (cname = "polo_backend_server_flash_outputs")]
            public void flash_outputs ();

            /* Blanked outputs back on, as input would. */
            [CCode (cname = "polo_backend_server_unblank_outputs")]
            public void unblank_outputs ();

            [CCode (cname = "polo_backend_server_set_output_config_handler")]
            public void set_output_config_handler (owned OutputConfigHandler? handler);

            /* Outputs changed outside a hotplug: resend their state to output managers. */
            [CCode (cname = "polo_backend_server_output_state_changed")]
            public void output_state_changed ();

            /* How many monitors windows are laid out on, for tests. */
            [CCode (cname = "polo_backend_server_set_monitor_count")]
            public void set_monitor_count (uint count);

            [CCode (cname = "polo_backend_server_get_lock_state")]
            public LockState get_lock_state ();

            /* Sends finished to the lock, if any, and takes the curtain down. */
            [CCode (cname = "polo_backend_server_force_unlock")]
            public void force_unlock ();

            /* Shows buffer's view centred above the curtain while locked; null hides it. */
            [CCode (cname = "polo_backend_server_set_lock_prompt")]
            public void set_lock_prompt (PixelBuffer? buffer);

            /* Valid until stop (). */
            [CCode (cname = "polo_backend_server_get_layer")]
            public unowned Layer get_layer (LayerKind kind);

            [CCode (cname = "polo_backend_server_set_global_filter")]
            public void set_global_filter (owned GlobalFilter? filter);

            [CCode (cname = "polo_backend_server_set_key_prefilter")]
            public void set_key_prefilter (owned KeyFilter? filter);

            [CCode (cname = "polo_backend_server_set_activation_mint")]
            public void set_activation_mint (owned ActivationMint? mint);

            [CCode (cname = "polo_backend_server_set_activation_activate")]
            public void set_activation_activate (owned ActivationActivate? activate);

            /* Compositor time for input events, in milliseconds. */
            [CCode (cname = "polo_backend_server_get_time_msec")]
            public uint32 get_time_msec ();

            /* Creates a caller-owned fd for WAYLAND_SOCKET. @client remains
             * valid until its destroyed signal. */
            [CCode (cname = "polo_backend_server_create_client_socket")]
            public int create_client_socket (out unowned Client client) throws GLib.Error;

            [CCode (cname = "polo_backend_server_has_test_protocol")]
            public static bool has_test_protocol ();

            [CCode (cname = "polo_backend_server_set_test_inspector")]
            public void set_test_inspector (owned TestInspector? inspector);

            [CCode (cname = "polo_backend_server_set_test_settings")]
            public void set_test_settings (owned TestSettings? settings);

            [CCode (cname = "polo_backend_server_set_keyboard_repeat")]
            public void set_keyboard_repeat (int rate, int delay);

            [CCode (cname = "polo_backend_server_get_workspace_manager")]
            public unowned WorkspaceManager get_workspace_manager ();

            [CCode (cname = "polo_backend_server_get_shell_ui")]
            public unowned ShellUi get_shell_ui ();

            public signal void outputs_changed ();
            public signal void output_added (Output output);
            public signal void output_removed (Output output);
            public signal void seat_added (Seat seat);
            /* DRM backend only; emitted for a configured libinput device. */
            public signal void input_device_added (InputDevice device);
            /* xdg_system_bell_v1 event; @toplevel may be null. */
            public signal void system_bell (Toplevel? toplevel);

            public signal void new_toplevel (Toplevel toplevel);
            public signal void new_layer_surface (LayerSurface surface);
            /* libweston requested shutdown, e.g. when the nested window closed. */
            public signal void shutdown ();

            /* The first input after a second without any. */
            public signal void activity ();

            /* state is a LockState. */
            public signal void lock_state_changed (int state);

            /* Enter while the lock is abandoned. */
            public signal void lock_restart_requested ();

            [CCode (cname = "polo_backend_server_supports_deformation")]
            public bool supports_deformation ();

            /* While animating, outputs repaint continuously and emit frame just before each repaint. */
            [CCode (cname = "polo_backend_server_set_animating")]
            public void set_animating (bool animating);

            /* usec: the output's presentation time, monotonic microseconds. */
            public signal void frame (Output output, int64 usec);
        }

        [CCode (cname = "PoloBackendMode", has_type_id = false)]
        public struct Mode {
            public int width;
            public int height;
            public int refresh;
            public bool preferred;
            public bool current;
        }

        [CCode (cname = "PoloBackendOutput", type_id = "polo_backend_output_get_type ()")]
        public class Output : GLib.Object {
            public unowned string get_make ();
            public unowned string get_model ();
            public unowned string get_serial ();
            public string get_vendor ();
            public string get_product ();
            public string get_edid_serial ();
            public void get_physical_size (out int width, out int height);
            public bool get_internal ();
            public bool get_enabled ();
            public int get_transform ();
            public int get_mode_count ();
            public bool get_mode (int index, out Mode mode);
            public bool apply (int width, int height, int refresh, int scale, int transform,
                               int x, int y, bool enabled) throws GLib.Error;

            /* Variable refresh rate: libweston's game mode. */
            public bool get_adaptive_sync_supported ();
            public bool get_adaptive_sync ();
            public bool set_adaptive_sync (bool enabled);

            [CCode (cname = "polo_backend_output_get_name")]
            public unowned string get_name ();

            /* Returns global logical geometry. */
            [CCode (cname = "polo_backend_output_get_geometry")]
            public void get_geometry (out int x, out int y, out int width, out int height);

            [CCode (cname = "polo_backend_output_get_scale")]
            public int get_scale ();

            [CCode (cname = "polo_backend_output_set_work_area")]
            public void set_work_area (int x, int y, int width, int height);

            [CCode (cname = "polo_backend_output_set_primary")]
            public void set_primary (bool primary);

            [CCode (cname = "polo_backend_output_set_background_color")]
            public void set_background_color (double red, double green, double blue);

            public signal void destroyed ();
        }

        [CCode (cname = "PoloBackendDeviceCapability", cprefix = "POLO_BACKEND_DEVICE_", has_type_id = false)]
        public enum DeviceCapability {
            KEYBOARD,
            POINTER,
            TOUCH,
            TABLET_TOOL
        }

        [Flags]
        [CCode (cname = "PoloBackendDeviceFeatures", cprefix = "POLO_BACKEND_DEVICE_", has_type_id = false)]
        public enum DeviceFeatures {
            LEFT_HANDED,
            ACCEL,
            ACCEL_PROFILE,
            MIDDLE_EMULATION,
            DWT,
            TAP,
            CLICK_METHOD,
            SCROLL_METHOD,
            NATURAL_SCROLL,
            SEND_EVENTS
        }

        [CCode (cname = "PoloBackendAccelProfile", cprefix = "POLO_BACKEND_ACCEL_PROFILE_", has_type_id = false)]
        public enum AccelProfile {
            DEFAULT,
            ADAPTIVE,
            FLAT
        }

        [CCode (cname = "PoloBackendClickMethod", cprefix = "POLO_BACKEND_CLICK_METHOD_", has_type_id = false)]
        public enum ClickMethod {
            NONE,
            BUTTON_AREAS,
            CLICKFINGER
        }

        [CCode (cname = "PoloBackendScrollMethod", cprefix = "POLO_BACKEND_SCROLL_METHOD_", has_type_id = false)]
        public enum ScrollMethod {
            NONE,
            TWO_FINGER,
            EDGE,
            ON_BUTTON_DOWN
        }

        /* Input-setting setters report whether libinput accepted the value. */
        [CCode (cname = "PoloBackendInputDevice", type_id = "polo_backend_input_device_get_type ()")]
        public class InputDevice : GLib.Object {
            public signal void removed ();

            [CCode (cname = "polo_backend_input_device_get_name")]
            public unowned string get_name ();
            [CCode (cname = "polo_backend_input_device_has_capability")]
            public bool has_capability (DeviceCapability cap);
            [CCode (cname = "polo_backend_input_device_is_touchpad")]
            public bool is_touchpad ();
            [CCode (cname = "polo_backend_input_device_get_features")]
            public DeviceFeatures get_features ();
            [CCode (cname = "polo_backend_input_device_set_left_handed")]
            public bool set_left_handed (bool left_handed);
            [CCode (cname = "polo_backend_input_device_set_accel_speed")]
            public bool set_accel_speed (double speed);
            [CCode (cname = "polo_backend_input_device_set_accel_profile")]
            public bool set_accel_profile (AccelProfile profile);
            [CCode (cname = "polo_backend_input_device_set_middle_emulation")]
            public bool set_middle_emulation (bool enabled);
            [CCode (cname = "polo_backend_input_device_set_dwt")]
            public bool set_dwt (bool enabled);
            [CCode (cname = "polo_backend_input_device_set_tap")]
            public bool set_tap (bool enabled);
            [CCode (cname = "polo_backend_input_device_set_click_method")]
            public bool set_click_method (ClickMethod method);
            [CCode (cname = "polo_backend_input_device_set_scroll_method")]
            public bool set_scroll_method (ScrollMethod method);
            [CCode (cname = "polo_backend_input_device_set_natural_scroll")]
            public bool set_natural_scroll (bool enabled);
            [CCode (cname = "polo_backend_input_device_set_send_events")]
            public bool set_send_events (bool enabled);
        }

        [CCode (cname = "PoloBackendSeat", type_id = "polo_backend_seat_get_type ()")]
        public class Seat : GLib.Object {
            [CCode (cname = "polo_backend_seat_get_name")]
            public unowned string get_name ();

            [CCode (cname = "polo_backend_seat_get_pointer_position")]
            public void get_pointer_position (out double x, out double y);

            [CCode (cname = "polo_backend_seat_warp_pointer")]
            public void warp_pointer (double x, double y);

            [CCode (cname = "polo_backend_seat_inject_key")]
            public void inject_key (uint32 key, bool pressed);

            [CCode (cname = "polo_backend_seat_get_modifier_state")]
            public void get_modifier_state (out Modifiers depressed, out Modifiers latched, out Modifiers locked);

            [CCode (cname = "polo_backend_seat_set_latched_locked")]
            public void set_latched_locked (Modifiers latched, Modifiers locked);

            [CCode (cname = "polo_backend_seat_move_pointer")]
            public void move_pointer (double dx, double dy);

            [CCode (cname = "polo_backend_seat_inject_button")]
            public void inject_button (uint32 button, bool pressed);

            /* Sets the cursor until the focused surface's next enter event. */
            [CCode (cname = "polo_backend_seat_set_cursor")]
            public void set_cursor (Cursor cursor);

            [CCode (cname = "polo_backend_seat_set_pointer_hidden")]
            public void set_pointer_hidden (bool hidden);

            [CCode (cname = "polo_backend_seat_get_modifiers")]
            public Modifiers get_modifiers ();

            /* Emitted after pointer motion outside a grab. @view is the toplevel
             * or surface under the pointer. */
            public signal void motion (double x, double y, uint time_msec, View? view);
            public signal void group_changed (uint group);
            public signal void numlock_changed (bool locked);
            public signal void modifiers_changed (Modifiers modifiers);

            [CCode (cname = "polo_backend_seat_set_keymap")]
            public bool set_keymap (string? rules, string? model, string? layout, string? variant,
                                    string? options) throws GLib.Error;

            [CCode (cname = "polo_backend_seat_lock_group")]
            public void lock_group (uint group);

            [CCode (cname = "polo_backend_seat_get_group")]
            public uint get_group ();

            [CCode (cname = "polo_backend_seat_set_numlock")]
            public void set_numlock (bool locked);

            /* null clears keyboard focus. */
            [CCode (cname = "polo_backend_seat_set_keyboard_focus")]
            public void set_keyboard_focus (View? view);

            /* A filter that consumes a press must consume its release too. */
            [CCode (cname = "polo_backend_seat_set_button_filter")]
            public void set_button_filter (owned ButtonFilter? filter);

            /* A filter that consumes a press gets its release consumed too. */
            [CCode (cname = "polo_backend_seat_set_key_filter")]
            public void set_key_filter (owned KeyFilter? filter);

            [CCode (cname = "polo_backend_seat_set_axis_filter")]
            public void set_axis_filter (owned AxisFilter? filter);

            /* Starts a grab for a held button. Returns null unless @serial is
             * that button's press and the pointer is over @view. C validates
             * the protocol state. */
            [CCode (cname = "polo_backend_seat_start_pointer_grab")]
            public PointerGrab? start_pointer_grab (uint32 serial, View view);

            /* Starts a compositor operation without a held button; null if another grab is active. */
            [CCode (cname = "polo_backend_seat_begin_pointer_grab")]
            public PointerGrab? begin_pointer_grab ();

            [CCode (cname = "polo_backend_seat_start_pointer_grab_for_binding")]
            public PointerGrab? start_pointer_grab_for_binding (View view);

            public signal void destroyed ();
        }

        [CCode (cname = "PoloBackendPointerGrab", type_id = "polo_backend_pointer_grab_get_type ()")]
        public class PointerGrab : GLib.Object {
            [CCode (cname = "polo_backend_pointer_grab_end")]
            public void end ();

            [CCode (cname = "polo_backend_pointer_grab_set_relative_locked")]
            public void set_relative_locked (bool locked);

            public signal void motion (double x, double y, uint time_msec);
            public signal void motion_delta (double dx, double dy, bool relative, uint time_msec);
            public signal void button (uint button, bool pressed, uint buttons_held);
            public signal void axis (uint axis, double value, bool has_discrete, int discrete);
            /* Emitted once; the grab is inert afterwards. */
            public signal void ended (bool cancelled);
        }

        [CCode (cname = "PoloBackendView", type_id = "polo_backend_view_get_type ()")]
        public class View : GLib.Object {
            /* Surface origin, global logical coordinates. */
            [CCode (cname = "polo_backend_view_set_position")]
            public void set_position (double x, double y);

            [CCode (cname = "polo_backend_view_get_position")]
            public void get_position (out double x, out double y);

            [CCode (cname = "polo_backend_view_get_size")]
            public void get_size (out int width, out int height);

            /* cols × rows vertices of x, y, u, v; false leaves the view undeformed. */
            [CCode (cname = "polo_backend_view_set_deformation")]
            public bool set_deformation ([CCode (array_length = false)] float[] vertices, uint cols, uint rows);

            [CCode (cname = "polo_backend_view_clear_deformation")]
            public void clear_deformation ();
            [CCode (cname = "polo_backend_view_set_alpha")]
            public void set_alpha (float alpha);
            [CCode (cname = "polo_backend_view_mirror")]
            public View? mirror ();
            [CCode (cname = "polo_backend_view_ghost")]
            public View? ghost ();

            [CCode (cname = "polo_backend_view_set_effect_transform")]
            public void set_effect_transform (double cx, double cy, double scale_x, double scale_y, double angle,
                                              double dx, double dy);

            [CCode (cname = "polo_backend_view_clear_effect_transform")]
            public void clear_effect_transform ();

            [CCode (cname = "polo_backend_view_mirror_views")]
            public GLib.GenericArray<View> mirror_views ();
            [CCode (cname = "polo_backend_view_place")]
            public void place (double x, double y, double scale);

            /* On top of layer; null removes the view from any layer. */
            [CCode (cname = "polo_backend_view_move_to_layer")]
            public void move_to_layer (Layer? layer);

            [CCode (cname = "polo_backend_view_get_toplevel")]
            public unowned Toplevel? get_toplevel ();

            [CCode (cname = "polo_backend_view_get_client")]
            public unowned Client? get_client ();

            public signal void destroyed ();
        }

        [CCode (cname = "PoloBackendToplevel", type_id = "polo_backend_toplevel_get_type ()")]
        public class Toplevel : GLib.Object {
            public string? title { [CCode (cname = "polo_backend_toplevel_get_title")] get; }
            public string? app_id { [CCode (cname = "polo_backend_toplevel_get_app_id")] get; }

            [CCode (cname = "polo_backend_toplevel_get_pid")]
            public int get_pid ();

            [CCode (cname = "polo_backend_toplevel_get_icon_name")]
            public unowned string? get_icon_name ();

            /* Cairo ARGB32, size × size. */
            [CCode (cname = "polo_backend_toplevel_get_icon_pixels")]
            public unowned GLib.Bytes? get_icon_pixels (out int size);

            [CCode (cname = "polo_backend_toplevel_get_parent")]
            public unowned Toplevel? get_parent ();

            /* Window geometry relative to the surface origin. */
            /* Whether Polo draws this toplevel's frame. */
            [CCode (cname = "polo_backend_toplevel_get_server_decorated")]
            public bool get_server_decorated ();

            [CCode (cname = "polo_backend_toplevel_get_window_geometry")]
            public void get_window_geometry (out int x, out int y, out int width, out int height);

            /* 0 means unconstrained. */
            [CCode (cname = "polo_backend_toplevel_get_min_size")]
            public void get_min_size (out int width, out int height);

            [CCode (cname = "polo_backend_toplevel_get_max_size")]
            public void get_max_size (out int width, out int height);

            [CCode (cname = "polo_backend_toplevel_has_content")]
            public bool has_content ();

            [CCode (cname = "polo_backend_toplevel_is_mapped")]
            public bool is_mapped ();

            [CCode (cname = "polo_backend_toplevel_map")]
            public void map ();

            /* Valid until destroyed. */
            [CCode (cname = "polo_backend_toplevel_get_view")]
            public unowned View get_view ();

            [CCode (cname = "polo_backend_toplevel_get_surface_tree_views")]
            public GLib.GenericArray<View> get_surface_tree_views ();

            [CCode (cname = "polo_backend_toplevel_get_client")]
            public unowned Client? get_client ();

            /* Window-geometry size; 0x0 lets the client choose. */
            [CCode (cname = "polo_backend_toplevel_configure_size")]
            public void configure_size (int width, int height);

            [CCode (cname = "polo_backend_toplevel_set_activated")]
            public void set_activated (bool activated);

            [CCode (cname = "polo_backend_toplevel_set_maximized")]
            public void set_maximized (bool maximized);

            [CCode (cname = "polo_backend_toplevel_set_fullscreen")]
            public void set_fullscreen (bool fullscreen);

            [CCode (cname = "polo_backend_toplevel_set_resizing")]
            public void set_resizing (bool resizing);

            [CCode (cname = "polo_backend_toplevel_set_tiled")]
            public void set_tiled (Edges edges);

            [CCode (cname = "polo_backend_toplevel_close")]
            public void close ();

            [CCode (cname = "polo_backend_toplevel_ping")]
            public void ping ();

            public signal void committed ();
            public signal void request_move (Seat seat, uint serial);
            /* edges is an Edges mask. */
            public signal void request_resize (Seat seat, uint serial, uint edges);
            public signal void request_maximize (bool maximized);
            public signal void request_fullscreen (bool fullscreen);
            public signal void request_minimize ();
            public signal void parent_changed (Toplevel? parent);

            public signal void modal_hint_changed (bool modal);
            public signal void decoration_changed ();
            public signal void request_window_menu (Seat seat, double x, double y);
            public signal void ping_timeout ();
            public signal void pong ();
            public signal void icon_changed ();
            /* The client is closing the toplevel; its views are still in place. */
            public signal void closing ();
            public signal void destroyed ();
        }

        [CCode (cname = "PoloBackendLayerSurface", type_id = "polo_backend_layer_surface_get_type ()")]
        public class LayerSurface : GLib.Object {
            [CCode (cname = "polo_backend_layer_surface_get_namespace")]
            public unowned string get_namespace ();

            /* Valid until destroyed; the surface's own view. */
            [CCode (cname = "polo_backend_layer_surface_get_view")]
            public unowned View get_view ();

            /* The output the arranger assigned, or null until it does. */
            [CCode (cname = "polo_backend_layer_surface_get_output")]
            public unowned Output? get_output ();

            [CCode (cname = "polo_backend_layer_surface_set_output")]
            public void set_output (Output? output);

            /* Desired size for the next configure; 0 lets the client decide. */
            [CCode (cname = "polo_backend_layer_surface_configure")]
            public void configure (uint32 width, uint32 height);

            [CCode (cname = "polo_backend_layer_surface_close")]
            public void close ();

            [CCode (cname = "polo_backend_layer_surface_is_mapped")]
            public bool is_mapped ();

            /* This surface, one of its popups, or a popup under them. */
            [CCode (cname = "polo_backend_layer_surface_owns_view")]
            public bool owns_view (View view);

            /* Actual surface size, surface-local coordinates. */
            [CCode (cname = "polo_backend_layer_surface_get_size")]
            public void get_size (out int width, out int height);

            /* zwlr_layer_surface_v1 state, from the latest commit. */
            public uint layer { [CCode (cname = "polo_backend_layer_surface_get_layer")] get; }
            public uint anchor { [CCode (cname = "polo_backend_layer_surface_get_anchor")] get; }
            public int exclusive_zone {
                [CCode (cname = "polo_backend_layer_surface_get_exclusive_zone")] get;
            }
            public uint exclusive_edge {
                [CCode (cname = "polo_backend_layer_surface_get_exclusive_edge")] get;
            }
            public int margin_top {
                [CCode (cname = "polo_backend_layer_surface_get_margin_top")] get;
            }
            public int margin_right {
                [CCode (cname = "polo_backend_layer_surface_get_margin_right")] get;
            }
            public int margin_bottom {
                [CCode (cname = "polo_backend_layer_surface_get_margin_bottom")] get;
            }
            public int margin_left {
                [CCode (cname = "polo_backend_layer_surface_get_margin_left")] get;
            }
            public uint keyboard_interactivity {
                [CCode (cname = "polo_backend_layer_surface_get_keyboard_interactivity")] get;
            }
            /* The size the client last committed as desired. */
            public uint desired_width {
                [CCode (cname = "polo_backend_layer_surface_get_desired_width")] get;
            }
            public uint desired_height {
                [CCode (cname = "polo_backend_layer_surface_get_desired_height")] get;
            }
            public bool mapped {
                [CCode (cname = "polo_backend_layer_surface_get_mapped")] get;
            }
            /* The output the arranger assigned; null clears it. */
            public Output? output {
                [CCode (cname = "polo_backend_layer_surface_get_output")] get;
                [CCode (cname = "polo_backend_layer_surface_set_output")] set;
            }
            /* From get_layer_surface's output argument, may be null. */
            public Output? requested_output {
                [CCode (cname = "polo_backend_layer_surface_get_requested_output")] get;
            }

            public signal void committed ();
            public signal void new_popup (View view);
            public signal void destroyed ();
        }

        /* Surface Polo renders itself. */
        [CCode (cname = "PoloBackendPixelBuffer", type_id = "polo_backend_pixel_buffer_get_type ()")]
        public class PixelBuffer : GLib.Object {
            [CCode (cname = "polo_backend_pixel_buffer_new")]
            public PixelBuffer (Server server, int width, int height, int scale);

            [CCode (cname = "polo_backend_pixel_buffer_is_ready")]
            public bool is_ready ();

            /* Null until ready. */
            [CCode (cname = "polo_backend_pixel_buffer_get_view")]
            public unowned View? get_view ();

            [CCode (cname = "polo_backend_pixel_buffer_get_size")]
            public void get_size (out int width, out int height, out int scale);

            /* The next frame, holding the last one committed: ARGB32
             * premultiplied, writable from any thread until commit ().
             * Empty without the internal client. */
            [CCode (cname = "polo_backend_pixel_buffer_begin", array_length_pos = 0.1)]
            public unowned uint8[] begin (out int stride);

            [CCode (cname = "polo_backend_pixel_buffer_peek", array_length_pos = 0.1)]
            public unowned uint8[] peek (out int stride);

            [CCode (cname = "polo_backend_pixel_buffer_commit")]
            public void commit ();

            [CCode (cname = "polo_backend_pixel_buffer_commit_damage")]
            public void commit_damage (int x, int y, int width, int height);

            [CCode (cname = "polo_backend_pixel_buffer_set_accepts_input")]
            public void set_accepts_input (bool accepts);

            public signal void ready ();
        }

        /* One window as task bars see it (zwlr_foreign_toplevel_manager_v1). */
        [CCode (cname = "PoloBackendForeignToplevel", type_id = "polo_backend_foreign_toplevel_get_type ()")]
        public class ForeignToplevel : GLib.Object {
            [CCode (cname = "polo_backend_foreign_toplevel_new")]
            public ForeignToplevel (Server server);

            [CCode (cname = "polo_backend_foreign_toplevel_set_title")]
            public void set_title (string? title);

            [CCode (cname = "polo_backend_foreign_toplevel_set_app_id")]
            public void set_app_id (string? app_id);

            [CCode (cname = "polo_backend_foreign_toplevel_set_state")]
            public void set_state (ForeignToplevelState state);

            [CCode (cname = "polo_backend_foreign_toplevel_set_extra_state")]
            public void set_extra_state (ForeignToplevelExtraState state);

            [CCode (cname = "polo_backend_foreign_toplevel_set_workspaces")]
            public void set_workspaces ([CCode (array_length_type = "int")] Workspace[] workspaces);

            [CCode (cname = "polo_backend_foreign_toplevel_set_outputs")]
            public void set_outputs ([CCode (array_length_type = "int")] Output[] outputs);

            [CCode (cname = "polo_backend_foreign_toplevel_set_parent")]
            public void set_parent (ForeignToplevel? parent);
            [CCode (cname = "polo_backend_foreign_toplevel_set_window")]
            public void set_window (Toplevel? window);
            [CCode (cname = "polo_backend_foreign_toplevel_get_identifier")]
            public unowned string get_identifier ();

            /* Sends the queued changes. */
            [CCode (cname = "polo_backend_foreign_toplevel_done")]
            public void done ();

            /* Marks the window closed; the handle ignores later requests. */
            [CCode (cname = "polo_backend_foreign_toplevel_close")]
            public void close ();

            /* The latest set_rectangle, global coordinates. */
            [CCode (cname = "polo_backend_foreign_toplevel_get_rectangle")]
            public bool get_rectangle (out int x, out int y, out int width, out int height);

            public signal void request_maximize (bool maximized);
            public signal void request_minimize (bool minimized);
            public signal void request_activate ();
            public signal void request_close ();
            public signal void request_fullscreen (bool fullscreen, Output? output);
            public signal void request_shade (bool shaded);
            public signal void request_sticky (bool sticky);
            public signal void request_above (bool above);
            public signal void request_below (bool below);
            public signal void request_move ();
            public signal void request_resize ();
            public signal void request_move_to_output (Output output);
            public signal void request_move_to_workspace (Workspace workspace);
        }

        [Flags]
        [CCode (cname = "PoloBackendWorkspace", type_id = "polo_backend_workspace_get_type ()")]
        public class Workspace : GLib.Object {
            [CCode (cname = "polo_backend_workspace_set_name")]
            public void set_name (string name);
            [CCode (cname = "polo_backend_workspace_set_coordinates")]
            public void set_coordinates (uint col, uint row);
            [CCode (cname = "polo_backend_workspace_set_state")]
            public void set_state (uint state);
            [CCode (cname = "polo_backend_workspace_remove")]
            public void remove_workspace ();

            public signal void activate ();
            public signal void remove ();
        }

        [CCode (cname = "PoloBackendWorkspaceManager", type_id = "polo_backend_workspace_manager_get_type ()")]
        public class WorkspaceManager : GLib.Object {
            [CCode (cname = "polo_backend_workspace_manager_add")]
            public unowned Workspace add (string id, string name, uint col, uint row, uint state, uint capabilities);
            [CCode (cname = "polo_backend_workspace_manager_done")]
            public void done ();

            public signal void create_workspace (string name);
        }

        [CCode (cname = "PoloBackendSwitcherEntryFlags", cprefix = "POLO_BACKEND_SWITCHER_ENTRY_", has_type_id = false)]
        public enum SwitcherEntryFlags {
            BLANK,
            HIDDEN,
            DEMANDS_ATTENTION
        }

        [Flags]
        [CCode (cname = "PoloBackendMenuOp", cprefix = "POLO_BACKEND_MENU_OP_", has_type_id = false)]
        public enum MenuOp {
            NONE,
            DELETE,
            MINIMIZE,
            UNMAXIMIZE,
            MAXIMIZE,
            UNSHADE,
            SHADE,
            UNSTICK,
            STICK,
            WORKSPACES,
            MOVE,
            RESIZE,
            ABOVE,
            UNABOVE,
            MOVE_LEFT,
            MOVE_RIGHT,
            MOVE_UP,
            MOVE_DOWN,
            RECOVER
        }

        [CCode (cname = "PoloBackendOsdKind", cprefix = "POLO_BACKEND_OSD_", has_type_id = false)]
        public enum OsdKind {
            WORKSPACE,
            VOLUME,
            BRIGHTNESS,
            MESSAGE
        }

        [CCode (cname = "PoloBackendDialogKind", cprefix = "POLO_BACKEND_DIALOG_", has_type_id = false)]
        public enum DialogKind {
            NOT_RESPONDING,
            RENAME_WORKSPACE,
            END_SESSION,
            MESSAGE,
            DISPLAY_CONFIRMATION
        }

        /* ext_session_lock_v1. ABANDONED: the locker went away while locked; the curtain stays. */
        [CCode (cname = "PoloBackendLockState", cprefix = "POLO_BACKEND_LOCK_", has_type_id = false)]
        public enum LockState {
            UNLOCKED,
            LOCKING,
            LOCKED,
            ABANDONED
        }

        /* idled after timeout_msec without input (0: never), resumed on the next input. With
         * respect_inhibitors, idle inhibitors hold it off and their arrival resumes it. */
        [CCode (cname = "PoloBackendIdleWatch", type_id = "polo_backend_idle_watch_get_type ()")]
        public class IdleWatch : GLib.Object {
            [CCode (cname = "polo_backend_idle_watch_new")]
            public IdleWatch (Server server, uint timeout_msec, bool respect_inhibitors);

            [CCode (cname = "polo_backend_idle_watch_set_timeout")]
            public void set_timeout (uint timeout_msec);

            [CCode (cname = "polo_backend_idle_watch_get_idled")]
            public bool get_idled ();

            public signal void idled ();
            public signal void resumed ();
        }

        /* polo_shell_ui_v1 events are dropped while no client is bound. */
        [CCode (cname = "PoloBackendShellUi", type_id = "polo_backend_shell_ui_get_type ()")]
        public class ShellUi : GLib.Object {
            [CCode (cname = "polo_backend_shell_ui_is_bound")]
            public bool is_bound ();
            [CCode (cname = "polo_backend_shell_ui_get_client")]
            public unowned Client? get_client ();
            [CCode (cname = "polo_backend_shell_ui_switcher_entry")]
            public void switcher_entry (uint id, string title, string? app_id, SwitcherEntryFlags flags);
            [CCode (cname = "polo_backend_shell_ui_switcher_icon")]
            public void switcher_icon (uint id, Toplevel toplevel);
            [CCode (cname = "polo_backend_shell_ui_switcher_heading")]
            public void switcher_heading (string text);
            [CCode (cname = "polo_backend_shell_ui_switcher_icon_name")]
            public void switcher_icon_name (uint id, string name);

            [CCode (cname = "polo_backend_shell_ui_switcher_thumbnail")]
            public bool switcher_thumbnail (uint id, View view, double landscape_size, double portrait_size);
            [CCode (cname = "polo_backend_shell_ui_show_switcher")]
            public void show_switcher (Output? output, uint selected, uint columns, bool expand_to_fit,
                                       uint icon_size);
            [CCode (cname = "polo_backend_shell_ui_update_selected")]
            public void update_selected (uint selected);
            [CCode (cname = "polo_backend_shell_ui_hide_switcher")]
            public void hide_switcher ();
            [CCode (cname = "polo_backend_shell_ui_workspace_switcher_entry")]
            public void workspace_switcher_entry (uint id, string title, bool blank);
            [CCode (cname = "polo_backend_shell_ui_workspace_switcher_window")]
            public void workspace_switcher_window (uint id, int x, int y, int width, int height, bool focused,
                                                   string? app_id);
            [CCode (cname = "polo_backend_shell_ui_show_workspace_switcher")]
            public void show_workspace_switcher (Output? output, uint selected, uint columns, uint active,
                                                 int screen_width, int screen_height, int output_width,
                                                 int output_height, uint icon_size);
            [CCode (cname = "polo_backend_shell_ui_update_workspace_switcher")]
            public void update_workspace_switcher (uint selected, uint active);
            [CCode (cname = "polo_backend_shell_ui_window_menu_workspace")]
            public void window_menu_workspace (uint index, string name);
            [CCode (cname = "polo_backend_shell_ui_show_window_menu")]
            public void show_window_menu (uint window_id, Output? output, int x, int y, MenuOp ops,
                                          MenuOp insensitive, uint active_workspace, uint n_workspaces);
            [CCode (cname = "polo_backend_shell_ui_hide_window_menu")]
            public void hide_window_menu (uint window_id);
            [CCode (cname = "polo_backend_shell_ui_show_osd")]
            public void show_osd (OsdKind kind, Output? output, string? icon, string? text, int value);
            [CCode (cname = "polo_backend_shell_ui_show_dialog")]
            public void show_dialog (uint dialog_id, DialogKind kind, Output? output, string? payload);
            [CCode (cname = "polo_backend_shell_ui_lookup_color")]
            public void lookup_color (string name);

            [CCode (cname = "polo_backend_shell_ui_lookup_icon")]
            public bool lookup_icon (uint serial, string? name, string? desktop_id, int mini_size, int size,
                                     int scale, bool fallback);

            [CCode (cname = "polo_backend_shell_ui_hide_dialog")]
            public void hide_dialog (uint dialog_id);
            /* Returns the dialog id for @toplevel's surface, or 0. */
            [CCode (cname = "polo_backend_shell_ui_get_dialog")]
            public uint get_dialog (Toplevel toplevel);

            public signal void bound (Client client);
            public signal void unbound ();
            public signal void switcher_select (uint id);
            public signal void menu_action (uint window_id, MenuOp op, int workspace);
            public signal void dialog_response (uint dialog_id, int response, string? text);
            public signal void custom_color (string name, GLib.Bytes? rgba);
            /* Cairo ARGB32 pixels with stride width × 4; null when nothing was found. */
            public signal void icon (uint serial, GLib.Bytes? mini_pixels, int mini_width, int mini_height,
                                     GLib.Bytes? pixels, int width, int height);
            public signal void icon_theme_changed ();
            public signal void color_table (GLib.Bytes colors);
        }

        [CCode (cname = "PoloBackendClient", type_id = "polo_backend_client_get_type ()")]
        public class Client : GLib.Object {
            [CCode (cname = "polo_backend_client_get_pid")]
            public int get_pid ();

            [CCode (cname = "polo_backend_client_get_uid")]
            public uint get_uid ();

            [CCode (cname = "polo_backend_client_get_executable")]
            public unowned string? get_executable ();

            [CCode (cname = "polo_backend_client_get_trusted")]
            public bool get_trusted ();

            [CCode (cname = "polo_backend_client_get_xserver")]
            public bool get_xserver ();

            [CCode (cname = "polo_backend_client_get_sandbox_engine")]
            public unowned string? get_sandbox_engine ();

            [CCode (cname = "polo_backend_client_get_sandbox_app_id")]
            public unowned string? get_sandbox_app_id ();

            [CCode (cname = "polo_backend_client_get_sandbox_instance_id")]
            public unowned string? get_sandbox_instance_id ();

            [CCode (cname = "polo_backend_client_set_trusted")]
            public void set_trusted (bool trusted);

            /* Associates the child given a socket-pair endpoint; credentials name its creator. */
            [CCode (cname = "polo_backend_client_set_process")]
            public void set_process (int pid);

            public signal void destroyed ();
        }
    }
}
