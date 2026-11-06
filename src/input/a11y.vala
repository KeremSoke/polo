/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AccessX, which X does in the server (xkb/xkbAccessX.c and the LatchMods
 * and MovePtr filters of xkb/xkbActions.c, Xorg 21.1) with the controls
 * mate-settings-daemon 1.28's a11y-keyboard plugin sets from
 * org.mate.accessibility-keyboard
 */

namespace Polo.Input {
    public class AccessX : GLib.Object {
        public const string SCHEMA = "org.mate.accessibility-keyboard";
        private const string DOMAIN = "mate-settings-daemon";

        private const uint KRG_WARN_MS = 4000;
        private const uint KRG_MS = 8000;
        private const int64 SHIFT_RESET_MS = 15000;
        private const uint MAX_SLOW_KEYS_DELAY = 500;
        private const uint MK_INTERVAL = 100;
        private const double MK_CURVE = 1.0 + 50 * 0.001;

        private const uint32 BTN_LEFT = 0x110;
        private const uint32 BTN_RIGHT = 0x111;
        private const uint32 BTN_MIDDLE = 0x112;

        private const Backend.Modifiers STICKY_MODS = Backend.Modifiers.SHIFT | Backend.Modifiers.CONTROL |
            Backend.Modifiers.MOD1 | Backend.Modifiers.MOD4 | Backend.Modifiers.MOD5;

        [CCode (cname = "g_dgettext", cheader_filename = "glib.h")]
        private static extern unowned string dgettext (string domain, string msgid);

        public delegate void Beep ();
        public delegate void Reply (bool accepted);
        public delegate void Confirm (string title, string question, string message, string accept, string reject,
                                      owned Reply reply);

        private Backend.Server server;
        private GLib.Settings? settings;
        private Beep beep;
        private Confirm confirm;

        private Backend.Seat? seat = null;
        private uint32 slow_key = 0;
        private uint slow_timer = 0;
        private GLib.HashTable<uint32, bool> held_back = new GLib.HashTable<uint32, bool> (null, null);
        private uint32 inactive_key = 0;
        private uint bounce_timer = 0;
        private uint shift_count = 0;
        private int64 last_shift_ms = 0;
        private uint krg_timer = 0;
        private uint timeout_timer = 0;
        private int64 last_pointer_usec = 0;

        private Backend.Modifiers latched = 0;
        private Backend.Modifiers locked = 0;
        private GLib.HashTable<uint32, uint> latch_keys = new GLib.HashTable<uint32, uint> (null, null);
        private uint break_idle = 0;
        private uint apply_idle = 0;

        private GLib.HashTable<uint32, bool> mouse_keys_down = new GLib.HashTable<uint32, bool> (null, null);
        private uint32 mouse_key = 0;
        private int mouse_dx = 0;
        private int mouse_dy = 0;
        private int mouse_counter = 0;
        private uint mouse_timer = 0;
        private uint32 default_button = BTN_LEFT;
        private uint32 locked_button = 0;

        public AccessX (Backend.Server server, GLib.Settings? settings, owned Beep beep, owned Confirm confirm) {
            this.server = server;
            this.settings = settings;
            this.beep = (owned) beep;
            this.confirm = (owned) confirm;
            if (settings != null) {
                settings.changed.connect (on_settings_changed);
            }
            server.set_key_prefilter (filter);
        }

        public void shutdown () {
            server.set_key_prefilter (null);
            if (settings != null) {
                settings.changed.disconnect (on_settings_changed);
            }
            foreach (uint id in new uint[] { slow_timer, bounce_timer, krg_timer, timeout_timer, mouse_timer,
                                             break_idle, apply_idle }) {
                if (id != 0) {
                    GLib.Source.remove (id);
                }
            }
            slow_timer = bounce_timer = krg_timer = timeout_timer = mouse_timer = break_idle = apply_idle = 0;
        }

        /* ---- Controls ------------------------------------------------------- */

        private bool flag (string key) {
            return settings != null && settings.settings_schema.has_key (key) && settings.get_boolean (key);
        }

