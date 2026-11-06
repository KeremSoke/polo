/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What a media key does: msd-media-keys-manager.c do_action () for the
 * plugin's keys, gpm-manager.c, gpm-backlight.c and gpm-kbd-backlight.c for
 * mate-power-manager's. Commands and URIs are the ones msd starts; the OSD
 * is msd's, drawn by polo-shell-ui on the output under the pointer. Polo
 * locks the screen itself instead of calling mate-screensaver-command.
 * Brightness goes through mate-power-manager, or logind when it is not
 * running.
 */

namespace Polo.Shell {
    public class MediaKeyActions : GLib.Object, Input.MediaKeyTarget {
        private const string DOMAIN = "mate-settings-daemon";
        private const string SOUND_SCHEMA = "org.mate.sound";
        private const string TOUCHPAD_SCHEMA = "org.mate.peripherals-touchpad";
        private const string A11Y_SCHEMA = "org.gnome.desktop.a11y.applications";
        private const string POWER_SCHEMA = "org.mate.power-manager";
        private const string SCREENSAVER_SCHEMA = "org.mate.screensaver";
        private const string MPM = "org.mate.PowerManager";
        private const string RFKILL_NAME = "org.mate.SettingsDaemon.Rfkill";
        private const uint KBD_BACKLIGHT_STEP = 10;
        private const uint BRIGHTNESS_STEP = 5;

        [CCode (cname = "g_dgettext", cheader_filename = "glib.h")]
        private static extern unowned string dgettext (string domain, string msgid);

        public delegate void LockScreen ();

        private Backend.Server server;
        private Policy.Model model;
        private Desktop desktop;
        private Spawn spawn;
        private Logind? logind;
        private LockScreen lock_screen;
        private Input.ErrorDialog? error_dialog;
        private GLib.Settings? settings;
        private GLib.Settings? sound_settings;
        private GLib.Settings? power_settings;
        private Input.Volume volume;
        private Input.Mpris? mpris = null;
        private GLib.DBusConnection? bus = null;
        private Canberra.Context? sounds = null;
        private uint kbd_saved = 100;

        public MediaKeyActions (Backend.Server server, Policy.Model model, Desktop desktop, Spawn spawn, Logind? logind,
                                owned LockScreen lock_screen, owned Input.ErrorDialog? error_dialog) {
            this.server = server;
            this.model = model;
            this.desktop = desktop;
            this.spawn = spawn;
            this.logind = logind;
            this.lock_screen = (owned) lock_screen;
            this.error_dialog = (owned) error_dialog;
            settings = installed (Input.MediaKeys.SCHEMA);
            sound_settings = installed (SOUND_SCHEMA);
            power_settings = installed (POWER_SCHEMA);
            volume = new Input.Volume ();
            if (have_session_bus ()) {
                try {
                    bus = GLib.Bus.get_sync (GLib.BusType.SESSION);
                    mpris = new Input.Mpris (bus);
                } catch (GLib.Error e) {
                    Logging.scope (LogDomain.INPUT).info ("media keys without a session bus: %s", e.message);
                }
            }
            if (!CommandLine.test_mode && Canberra.Context.create (out sounds) == Canberra.SUCCESS) {
                sounds.change_props (Canberra.PROP_APPLICATION_NAME, "Polo",
                                     Canberra.PROP_APPLICATION_ID, "org.mate.SettingsDaemon");
            } else {
                sounds = null;
            }
        }

        private static bool have_session_bus () {
            if (GLib.Environment.get_variable ("DBUS_SESSION_BUS_ADDRESS") != null) {
                return true;
            }
            unowned string? runtime = GLib.Environment.get_variable ("XDG_RUNTIME_DIR");
            return runtime != null && GLib.FileUtils.test (GLib.Path.build_filename (runtime, "bus"),
                                                           GLib.FileTest.EXISTS);
        }

        public void shutdown () {
            volume.shutdown ();
            if (mpris != null) {
                mpris.shutdown ();
            }
        }

        public GLib.Settings? media_keys_settings () {
            return settings;
        }

        private static GLib.Settings? installed (string id) {
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (id, true) : null;
            return schema != null ? new GLib.Settings.full (schema, null, null) : null;
        }

        private static bool has_key (GLib.Settings? settings, string key) {
            return settings != null && settings.settings_schema.has_key (key);
        }

