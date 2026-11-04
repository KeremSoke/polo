/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Protocols {
    public class SessionLock : GLib.Object {
        private const string POLO_SESSION_SCHEMA = "org.mate.polo.session";
        private const string SCREENSAVER_SCHEMA = "org.mate.screensaver";
        private const string SCREENSAVER = "org.mate.ScreenSaver";
        private const string SCREENSAVER_PATH = "/org/mate/ScreenSaver";

        public signal void shown ();

        private Backend.Server server;
        private Spawn spawn;
        private Dbus.Presence? presence;
        private GLib.Settings? polo_session = null;
        private GLib.Settings? screensaver = null;
        private GLib.Subprocess? locker = null;
        private Backend.PixelBuffer? prompt = null;
        private GLib.Cancellable cancellable = new GLib.Cancellable ();
        private uint idle_timer = 0;

        public SessionLock (Backend.Server server, Spawn spawn, Dbus.Presence? presence) {
            this.server = server;
            this.spawn = spawn;
            this.presence = presence;
            polo_session = settings_if_installed (POLO_SESSION_SCHEMA);
            screensaver = settings_if_installed (SCREENSAVER_SCHEMA);
            server.lock_state_changed.connect (on_lock_state_changed);
            server.lock_restart_requested.connect (on_restart_requested);
            if (presence != null) {
                presence.notify["status"].connect (on_presence_status);
            }
        }

        public void shutdown () {
            cancellable.cancel ();
            server.lock_state_changed.disconnect (on_lock_state_changed);
            server.lock_restart_requested.disconnect (on_restart_requested);
            if (presence != null) {
                presence.notify["status"].disconnect (on_presence_status);
            }
            stop_idle_timer ();
            hide_prompt ();
        }

        private static GLib.Settings? settings_if_installed (string id) {
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (id, true) : null;
            return schema != null ? new GLib.Settings.full (schema, null, null) : null;
        }

        private bool screensaver_flag (string key) {
            return screensaver != null && screensaver.settings_schema.has_key (key) &&
                   screensaver.get_boolean (key);
        }

        private string[]? locker_argv () {
            if (polo_session == null || !polo_session.settings_schema.has_key ("locker-command")) {
                return null;
            }
            string command = polo_session.get_string ("locker-command").strip ();
            string[] argv = {};
            try {
                if (command == "" || !GLib.Shell.parse_argv (command, out argv)) {
                    return null;
                }
            } catch (GLib.ShellError e) {
                Logging.scope (LogDomain.POLO).warning ("locker-command: %s", e.message);
                return null;
            }
            return GLib.Environment.find_program_in_path (argv[0]) != null ? argv : null;
        }

        private async bool screensaver_running () {
            try {
                var bus = yield GLib.Bus.get (GLib.BusType.SESSION, cancellable);
                var reply = yield bus.call ("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                            "NameHasOwner", new GLib.Variant ("(s)", SCREENSAVER),
                                            new GLib.VariantType ("(b)"), GLib.DBusCallFlags.NONE, 5000,
                                            cancellable);
                bool owned;
                reply.get ("(b)", out owned);
                return owned;
            } catch (GLib.Error e) {
                return false;
            }
        }

        public async void lock_screen () {
            unowned LogScope log = Logging.scope (LogDomain.POLO);
            if (server.get_lock_state () != Backend.LockState.UNLOCKED) {
                shown ();
                return;
            }
            if (yield screensaver_running ()) {
                try {
                    var bus = yield GLib.Bus.get (GLib.BusType.SESSION, cancellable);
                    yield bus.call (SCREENSAVER, SCREENSAVER_PATH, SCREENSAVER, "Lock", null, null,
                                    GLib.DBusCallFlags.NONE, 5000, cancellable);
                } catch (GLib.Error e) {
                    log.warning ("Couldn't lock screen: %s", e.message);
                }
                shown ();
                return;
            }
            if (!start_locker ()) {
                log.warning ("Couldn't find any screen locker: mate-screensaver is not running and " +
                             "org.mate.polo.session locker-command is not installed");
                shown ();
            }
        }

        public void unlock_requested () {
            if (server.get_lock_state () != Backend.LockState.UNLOCKED) {
                Logging.scope (LogDomain.POLO).info ("logind asked to unlock the session");
                server.force_unlock ();
            }
        }

        private bool start_locker () {
            string[]? argv = locker_argv ();
            if (argv == null) {
                return false;
            }
            if (locker != null) {
                locker.force_exit ();
                locker = null;
            }
            try {
                locker = spawn.launch (argv, SpawnFlags.TRUSTED);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).warning ("cannot start %s: %s", argv[0], e.message);
                return false;
            }
            return true;
        }

        private void on_lock_state_changed (int state) {
            switch ((Backend.LockState) state) {
                case Backend.LockState.LOCKED:
                    hide_prompt ();
                    shown ();
                    break;
                case Backend.LockState.ABANDONED:
                    Logging.scope (LogDomain.POLO).warning ("the screen locker went away; the session stays locked");
                    show_prompt (locker_argv () != null);
                    break;
                case Backend.LockState.UNLOCKED:
                    hide_prompt ();
                    locker = null;
                    break;
                default:
                    break;
            }
        }

        private void on_restart_requested () {
            if (server.get_lock_state () == Backend.LockState.ABANDONED) {
                start_locker ();
            }
        }

        private void show_prompt (bool can_restart) {
            hide_prompt ();
            string text = can_restart ? "The screen locker stopped.\nPress Enter to start it again."
                                      : "The screen locker stopped.";
            int width, height;
            Marco.label_size (text, null, out width, out height);
            prompt = new Backend.PixelBuffer (server, width, height, 1);
            int stride;
            unowned uint8[] pixels = prompt.begin (out stride);
            if (pixels.length > 0) {
                Marco.label_render (text, null, width, height, pixels, stride);
            }
            prompt.set_accepts_input (false);
            prompt.commit ();
            if (prompt.is_ready ()) {
                place_prompt ();
            } else {
                prompt.ready.connect (place_prompt);
            }
        }

        private void place_prompt () {
            if (prompt != null && server.get_lock_state () == Backend.LockState.ABANDONED) {
                server.set_lock_prompt (prompt);
            }
        }

        private void hide_prompt () {
            if (prompt == null) {
                return;
            }
            prompt.ready.disconnect (place_prompt);
            server.set_lock_prompt (null);
            prompt = null;
        }

        private void stop_idle_timer () {
            if (idle_timer != 0) {
                GLib.Source.remove (idle_timer);
                idle_timer = 0;
            }
        }

        private void on_presence_status () {
            stop_idle_timer ();
            if (presence.status != Dbus.PresenceStatus.IDLE ||
                !screensaver_flag ("idle-activation-enabled") || !screensaver_flag ("lock-enabled")) {
                return;
            }
            uint minutes = (uint) int.max (0, screensaver.get_int ("lock-delay"));
            idle_timer = GLib.Timeout.add_seconds (minutes * 60 + 1, () => {
                idle_timer = 0;
                lock_on_idle.begin ();
                return GLib.Source.REMOVE;
            });
        }

        private async void lock_on_idle () {
            if (yield screensaver_running ()) {
                return;
            }
            yield lock_screen ();
        }
    }
}
