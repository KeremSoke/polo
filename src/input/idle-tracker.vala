/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo {
    public class IdleTracker : GLib.Object {
        private const string POWER_SCHEMA = "org.mate.power-manager";
        private const string UPOWER = "org.freedesktop.UPower";
        private const string UPOWER_PATH = "/org/freedesktop/UPower";

        private Backend.Server server;
        private Dbus.Presence? presence;
        private Logind? logind;
        private GLib.Settings? power = null;
        private Backend.IdleWatch? presence_watch = null;
        private Backend.IdleWatch blank_watch;
        private GLib.DBusProxy? upower = null;
        private GLib.Cancellable cancellable = new GLib.Cancellable ();
        private bool on_battery = false;
        private bool blanked = false;

        public IdleTracker (Backend.Server server, Dbus.Presence? presence, Logind? logind) {
            this.server = server;
            this.presence = presence;
            this.logind = logind;
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (POWER_SCHEMA, true) : null;
            if (schema != null && schema.has_key ("sleep-display-ac") && schema.has_key ("sleep-display-battery")) {
                power = new GLib.Settings.full (schema, null, null);
                power.changed.connect (on_power_changed);
            }
            blank_watch = new Backend.IdleWatch (server, blank_timeout (), true);
            blank_watch.idled.connect (on_blank_idled);
            blank_watch.resumed.connect (on_blank_resumed);
            server.activity.connect (on_activity);
            if (presence != null) {
                presence_watch = new Backend.IdleWatch (server, presence_timeout (), true);
                presence_watch.idled.connect (on_presence_idled);
                presence_watch.resumed.connect (on_presence_resumed);
                presence.notify["idle-timeout-ms"].connect (update_presence);
                presence.notify["screensaver-active"].connect (update_presence);
                presence.notify["idle-enabled"].connect (update_inhibited);
                update_inhibited ();
            }
            if (logind != null && power != null) {
                watch_battery.begin ();
            }
        }

        public void shutdown () {
            cancellable.cancel ();
            server.activity.disconnect (on_activity);
            blank_watch.idled.disconnect (on_blank_idled);
            blank_watch.resumed.disconnect (on_blank_resumed);
            if (power != null) {
                power.changed.disconnect (on_power_changed);
            }
            if (presence_watch != null) {
                presence_watch.idled.disconnect (on_presence_idled);
                presence_watch.resumed.disconnect (on_presence_resumed);
                presence.notify["idle-timeout-ms"].disconnect (update_presence);
                presence.notify["screensaver-active"].disconnect (update_presence);
                presence.notify["idle-enabled"].disconnect (update_inhibited);
                presence_watch = null;
            }
            if (upower != null) {
                upower.g_properties_changed.disconnect (on_upower_changed);
                upower = null;
            }
        }

        private uint presence_timeout () {
            return presence.screensaver_active ? 0 : presence.idle_timeout_ms;
        }

        private uint blank_timeout () {
            if (power == null) {
                return 0;
            }
            int seconds = power.get_int (on_battery ? "sleep-display-battery" : "sleep-display-ac");
            return (uint) int.max (0, seconds) * 1000;
        }

        private void update_presence () {
            presence_watch.set_timeout (presence_timeout ());
        }

        private void update_inhibited () {
            server.set_session_idle_inhibited (!presence.idle_enabled);
        }

        private void on_power_changed (string key) {
            if (key.has_prefix ("sleep-display-")) {
                blank_watch.set_timeout (blank_timeout ());
            }
        }

        private void on_presence_idled () {
            presence.set_session_idle (true);
        }

        private void on_presence_resumed () {
            presence.set_session_idle (false);
        }

        private void on_blank_idled () {
            Logging.scope (LogDomain.POLO).info ("idle: blanking the outputs");
            blanked = true;
            server.blank_outputs ();
        }

        private void on_blank_resumed () {
            if (blanked) {
                blanked = false;
                server.unblank_outputs ();
            }
        }

        private void on_activity () {
            blanked = false;
        }

        private async void watch_battery () {
            var bus = yield logind.bus ();
            if (bus == null || cancellable.is_cancelled ()) {
                return;
            }
            try {
                upower = yield new GLib.DBusProxy (bus, GLib.DBusProxyFlags.DO_NOT_AUTO_START, null, UPOWER,
                                                   UPOWER_PATH, UPOWER, cancellable);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("UPower: %s", e.message);
                return;
            }
            upower.g_properties_changed.connect (on_upower_changed);
            read_battery ();
        }

        private void on_upower_changed (GLib.Variant changed, string[] invalidated) {
            read_battery ();
        }

        private void read_battery () {
            var value = upower.get_cached_property ("OnBattery");
            bool now = value != null && value.is_of_type (GLib.VariantType.BOOLEAN) && value.get_boolean ();
            if (now != on_battery) {
                on_battery = now;
                blank_watch.set_timeout (blank_timeout ());
            }
        }
    }
}