        public void run (Input.MediaKey key) {
            switch (key) {
                case Input.MediaKey.TOUCHPAD:
                    touchpad ();
                    break;
                case Input.MediaKey.TOUCHPAD_ON:
                    touchpad_osd (true);
                    break;
                case Input.MediaKey.TOUCHPAD_OFF:
                    touchpad_osd (false);
                    break;
                case Input.MediaKey.MUTE:
                case Input.MediaKey.VOLUME_DOWN:
                case Input.MediaKey.VOLUME_UP:
                case Input.MediaKey.MIC_MUTE:
                    sound (key, false);
                    break;
                case Input.MediaKey.MUTE_QUIET:
                    sound (Input.MediaKey.MUTE, true);
                    break;
                case Input.MediaKey.VOLUME_DOWN_QUIET:
                    sound (Input.MediaKey.VOLUME_DOWN, true);
                    break;
                case Input.MediaKey.VOLUME_UP_QUIET:
                    sound (Input.MediaKey.VOLUME_UP, true);
                    break;
                case Input.MediaKey.POWER:
                    execute ("mate-session-save --shutdown-dialog");
                    break;
                case Input.MediaKey.LOGOUT:
                    execute ("mate-session-save --logout-dialog");
                    break;
                case Input.MediaKey.EJECT:
                    eject ();
                    break;
                case Input.MediaKey.HOME:
                    execute ("caja --no-desktop " + GLib.Shell.quote (GLib.Environment.get_home_dir ()));
                    break;
                case Input.MediaKey.SEARCH:
                    if (GLib.Environment.find_program_in_path ("beagle-search") != null) {
                        execute ("beagle-search");
                    } else if (GLib.Environment.find_program_in_path ("tracker-search-tool") != null) {
                        execute ("tracker-search-tool");
                    } else {
                        execute ("mate-search-tool");
                    }
                    break;
                case Input.MediaKey.EMAIL:
                    url ("mailto");
                    break;
                case Input.MediaKey.CONTROL_CENTER:
                    if (GLib.Environment.find_program_in_path ("mate-control-center") != null) {
                        execute ("mate-control-center");
                    }
                    break;
                case Input.MediaKey.SCREENSAVER:
                case Input.MediaKey.LOCK_BUTTON:
                    lock_screen ();
                    break;
                case Input.MediaKey.HELP:
                    url ("help");
                    break;
                case Input.MediaKey.WWW:
                    url ("http");
                    break;
                case Input.MediaKey.MEDIA:
                    media ();
                    break;
                case Input.MediaKey.CALCULATOR:
                    execute_setting ("org.mate.applications-calculator");
                    break;
                case Input.MediaKey.MESSENGER:
                    execute_setting ("org.mate.applications-messenger");
                    break;
                case Input.MediaKey.PLAY:
                    player ("Play");
                    break;
                case Input.MediaKey.PAUSE:
                    player ("Pause");
                    break;
                case Input.MediaKey.STOP:
                    player ("Stop");
                    break;
                case Input.MediaKey.PREVIOUS:
                    player ("Previous");
                    break;
                case Input.MediaKey.NEXT:
                    player ("Next");
                    break;
                case Input.MediaKey.REWIND:
                    player ("Rewind");
                    break;
                case Input.MediaKey.FORWARD:
                    player ("FastForward");
                    break;
                case Input.MediaKey.REPEAT:
                    player ("Repeat");
                    break;
                case Input.MediaKey.RANDOM:
                    player ("Shuffle");
                    break;
                case Input.MediaKey.MAGNIFIER:
                    toggle_a11y ("screen-magnifier-enabled");
                    break;
                case Input.MediaKey.SCREENREADER:
                    toggle_a11y ("screen-reader-enabled");
                    break;
                case Input.MediaKey.ON_SCREEN_KEYBOARD:
                    toggle_a11y ("screen-keyboard-enabled");
                    break;
                case Input.MediaKey.RFKILL:
                    rfkill.begin (false);
                    break;
                case Input.MediaKey.BLUETOOTH_RFKILL:
                    rfkill.begin (true);
                    break;
                case Input.MediaKey.DISPLAY:
                    if (model.get_outputs ().length > 1) {
                        osd (Backend.OsdKind.MESSAGE, "video-joined-displays-symbolic",
                             dgettext (DOMAIN, "Changing Screen Layout"));
                    } else {
                        osd (Backend.OsdKind.MESSAGE, "video-single-display-symbolic",
                             dgettext (DOMAIN, "No External Display"));
                    }
                    break;
                case Input.MediaKey.POWER_BUTTON:
                    power_policy ("button-power");
                    break;
                case Input.MediaKey.SUSPEND_BUTTON:
                    power_policy ("button-suspend");
                    break;
                case Input.MediaKey.HIBERNATE_BUTTON:
                    power_policy ("button-hibernate");
                    break;
                case Input.MediaKey.BRIGHTNESS_UP:
                    brightness.begin (true);
                    break;
                case Input.MediaKey.BRIGHTNESS_DOWN:
                    brightness.begin (false);
                    break;
                case Input.MediaKey.KBD_BRIGHTNESS_UP:
                    kbd_brightness.begin (1);
                    break;
                case Input.MediaKey.KBD_BRIGHTNESS_DOWN:
                    kbd_brightness.begin (-1);
                    break;
                case Input.MediaKey.KBD_BRIGHTNESS_TOGGLE:
                    kbd_brightness.begin (0);
                    break;
                case Input.MediaKey.BATTERY:
                    Logging.scope (LogDomain.INPUT).info ("the battery key is mate-power-manager's notification");
                    break;
            }
        }