        private int number (string key, int fallback) {
            return settings != null && settings.settings_schema.has_key (key) ? settings.get_int (key) : fallback;
        }

        private void set_flag (string key, bool value) {
            if (settings != null && settings.settings_schema.has_key (key) && settings.get_boolean (key) != value) {
                settings.set_boolean (key, value);
            }
        }

        private bool sticky_enabled { get { return flag ("stickykeys-enable"); } }
        private bool slow_enabled { get { return flag ("slowkeys-enable"); } }
        private bool bounce_enabled { get { return flag ("bouncekeys-enable"); } }
        private bool mouse_enabled { get { return flag ("mousekeys-enable"); } }

        private void on_settings_changed (string key) {
            if (key == "stickykeys-enable" && !sticky_enabled) {
                clear_sticky ();
            } else if (key == "mousekeys-enable" && !mouse_enabled) {
                stop_mouse_key ();
                if (locked_button != 0) {
                    inject_button (locked_button, false);
                    locked_button = 0;
                }
            } else if (key == "slowkeys-enable" && !slow_enabled) {
                cancel_slow_key ();
            } else if (key == "timeout-enable" && !flag ("timeout-enable") && timeout_timer != 0) {
                GLib.Source.remove (timeout_timer);
                timeout_timer = 0;
            }
        }

        private void feedback (string option) {
            if (flag (option)) {
                beep ();
            }
        }

        /* ---- The prefilter ----------------------------------------------------- */

        private Backend.FilterResult filter (Backend.Seat seat, Backend.KeyEvent event) {
            this.seat = seat;
            return event.pressed ? press (event) : release (event);
        }

        private static bool is_shift (Backend.KeyEvent event) {
            return event.base_keysym == Backend.Keysym.from_name ("Shift_L") ||
                   event.base_keysym == Backend.Keysym.from_name ("Shift_R");
        }

        private Backend.FilterResult press (Backend.KeyEvent event) {
            if (flag ("enable")) {
                if (is_shift (event)) {
                    start_krg ();
                    if (!slow_enabled) {
                        int64 now = GLib.get_monotonic_time () / 1000;
                        shift_count = now - last_shift_ms > SHIFT_RESET_MS ? 1 : shift_count + 1;
                        last_shift_ms = now;
                    }
                } else {
                    cancel_krg ();
                }
            }
            bool ignore = false;
            if (slow_enabled) {
                if (slow_key == event.keycode + 1) {
                    return Backend.FilterResult.CONSUME;
                }
                feedback ("slowkeys-beep-press");
                cancel_slow_key ();
                slow_key = event.keycode + 1;
                held_back[event.keycode] = true;
                var accepted = event;
                uint delay = (uint) number ("slowkeys-delay", 300).clamp (0, (int) MAX_SLOW_KEYS_DELAY);
                slow_timer = GLib.Timeout.add (delay, () => {
                    slow_timer = 0;
                    accept_slow_key (accepted);
                    return GLib.Source.REMOVE;
                });
                ignore = true;
            } else if (bounce_enabled && inactive_key == event.keycode + 1) {
                feedback ("bouncekeys-beep-reject");
                held_back[event.keycode] = true;
                ignore = true;
            }
            if (sticky_enabled && flag ("stickykeys-two-key-off") && depressed () != 0) {
                toggle_sticky_from_keyboard (false);
            }
            if (ignore) {
                return Backend.FilterResult.CONSUME;
            }
            return process_press (event);
        }

        private Backend.FilterResult process_press (Backend.KeyEvent event) {
            if (mouse_enabled && mouse_press (event)) {
                return Backend.FilterResult.CONSUME;
            }
            if (sticky_enabled) {
                sticky_press (event);
            }
            if (flag ("togglekeys-enable")) {
                toggle_key_press (event);
            }
            return Backend.FilterResult.PASS;
        }

