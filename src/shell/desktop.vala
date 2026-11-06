/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The desktop: projects the policy model onto the backend.
 * Polo.Policy.Model decides placement, stacking, focus and workspaces; this
 * class turns its stack into layer order, its focus into seat focus, and
 * backend events (outputs, clicks, new toplevels) into model calls.
 */

namespace Polo.Shell {
    /* The window another client's toplevel belongs to, without xdg_toplevel.set_parent. */
    public delegate Window? ForeignParent (Backend.Toplevel toplevel);

    public class Desktop : GLib.Object {
        private const uint BTN_LEFT = 0x110;
        private const uint BTN_RIGHT = 0x111;
        private const uint BTN_MIDDLE = 0x112;
        /* MATE's default desktop grey. */
        private const double BACKGROUND_RED = 0.235;
        private const double BACKGROUND_GREEN = 0.259;
        private const double BACKGROUND_BLUE = 0.282;

        public Backend.Server server { get; construct; }
        public Policy.Model model { get; construct; }

        private GLib.HashTable<Backend.Output, Policy.Output> outputs =
            new GLib.HashTable<Backend.Output, Policy.Output> (GLib.direct_hash, GLib.direct_equal);
        private GLib.GenericArray<Backend.Seat> seats = new GLib.GenericArray<Backend.Seat> ();
        private GLib.HashTable<Backend.Toplevel, XdgWindow> windows =
            new GLib.HashTable<Backend.Toplevel, XdgWindow> (GLib.direct_hash, GLib.direct_equal);

        public LayerShell layer_shell { get; private set; }
        private Settings.Background background;
        private Settings.InterfaceMirror interface_mirror;
        private Bell bell;
        private Input.Keyboard keyboard;
        private Input.DeviceSettings device_settings;

        private Outputs.Store store;
        public Effects.Engine effects { get; construct; }
        private bool drm;
        private GLib.GenericArray<Backend.Output> heads = new GLib.GenericArray<Backend.Output> ();

        public Policy.ActivationTokens activation_tokens { get; private set; }
        public Input.Bindings bindings { get; private set; }
        private Input.MarcoActions actions;
        public Input.MoveResize move_resize { get; private set; }
        public Switcher switcher { get; private set; }
        public WindowMenu window_menu { get; private set; }
        private FrameInput frame_input;
        public WorkspaceSwitcher workspace_switcher { get; private set; }
        private TilePreview tile_preview;
        public Decor.Decorations decorations { get; private set; }

        public Desktop (Backend.Server server, Policy.Model model, Outputs.Store store, bool drm,
                        Effects.Engine effects,
                        Input.XkbConfig system_keyboard) {
            Object (server: server, model: model, effects: effects);
            this.store = store;
            this.drm = drm;
            keyboard = new Input.Keyboard (server, model, system_keyboard);
            device_settings = new Input.DeviceSettings (settings_or_null (Input.DeviceSettings.MOUSE_SCHEMA),
                                                        settings_or_null (Input.DeviceSettings.TOUCHPAD_SCHEMA));
            server.input_device_added.connect (on_input_device_added);
            layer_shell = new LayerShell (server, model, this);
            background = new Settings.Background (server, model, this, layer_shell);
            interface_mirror =
                new Settings.InterfaceMirror (settings_or_null (Settings.InterfaceMirror.GENERAL_SCHEMA));
            bell = new Bell (server, model);
            server.system_bell.connect (on_system_bell);
            decorations = new Decor.Decorations ();
            interface_settings = settings ("org.mate.interface");
            if (interface_settings != null) {
                interface_settings.changed["font-name"].connect (load_decorations);
            }
            load_decorations ();
            decorations.changed.connect (on_decorations_changed);
            /* In the body: construct blocks run before layer_shell exists. */
            model.layer_struts = layer_shell.all_struts;
        }

        internal void frames_changed () {
            restack ();
        }

        private void on_decorations_changed () {
            foreach (var xdg in windows.get_values ()) {
                xdg.decorations_changed ();
            }
        }

        /* org.mate.interface font-name: the title font when titlebar-uses-system-font is set. */
        private GLib.Settings? interface_settings = null;

        /* Marco's prefs_changed_callback (): the theme, the title font and the button layout. */
        private void load_decorations () {
            var prefs = model.prefs;
            decorations.set_fonts (interface_settings != null ? interface_settings.get_string ("font-name") : null,
                                   prefs.titlebar_uses_system_font ? null : prefs.titlebar_font);
            decorations.set_button_layout (prefs.button_layout);
            decorations.set_icon_size (prefs.icon_size);
            decorations.set_theme (prefs.theme);
            decorations.ensure_theme ();
        }