        private void osd (Backend.OsdKind kind, string? icon, string? text, int value = -1, bool msd = true) {
            if (msd && has_key (settings, "enable-osd") && !settings.get_boolean ("enable-osd")) {
                return;
            }
            var output = model.output_at (model.pointer_x, model.pointer_y);
            server.get_shell_ui ().show_osd (kind, output != null ? desktop.backend_output (output) : null, icon,
                                             text, value);
        }

        private void execute (string command) {
            try {
                spawn.launch_command (command);
            } catch (GLib.Error e) {
                if (error_dialog != null) {
                    unowned string text = dgettext (
                        DOMAIN, "Couldn't execute command: %s\nVerify that this is a valid command.");
                    error_dialog (text.printf (command));
                }
            }
        }

        private void execute_setting (string schema) {
            var app = installed (schema);
            if (has_key (app, "exec")) {
                execute (app.get_string ("exec"));
            }
        }

        private GLib.AppLaunchContext launch_context () {
            var context = new GLib.AppLaunchContext ();
            context.setenv ("WAYLAND_DISPLAY", server.get_socket_name ());
            context.unsetenv ("WAYLAND_SOCKET");
            var token = desktop.activation_tokens.mint_for_launch (server.get_time_msec ());
            context.setenv ("XDG_ACTIVATION_TOKEN", token);
            context.setenv ("DESKTOP_STARTUP_ID", token);
            return context;
        }

        private void url (string scheme) {
            GLib.AppInfo.launch_default_for_uri_async.begin (scheme, launch_context (), null, (obj, res) => {
                try {
                    GLib.AppInfo.launch_default_for_uri_async.end (res);
                } catch (GLib.Error e) {
                    Logging.scope (LogDomain.INPUT).warning ("Could not launch '%s': %s", scheme, e.message);
                }
            });
        }

        private void media () {
            var app = GLib.AppInfo.get_default_for_type ("audio/x-vorbis+ogg", false);
            if (app == null) {
                Logging.scope (LogDomain.INPUT).warning ("Could not find default application for '%s' mime-type",
                                                         "audio/x-vorbis+ogg");
                return;
            }
            try {
                app.launch (null, launch_context ());
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.INPUT).warning ("Could not launch '%s': %s", app.get_commandline (),
                                                         e.message);
            }
        }

        private void player (string key) {
            if (mpris != null) {
                mpris.key_pressed (key);
            }
        }

        private void toggle_a11y (string key) {
            var a11y = installed (A11Y_SCHEMA);
            if (has_key (a11y, key)) {
                a11y.set_boolean (key, !a11y.get_boolean (key));
            }
        }

        private void touchpad_osd (bool state) {
            osd (Backend.OsdKind.MESSAGE, state ? "input-touchpad" : "touchpad-disabled",
                 state ? dgettext (DOMAIN, "Touchpad enabled") : dgettext (DOMAIN, "Touchpad disabled"));
        }

