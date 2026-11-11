/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * org.freedesktop.a11y.KeyboardMonitor, as GNOME's mutter offers it: on
 * Wayland, Orca (libatspi's a11y manager device) sees keys and takes its own
 * commands through it. It hands every keystroke to its client, so it answers
 * only the screen reader: a caller that owns org.gnome.Orca.KeyboardMonitor,
 * runs a root-owned orca program, while the screen reader is turned on.
 */

namespace Polo.Dbus {
    public class KeyboardMonitor : GLib.Object {
        public const string NAME = "org.freedesktop.a11y.Manager";
        public const string PATH = "/org/freedesktop/a11y/Manager";
        public const string INTERFACE = "org.freedesktop.a11y.KeyboardMonitor";
        private const string RESOURCE = "/org/mate/polo/dbus/org.freedesktop.a11y.KeyboardMonitor.xml";
        public const string SCREEN_READER_NAME = "org.gnome.Orca.KeyboardMonitor";
        private const string GNOME_A11Y = "org.gnome.desktop.a11y.applications";
        private const string MATE_AT_VISUAL = "org.mate.applications-at-visual";
        private const int64 DOUBLE_PRESS_MS = 500;
        private const uint MAX_GRABS = 256;

        private class Keystroke {
            public uint32 keysym;
            public uint32 modifiers;
        }

        private class Client {
            public string sender;
            public bool grabbed = false;
            public bool watching = false;
            public uint32[] modifiers = {};
            public Keystroke[] keystrokes = {};
            public uint watch = 0;
            public uint32 held_modifier = 0;
            public bool used_while_held = false;
            public uint32 lone_keysym = 0;
            public int64 lone_released_ms = 0;
        }

        private GLib.DBusConnection connection;
        private Registration? exported = null;
        private uint owner = 0;
        private GLib.HashTable<string, Client> clients =
            new GLib.HashTable<string, Client> (GLib.str_hash, GLib.str_equal);
        private GLib.HashTable<uint, bool> consumed = new GLib.HashTable<uint, bool> (GLib.direct_hash,
                                                                                         GLib.direct_equal);
        private GLib.HashTable<string, bool> refused = new GLib.HashTable<string, bool> (GLib.str_hash,
                                                                                          GLib.str_equal);

        public KeyboardMonitor (GLib.DBusConnection connection) {
            this.connection = connection;
        }

        public void export () throws GLib.Error {
            exported = new Registration (connection, PATH, Registration.load (RESOURCE, INTERFACE), on_method);
            owner = GLib.Bus.own_name_on_connection (connection, NAME, GLib.BusNameOwnerFlags.DO_NOT_QUEUE);
        }

        public void unexport () {
            foreach (var client in clients.get_values ()) {
                if (client.watch != 0) {
                    GLib.Bus.unwatch_name (client.watch);
                }
            }
            clients.remove_all ();
            if (owner != 0) {
                GLib.Bus.unown_name (owner);
                owner = 0;
            }
            if (exported != null) {
                exported.unregister ();
                exported = null;
            }
        }

        /* ---- Who may ask ------------------------------------------------- */

        private static bool screen_reader_on () {
            var source = GLib.SettingsSchemaSource.get_default ();
            if (source == null) {
                return false;
            }
            var gnome = source.lookup (GNOME_A11Y, true);
            if (gnome != null && gnome.has_key ("screen-reader-enabled") &&
                new GLib.Settings.full (gnome, null, null).get_boolean ("screen-reader-enabled")) {
                return true;
            }
            var mate = source.lookup (MATE_AT_VISUAL, true);
            if (mate != null && mate.has_key ("startup") && mate.has_key ("exec")) {
                var settings = new GLib.Settings.full (mate, null, null);
                return settings.get_boolean ("startup") &&
                       GLib.Path.get_basename (settings.get_string ("exec").strip ().split (" ")[0]) == "orca";
            }
            return false;
        }

        private static bool runs_orca (uint32 pid) {
            uint8[] raw;
            try {
                GLib.FileUtils.get_data ("/proc/%u/cmdline".printf (pid), out raw);
            } catch (GLib.FileError e) {
                return false;
            }
            string[] args = {};
            int start = 0;
            for (int i = 0; i < raw.length && args.length < 3; i++) {
                if (raw[i] == 0) {
                    args += (string) raw[start:i];
                    start = i + 1;
                }
            }
            for (int i = 0; i < args.length && i < 2; i++) {
                if (GLib.Path.get_basename (args[i]) == "orca") {
                    return CommandLine.test_mode || Policy.TrustRules.root_owned_file (args[i]);
                }
            }
            return false;
        }

        private async bool authorized (string sender) {
            try {
                var reply = yield connection.call ("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                   "org.freedesktop.DBus", "GetNameOwner",
                                                   new GLib.Variant ("(s)", SCREEN_READER_NAME),
                                                   new GLib.VariantType ("(s)"), GLib.DBusCallFlags.NONE, 2000,
                                                   null);
                string name_owner;
                reply.get ("(s)", out name_owner);
                if (name_owner != sender) {
                    return false;
                }
                reply = yield connection.call ("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                               "org.freedesktop.DBus", "GetConnectionUnixProcessID",
                                               new GLib.Variant ("(s)", sender), new GLib.VariantType ("(u)"),
                                               GLib.DBusCallFlags.NONE, 2000, null);
                uint32 pid;
                reply.get ("(u)", out pid);
                return runs_orca (pid) && screen_reader_on ();
            } catch (GLib.Error e) {
                return false;
            }
        }

        /* ---- Requests ---------------------------------------------------- */

        private void on_method (string sender, string method, GLib.Variant parameters,
                                owned GLib.DBusMethodInvocation invocation) {
            handle.begin (sender, method, parameters, (owned) invocation);
        }

        private async void handle (string sender, string method, GLib.Variant parameters,
                                   owned GLib.DBusMethodInvocation invocation) {
            if (!yield authorized (sender)) {
                if (!refused.contains (sender)) {
                    refused.insert (sender, true);
                    Logging.scope (LogDomain.POLO).info (
                        "refused the keyboard monitor to %s: only the screen reader may use it", sender);
                }
                invocation.return_dbus_error ("org.freedesktop.DBus.Error.AccessDenied",
                                              "only the screen reader may watch the keyboard");
                return;
            }
            var client = client_for (sender);
            switch (method) {
                case "GrabKeyboard":
                    client.grabbed = true;
                    break;
                case "UngrabKeyboard":
                    client.grabbed = false;
                    break;
                case "WatchKeyboard":
                    client.watching = true;
                    break;
                case "UnwatchKeyboard":
                    client.watching = false;
                    break;
                case "SetKeyGrabs":
                    set_key_grabs (client, parameters);
                    break;
                default:
                    invocation.return_dbus_error ("org.freedesktop.DBus.Error.UnknownMethod", method);
                    return;
            }
            invocation.return_value (null);
        }

        private Client client_for (string sender) {
            var client = clients.lookup (sender);
            if (client != null) {
                return client;
            }
            client = new Client ();
            client.sender = sender;
            clients.insert (sender, client);
            client.watch = GLib.Bus.watch_name_on_connection (connection, sender, GLib.BusNameWatcherFlags.NONE,
                                                              null, on_vanished);
            Logging.scope (LogDomain.POLO).info ("the screen reader (%s) uses the keyboard monitor", sender);
            return client;
        }

        private void on_vanished (GLib.DBusConnection connection, string name) {
            var client = clients.lookup (name);
            if (client == null) {
                return;
            }
            if (client.watch != 0) {
                GLib.Bus.unwatch_name (client.watch);
            }
            clients.remove (name);
            Logging.scope (LogDomain.POLO).info ("the screen reader (%s) left the keyboard monitor", name);
        }

        private void set_key_grabs (Client client, GLib.Variant parameters) {
            var modifiers = parameters.get_child_value (0);
            var keystrokes = parameters.get_child_value (1);
            client.modifiers = {};
            client.keystrokes = {};
            for (size_t i = 0; i < modifiers.n_children () && i < MAX_GRABS; i++) {
                client.modifiers += modifiers.get_child_value (i).get_uint32 ();
            }
            for (size_t i = 0; i < keystrokes.n_children () && i < MAX_GRABS; i++) {
                uint32 keysym, mask;
                keystrokes.get_child_value (i).get ("(uu)", out keysym, out mask);
                client.keystrokes += new Keystroke () { keysym = keysym, modifiers = mask };
            }
            client.held_modifier = 0;
        }

        /* ---- Keys -------------------------------------------------------- */

        private static bool has (uint32[] keysyms, uint32 keysym) {
            foreach (var k in keysyms) {
                if (k == keysym) {
                    return true;
                }
            }
            return false;
        }

        private static bool grabs (Client client, Backend.KeyEvent event, int64 now_ms) {
            if (client.grabbed) {
                return true;
            }
            uint32 keysym = event.keysym;
            if (has (client.modifiers, keysym)) {
                if (event.pressed) {
                    bool second = client.lone_keysym == keysym && now_ms - client.lone_released_ms < DOUBLE_PRESS_MS;
                    client.lone_keysym = 0;
                    if (second) {
                        return false;
                    }
                    client.held_modifier = keysym;
                    client.used_while_held = false;
                    return true;
                }
                if (client.held_modifier == keysym) {
                    if (!client.used_while_held) {
                        client.lone_keysym = keysym;
                        client.lone_released_ms = now_ms;
                    }
                    client.held_modifier = 0;
                    return true;
                }
                return false;
            }
            if (client.held_modifier != 0) {
                client.used_while_held = true;
                return true;
            }
            uint32 state = (uint32) event.modifiers;
            foreach (var stroke in client.keystrokes) {
                if (stroke.keysym == keysym && stroke.modifiers == state) {
                    return true;
                }
            }
            return false;
        }

        public Backend.FilterResult filter (Backend.KeyEvent event) {
            if (clients.size () == 0) {
                return Backend.FilterResult.PASS;
            }
            int64 now_ms = GLib.get_monotonic_time () / 1000;
            bool take = false;
            var tell = new GLib.GenericArray<Client> ();
            foreach (var client in clients.get_values ()) {
                bool grabbed = grabs (client, event, now_ms);
                take |= grabbed;
                if (grabbed || client.watching) {
                    tell.add (client);
                }
            }
            if (!event.pressed && consumed.contains (event.keycode)) {
                take = true;
            }
            if (event.pressed && take) {
                consumed.insert (event.keycode, true);
            } else if (!event.pressed) {
                consumed.remove (event.keycode);
            }
            var signal = new GLib.Variant ("(buuuq)", !event.pressed, (uint32) event.modifiers, event.keysym,
                                          Backend.keysym_to_unicode (event.keysym), (uint16) (event.keycode + 8));
            foreach (var client in tell) {
                try {
                    connection.emit_signal (client.sender, PATH, INTERFACE, "KeyEvent", signal);
                } catch (GLib.Error e) {
                    Logging.scope (LogDomain.POLO).debug ("KeyEvent to %s: %s", client.sender, e.message);
                }
            }
            return take ? Backend.FilterResult.CONSUME : Backend.FilterResult.PASS;
        }
    }
}