        construct {
            server.output_added.connect (on_output_added);
            server.outputs_changed.connect (apply_layout);
            server.output_removed.connect (on_output_removed);
            server.seat_added.connect (on_seat_added);
            server.new_toplevel.connect (on_new_toplevel);
            model.stack_changed.connect (restack);
            model.focus_changed.connect (on_focus_changed);
            model.pointer_warped.connect (on_pointer_warped);
            model.window_geometry_transition.connect (on_window_geometry_transition);
            model.work_areas_changed.connect (push_work_areas);
            activation_tokens = new Policy.ActivationTokens ();
            bindings = new Input.Bindings ();
            move_resize = new Input.MoveResize (model, effects);
            actions = new Input.MarcoActions (model, bindings, move_resize);
            tile_preview = new TilePreview (server);
            tile_preview.changed.connect (restack);
            move_resize.tile_preview_shown.connect (tile_preview.show);
            move_resize.tile_preview_hidden.connect (tile_preview.hide);
            switcher = new Switcher (server, model, this, bindings);
            window_menu = new WindowMenu (server, model, this, bindings);
            frame_input = new FrameInput (this, model);
            workspace_switcher = new WorkspaceSwitcher (server, model, this, bindings);
            model.keyboard_move_handler = (window) => {
                foreach (var seat in seats) {
                    move_resize.begin_keyboard (seat, window, Input.GrabOp.KEYBOARD_MOVING,
                                                model.last_user_time);
                    return true;
                }
                return false;
            };
            bindings.rebuild (model.prefs);
            model.notify["prefs"].connect (on_prefs_changed);
            server.set_activation_mint (on_activation_mint);
            server.set_activation_activate (on_activation_activate);
        }

        /* Keep maximize and tile wobble animations under half a second. */
        private const double TRANSITION_SECONDS = 0.5;

        private void on_window_geometry_transition (Window window, Marco.Rect old_rect,
                                                    Marco.Rect new_rect) {
            if (effects.settings == null) {
                return;
            }
            /* A drag can end in a tile after its wobble settles; update that
             * effect to the tiled geometry. */
            var running = effects.effect_of (window) as Effects.WobblyEffect;
            if (running != null) {
                running.limit_to (TRANSITION_SECONDS);
                return;
            }
            var xdg = window.adapter as XdgWindow;
            if (xdg == null) {
                return;
            }
            var target = xdg.create_effect_target ();
            Marco.Rect drawn;
            if (!target.get_surface_rect (out drawn)) {
                return;
            }
            /* The views still have their old geometry, so animate them to the new one. */
            var effect = new Effects.WobblyEffect (effects.settings);
            if (effects.allowed (effect) && effect.prepare (target, drawn, new_rect, false)) {
                effect.limit_to (TRANSITION_SECONDS);
                effects.run (target, effect);
            }
        }

        public GLib.GenericArray<Window> windows_of_client (uint pid, string app_id) {
            var found = new GLib.GenericArray<Window> ();
            foreach (var window in model.get_windows ()) {
                var xdg = window.adapter as XdgWindow;
                if (pid > 0 && xdg != null && xdg.client_pid () == (int) pid) {
                    found.add (window);
                }
            }
            if (found.length > 0 || app_id == "") {
                return found;
            }
            string bare = app_id.has_suffix (".desktop") ? app_id[0 : app_id.length - ".desktop".length] : app_id;
            foreach (var window in model.get_windows ()) {
                if (window.app_id != null && (window.app_id == app_id || window.app_id == bare)) {
                    found.add (window);
                }
            }
            return found;
        }

        private string? on_activation_mint (Backend.ActivationRequest request) {
            var token = activation_tokens.mint (request.input_valid, request.input_time_msec,
                                                server.get_time_msec ());
            Logging.scope (LogDomain.POLICY).debug ("activation token for %s: %s",
                                                    request.app_id ?? "?",
                                                    request.input_valid ? "from input" : "worthless");
            return token;
        }

        private void on_activation_activate (string token, Backend.Toplevel toplevel) {
            var xdg = windows.lookup (toplevel);
            if (xdg == null) {
                return;
            }
            int64 user_time;
            bool valid = activation_tokens.consume (token, server.get_time_msec (), out user_time);
            Logging.scope (LogDomain.POLICY).debug ("activation of %s: %s", xdg.window.describe (),
                                                    valid ? "honoured" : "attention only");
            model.activate_with_token (xdg.window, valid, user_time);
        }

        /* Drop every callback the backend holds into this object. */
        public void shutdown () {
            server.output_added.disconnect (on_output_added);
            server.output_removed.disconnect (on_output_removed);
            server.outputs_changed.disconnect (apply_layout);
            server.seat_added.disconnect (on_seat_added);
            server.new_toplevel.disconnect (on_new_toplevel);
            model.notify["prefs"].disconnect (on_prefs_changed);
            server.set_activation_mint (null);
            server.set_activation_activate (null);
            heads.remove_range (0, heads.length);
            model.stack_changed.disconnect (restack);
            model.focus_changed.disconnect (on_focus_changed);
            model.pointer_warped.disconnect (on_pointer_warped);
            model.focus_policy.shutdown ();
            model.work_areas_changed.disconnect (push_work_areas);
            switcher.shutdown ();
            workspace_switcher.shutdown ();
#if POLO_HAVE_XWAYLAND
            if (ewmh != null) {
                ewmh.shutdown ();
            }
#endif
            window_menu.shutdown ();
            frame_input.shutdown ();
            background.shutdown ();
            interface_mirror.shutdown ();
            server.system_bell.disconnect (on_system_bell);
            bell.shutdown ();
            keyboard.shutdown ();
            server.input_device_added.disconnect (on_input_device_added);
            device_settings.shutdown ();
            layer_shell.shutdown ();
            foreach (var seat in seats) {
                seat.set_button_filter (null);
                seat.set_key_filter (null);
                seat.motion.disconnect (on_motion);
            }
            seats.remove_range (0, seats.length);
            windows.remove_all ();
            outputs.remove_all ();
        }

