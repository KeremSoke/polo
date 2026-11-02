/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate-settings-daemon 1.28's keyboard plugin for Polo's seats:
 * msd-keyboard-manager.c (repeat, rate, delay, NumLock) and
 * msd-keyboard-xkb.c (the layouts of xkb-settings.vala), with libxklavier's
 * window states (xklavier_toplevel.c, xklavier_evt.c) for group-per-window
 * and default-group. Key click has no Wayland counterpart.
 */

namespace Polo.Input {
    public class Keyboard : GLib.Object {
        private const string KEYBOARD_SCHEMA = "org.mate.peripherals-keyboard";
        private const string KBD_SCHEMA = "org.mate.peripherals-keyboard-xkb.kbd";
        private const string DESKTOP_SCHEMA = "org.mate.peripherals-keyboard-xkb.general";
        /* xkl_engine_get_max_num_groups () with XKB: XkbNumKbdGroups. */
        private const int MAX_NUM_GROUPS = 4;

        private enum NumLockState {
            OFF = 0,
            ON = 1,
            UNKNOWN = 2
        }

        private Backend.Server server;
        private Policy.Model model;
        private XkbConfig system;
        private GLib.Settings? keyboard = null;
        private GLib.Settings? kbd = null;
        private GLib.Settings? desktop = null;
        private GLib.GenericArray<Backend.Seat> seats = new GLib.GenericArray<Backend.Seat> ();
        private XkbConfig? active = null;
        private bool group_per_window = true;
        private int default_group = -1;

        /* system: the configuration before the user's (initial_sys_kbd_config). */
        public Keyboard (Backend.Server server, Policy.Model model, XkbConfig system) {
            this.server = server;
            this.model = model;
            this.system = system;
            keyboard = lookup (KEYBOARD_SCHEMA);
            kbd = lookup (KBD_SCHEMA);
            desktop = lookup (DESKTOP_SCHEMA);
            if (keyboard != null) {
                keyboard.changed.connect (on_keyboard_changed);
            }
            if (kbd != null) {
                kbd.changed.connect (apply_xkb_settings);
            }
            if (desktop != null) {
                desktop.changed.connect (apply_desktop_settings);
            }
            model.window_mapped.connect (add_toplevel_window);
            model.focus_changed.connect (on_focus_changed);
            apply_desktop_settings ();
        }

        public void shutdown () {
            if (keyboard != null) {
                keyboard.changed.disconnect (on_keyboard_changed);
            }
            if (kbd != null) {
                kbd.changed.disconnect (apply_xkb_settings);
            }
            if (desktop != null) {
                desktop.changed.disconnect (apply_desktop_settings);
            }
            model.window_mapped.disconnect (add_toplevel_window);
            model.focus_changed.disconnect (on_focus_changed);
            foreach (var seat in seats) {
                seat.group_changed.disconnect (on_group_changed);
                seat.numlock_changed.disconnect (on_numlock_changed);
            }
            seats.remove_range (0, seats.length);
        }

        private static GLib.Settings? lookup (string id) {
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (id, true) : null;
            if (schema == null) {
                Logging.scope (LogDomain.INPUT).info ("no %s schema", id);
                return null;
            }
            return new GLib.Settings.full (schema, null, null);
        }

        /*
         * The configuration the X server would start with: XKB_DEFAULT_*
         * when set, else what localed wrote for Xorg, else xkbcommon's
         * defaults.
         */
        public static XkbConfig system_config (bool use_localed) {
            if (GLib.Environment.get_variable ("XKB_DEFAULT_LAYOUT") != null || !use_localed) {
                return XkbConfig.from_rmlvo (GLib.Environment.get_variable ("XKB_DEFAULT_MODEL"),
                                             GLib.Environment.get_variable ("XKB_DEFAULT_LAYOUT"),
                                             GLib.Environment.get_variable ("XKB_DEFAULT_VARIANT"),
                                             GLib.Environment.get_variable ("XKB_DEFAULT_OPTIONS"));
            }
            try {
                var locale = new GLib.DBusProxy.for_bus_sync (GLib.BusType.SYSTEM, GLib.DBusProxyFlags.NONE,
                                                              null, "org.freedesktop.locale1",
                                                              "/org/freedesktop/locale1",
                                                              "org.freedesktop.locale1");
                return XkbConfig.from_rmlvo (property (locale, "X11Model"), property (locale, "X11Layout"),
                                             property (locale, "X11Variant"), property (locale, "X11Options"));
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.INPUT).info ("no localed keyboard configuration: %s", e.message);
                return new XkbConfig ();
            }
        }

        private static string? property (GLib.DBusProxy proxy, string name) {
            var value = proxy.get_cached_property (name);
            return value != null && value.is_of_type (GLib.VariantType.STRING) ? value.get_string () : null;
        }

