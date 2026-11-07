/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The display-mode popup, as GNOME's monitor switcher: primary only,
 * mirror, extend and secondary only in the Alt-Tab popup, the next mode
 * after the current one selected. The key again (Super+P with Super held,
 * or the laptop's display key) moves on; releasing Super, Enter or a click
 * applies and saves it, Escape or a click elsewhere leaves the screens as
 * they are. Mirror is greyed out and skipped without a common resolution.
 */

namespace Polo.Shell {
    public class DisplaySwitcher : GLib.Object {
        public const string BINDING = "polo-display-mode";
        private const string GROUP = "polo-display";
        private const string KEYS_SCHEMA = "org.mate.polo.keybindings";
        private const uint32 XF86_DISPLAY = 0x1008ff59;
        private const uint KEY_ESC = 1;
        private const uint KEY_TAB = 15;
        private const uint KEY_ENTER = 28;
        private const uint KEY_SPACE = 57;
        private const uint KEY_KPENTER = 96;
        private const uint KEY_LEFT = 105;
        private const uint KEY_RIGHT = 106;

        private class Request {
            public bool[] available = new bool[Outputs.DisplayMode.COUNT];
            public int current;
            public unowned Backend.Seat? seat;
            public Backend.Modifiers primary = 0;
        }

        private Backend.Server server;
        private Desktop desktop;
        private Input.Bindings bindings;
        private GLib.Settings? keys = null;
        private Request? request = null;

        public bool active { get { return request != null; } }

        public DisplaySwitcher (Backend.Server server, Desktop desktop, Input.Bindings bindings) {
            this.server = server;
            this.desktop = desktop;
            this.bindings = bindings;
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (KEYS_SCHEMA, true) : null;
            if (schema != null && schema.has_key ("display-mode")) {
                keys = new GLib.Settings.full (schema, null, null);
                keys.changed["display-mode"].connect (bind_key);
            }
            bindings.register (BINDING, (binding, seat, event) => {
                var mask = binding.accelerator.mask;
                cycle (seat, Switcher.primary_modifier (mask));
            });
            bind_key ();
        }

        public void shutdown () {
            finish (false);
            if (keys != null) {
                keys.changed["display-mode"].disconnect (bind_key);
            }
            bindings.unregister (BINDING);
            bindings.set_custom (GROUP, new GLib.GenericArray<Input.Binding> ());
        }

        private void bind_key () {
            var custom = new GLib.GenericArray<Input.Binding> ();
            var accelerator = Input.Accelerator ();
            bool parsed = keys != null && Input.Accelerators.parse (keys.get_string ("display-mode"), out accelerator);
            if (parsed && !accelerator.is_disabled ()) {
                custom.add (new Input.Binding () {
                    name = BINDING, kind = Input.BindingKind.GLOBAL, accelerator = accelerator
                });
            }
            bindings.set_custom (GROUP, custom);
        }

        public bool can_switch () {
            return desktop.manages_outputs && desktop.current_outputs ().length >= 2;
        }

        public void cycle (Backend.Seat? seat, Backend.Modifiers held = 0) {
            if (request != null) {
                move (1);
                return;
            }
            var shell_ui = server.get_shell_ui ();
            if (!can_switch () || !shell_ui.is_bound () || desktop.grab_active) {
                return;
            }
            var outputs = desktop.current_outputs ();
            var next = new Request ();
            for (int i = 0; i < Outputs.DisplayMode.COUNT; i++) {
                next.available[i] = Outputs.DisplayModes.available (outputs, (Outputs.DisplayMode) i);
            }
            next.current = Outputs.DisplayModes.current (outputs);
            next.seat = seat ?? desktop.default_seat ();
            if (held != 0 && next.seat != null && held in (Backend.Modifiers) next.seat.get_modifiers ()) {
                next.primary = held;
            }
            request = next;
            move_from (next.current < 0 ? Outputs.DisplayMode.EXTEND - 1 : next.current, 1);
            if (request.primary != 0) {
                request.seat.modifiers_changed.connect (on_modifiers_changed);
            }
            shell_ui.switcher_select.connect (on_switcher_select);
            shell_ui.unbound.connect (on_unbound);
            show ();
        }

        private void move_from (int from, int delta) {
            int n = Outputs.DisplayMode.COUNT;
            int at = from;
            for (int i = 0; i < n; i++) {
                at = ((at + delta) % n + n) % n;
                if (request.available[at]) {
                    request.current = at;
                    return;
                }
            }
        }

        private void move (int delta) {
            move_from (request.current, delta);
            server.get_shell_ui ().update_selected (request.current + 1);
        }

        private void show () {
            var shell_ui = server.get_shell_ui ();
            for (int i = 0; i < Outputs.DisplayMode.COUNT; i++) {
                var mode = (Outputs.DisplayMode) i;
                shell_ui.switcher_entry (i + 1, mode.label (), null,
                                         request.available[i] ? 0 : Backend.SwitcherEntryFlags.HIDDEN);
                shell_ui.switcher_icon_name (i + 1, mode.icon_name ());
            }
            var model = desktop.model;
            var output = model.output_at (model.pointer_x, model.pointer_y);
            if (output == null && model.get_outputs ().length > 0) {
                output = model.get_outputs ()[0];
            }
            shell_ui.show_switcher (output != null ? desktop.backend_output (output) : null, request.current + 1,
                                    Outputs.DisplayMode.COUNT, false, (uint) int.max (model.prefs.icon_size, 96));
        }

        public Backend.FilterResult key (Backend.Seat seat, Backend.KeyEvent event) {
            if (request == null) {
                return Backend.FilterResult.PASS;
            }
            if (!event.pressed) {
                return Backend.FilterResult.CONSUME;
            }
            if (event.keysym == XF86_DISPLAY) {
                move (1);
                return Backend.FilterResult.CONSUME;
            }
            switch (event.keycode) {
                case KEY_ESC:
                    finish (false);
                    break;
                case KEY_ENTER:
                case KEY_KPENTER:
                case KEY_SPACE:
                    finish (true);
                    break;
                case KEY_TAB:
                    move (Backend.Modifiers.SHIFT in event.modifiers ? -1 : 1);
                    break;
                case KEY_RIGHT:
                    move (1);
                    break;
                case KEY_LEFT:
                    move (-1);
                    break;
                default:
                    break;
            }
            return Backend.FilterResult.CONSUME;
        }

        public void button (Backend.ButtonEvent event) {
            if (request == null || !event.pressed) {
                return;
            }
            if (event.view != null) {
                unowned Backend.Client? client = event.view.get_client ();
                if (client != null && client == server.get_shell_ui ().get_client ()) {
                    return;
                }
            }
            finish (false);
        }

        private void on_modifiers_changed (Backend.Seat seat, Backend.Modifiers modifiers) {
            if (request != null && seat == request.seat && !(request.primary in modifiers)) {
                finish (true);
            }
        }

        private void on_switcher_select (uint id) {
            if (request == null || id < 1 || id > Outputs.DisplayMode.COUNT || !request.available[id - 1]) {
                return;
            }
            request.current = (int) id - 1;
            finish (true);
        }

        private void on_unbound () {
            finish (false);
        }

        private void finish (bool apply) {
            if (request == null) {
                return;
            }
            var done = request;
            request = null;
            var shell_ui = server.get_shell_ui ();
            shell_ui.switcher_select.disconnect (on_switcher_select);
            shell_ui.unbound.disconnect (on_unbound);
            if (done.primary != 0 && done.seat != null) {
                done.seat.modifiers_changed.disconnect (on_modifiers_changed);
            }
            shell_ui.hide_switcher ();
            if (apply) {
                set_mode ((Outputs.DisplayMode) done.current);
            }
        }

        public bool set_mode (Outputs.DisplayMode mode) {
            var wanted = Outputs.DisplayModes.configure (desktop.current_outputs (), mode, desktop.window_scaling ());
            if (wanted == null || !desktop.apply_monitors (wanted, true)) {
                Logging.scope (LogDomain.POLO).warning ("cannot switch the screens to %s", mode.label ());
                return false;
            }
            return true;
        }
    }
}