        /* ---- Outputs ---------------------------------------------------- */

        private void on_output_added (Backend.Output backend_output) {
            heads.add (backend_output);
        }

        private void on_output_removed (Backend.Output backend_output) {
            heads.remove (backend_output);
            var output = outputs.lookup (backend_output);
            if (output != null) {
                outputs.remove (backend_output);
                model.remove_output (output);
                layer_shell.arrange ();
            }
        }

        private static GLib.Settings? settings (string schema) {
            var source = GLib.SettingsSchemaSource.get_default ();
            var found = source != null ? source.lookup (schema, true) : null;
            return found != null ? new GLib.Settings.full (found, null, null) : null;
        }

        private Polo.Output describe (Backend.Output head) {
            var o = new Polo.Output ();
            o.name = head.get_name ();
            o.vendor = head.get_vendor ();
            o.model = head.get_model ();
            o.product = head.get_product ();
            o.serial = head.get_edid_serial ();
            o.internal_panel = head.get_internal ();
            head.get_physical_size (out o.mm_width, out o.mm_height);
            for (int i = 0; i < head.get_mode_count (); i++) {
                Backend.Mode mode;
                if (head.get_mode (i, out mode)) {
                    var m = new OutputMode ();
                    m.width = mode.width;
                    m.height = mode.height;
                    m.rate = mode.refresh;
                    m.preferred = mode.preferred;
                    m.current = mode.current;
                    o.modes.add (m);
                    if (m.current) {
                        o.width = m.width;
                        o.height = m.height;
                        o.rate = m.rate;
                    }
                }
            }
            o.scale = head.get_scale ();
            o.transform = head.get_transform ();
            o.enabled = head.get_enabled ();
            if (o.enabled) {
                int width, height;
                head.get_geometry (out o.x, out o.y, out width, out height);
            }
            var existing = outputs.lookup (head);
            o.primary = existing != null && existing.primary;
            return o;
        }

        private void apply_layout () {
            var monitors = new GLib.GenericArray<Polo.Output> ();
            var backends = new GLib.HashTable<Polo.Output, Backend.Output> (GLib.direct_hash, GLib.direct_equal);
            foreach (var head in heads) {
                var o = describe (head);
                monitors.add (o);
                backends.insert (o, head);
            }
            if (drm) {
                var iface = settings ("org.mate.interface");
                var xrandr = settings ("org.mate.SettingsDaemon.plugins.xrandr");
                Outputs.Layout.arrange (monitors, store.saved, store.imported,
                    iface != null ? iface.get_int ("window-scaling-factor") : 0,
                    xrandr == null || xrandr.get_boolean ("turn-on-laptop-monitor-at-startup"),
                    xrandr == null || xrandr.get_boolean ("turn-on-external-monitors-at-startup"));
            } else {
                Outputs.Layout.ensure_primary (monitors);
            }
            commit (monitors, backends, true);
        }

        private bool commit (GLib.GenericArray<Polo.Output> monitors,
                             GLib.HashTable<Polo.Output, Backend.Output> backends, bool remember) {
            bool success = true;
            /* Enable destinations before disabling sources, so windows can relocate. */
            for (int pass = 0; pass < 2; pass++) {
                foreach (var o in monitors) {
                    if (o.enabled != (pass == 0)) {
                        continue;
                    }
                    var head = backends[o];
                    if (drm) {
                        try {
                            head.apply (o.width, o.height, o.rate, o.scale, o.transform, o.x, o.y, o.enabled);
                        } catch (GLib.Error e) {
                            GLib.warning ("Output configuration failed: %s", e.message);
                            success = false;
                        }
                    }
                    var existing = outputs.lookup (head);
                    if (!head.get_enabled ()) {
                        if (existing != null) {
                            model.remove_output (existing);
                            outputs.remove (head);
                        }
                        continue;
                    }
                    var rect = Marco.Rect ();
                    head.get_geometry (out rect.x, out rect.y, out rect.width, out rect.height);
                    if (existing == null) {
                        existing = new Policy.Output (o.name, rect);
                        existing.primary = o.primary;
                        existing.scale = head.get_scale ();
                        outputs.insert (head, existing);
                        model.add_output (existing);
                    } else {
                        existing.primary = o.primary;
                        existing.scale = head.get_scale ();
                        model.reconfigure_output (existing, rect);
                    }
                    head.set_background_color (BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE);
                }
            }
            if (drm && success && remember) {
                store.remember (monitors);
            }
            int image_scale = 1;
            foreach (var output in model.get_outputs ()) {
                image_scale = int.max (image_scale, output.scale);
            }
            decorations.set_image_scale (image_scale);
            layer_shell.arrange ();
            server.output_state_changed ();
            layout_changed ();
            return success;
        }