        private void accept_slow_key (Backend.KeyEvent event) {
            if (slow_key != event.keycode + 1 || seat == null) {
                return;
            }
            feedback ("slowkeys-beep-accept");
            held_back.remove (event.keycode);
            if (process_press (event) == Backend.FilterResult.CONSUME) {
                mouse_keys_down[event.keycode] = true;
            } else {
                seat.inject_key (event.keycode, true);
            }
            if (flag ("enable") && is_shift (event)) {
                shift_count++;
            }
        }

        private void cancel_slow_key () {
            if (slow_timer != 0) {
                GLib.Source.remove (slow_timer);
                slow_timer = 0;
            }
            slow_key = 0;
        }

        private Backend.FilterResult release (Backend.KeyEvent event) {
            bool ignore = held_back.remove (event.keycode);
            if (bounce_enabled) {
                inactive_key = event.keycode + 1;
                if (bounce_timer != 0) {
                    GLib.Source.remove (bounce_timer);
                }
                bounce_timer = GLib.Timeout.add ((uint) number ("bouncekeys-delay", 300).clamp (0, int.MAX), () => {
                    bounce_timer = 0;
                    inactive_key = 0;
                    return GLib.Source.REMOVE;
                });
            }
            if (slow_enabled && ignore) {
                feedback ("slowkeys-beep-reject");
            }
            if (slow_key == event.keycode + 1) {
                cancel_slow_key ();
            }
            if (flag ("timeout-enable") && number ("timeout", 120) > 0) {
                restart_timeout ();
            }
            cancel_krg ();
            if (!ignore && shift_count != 0) {
                if (!is_shift (event)) {
                    shift_count = 0;
                } else if (shift_count >= 5) {
                    shift_count = 0;
                    toggle_sticky_from_keyboard (!sticky_enabled);
                }
            }
            if (ignore) {
                return Backend.FilterResult.CONSUME;
            }
            if (mouse_keys_down.remove (event.keycode)) {
                mouse_release (event);
                return Backend.FilterResult.CONSUME;
            }
            if (sticky_enabled) {
                sticky_release (event);
            }
            return Backend.FilterResult.PASS;
        }

        public void pointer (bool button_released) {
            shift_count = 0;
            last_pointer_usec = GLib.get_monotonic_time ();
            if (button_released && latched != 0) {
                schedule_break ();
            }
        }

        /* ---- The keyboard response group ---------------------------------- */

        private void start_krg () {
            cancel_krg ();
            bool warn = flag ("feature-state-change-beep");
            krg_timer = GLib.Timeout.add (warn ? KRG_WARN_MS : KRG_MS, () => {
                if (warn) {
                    warn = false;
                    beep ();
                    krg_timer = GLib.Timeout.add (KRG_WARN_MS, () => {
                        krg_timer = 0;
                        toggle_slow_from_keyboard ();
                        return GLib.Source.REMOVE;
                    });
                    return GLib.Source.REMOVE;
                }
                krg_timer = 0;
                toggle_slow_from_keyboard ();
                return GLib.Source.REMOVE;
            });
        }

        private void cancel_krg () {
            if (krg_timer != 0) {
                GLib.Source.remove (krg_timer);
                krg_timer = 0;
            }
        }

        private void feature_beep (bool on) {
            if (flag ("feature-state-change-beep")) {
                beep ();
                if (!on) {
                    GLib.Timeout.add (150, () => {
                        beep ();
                        return GLib.Source.REMOVE;
                    });
                }
            }
        }

        private void toggle_slow_from_keyboard () {
            bool on = !slow_enabled;
            set_flag ("slowkeys-enable", on);
            feature_beep (on);
            confirm (dgettext (DOMAIN, "Slow Keys Alert"),
                     on ? dgettext (DOMAIN, "Do you want to activate Slow Keys?")
                        : dgettext (DOMAIN, "Do you want to deactivate Slow Keys?"),
                     dgettext (DOMAIN, "You just held down the Shift key for 8 seconds.  This is the shortcut " +
                                       "for the Slow Keys feature, which affects the way your keyboard works."),
                     on ? dgettext (DOMAIN, "_Activate") : dgettext (DOMAIN, "_Deactivate"),
                     on ? dgettext (DOMAIN, "Do_n't activate") : dgettext (DOMAIN, "Do_n't deactivate"),
                     (accepted) => {
                         if (!accepted) {
                             set_flag ("slowkeys-enable", !on);
                         }
                     });
        }