        private void touchpad () {
            var touchpad = installed (TOUCHPAD_SCHEMA);
            if (!desktop.has_touchpad () || !has_key (touchpad, "touchpad-enabled")) {
                touchpad_osd (false);
                return;
            }
            bool state = touchpad.get_boolean ("touchpad-enabled");
            touchpad_osd (!state);
            touchpad.set_boolean ("touchpad-enabled", !state);
        }

        private void sound (Input.MediaKey key, bool quiet) {
            bool mic = key == Input.MediaKey.MIC_MUTE;
            var control = mic ? volume.source : volume.sink;
            if (control == null) {
                return;
            }
            uint32 min = 0;
            uint32 max = has_key (sound_settings, "volume-overamplifiable") &&
                         sound_settings.get_boolean ("volume-overamplifiable")
                ? Input.Volume.max_volume () : Input.Volume.normal_volume ();
            int step = has_key (settings, "volume-step") ? settings.get_int ("volume-step") : 0;
            if (step <= 0 || step > 100) {
                step = has_key (settings, "volume-step") ? settings.get_default_value ("volume-step").get_int32 () : 6;
            }
            uint32 level;
            bool muted;
            Input.MediaKeys.sound_step (key, control.level, control.muted, min, max, step, out level, out muted);
            bool changed;
            if (mic) {
                changed = muted != control.muted && volume.change_source_mute (muted);
            } else {
                changed = (muted != control.muted || level != control.level) && volume.change_sink (level, muted);
            }
            if (!changed) {
                muted = control.muted;
                level = control.level;
            }
            int percent = (int) uint64.min ((uint64) 100 * level / (max - min), 100);
            osd (Backend.OsdKind.VOLUME, Input.MediaKeys.volume_icon (mic, muted, muted ? 0 : percent), null,
                 muted ? 0 : percent);
            if (!quiet && changed && !muted && !mic) {
                volume_change_sound ();
            }
        }

        private void volume_change_sound () {
            if (sounds == null || (has_key (sound_settings, "event-sounds") &&
                                   !sound_settings.get_boolean ("event-sounds"))) {
                return;
            }
            string theme = has_key (sound_settings, "theme-name") ? sound_settings.get_string ("theme-name") : "";
            sounds.play (0, Canberra.PROP_EVENT_ID, "audio-volume-change",
                         Canberra.PROP_EVENT_DESCRIPTION, "Volume changed through key press",
                         Canberra.PROP_CANBERRA_XDG_THEME_NAME, theme != "" ? theme : "freedesktop");
        }

        private void eject () {
            GLib.Drive? best = null;
            foreach (var drive in GLib.VolumeMonitor.get ().get_connected_drives ()) {
                if (!drive.can_eject () || !drive.is_media_removable ()) {
                    continue;
                }
                if (best == null) {
                    best = drive;
                }
                if (drive.has_media ()) {
                    best = drive;
                    break;
                }
            }
            osd (Backend.OsdKind.MESSAGE, "media-eject", null);
            if (best != null) {
                best.eject_with_operation.begin (GLib.MountUnmountFlags.FORCE, null, null);
            }
        }

        private async bool has_owner (string name) {
            if (bus == null) {
                return false;
            }
            try {
                var reply = yield bus.call ("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                            "NameHasOwner", new GLib.Variant ("(s)", name),
                                            new GLib.VariantType ("(b)"), GLib.DBusCallFlags.NONE, 5000, null);
                bool owned;
                reply.get ("(b)", out owned);
                return owned;
            } catch (GLib.Error e) {
                return false;
            }
        }