        public signal void layout_changed ();

        public GLib.GenericArray<Polo.Output> current_outputs () {
            var current = new GLib.GenericArray<Polo.Output> ();
            foreach (var head in heads) {
                current.add (describe (head));
            }
            return current;
        }

        public void remember_current () {
            if (drm) {
                store.remember (current_outputs ());
            }
        }

        public bool apply_monitors (GLib.GenericArray<Polo.Output> wanted, bool remember) {
            if (!drm || wanted.length != heads.length) {
                return false;
            }
            var before = new GLib.GenericArray<Polo.Output> ();
            var after = new GLib.GenericArray<Polo.Output> ();
            var backends = new GLib.HashTable<Polo.Output, Backend.Output> (GLib.direct_hash, GLib.direct_equal);
            bool any_enabled = false;
            foreach (var head in heads) {
                var current = describe (head);
                before.add (current);
                backends.insert (current, head);
                Polo.Output? match = null;
                foreach (var candidate in wanted) {
                    if (candidate.name == current.name) {
                        match = candidate;
                    }
                }
                if (match == null) {
                    return false;
                }
                if (match.enabled) {
                    if (!has_mode (current, match.width, match.height, match.rate) ||
                        match.scale < 1 || match.scale > 8 || match.transform < 0 || match.transform > 7) {
                        return false;
                    }
                    any_enabled = true;
                }
                var next = describe (head);
                next.copy_configuration (match);
                after.add (next);
                backends.insert (next, head);
            }
            if (!any_enabled) {
                return false;
            }
            Outputs.Layout.ensure_primary (after);
            if (!commit (after, backends, remember)) {
                commit (before, backends, false);
                return false;
            }
            return true;
        }

        private static bool has_mode (Polo.Output o, int width, int height, int refresh) {
            foreach (var mode in o.modes) {
                if (mode.width == width && mode.height == height &&
                    (refresh == 0 || (mode.rate - refresh).abs () <= 1000)) {
                    return true;
                }
            }
            return false;
        }

        public bool apply_requests (Backend.OutputRequest[] requests, bool test) {
            if (!drm) {
                return false;
            }
            var wanted = new GLib.GenericArray<Polo.Output> ();
            var vrr = new GLib.HashTable<Backend.Output, bool> (GLib.direct_hash, GLib.direct_equal);
            foreach (var head in heads) {
                var next = describe (head);
                Backend.OutputRequest? request = null;
                foreach (var candidate in requests) {
                    if (candidate.output == head) {
                        request = candidate;
                    }
                }
                if (request == null) {
                    return false;
                }
                next.enabled = request.enabled;
                if (next.enabled) {
                    if (!has_mode (next, request.width, request.height, request.refresh) ||
                        (request.adaptive_sync && !head.get_adaptive_sync_supported ())) {
                        return false;
                    }
                    next.width = request.width;
                    next.height = request.height;
                    next.rate = request.refresh;
                    next.x = request.x;
                    next.y = request.y;
                    next.transform = request.transform;
                    next.scale = (int) GLib.Math.round (request.scale).clamp (1, 8);
                }
                vrr.insert (head, request.adaptive_sync);
                wanted.add (next);
            }
            bool any_enabled = false;
            foreach (var next in wanted) {
                any_enabled |= next.enabled;
            }
            if (!any_enabled) {
                return false;
            }
            if (test) {
                return true;
            }
            var before = current_outputs ();
            if (!apply_monitors (wanted, true)) {
                return false;
            }
            foreach (var head in heads) {
                if (head.get_enabled () && !head.set_adaptive_sync (vrr[head])) {
                    apply_monitors (before, true);
                    return false;
                }
            }
            return true;
        }

        /* ---- Seats and input -------------------------------------------- */

        private void push_work_areas () {
            foreach (var head in heads) {
                var output = outputs.lookup (head);
                if (output != null) {
                    var area = output.work_area;
                    head.set_work_area (area.x, area.y, area.width, area.height);
                    head.set_primary (output.primary);
                }
            }
        }

        public Policy.Output? policy_output (Backend.Output backend_output) {
            return outputs.lookup (backend_output);
        }

        public bool has_touchpad () {
            return device_settings.has_touchpad ();
        }

        public Backend.Output? backend_output (Policy.Output output) {
            foreach (var head in heads) {
                if (outputs.lookup (head) == output) {
                    return head;
                }
            }
            return null;
        }

        private static GLib.Settings? settings_or_null (string id) {
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (id, true) : null;
            return schema != null ? new GLib.Settings.full (schema, null, null) : null;
        }

        private void on_input_device_added (Backend.InputDevice device) {
            var adapter = new Input.LibinputDevice (device);
            Logging.scope (LogDomain.INPUT).info ("input device \"%s\"%s", adapter.name,
                                                  adapter.is_touchpad () ? " (touchpad)" : "");
            device_settings.add (adapter);
            device.removed.connect (() => device_settings.remove (adapter));
        }