        private void toggle_sticky_from_keyboard (bool on) {
            shift_count = 0;
            set_flag ("stickykeys-enable", on);
            feature_beep (on);
            if (!flag ("enable")) {
                return;
            }
            confirm (dgettext (DOMAIN, "Sticky Keys Alert"),
                     on ? dgettext (DOMAIN, "Do you want to activate Sticky Keys?")
                        : dgettext (DOMAIN, "Do you want to deactivate Sticky Keys?"),
                     on ? dgettext (DOMAIN, "You just pressed the Shift key 5 times in a row.  This is the shortcut " +
                                           "for the Sticky Keys feature, which affects the way your keyboard works.")
                        : dgettext (DOMAIN, "You just pressed two keys at once, or pressed the Shift key 5 times in " +
                                           "a row.  This turns off the Sticky Keys feature, which affects the way " +
                                           "your keyboard works."),
                     on ? dgettext (DOMAIN, "_Activate") : dgettext (DOMAIN, "_Deactivate"),
                     on ? dgettext (DOMAIN, "Do_n't activate") : dgettext (DOMAIN, "Do_n't deactivate"),
                     (accepted) => {
                         if (!accepted) {
                             set_flag ("stickykeys-enable", !on);
                         }
                     });
        }

        private void restart_timeout () {
            if (timeout_timer != 0) {
                GLib.Source.remove (timeout_timer);
            }
            last_pointer_usec = 0;
            arm_timeout ((uint) number ("timeout", 120) * 1000);
        }

        private void arm_timeout (uint ms) {
            timeout_timer = GLib.Timeout.add (ms, () => {
                timeout_timer = 0;
                uint wait = (uint) number ("timeout", 120) * 1000;
                if (last_pointer_usec != 0) {
                    uint elapsed = (uint) ((GLib.get_monotonic_time () - last_pointer_usec) / 1000);
                    if (wait > elapsed) {
                        arm_timeout (wait - elapsed);
                        return GLib.Source.REMOVE;
                    }
                }
                shift_count = 0;
                set_flag ("enable", false);
                return GLib.Source.REMOVE;
            });
        }

        /* ---- StickyKeys --------------------------------------------------- */

        public static Backend.Modifiers modifier_of (uint32 keysym) {
            switch (Backend.Keysym.name (keysym)) {
                case "Shift_L":
                case "Shift_R":
                    return Backend.Modifiers.SHIFT;
                case "Control_L":
                case "Control_R":
                    return Backend.Modifiers.CONTROL;
                case "Alt_L":
                case "Alt_R":
                case "Meta_L":
                case "Meta_R":
                    return Backend.Modifiers.MOD1;
                case "Super_L":
                case "Super_R":
                case "Hyper_L":
                case "Hyper_R":
                    return Backend.Modifiers.MOD4;
                case "ISO_Level3_Shift":
                case "Mode_switch":
                    return Backend.Modifiers.MOD5;
                default:
                    return 0;
            }
        }

        private Backend.Modifiers depressed () {
            if (seat == null) {
                return 0;
            }
            Backend.Modifiers down, now_latched, now_locked;
            seat.get_modifier_state (out down, out now_latched, out now_locked);
            return down;
        }

        private void sticky_press (Backend.KeyEvent event) {
            var mods = modifier_of (event.base_keysym);
            if (mods != 0) {
                latch_keys[event.keycode] = (uint) mods;
                return;
            }
            latch_keys.remove_all ();
            if (latched != 0) {
                schedule_break ();
            }
        }

