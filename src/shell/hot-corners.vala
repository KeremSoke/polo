/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hot corners and edges from org.mate.polo.hot-corners: places them on the
 * screens the settings choose, feeds pointer motion to the policy's
 * tracker, and runs the action of the corner or edge that fires. Nothing
 * fires while the session is locked, the magnifier is on, another grab is
 * active, or, unless fullscreen is set, a fullscreen window covers the spot.
 */

namespace Polo.Shell {
    public class HotCorners : GLib.Object {
        public const string SCHEMA = "org.mate.polo.hot-corners";
        public const string SCREEN_SCHEMA = "org.mate.polo.hot-corners.screen";
        public const string SCREEN_PATH = "/org/mate/polo/hot-corners/screens/";
        private const uint BTN_LEFT = 0x110;

        public delegate void LockScreen ();

        private Backend.Server server;
        private Policy.Model model;
        private Desktop desktop;
        private GLib.Settings? settings;
        private Policy.HotCornerTracker tracker = new Policy.HotCornerTracker ();
        private bool dirty = true;
        private bool enabled = false;
        private uint hover_source = 0;
        private unowned Backend.Seat? hover_seat = null;
        private Input.CommandLauncher? launcher = null;
        private Input.ErrorDialog? error_dialog = null;
        private LockScreen? lock_screen = null;
        private GLib.GenericArray<Backend.Seat> seats = new GLib.GenericArray<Backend.Seat> ();
        private GLib.SettingsSchema? screen_schema = null;
        private Effects.HotCornerRipple ripple;
        private GLib.HashTable<string, GLib.Settings> screen_settings =
            new GLib.HashTable<string, GLib.Settings> (GLib.str_hash, GLib.str_equal);
        private GLib.GenericArray<GLib.Settings?> screen_of = new GLib.GenericArray<GLib.Settings?> ();

        public HotCorners (Backend.Server server, Policy.Model model, Desktop desktop) {
            this.server = server;
            this.model = model;
            this.desktop = desktop;
            ripple = new Effects.HotCornerRipple (server, () => desktop.decorations.get_colors ());
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (SCHEMA, true) : null;
            if (schema != null) {
                settings = new GLib.Settings.full (schema, null, null);
                settings.changed.connect (invalidate);
                screen_schema = source.lookup (SCREEN_SCHEMA, true);
            }
            desktop.layout_changed.connect (invalidate);
            model.work_areas_changed.connect (invalidate);
        }

        public void shutdown () {
            cancel_hover ();
            ripple.shutdown ();
            if (settings != null) {
                settings.changed.disconnect (invalidate);
            }
            desktop.layout_changed.disconnect (invalidate);
            model.work_areas_changed.disconnect (invalidate);
            foreach (var seat in seats) {
                seat.edge_motion.disconnect (on_edge_motion);
            }
            seats.remove_range (0, seats.length);
            foreach (var own in screen_settings.get_values ()) {
                own.changed.disconnect (invalidate);
            }
            screen_settings.remove_all ();
            screen_of.remove_range (0, screen_of.length);
            launcher = null;
            error_dialog = null;
            lock_screen = null;
        }

        public void add_seat (Backend.Seat seat) {
            seats.add (seat);
            seat.destroyed.connect (() => {
                if (hover_seat == seat) {
                    cancel_hover ();
                }
                seats.remove (seat);
            });
            seat.edge_motion.connect (on_edge_motion);
        }

        public void set_launcher (owned Input.CommandLauncher? launcher) {
            this.launcher = (owned) launcher;
        }

        public void set_error_dialog (owned Input.ErrorDialog? dialog) {
            error_dialog = (owned) dialog;
        }

        public void set_lock_screen (owned LockScreen? lock_screen) {
            this.lock_screen = (owned) lock_screen;
        }

        private void invalidate () {
            dirty = true;
            cancel_hover ();
        }

        private static int64 now_ms () {
            return GLib.get_monotonic_time () / 1000;
        }

        private bool allowed (Policy.Output output, string screens, string[] selected) {
            if (screens == "primary") {
                return output.primary;
            }
            if (screens == "selected") {
                foreach (var id in desktop.monitor_ids_of (output)) {
                    if (id in selected) {
                        return true;
                    }
                }
                return false;
            }
            return true;
        }