        private void on_seat_added (Backend.Seat seat) {
            seats.add (seat);
            seat.destroyed.connect (() => seats.remove (seat));
            seat.set_button_filter (on_button);
            seat.set_key_filter (on_key);
            seat.motion.connect (on_motion);
            keyboard.add_seat (seat);
            double x, y;
            seat.get_pointer_position (out x, out y);
            model.pointer_moved (x, y);
        }

        /* display->grab_op: meta_display_begin_grab_op () refuses a second one. */
        public bool grab_active {
            get { return move_resize.active || switcher.active || workspace_switcher.active || frame_input.active; }
        }

        private Input.MediaKeys? media_keys = null;

        private void on_system_bell (Backend.Toplevel? toplevel) {
            var xdg = toplevel != null ? windows.lookup (toplevel) : null;
            bell.ring (xdg != null && xdg.mapped ? xdg.window : null);
        }

        public void set_media_keys (Input.MediaKeys? keys) {
            if (media_keys != null) {
                media_keys.stop_repeat ();
            }
            media_keys = keys;
        }

        private Backend.FilterResult on_key (Backend.Seat seat, Backend.KeyEvent event) {
            bool locked = server.get_lock_state () != Backend.LockState.UNLOCKED;
            if (media_keys != null && (!grab_active || !event.pressed) &&
                media_keys.filter (event, locked) == Backend.FilterResult.CONSUME) {
                return Backend.FilterResult.CONSUME;
            }
            if (locked) {
                return Backend.FilterResult.PASS;
            }
            if (grab_active) {
                Backend.FilterResult grabbed;
                if (move_resize.active) {
                    grabbed = move_resize.key (seat, event);
                } else if (switcher.active) {
                    grabbed = switcher.key (seat, event);
                } else {
                    grabbed = workspace_switcher.key (seat, event);
                }
                /* meta_display_process_key_event (): a kept grab goes on to the global bindings. */
                if (grab_active && event.pressed && grabbed == Backend.FilterResult.CONSUME) {
                    bindings.run_global (seat, event);
                    model.focus_policy.binding_ran ();
                }
                return grabbed;
            }
            var result = bindings.filter (seat, event);
            if (result == Backend.FilterResult.CONSUME) {
                model.focus_policy.binding_ran ();
            }
            return result;
        }

        private void on_prefs_changed () {
            bindings.rebuild (model.prefs);
            load_decorations ();
            /* Changing general/compositing-manager can add or remove shadows. */
            restack ();
        }

        public void set_command_launcher (owned Input.CommandLauncher? launcher) {
            actions.set_launcher ((owned) launcher);
        }

        public void set_error_dialog (owned Input.ErrorDialog? dialog) {
            actions.set_error_dialog ((owned) dialog);
        }

        private XdgWindow? frame_owner (Backend.View view) {
            if (view.get_toplevel () != null) {
                return null;
            }
            foreach (var xdg in windows.get_values ()) {
                if (xdg.mapped && xdg.owns_frame_view (view)) {
                    return xdg;
                }
            }
            return null;
        }

        private Window? window_of (Backend.View? view) {
            if (view == null) {
                return null;
            }
            unowned Backend.Toplevel? toplevel = view.get_toplevel ();
            var xdg = toplevel != null ? windows.lookup (toplevel) : frame_owner (view);
            return xdg != null && xdg.mapped ? xdg.window : null;
        }

        private void on_motion (Backend.Seat seat, double x, double y, uint time_msec, Backend.View? view) {
            model.pointer_moved (x, y);
            model.pointer_entered (window_of (view), time_msec);
            frame_input.motion (seat, x, y, view != null ? frame_owner (view) : null);
        }

        private void on_pointer_warped (int x, int y) {
            foreach (var seat in seats) {
                seat.warp_pointer (x, y);
            }
        }

        /* Match Meta's passive button grabs. */
        private bool grabbed (Backend.ButtonEvent event) {
            Marco.VirtualModifier configured;
            if (!Marco.parse_modifier (model.prefs.mouse_button_modifier, out configured)) {
                return false;
            }
            var grab_mask = Input.Accelerators.canonical (configured);
            if (grab_mask == 0 || event.button < BTN_LEFT || event.button > BTN_MIDDLE) {
                return false;
            }
            var mods = Input.Accelerators.from_real ((Backend.Modifiers) event.modifiers);
            return mods == grab_mask ||
                   (event.button == BTN_LEFT && mods == (grab_mask | Marco.VirtualModifier.SHIFT_MASK));
        }

