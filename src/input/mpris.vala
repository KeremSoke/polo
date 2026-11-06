/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate-settings-daemon 1.28's mpris plugin (msd-mpris-manager.c), which
 * media-keys feeds through MediaPlayerKeyPressed on org.mate.SettingsDaemon.
 * That name stays msd's on Xwayland, so the keys come here instead: the
 * player whose org.mpris.MediaPlayer2.* name appeared last gets them, Play
 * as PlayPause.
 */

namespace Polo.Input {
    public class Mpris : GLib.Object {
        private const string PREFIX = "org.mpris.MediaPlayer2.";
        private const string NAMESPACE = "org.mpris.MediaPlayer2";
        private const string PATH = "/org/mpris/MediaPlayer2";
        private const string INTERFACE = "org.mpris.MediaPlayer2.Player";

        private GLib.DBusConnection bus;
        private GLib.GenericArray<string> players = new GLib.GenericArray<string> ();
        private uint subscription = 0;

        public Mpris (GLib.DBusConnection bus) {
            this.bus = bus;
            subscription = bus.signal_subscribe ("org.freedesktop.DBus", "org.freedesktop.DBus",
                                                 "NameOwnerChanged", "/org/freedesktop/DBus", NAMESPACE,
                                                 GLib.DBusSignalFlags.MATCH_ARG0_NAMESPACE,
                                                 on_name_owner_changed);
            list_names.begin ();
        }

        public void shutdown () {
            if (subscription != 0) {
                bus.signal_unsubscribe (subscription);
                subscription = 0;
            }
            players.length = 0;
        }

        public static string? player_name (string bus_name) {
            if (!bus_name.has_prefix (PREFIX)) {
                return null;
            }
            var tokens = bus_name.split (".", 4);
            return tokens.length > 3 ? tokens[3] : null;
        }

        public static unowned string? method_for (string key) {
            switch (key) {
                case "Play":
                    return "PlayPause";
                case "Pause":
                    return "Pause";
                case "Previous":
                    return "Previous";
                case "Next":
                    return "Next";
                case "Stop":
                    return "Stop";
                default:
                    return null;
            }
        }

        public unowned string? current {
            get { return players.length > 0 ? players[0] : null; }
        }

        private async void list_names () {
            try {
                var reply = yield bus.call ("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                            "ListNames", null, new GLib.VariantType ("(as)"),
                                            GLib.DBusCallFlags.NONE, -1, null);
                var names = reply.get_child_value (0);
                for (size_t i = 0; i < names.n_children (); i++) {
                    unowned string name = names.get_child_value (i).get_string ();
                    if (name.has_prefix (PREFIX)) {
                        appeared (name);
                    }
                }
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.INPUT).info ("MPRIS players: %s", e.message);
            }
        }

        private void on_name_owner_changed (GLib.DBusConnection connection, string? sender, string path,
                                            string interface_name, string signal_name, GLib.Variant parameters) {
            string name, old_owner, new_owner;
            parameters.get ("(sss)", out name, out old_owner, out new_owner);
            if (!name.has_prefix (PREFIX)) {
                return;
            }
            if (old_owner != "") {
                vanished (name);
            }
            if (new_owner != "") {
                appeared (name);
            }
        }

        private void appeared (string name) {
            var player = player_name (name);
            if (player != null) {
                players.insert (0, player);
            }
        }

        private void vanished (string name) {
            var player = player_name (name);
            if (player == null) {
                return;
            }
            uint index;
            if (players.find_with_equal_func (player, GLib.str_equal, out index)) {
                players.remove_index (index);
            }
        }

        public void key_pressed (string key) {
            unowned string? method = method_for (key);
            unowned string? head = current;
            if (method == null || head == null) {
                return;
            }
            bus.call.begin (PREFIX + head, PATH, INTERFACE, method, null, null,
                            GLib.DBusCallFlags.NO_AUTO_START, -1, null);
        }
    }
}