        public void add_seat (Backend.Seat seat) {
            seats.add (seat);
            seat.destroyed.connect (() => seats.remove (seat));
            seat.group_changed.connect (on_group_changed);
            seat.numlock_changed.connect (on_numlock_changed);
            if (active != null) {
                activate_on (seat, active);
            } else {
                apply_xkb_settings ();
            }
            apply_settings (null);
        }

        /* ---- msd-keyboard-manager.c apply_settings () ----------------- */

        private void on_keyboard_changed (string key) {
            apply_settings (key);
        }

        private void apply_settings (string? key) {
            if (keyboard == null) {
                return;
            }
            if (keyboard.get_boolean ("repeat")) {
                int delay = keyboard.get_int ("delay");
                int rate = keyboard.get_int ("rate");
                server.set_keyboard_repeat (rate <= 0 ? 0 : rate, delay <= 0 ? 1 : delay);
            } else {
                server.set_keyboard_repeat (0, keyboard.get_int ("delay"));
            }

            bool rnumlock = keyboard.get_boolean ("remember-numlock-state");
            if (!rnumlock || key == null) {
                if (rnumlock) {
                    var state = (NumLockState) keyboard.get_enum ("numlock-state");
                    if (state == NumLockState.ON || state == NumLockState.OFF) {
                        foreach (var seat in seats) {
                            seat.set_numlock (state == NumLockState.ON);
                        }
                    }
                }
            }
        }

        private void on_numlock_changed (bool locked) {
            if (keyboard != null) {
                keyboard.set_enum ("numlock-state", locked ? NumLockState.ON : NumLockState.OFF);
            }
        }

        /* ---- msd-keyboard-xkb.c ---------------------------------------- */

        private void apply_desktop_settings () {
            if (desktop == null) {
                return;
            }
            group_per_window = desktop.get_boolean ("group-per-window");
            default_group = desktop.get_int ("default-group");
            if (default_group < -1 || default_group >= MAX_NUM_GROUPS) {
                default_group = -1;
            }
        }

        private void apply_xkb_settings () {
            if (seats.length == 0) {
                return;
            }
            XkbConfig config;
            if (kbd != null) {
                config = XkbConfig.from_settings (kbd.get_string ("model"), kbd.get_strv ("layouts"),
                                                  kbd.get_strv ("options"), system);
            } else {
                config = system;
            }
            if (!try_activating_xkb_config_if_new (config)) {
                if (config.filter ()) {
                    if (!try_activating_xkb_config_if_new (config)) {
                        Logging.scope (LogDomain.INPUT).warning (
                            "Could not activate the filtered XKB configuration");
                    }
                } else {
                    Logging.scope (LogDomain.INPUT).warning ("Could not activate the XKB configuration");
                }
            }
        }

        private bool try_activating_xkb_config_if_new (XkbConfig config) {
            if (active != null && active.equals (config)) {
                return true;
            }
            foreach (var seat in seats) {
                if (!activate_on (seat, config)) {
                    return false;
                }
            }
            active = config;
            Logging.scope (LogDomain.INPUT).info ("keymap: %s", config.to_string ());
            return true;
        }

        private bool activate_on (Backend.Seat seat, XkbConfig config) {
            try {
                return seat.set_keymap ("evdev", config.model, config.layout_names (), config.variant_names (),
                                        config.option_names ());
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.INPUT).info ("%s: %s", config.to_string (), e.message);
                return false;
            }
        }

        /* ---- libxklavier window states ---------------------------------- */

        private uint current_group () {
            return seats.length > 0 ? seats[0].get_group () : 0;
        }

        private void lock_group (uint group) {
            foreach (var seat in seats) {
                seat.lock_group (group);
            }
        }

        private void add_toplevel_window (Window window) {
            if (window.xkb_group >= 0) {
                return;
            }
            int group = -1;
            var transient_for = window.transient_for;
            if (transient_for != null && transient_for.xkb_group >= 0) {
                group = transient_for.xkb_group;
            }
            if (group == -1) {
                group = default_group;
            }
            window.xkb_group = group != -1 ? group : (int) current_group ();
            if (group != -1 && model.focused == window) {
                lock_group (group);
            }
        }

        private void on_focus_changed () {
            var window = model.focused;
            if (window == null || !group_per_window) {
                return;
            }
            if (window.xkb_group < 0) {
                add_toplevel_window (window);
                return;
            }
            if (current_group () != window.xkb_group) {
                lock_group (window.xkb_group);
            }
        }

        private void on_group_changed (uint group) {
            var window = model.focused;
            if (window != null && window.xkb_group >= 0) {
                window.xkb_group = (int) group;
            }
        }
    }
}