        /* Marco display.c event_callback (), ButtonPress on a window with no grab. */
        private Backend.FilterResult on_button (Backend.Seat seat, Backend.ButtonEvent event) {
            Logging.scope (LogDomain.INPUT).debug (
                "button %u %s mods 0x%x at %.0f,%.0f over %s", event.button,
                event.pressed ? "pressed" : "released", event.modifiers, event.x, event.y,
                event.view != null ? "a view" : "nothing");

            model.pointer_moved (event.x, event.y);
            if (switcher.active) {
                switcher.button (event);
                return Backend.FilterResult.PASS;
            }
            if (workspace_switcher.active) {
                workspace_switcher.button (event);
                return Backend.FilterResult.PASS;
            }
            if (!event.pressed) {
                return Backend.FilterResult.PASS;
            }
            if (event.view == null) {
                layer_shell.click (null);
                return Backend.FilterResult.PASS;
            }
            unowned Backend.Toplevel? toplevel = event.view.get_toplevel ();
            var xdg = toplevel != null ? windows.lookup (toplevel) : frame_owner (event.view);
            if (xdg == null || !xdg.mapped) {
                layer_shell.click (event.view);
                return Backend.FilterResult.PASS;
            }
            var window = xdg.window;
            layer_shell.window_clicked ();

            bool unmodified = !grabbed (event);
            if (unmodified && toplevel == null) {
                return frame_input.press (seat, xdg, event) ? Backend.FilterResult.CONSUME
                                                            : Backend.FilterResult.PASS;
            }
            uint resize_button = model.prefs.resize_with_right_button ? BTN_RIGHT : BTN_MIDDLE;
            uint menu_button = model.prefs.resize_with_right_button ? BTN_MIDDLE : BTN_RIGHT;

            if (unmodified || event.button == BTN_LEFT) {
                model.button_pressed (window, false, event.time_msec);
                if (unmodified) {
                    return Backend.FilterResult.PASS;
                }
                if (window.has_feature (WindowFeatures.MOVE)) {
                    var grab = seat.start_pointer_grab_for_binding (event.view);
                    if (grab != null) {
                        move_resize.begin (seat, window, Input.GrabOp.MOVING, grab, false,
                                           event.button, (int) event.x, (int) event.y,
                                           event.time_msec);
                    }
                }
                return Backend.FilterResult.CONSUME;
            } else if (event.button == resize_button) {
                if (window.has_feature (WindowFeatures.RESIZE)) {
                    var client = window.client_rect ();
                    bool west = event.x < client.x + 1 * client.width / 3;
                    bool east = event.x > client.x + 2 * client.width / 3;
                    bool north = event.y < client.y + 1 * client.height / 3;
                    bool south = event.y > client.y + 2 * client.height / 3;
                    Input.GrabOp op;
                    if (north && west) {
                        op = Input.GrabOp.RESIZING_NW;
                    } else if (north && east) {
                        op = Input.GrabOp.RESIZING_NE;
                    } else if (south && west) {
                        op = Input.GrabOp.RESIZING_SW;
                    } else if (south && east) {
                        op = Input.GrabOp.RESIZING_SE;
                    } else if (north) {
                        op = Input.GrabOp.RESIZING_N;
                    } else if (west) {
                        op = Input.GrabOp.RESIZING_W;
                    } else if (east) {
                        op = Input.GrabOp.RESIZING_E;
                    } else if (south) {
                        op = Input.GrabOp.RESIZING_S;
                    } else {
                        op = Input.GrabOp.NONE;
                    }
                    if (op != Input.GrabOp.NONE) {
                        var grab = seat.start_pointer_grab_for_binding (event.view);
                        if (grab != null) {
                            move_resize.begin (seat, window, op, grab, false, event.button,
                                               (int) event.x, (int) event.y, event.time_msec);
                        }
                    }
                }
                return Backend.FilterResult.CONSUME;
            } else if (event.button == menu_button) {
                if (model.prefs.raise_on_click) {
                    model.raise (window);
                }
                window_menu.show (window, (int) event.x, (int) event.y);
                return Backend.FilterResult.CONSUME;
            }
            return Backend.FilterResult.PASS;
        }

        private ForeignParent? foreign_parent = null;

        public void set_foreign_parent (owned ForeignParent? parent) {
            foreign_parent = (owned) parent;
        }

        public Backend.Seat? default_seat () {
            return seats.length > 0 ? seats[0] : null;
        }

        public void set_keyboard_focus (Backend.View? view) {
            /* An exclusive layer surface keeps the keyboard. */
            unowned Backend.View? exclusive =
                layer_shell != null ? layer_shell.exclusive_view () : null;
            if (exclusive != null && view != exclusive) {
                return;
            }
            foreach (var seat in seats) {
                seat.set_keyboard_focus (view);
            }
#if POLO_HAVE_XWAYLAND
            if (view == null && xwm != null) {
                xwm.clear_focus ();
            }
#endif
        }

        /* Give keyboard focus back to the model's window after an
         * exclusive layer surface released it. */
        internal void restore_focus () {
            if (layer_shell.exclusive_view () != null) {
                return;
            }
            if (model.focused == null) {
                set_keyboard_focus (null);
                return;
            }
            var xdg = model.focused.adapter as XdgWindow;
            if (xdg != null && xdg.mapped) {
                set_keyboard_focus (xdg.toplevel.get_view ());
            }
        }

        public void pointer_position (out double x, out double y) {
            x = y = 0;
            if (seats.length > 0) {
                seats[0].get_pointer_position (out x, out y);
            }
        }

        private void on_focus_changed () {
            if (model.focused == null) {
                set_keyboard_focus (null);
            }
        }