        private void sticky_release (Backend.KeyEvent event) {
            if (!latch_keys.contains (event.keycode)) {
                return;
            }
            var mask = (Backend.Modifiers) latch_keys[event.keycode];
            latch_keys.remove (event.keycode);
            var common = mask & locked;
            if (common != 0) {
                mask &= ~common;
                locked &= ~common;
                feedback ("stickykeys-modifier-beep");
            }
            common = mask & latched;
            if (flag ("stickykeys-latch-to-lock") && common != 0) {
                mask &= ~common;
                var newly = common & ~locked;
                if (newly != 0) {
                    locked |= newly;
                    feedback ("stickykeys-modifier-beep");
                }
                latched &= ~common;
            }
            if (mask != 0) {
                latched |= mask;
                feedback ("stickykeys-modifier-beep");
            }
            apply_after_key ();
        }

        private void apply_after_key () {
            if (apply_idle == 0) {
                apply_idle = GLib.Idle.add (() => {
                    apply_idle = 0;
                    apply_sticky ();
                    return GLib.Source.REMOVE;
                }, GLib.Priority.HIGH);
            }
        }

        private void schedule_break () {
            if (break_idle != 0) {
                return;
            }
            break_idle = GLib.Idle.add (() => {
                break_idle = 0;
                latched = 0;
                apply_sticky ();
                return GLib.Source.REMOVE;
            }, GLib.Priority.HIGH);
        }

        private void apply_sticky () {
            if (seat == null) {
                return;
            }
            Backend.Modifiers down, now_latched, now_locked;
            seat.get_modifier_state (out down, out now_latched, out now_locked);
            seat.set_latched_locked (latched, (now_locked & ~STICKY_MODS) | locked);
        }

        private void clear_sticky () {
            latch_keys.remove_all ();
            if (latched != 0 || locked != 0) {
                latched = 0;
                locked = 0;
                apply_sticky ();
            }
        }

        public Backend.Modifiers sticky_latched { get { return latched; } }
        public Backend.Modifiers sticky_locked { get { return locked; } }

        /* ---- Toggle keys --------------------------------------------------- */

        private void toggle_key_press (Backend.KeyEvent event) {
            Backend.Modifiers bit;
            string name = Backend.Keysym.name (event.base_keysym);
            if (name == "Caps_Lock") {
                bit = Backend.Modifiers.LOCK;
            } else if (name == "Num_Lock") {
                bit = Backend.Modifiers.MOD2;
            } else {
                return;
            }
            GLib.Idle.add (() => {
                if (seat == null) {
                    return GLib.Source.REMOVE;
                }
                Backend.Modifiers down, now_latched, now_locked;
                seat.get_modifier_state (out down, out now_latched, out now_locked);
                bool on = (now_locked & bit) != 0;
                beeps (number (on ? "togglekeys-enable-beep-count" : "togglekeys-disable-beep-count", on ? 1 : 2),
                       number (on ? "togglekeys-enable-beep-delay" : "togglekeys-disable-beep-delay", 150));
                return GLib.Source.REMOVE;
            });
        }

        private void beeps (int count, int delay) {
            if (count <= 0) {
                return;
            }
            beep ();
            if (count > 1) {
                GLib.Timeout.add ((uint) int.max (delay, 1), () => {
                    beeps (count - 1, delay);
                    return GLib.Source.REMOVE;
                });
            }
        }

        /* ---- MouseKeys --------------------------------------------------- */

        public static bool mouse_direction (string name, out int dx, out int dy) {
            dx = dy = 0;
            switch (name) {
                case "KP_1": case "KP_End": dx = -1; dy = 1; return true;
                case "KP_2": case "KP_Down": dy = 1; return true;
                case "KP_3": case "KP_Next": dx = 1; dy = 1; return true;
                case "KP_4": case "KP_Left": dx = -1; return true;
                case "KP_6": case "KP_Right": dx = 1; return true;
                case "KP_7": case "KP_Home": dx = -1; dy = -1; return true;
                case "KP_8": case "KP_Up": dy = -1; return true;
                case "KP_9": case "KP_Prior": dx = 1; dy = -1; return true;
                default: return false;
            }
        }