        private async void rfkill (bool bluetooth) {
            if (!yield has_owner (RFKILL_NAME)) {
                return;
            }
            unowned string has_mode = bluetooth ? "BluetoothHasAirplaneMode" : "HasAirplaneMode";
            unowned string hw_mode = bluetooth ? "BluetoothHardwareAirplaneMode" : "HardwareAirplaneMode";
            unowned string mode = bluetooth ? "BluetoothAirplaneMode" : "AirplaneMode";
            try {
                var reply = yield bus.call ("org.mate.SettingsDaemon", "/org/mate/SettingsDaemon/Rfkill",
                                            "org.freedesktop.DBus.Properties", "GetAll",
                                            new GLib.Variant ("(s)", RFKILL_NAME), new GLib.VariantType ("(a{sv})"),
                                            GLib.DBusCallFlags.NONE, 5000, null);
                var props = new GLib.VariantDict (reply.get_child_value (0));
                bool value;
                if (!props.lookup (has_mode, "b", out value) || !value) {
                    return;
                }
                if (props.lookup (hw_mode, "b", out value) && value) {
                    osd (Backend.OsdKind.MESSAGE, "airplane-mode-symbolic",
                         dgettext (DOMAIN, "Hardware Airplane Mode"));
                    return;
                }
                bool current = props.lookup (mode, "b", out value) && value;
                bool target = !current;
                yield bus.call ("org.mate.SettingsDaemon", "/org/mate/SettingsDaemon/Rfkill",
                                "org.freedesktop.DBus.Properties", "Set",
                                new GLib.Variant ("(ssv)", RFKILL_NAME, mode, new GLib.Variant.boolean (target)),
                                null, GLib.DBusCallFlags.NONE, -1, null);
                if (bluetooth) {
                    osd (Backend.OsdKind.MESSAGE, target ? "bluetooth-disabled-symbolic" : "bluetooth-active-symbolic",
                         target ? dgettext (DOMAIN, "Bluetooth disabled") : dgettext (DOMAIN, "Bluetooth enabled"));
                } else {
                    osd (Backend.OsdKind.MESSAGE,
                         target ? "airplane-mode-symbolic" : "network-wireless-signal-excellent-symbolic",
                         target ? dgettext (DOMAIN, "Airplane mode enabled")
                                : dgettext (DOMAIN, "Airplane mode disabled"));
                }
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.INPUT).warning ("Failed to set '%s' property: %s", mode, e.message);
            }
        }

        private void power_policy (string key) {
            if (!has_key (power_settings, key)) {
                return;
            }
            unowned LogScope log = Logging.scope (LogDomain.INPUT);
            string policy = power_settings.get_string (key);
            switch (policy) {
                case "suspend":
                case "hibernate":
                case "shutdown":
                    if (logind == null) {
                        log.info ("%s: no logind outside a Polo session", key);
                        return;
                    }
                    logind.perform.begin (policy == "suspend" ? PowerAction.SUSPEND
                                          : policy == "hibernate" ? PowerAction.HIBERNATE : PowerAction.POWER_OFF);
                    break;
                case "blank":
                    if (blank_locks ()) {
                        lock_screen ();
                    }
                    server.blank_outputs ();
                    break;
                case "interactive":
                    if (bus != null) {
                        bus.call.begin ("org.gnome.SessionManager", "/org/gnome/SessionManager",
                                        "org.gnome.SessionManager", "Shutdown", null, null,
                                        GLib.DBusCallFlags.NO_AUTO_START, -1, null);
                    }
                    break;
                default:
                    break;
            }
        }

        private bool blank_locks () {
            if (has_key (power_settings, "lock-use-screensaver") &&
                power_settings.get_boolean ("lock-use-screensaver")) {
                var screensaver = installed (SCREENSAVER_SCHEMA);
                return has_key (screensaver, "lock-enabled") && screensaver.get_boolean ("lock-enabled");
            }
            return has_key (power_settings, "lock-blank-screen") && power_settings.get_boolean ("lock-blank-screen");
        }

        private async uint? get_percent (string path, string interface_name) {
            try {
                var reply = yield bus.call (MPM, path, interface_name, "GetBrightness", null,
                                            new GLib.VariantType ("(u)"), GLib.DBusCallFlags.NO_AUTO_START, 5000,
                                            null);
                uint value;
                reply.get ("(u)", out value);
                return value;
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.INPUT).info ("%s: %s", interface_name, e.message);
                return null;
            }
        }

        private async bool set_percent (string path, string interface_name, uint value) {
            try {
                yield bus.call (MPM, path, interface_name, "SetBrightness", new GLib.Variant ("(u)", value), null,
                                GLib.DBusCallFlags.NO_AUTO_START, 5000, null);
                return true;
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.INPUT).info ("%s: %s", interface_name, e.message);
                return false;
            }
        }

        private async void brightness (bool up) {
            const string PATH = "/org/mate/PowerManager/Backlight";
            const string INTERFACE = "org.mate.PowerManager.Backlight";
            if (!yield has_owner (MPM)) {
                yield logind_brightness (up);
                return;
            }
            uint? current = yield get_percent (PATH, INTERFACE);
            if (current == null) {
                return;
            }
            uint now = current;
            uint reached = now;
            for (uint step = BRIGHTNESS_STEP; step <= 100 && reached == now; step += BRIGHTNESS_STEP) {
                uint target = up ? uint.min (now + step, 100) : (now > step ? now - step : 0);
                if (!yield set_percent (PATH, INTERFACE, target)) {
                    return;
                }
                uint? read = yield get_percent (PATH, INTERFACE);
                reached = read ?? target;
                if (target == 0 || target == 100) {
                    break;
                }
            }
            osd (Backend.OsdKind.BRIGHTNESS, "gpm-brightness-lcd,display-brightness-symbolic", null, (int) reached,
                 false);
            bool battery = yield on_battery ();
            if (!battery && has_key (power_settings, "brightness-ac")) {
                power_settings.set_double ("brightness-ac", reached);
            }
        }

        private async bool on_battery () {
            if (logind == null) {
                return false;
            }
            var system = yield logind.bus ();
            if (system == null) {
                return false;
            }
            try {
                var reply = yield system.call ("org.freedesktop.UPower", "/org/freedesktop/UPower",
                                               "org.freedesktop.DBus.Properties", "Get",
                                               new GLib.Variant ("(ss)", "org.freedesktop.UPower", "OnBattery"),
                                               new GLib.VariantType ("(v)"), GLib.DBusCallFlags.NONE, 5000, null);
                return reply.get_child_value (0).get_variant ().get_boolean ();
            } catch (GLib.Error e) {
                return false;
            }
        }

        private async void logind_brightness (bool up) {
            if (logind == null) {
                return;
            }
            string? name = null;
            int best = 4;
            try {
                var dir = GLib.Dir.open ("/sys/class/backlight");
                for (string? entry = dir.read_name (); entry != null; entry = dir.read_name ()) {
                    string type = "";
                    try {
                        GLib.FileUtils.get_contents ("/sys/class/backlight/%s/type".printf (entry), out type);
                    } catch (GLib.FileError e) {
                        continue;
                    }
                    int rank = type.strip () == "firmware" ? 0 : type.strip () == "platform" ? 1
                             : type.strip () == "raw" ? 2 : 3;
                    if (rank < best) {
                        best = rank;
                        name = entry;
                    }
                }
            } catch (GLib.FileError e) {
                return;
            }
            if (name == null) {
                return;
            }
            string max_text, current_text;
            try {
                GLib.FileUtils.get_contents ("/sys/class/backlight/%s/max_brightness".printf (name), out max_text);
                GLib.FileUtils.get_contents ("/sys/class/backlight/%s/brightness".printf (name), out current_text);
            } catch (GLib.FileError e) {
                return;
            }
            int max = int.parse (max_text.strip ());
            int current = int.parse (current_text.strip ());
            if (max <= 0) {
                return;
            }
            int step = int.max (1, max * (int) BRIGHTNESS_STEP / 100);
            int target = (current + (up ? step : -step)).clamp (0, max);
            if (yield logind.set_brightness ("backlight", name, target)) {
                osd (Backend.OsdKind.BRIGHTNESS, "gpm-brightness-lcd,display-brightness-symbolic", null,
                     target * 100 / max, false);
            }
        }

        private async void kbd_brightness (int direction) {
            const string PATH = "/org/mate/PowerManager/KbdBacklight";
            const string INTERFACE = "org.mate.PowerManager.KbdBacklight";
            if (!yield has_owner (MPM)) {
                return;
            }
            uint? current = yield get_percent (PATH, INTERFACE);
            if (current == null) {
                return;
            }
            uint now = current;
            uint target;
            if (direction > 0) {
                target = uint.min (now + KBD_BACKLIGHT_STEP, 100);
            } else if (direction < 0) {
                target = now > KBD_BACKLIGHT_STEP ? now - KBD_BACKLIGHT_STEP : 0;
            } else if (now == 0) {
                target = kbd_saved;
            } else {
                kbd_saved = now;
                target = 0;
            }
            if (yield set_percent (PATH, INTERFACE, target)) {
                osd (Backend.OsdKind.BRIGHTNESS, "gpm-brightness-kbd,keyboard-brightness-symbolic", null,
                     (int) target, false);
            }
        }
    }
}