        /* ---- Windows ---------------------------------------------------- */

        private void on_new_toplevel (Backend.Toplevel toplevel) {
#if POLO_HAVE_XWAYLAND
            unowned Xwm.XWindow? xwindow = xwm != null ? xwm.window_for_toplevel (toplevel) : null;
            if (xwindow != null && xwindow.get_override_redirect ()) {
                override_redirects.insert (toplevel, new OverrideRedirectWindow (this, toplevel, xwindow));
                return;
            }
            if (xwindow != null) {
                var x_window = new X11Window (this, toplevel, xwindow);
                windows.insert (toplevel, x_window);
                x_windows.insert (xwindow, x_window);
                model.manage (x_window.window);
                return;
            }
#endif
            var window = new XdgWindow (this, toplevel);
            windows.insert (toplevel, window);
            model.manage (window.window);
            Logging.scope (LogDomain.POLICY).debug ("new %s", window.window.describe ());
        }

        public XdgWindow? lookup (Backend.Toplevel toplevel) {
            return windows.lookup (toplevel);
        }

        /* The nearest mapped xdg ancestor: an unmapped parent hands its children to its own parent. */
        internal Window? resolve_parent (XdgWindow child) {
#if POLO_HAVE_XWAYLAND
            var x_child = child as X11Window;
            if (x_child != null) {
                var parent = x_child.xwindow.get_transient_for ();
                var x_parent = parent != null ? x_windows.lookup (parent) : null;
                return x_parent != null && x_parent.mapped ? x_parent.window : null;
            }
#endif
            if (child.parent_toplevel == null && foreign_parent != null) {
                var foreign = foreign_parent (child.toplevel);
                if (foreign != null && model.stack_index (foreign) >= 0) {
                    return foreign;
                }
            }
            var seen = new GLib.HashTable<XdgWindow, bool> (GLib.direct_hash, GLib.direct_equal);
            seen.insert (child, true);
            for (var parent = child.parent_toplevel; parent != null;) {
                var xdg = windows.lookup (parent);
                if (xdg == null || seen.contains (xdg)) {
                    return null;
                }
                if (xdg.mapped) {
                    return xdg.window;
                }
                seen.insert (xdg, true);
                parent = xdg.parent_toplevel;
            }
            return null;
        }

        internal void relink_children () {
            foreach (var xdg in windows.get_values ()) {
                if (xdg.mapped) {
                    model.set_transient_for (xdg.window, resolve_parent (xdg));
                }
            }
        }

        /* A client of pid has something on screen, for the session's phases. */
        public signal void client_mapped (int pid);

        internal void window_ready (XdgWindow window) {
            double x, y;
            pointer_position (out x, out y);
            model.pointer_moved (x, y);
            model.manage (window.window);
            model.set_transient_for (window.window, resolve_parent (window));
            model.map (window.window);
            relink_children ();
            client_mapped (window.client_pid ());
        }

        internal void window_withdrawn (XdgWindow window, Window old) {
            model.unmanage (old);
            relink_children ();
        }

        internal void window_destroyed (XdgWindow window) {
#if POLO_HAVE_XWAYLAND
            var x_window = window as X11Window;
            if (x_window != null && x_windows.lookup (x_window.xwindow) == x_window) {
                x_windows.remove (x_window.xwindow);
            }
#endif
            frame_input.forget (window);
            windows.remove (window.toplevel);
            model.unmanage (window.window);
        }

        /* Map Marco stack layers to compositor layers. */
        private static Backend.LayerKind backend_layer (Window window) {
            switch (window.layer) {
                case Marco.StackLayer.DESKTOP:
                    return Backend.LayerKind.BOTTOM;
                case Marco.StackLayer.BOTTOM:
                    return Backend.LayerKind.BELOW;
                case Marco.StackLayer.TOP:
                    /* TOP and DOCK share Marco's value; docks are panels. */
                    return window.window_type == Marco.WindowType.DOCK
                        ? Backend.LayerKind.DOCK : Backend.LayerKind.ABOVE;
                case Marco.StackLayer.FULLSCREEN:
                case Marco.StackLayer.FOCUSED_WINDOW:
                    return Backend.LayerKind.FULLSCREEN;
                default:
                    return Backend.LayerKind.NORMAL;
            }
        }

#if POLO_HAVE_XWAYLAND
        private Xwm.Manager? xwm = null;
        private Ewmh? ewmh = null;

        internal Xwm.Manager? xwm_manager {
            get { return xwm; }
        }

        public void set_num_workspaces_writer (owned NumWorkspacesWriter writer) {
            if (ewmh != null) {
                ewmh.write_num_workspaces_func = (owned) writer;
            }
        }