        private GLib.Settings? settings_for_screen (Policy.Output output) {
            if (screen_schema == null) {
                return null;
            }
            var ids = desktop.monitor_ids_of (output);
            if (ids.length == 0) {
                return null;
            }
            var own = screen_settings.lookup (ids[0]);
            if (own == null) {
                own = new GLib.Settings.full (screen_schema, null, SCREEN_PATH + ids[0] + "/");
                own.changed.connect (invalidate);
                screen_settings.insert (ids[0], own);
            }
            return own;
        }

        private string action_for (Policy.HotSpot spot, int screen) {
            GLib.Settings? source = settings;
            if (screen >= 0 && screen < screen_of.length && screen_of[screen] != null &&
                !screen_of[screen].get_boolean ("use-shared")) {
                source = screen_of[screen];
            }
            var text = source.get_string (spot.to_key ()).strip ();
            return text == "" ? "none" : text;
        }

        private void refresh () {
            if (!dirty) {
                return;
            }
            dirty = false;
            enabled = settings != null && settings.get_boolean ("enabled");
            var config = new Policy.HotCornerSettings ();
            var regions = new GLib.GenericArray<Policy.HotRegion> ();
            if (enabled) {
                config.trigger = Policy.HotTrigger.parse (settings.get_string ("trigger"));
                config.pressure_threshold = settings.get_uint ("pressure-threshold");
                config.pressure_time = settings.get_uint ("pressure-time");
                config.cooldown = settings.get_uint ("cooldown");
                int64 delay = settings.get_uint ("hover-delay");
                var delays = settings.get_value ("corner-delays");
                for (int i = 0; i < Policy.HotCornerSettings.SPOTS; i++) {
                    var spot = (Policy.HotSpot) i;
                    config.hover_delay[i] = delay;
                    uint32 own;
                    if (delays.lookup (spot.to_key (), "u", out own)) {
                        config.hover_delay[i] = own;
                    }
                    config.spot_enabled[i] = true;
                }
                var outputs = model.get_outputs ();
                var rects = new Marco.Rect[outputs.length];
                var permitted = new bool[outputs.length];
                string screens = settings.get_string ("screens");
                string[] selected = settings.get_strv ("selected-screens");
                screen_of.remove_range (0, screen_of.length);
                for (int i = 0; i < outputs.length; i++) {
                    rects[i] = outputs[i].rect;
                    permitted[i] = allowed (outputs[i], screens, selected);
                    screen_of.add (settings_for_screen (outputs[i]));
                }
                foreach (var region in Policy.HotCornerGeometry.compute (
                             rects, permitted, Policy.HotLayout.parse (settings.get_string ("layout")))) {
                    if (action_for (region.spot, region.screen) != "none") {
                        regions.add (region);
                    }
                }
            }
            tracker.configure (regions, config);
            cancel_hover ();
            enabled = enabled && tracker.get_regions ().length > 0;
        }

        private void cancel_hover () {
            if (hover_source != 0) {
                GLib.Source.remove (hover_source);
                hover_source = 0;
            }
            hover_seat = null;
        }

        private void schedule_hover (Backend.Seat seat) {
            cancel_hover ();
            int64 deadline = tracker.hover_deadline ();
            if (deadline < 0) {
                return;
            }
            hover_seat = seat;
            uint wait = (uint) int64.max (0, deadline - now_ms ());
            hover_source = GLib.Timeout.add (wait + 1, () => {
                hover_source = 0;
                unowned Backend.Seat? fired_seat = hover_seat;
                var region = tracker.tick (now_ms ());
                if (region != null && fired_seat != null) {
                    hover_seat = null;
                    run (fired_seat, region);
                } else if (fired_seat != null) {
                    schedule_hover (fired_seat);
                }
                return GLib.Source.REMOVE;
            });
        }

        private void on_edge_motion (Backend.Seat seat, double x, double y, double push_x, double push_y,
                                     bool absolute, uint buttons) {
            refresh ();
            if (!enabled) {
                return;
            }
            var region = tracker.motion (x, y, push_x, push_y, absolute, buttons, now_ms ());
            schedule_hover (seat);
            if (region != null) {
                run (seat, region);
            }
        }