        private bool mouse_press (Backend.KeyEvent event) {
            string name = Backend.Keysym.name (event.keysym);
            int dx, dy;
            if (mouse_direction (name, out dx, out dy)) {
                start_mouse_key (event.keycode, dx, dy);
            } else if (name == "KP_5" || name == "KP_Begin") {
                inject_button (default_button, true);
            } else if (name == "KP_Add" || name == "KP_Separator") {
                for (int i = 0; i < 2; i++) {
                    inject_button (default_button, true);
                    inject_button (default_button, false);
                }
            } else if (name == "KP_0" || name == "KP_Insert") {
                if (locked_button == 0) {
                    locked_button = default_button;
                    inject_button (locked_button, true);
                }
            } else if (name == "KP_Decimal" || name == "KP_Delete") {
                if (locked_button != 0) {
                    inject_button (locked_button, false);
                    locked_button = 0;
                }
            } else if (name == "KP_Divide" || name == "KP_F2") {
                default_button = BTN_LEFT;
            } else if (name == "KP_Multiply" || name == "KP_F3") {
                default_button = BTN_MIDDLE;
            } else if (name == "KP_Subtract" || name == "KP_F4") {
                default_button = BTN_RIGHT;
            } else {
                return false;
            }
            mouse_keys_down[event.keycode] = true;
            return true;
        }

        private void mouse_release (Backend.KeyEvent event) {
            string name = Backend.Keysym.name (event.keysym);
            if (mouse_key == event.keycode + 1) {
                stop_mouse_key ();
            } else if (name == "KP_5" || name == "KP_Begin") {
                inject_button (default_button, false);
            }
        }

        private void inject_button (uint32 button, bool pressed) {
            if (seat != null) {
                seat.inject_button (button, pressed);
            }
        }

        public static void mouse_controls (int max_speed, int accel_time, out int per_event, out int time_to_max) {
            per_event = int.max (1, max_speed / (int) (1000 / MK_INTERVAL));
            time_to_max = int.max (1, accel_time / (int) MK_INTERVAL);
        }

        public static int mouse_step (int direction, int counter, int per_event, int time_to_max) {
            if (counter >= time_to_max) {
                return direction * per_event;
            }
            double factor = per_event / Math.pow (time_to_max, MK_CURVE);
            double step = factor * Math.pow (counter, MK_CURVE);
            return direction < 0 ? (int) Math.floor (direction * step) : (int) Math.ceil (direction * step);
        }

        private void start_mouse_key (uint32 keycode, int dx, int dy) {
            stop_mouse_key ();
            mouse_key = keycode + 1;
            mouse_dx = dx;
            mouse_dy = dy;
            mouse_counter = 0;
            if (seat != null) {
                seat.move_pointer (dx, dy);
            }
            mouse_timer = GLib.Timeout.add ((uint) number ("mousekeys-init-delay", 160).clamp (0, int.MAX), () => {
                mouse_timer = GLib.Timeout.add (MK_INTERVAL, mouse_tick);
                mouse_tick ();
                return GLib.Source.REMOVE;
            });
        }

        private bool mouse_tick () {
            if (mouse_key == 0 || seat == null) {
                mouse_timer = 0;
                return GLib.Source.REMOVE;
            }
            int per_event, time_to_max;
            mouse_controls (number ("mousekeys-max-speed", 750), number ("mousekeys-accel-time", 1200),
                            out per_event, out time_to_max);
            if (mouse_counter < time_to_max) {
                mouse_counter++;
            }
            seat.move_pointer (mouse_step (mouse_dx, mouse_counter, per_event, time_to_max),
                               mouse_step (mouse_dy, mouse_counter, per_event, time_to_max));
            return GLib.Source.CONTINUE;
        }

        private void stop_mouse_key () {
            if (mouse_timer != 0) {
                GLib.Source.remove (mouse_timer);
                mouse_timer = 0;
            }
            mouse_key = 0;
        }
    }
}