        /* Handle _NET_WM_MOVERESIZE requests. */
        internal void net_wm_moveresize (Window window, int x_root, int y_root, uint action, uint button) {
            const uint CANCEL = 11;
            Input.GrabOp[] ops = {
                Input.GrabOp.RESIZING_NW, Input.GrabOp.RESIZING_N, Input.GrabOp.RESIZING_NE,
                Input.GrabOp.RESIZING_E, Input.GrabOp.RESIZING_SE, Input.GrabOp.RESIZING_S,
                Input.GrabOp.RESIZING_SW, Input.GrabOp.RESIZING_W, Input.GrabOp.MOVING,
                Input.GrabOp.KEYBOARD_RESIZING_UNKNOWN, Input.GrabOp.KEYBOARD_MOVING
            };
            if (action == CANCEL) {
                move_resize.end_grab_op ();
                return;
            }
            if (action >= ops.length || seats.length == 0) {
                return;
            }
            var op = ops[action];
            var seat = seats[0];
            if (!window.has_feature (op.is_moving () ? WindowFeatures.MOVE : WindowFeatures.RESIZE)) {
                return;
            }
            if (op.is_keyboard ()) {
                move_resize.begin_keyboard (seat, window, op, model.last_user_time);
                return;
            }
            var grab = seat.begin_pointer_grab ();
            if (grab != null) {
                move_resize.begin (seat, window, op, grab, true, button, x_root, y_root, model.last_user_time);
            }
        }
        private GLib.HashTable<Xwm.XWindow, X11Window> x_windows =
            new GLib.HashTable<Xwm.XWindow, X11Window> (GLib.direct_hash, GLib.direct_equal);
        private GLib.HashTable<Backend.Toplevel, OverrideRedirectWindow> override_redirects =
            new GLib.HashTable<Backend.Toplevel, OverrideRedirectWindow> (GLib.direct_hash, GLib.direct_equal);

        internal void override_redirect_gone (Backend.Toplevel toplevel) {
            override_redirects.remove (toplevel);
        }

        /* eager: start the X server now and keep it running. */
        public void start_xwayland (bool eager) {
            xwm = new Xwm.Manager (server);
            xwm.set_eager (eager);
            xwm.ready.connect ((name) => {
                Logging.scope (LogDomain.POLO).info ("X display %s", name);
            });
            ewmh = new Ewmh (model, xwm, decorations);
            xwm.bell.connect ((xwindow) => {
                var x_window = xwindow != null ? x_windows.lookup (xwindow) : null;
                bell.ring (x_window != null && x_window.mapped ? x_window.window : null);
            });
            xwm.watched_property_changed.connect ((xid, atom) => {
                foreach (var x_window in x_windows.get_values ()) {
                    x_window.on_watched_property (xid, atom);
                }
            });
            xwm.exited.connect ((crashed) => {
                if (crashed) {
                    Logging.scope (LogDomain.POLO).warning ("Xwayland crashed; X clients died, the session goes on");
                }
            });
            try {
                xwm.start ();
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).warning ("no X server: %s", e.message);
                xwm = null;
            }
        }
#endif

        /* Project the model's stack onto the backend layers. */
        private void restack () {
            var per_layer = new GLib.HashTable<int, GLib.GenericArray<Backend.View>> (
                GLib.direct_hash, GLib.direct_equal);
            foreach (var window in model.get_stack ()) {
                var xdg = window.adapter as XdgWindow;
                if (xdg == null || !xdg.mapped) {
                    continue;
                }
                unowned Backend.View view = xdg.toplevel.get_view ();
                if (!model.is_showing (window)) {
                    view.move_to_layer (null);
                    xdg.stack_frame (new GLib.GenericArray<Backend.View> (), null);
                    continue;
                }
                int kind = (int) backend_layer (window);
                var views = per_layer.lookup (kind);
                if (views == null) {
                    views = new GLib.GenericArray<Backend.View> ();
                    per_layer.insert (kind, views);
                }
                if (window == model.focused && tile_preview.view != null) {
                    tile_preview.view.move_to_layer (server.get_layer ((Backend.LayerKind) kind));
                    views.add (tile_preview.view);
                }
                xdg.refresh_frame ();
                xdg.stack_frame (views, server.get_layer ((Backend.LayerKind) kind));
                /* When shaded, hide the client below its titlebar. */
                if (window.has_state (WindowState.SHADED) && window.has_frame ()) {
                    view.move_to_layer (null);
                } else {
                    views.add (view);
                }
            }
            foreach (var kind in per_layer.get_keys ()) {
                server.get_layer ((Backend.LayerKind) kind).restack (per_layer[kind].data);
            }
        }

        /* Backend.TestInspector: report the model, not libweston. */
        public bool inspect (Backend.Toplevel toplevel, ref Backend.TestWindowInfo info) {
            var xdg = windows.lookup (toplevel);
            if (xdg == null || !xdg.mapped) {
                return false;
            }
            var window = xdg.window;
            var layer = backend_layer (window);
            int index = 0;
            foreach (var other in model.get_stack ()) {
                if (other == window) {
                    break;
                }
                if (model.is_showing (other) && backend_layer (other) == layer) {
                    index++;
                }
            }

            var client = window.client_rect ();
            info.x = client.x;
            info.y = client.y;
            info.width = client.width;
            info.height = client.height;
            info.layer = (int) layer;
            info.stack_index = index;
            info.workspace = window.workspace;
            info.state = (uint32) window.state;
            info.focused = model.focused == window;
            return true;
        }
    }
}