        public bool button (Backend.Seat seat, Backend.ButtonEvent event) {
            refresh ();
            if (!enabled || !event.pressed || event.button != BTN_LEFT || !settings.get_boolean ("click")) {
                return false;
            }
            var under = tracker.region_at (event.x, event.y);
            if (under == null || blocked (under, action_for (under.spot, under.screen))) {
                return false;
            }
            var region = tracker.click (event.x, event.y, now_ms ());
            if (region != null) {
                run (seat, region);
            }
            return true;
        }

        private bool closes (string action) {
            return (action == "expo" && desktop.expo.active) ||
                   (action == "cube" && (desktop.cube.active || desktop.expo.active)) ||
                   (action == "window-switcher" && desktop.switcher.active) ||
                   (action.has_prefix ("scale") && desktop.scale.active);
        }

        private bool covered_by_fullscreen (Policy.HotRegion region) {
            foreach (var window in model.get_stack ()) {
                if (!window.has_state (WindowState.FULLSCREEN) || !model.is_showing (window)) {
                    continue;
                }
                var rect = window.outer_rect ();
                if (region.x >= rect.x && region.x < rect.x + rect.width &&
                    region.y >= rect.y && region.y < rect.y + rect.height) {
                    return true;
                }
            }
            return false;
        }

        private bool blocked (Policy.HotRegion region, string action) {
            if (server.get_lock_state () != Backend.LockState.UNLOCKED) {
                return true;
            }
            if (server.get_zoom () > 1.0) {
                return true;
            }
            bool closing = closes (action);
            if (desktop.grab_active && !closing) {
                return true;
            }
            return !closing && !settings.get_boolean ("fullscreen") && covered_by_fullscreen (region);
        }

        private void launch (string command) {
            if (launcher == null) {
                return;
            }
            try {
                launcher (command);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).warning ("hot corner: cannot run \"%s\": %s", command, e.message);
                if (error_dialog != null) {
                    error_dialog ("There was an error running <tt>%s</tt>:\n\n%s".printf (
                        GLib.Markup.escape_text (command), GLib.Markup.escape_text (e.message)));
                }
            }
        }

        private void run (Backend.Seat seat, Policy.HotRegion region) {
            string action = action_for (region.spot, region.screen);
            if (blocked (region, action)) {
                return;
            }
            Logging.scope (LogDomain.POLO).debug ("hot corner %s fired: %s", region.spot.to_key (), action);
            if (settings.get_boolean ("animation")) {
                var output = model.output_at (model.pointer_x, model.pointer_y);
                ripple.start (region, model.pointer_x, model.pointer_y, output != null ? output.scale : 1);
            }
            switch (action) {
                case "expo":
                    desktop.expo.toggle (seat);
                    break;
                case "cube":
                    if (desktop.cube.active || desktop.cube.available ()) {
                        desktop.cube.toggle (seat);
                    } else {
                        desktop.expo.toggle (seat);
                    }
                    break;
                case "scale":
                case "scale-all-workspaces":
                case "scale-application":
                case "scale-application-all-workspaces":
                    Policy.ScaleMode mode;
                    Policy.ScaleMode.from_key (action, out mode);
                    desktop.scale.toggle (seat, mode);
                    break;
                case "show-desktop":
                    if (model.showing_desktop) {
                        model.unshow_desktop ();
                        model.focus_default ();
                    } else {
                        model.show_desktop ();
                    }
                    break;
                case "window-switcher":
                    if (desktop.switcher.active) {
                        desktop.switcher.close ();
                    } else {
                        desktop.switcher.open_for_pointer (seat);
                    }
                    break;
                case "lock-screen":
                    if (lock_screen != null) {
                        lock_screen ();
                    }
                    break;
                case "run-dialog":
                    launch ("mate-panel --run-dialog");
                    break;
                default:
                    if (action.has_prefix ("command:")) {
                        string command = action.substring ("command:".length).strip ();
                        if (command != "") {
                            launch (command);
                        }
                    } else {
                        Logging.scope (LogDomain.POLO).warning ("hot corner %s: unknown action \"%s\"",
                                                                region.spot.to_key (), action);
                    }
                    break;
            }
        }
    }
}
