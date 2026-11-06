/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The keys mate-settings-daemon 1.28's media-keys plugin grabs on X (acme.h:
 * the org.mate.SettingsDaemon.plugins.media-keys accelerators and its
 * hard-coded XF86 keysyms) and the ones mate-power-manager grabs itself
 * (gpm-button.c). On Wayland neither sees a key unless an X window has the
 * focus, so Polo takes them before its own key bindings, as msd's grabs,
 * made first, won over Marco's. A key matches as in Bindings: the keysym,
 * then the base keysym, then the keycode. Volume keys repeat while held, as
 * X auto-repeat did. While the session is locked only the volume,
 * brightness and media player keys act.
 */

namespace Polo.Input {
    public enum MediaKey {
        TOUCHPAD,
        TOUCHPAD_ON,
        TOUCHPAD_OFF,
        MUTE,
        VOLUME_DOWN,
        VOLUME_UP,
        MUTE_QUIET,
        VOLUME_DOWN_QUIET,
        VOLUME_UP_QUIET,
        MIC_MUTE,
        POWER,
        EJECT,
        HOME,
        MEDIA,
        CALCULATOR,
        MESSENGER,
        SEARCH,
        EMAIL,
        CONTROL_CENTER,
        SCREENSAVER,
        HELP,
        WWW,
        PLAY,
        PAUSE,
        STOP,
        PREVIOUS,
        NEXT,
        REWIND,
        FORWARD,
        REPEAT,
        RANDOM,
        MAGNIFIER,
        SCREENREADER,
        ON_SCREEN_KEYBOARD,
        LOGOUT,
        RFKILL,
        BLUETOOTH_RFKILL,
        DISPLAY,
        POWER_BUTTON,
        SUSPEND_BUTTON,
        HIBERNATE_BUTTON,
        BRIGHTNESS_UP,
        BRIGHTNESS_DOWN,
        LOCK_BUTTON,
        BATTERY,
        KBD_BRIGHTNESS_UP,
        KBD_BRIGHTNESS_DOWN,
        KBD_BRIGHTNESS_TOGGLE;

        public bool repeats () {
            return this == VOLUME_DOWN || this == VOLUME_UP || this == VOLUME_DOWN_QUIET || this == VOLUME_UP_QUIET;
        }

        public bool works_locked () {
            switch (this) {
                case MUTE:
                case VOLUME_DOWN:
                case VOLUME_UP:
                case MUTE_QUIET:
                case VOLUME_DOWN_QUIET:
                case VOLUME_UP_QUIET:
                case MIC_MUTE:
                case PLAY:
                case PAUSE:
                case STOP:
                case PREVIOUS:
                case NEXT:
                case BRIGHTNESS_UP:
                case BRIGHTNESS_DOWN:
                case KBD_BRIGHTNESS_UP:
                case KBD_BRIGHTNESS_DOWN:
                case KBD_BRIGHTNESS_TOGGLE:
                    return true;
                default:
                    return false;
            }
        }
    }

    public interface MediaKeyTarget : GLib.Object {
        public abstract void run (MediaKey key);
    }

    public class MediaKeys : GLib.Object {
        public const string SCHEMA = "org.mate.SettingsDaemon.plugins.media-keys";
        private const string KEYBOARD_SCHEMA = "org.mate.peripherals-keyboard";
        private const uint64 KEYCODE_KEY = 1ULL << 56;

        private struct Entry {
            MediaKey key;
            unowned string? settings_key;
            unowned string? hard_coded;
        }

        private const Entry[] ENTRIES = {
            { MediaKey.TOUCHPAD, "touchpad", null },
            { MediaKey.TOUCHPAD_ON, null, "XF86TouchpadOn" },
            { MediaKey.TOUCHPAD_OFF, null, "XF86TouchpadOff" },
            { MediaKey.MUTE, "volume-mute", null },
            { MediaKey.VOLUME_DOWN, "volume-down", null },
            { MediaKey.VOLUME_UP, "volume-up", null },
            { MediaKey.MUTE_QUIET, "volume-mute-quiet", null },
            { MediaKey.VOLUME_DOWN_QUIET, "volume-down-quiet", null },
            { MediaKey.VOLUME_UP_QUIET, "volume-up-quiet", null },
            { MediaKey.MIC_MUTE, "mic-mute", null },
            { MediaKey.POWER, "power", null },
            { MediaKey.EJECT, "eject", null },
            { MediaKey.HOME, "home", null },
            { MediaKey.MEDIA, "media", null },
            { MediaKey.CALCULATOR, "calculator", null },
            { MediaKey.MESSENGER, "messenger", null },
            { MediaKey.SEARCH, "search", null },
            { MediaKey.EMAIL, "email", null },
            { MediaKey.CONTROL_CENTER, "control-center", null },
            { MediaKey.SCREENSAVER, "screensaver", null },
            { MediaKey.HELP, "help", null },
            { MediaKey.WWW, "www", null },
            { MediaKey.PLAY, "play", null },
            { MediaKey.PAUSE, "pause", null },
            { MediaKey.STOP, "stop", null },
            { MediaKey.PREVIOUS, "previous", null },
            { MediaKey.NEXT, "next", null },
            { MediaKey.REWIND, null, "XF86AudioRewind" },
            { MediaKey.FORWARD, null, "XF86AudioForward" },
            { MediaKey.REPEAT, null, "XF86AudioRepeat" },
            { MediaKey.RANDOM, null, "XF86AudioRandomPlay" },
            { MediaKey.MAGNIFIER, "magnifier", null },
            { MediaKey.SCREENREADER, "screenreader", null },
            { MediaKey.ON_SCREEN_KEYBOARD, "on-screen-keyboard", null },
            { MediaKey.LOGOUT, "logout", null },
            { MediaKey.RFKILL, null, "XF86WLAN" },
            { MediaKey.BLUETOOTH_RFKILL, null, "XF86Bluetooth" },
            { MediaKey.DISPLAY, null, "XF86Display" },
            { MediaKey.POWER_BUTTON, null, "XF86PowerOff" },
            { MediaKey.HIBERNATE_BUTTON, null, "XF86Suspend" },
            { MediaKey.SUSPEND_BUTTON, null, "XF86Sleep" },
            { MediaKey.HIBERNATE_BUTTON, null, "XF86Hibernate" },
            { MediaKey.BRIGHTNESS_UP, null, "XF86MonBrightnessUp" },
            { MediaKey.BRIGHTNESS_DOWN, null, "XF86MonBrightnessDown" },
            { MediaKey.LOCK_BUTTON, null, "XF86ScreenSaver" },
            { MediaKey.BATTERY, null, "XF86Battery" },
            { MediaKey.KBD_BRIGHTNESS_UP, null, "XF86KbdBrightnessUp" },
            { MediaKey.KBD_BRIGHTNESS_DOWN, null, "XF86KbdBrightnessDown" },
            { MediaKey.KBD_BRIGHTNESS_TOGGLE, null, "XF86KbdLightOnOff" },
        };

        private GLib.Settings? settings;
        private GLib.Settings? keyboard = null;
        private MediaKeyTarget target;
        private GLib.HashTable<void*, MediaKey?> table =
            new GLib.HashTable<void*, MediaKey?> (GLib.direct_hash, GLib.direct_equal);
        private uint repeat_id = 0;
        private uint repeat_keycode = 0;

        public MediaKeys (GLib.Settings? settings, MediaKeyTarget target) {
            this.settings = settings;
            this.target = target;
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (KEYBOARD_SCHEMA, true) : null;
            if (schema != null && schema.has_key ("delay") && schema.has_key ("rate")) {
                keyboard = new GLib.Settings.full (schema, null, null);
            }
            if (settings != null) {
                settings.changed.connect (rebuild);
            }
            rebuild ();
        }

        public void shutdown () {
            if (settings != null) {
                settings.changed.disconnect (rebuild);
            }
            stop_repeat ();
        }

        private static void* key (uint64 value) {
            return (void*) (ulong) value;
        }

        private static void* keysym_key (uint32 keysym, Marco.VirtualModifier mask) {
            return key ((uint64) keysym << 16 | (uint64) mask);
        }

        private static void* keycode_key (uint32 keycode, Marco.VirtualModifier mask) {
            return key (KEYCODE_KEY | (uint64) keycode << 16 | (uint64) mask);
        }

        private static bool parse (string? text, out Accelerator accelerator) {
            accelerator = Accelerator ();
            if (text == null || text == "" || text == "disabled") {
                return false;
            }
            return Accelerators.parse (text, out accelerator) && !accelerator.is_disabled ();
        }

        private void rebuild () {
            var fresh = new GLib.HashTable<void*, MediaKey?> (GLib.direct_hash, GLib.direct_equal);
            foreach (unowned Entry entry in ENTRIES) {
                string? text = entry.hard_coded;
                if (entry.settings_key != null) {
                    if (settings == null || !settings.settings_schema.has_key (entry.settings_key)) {
                        continue;
                    }
                    text = settings.get_string (entry.settings_key);
                }
                Accelerator accelerator;
                if (!parse (text, out accelerator)) {
                    continue;
                }
                var k = accelerator.keysym != 0 ? keysym_key (accelerator.keysym, accelerator.mask)
                                                : keycode_key (accelerator.keycode, accelerator.mask);
                if (!fresh.contains (k)) {
                    fresh.insert (k, entry.key);
                }
            }
            table = fresh;
        }

        public MediaKey? match (Backend.KeyEvent event) {
            var mask = Accelerators.from_real (event.modifiers);
            MediaKey? found = table.lookup (keysym_key (Backend.Keysym.to_lower (event.keysym), mask));
            if (found == null && event.base_keysym != event.keysym) {
                found = table.lookup (keysym_key (Backend.Keysym.to_lower (event.base_keysym), mask));
            }
            if (found == null) {
                found = table.lookup (keycode_key (event.keycode + 8, mask));
            }
            return found;
        }

        public Backend.FilterResult filter (Backend.KeyEvent event, bool locked) {
            if (!event.pressed) {
                if (event.keycode == repeat_keycode) {
                    stop_repeat ();
                }
                return Backend.FilterResult.PASS;
            }
            MediaKey? found = match (event);
            if (found == null || (locked && !found.works_locked ())) {
                return Backend.FilterResult.PASS;
            }
            MediaKey media_key = found;
            target.run (media_key);
            if (media_key.repeats ()) {
                start_repeat (event.keycode, media_key);
            }
            return Backend.FilterResult.CONSUME;
        }

        private void start_repeat (uint keycode, MediaKey media_key) {
            stop_repeat ();
            if (keyboard != null && keyboard.settings_schema.has_key ("repeat") && !keyboard.get_boolean ("repeat")) {
                return;
            }
            uint delay = keyboard != null ? (uint) int.max (1, keyboard.get_int ("delay")) : 500;
            uint interval = keyboard != null ? 1000 / (uint) int.max (1, keyboard.get_int ("rate")) : 33;
            repeat_keycode = keycode;
            repeat_id = GLib.Timeout.add (delay, () => {
                repeat_id = GLib.Timeout.add (interval, () => {
                    target.run (media_key);
                    return GLib.Source.CONTINUE;
                });
                target.run (media_key);
                return GLib.Source.REMOVE;
            });
        }

        public static void sound_step (MediaKey key, uint32 volume, bool muted, uint32 min, uint32 max, int step,
                                       out uint32 new_volume, out bool new_muted) {
            uint32 scaled = (uint32) ((uint64) (max - min) * (uint) step / 100);
            new_volume = volume;
            new_muted = muted;
            switch (key) {
                case MediaKey.MUTE:
                case MediaKey.MIC_MUTE:
                    new_muted = !muted;
                    break;
                case MediaKey.VOLUME_DOWN:
                    if (volume <= min + scaled) {
                        new_volume = min;
                        new_muted = true;
                    } else {
                        new_volume = volume - scaled;
                        new_muted = false;
                    }
                    break;
                case MediaKey.VOLUME_UP:
                    if (muted) {
                        new_muted = false;
                        if (volume <= min) {
                            new_volume = min + scaled;
                        }
                    } else {
                        new_volume = (uint32) ((uint64) volume + scaled).clamp (min, max);
                    }
                    break;
                default:
                    break;
            }
        }

        public static unowned string volume_icon (bool mic, bool muted, int level) {
            const string[] NAMES = {
                "audio-volume-muted", "audio-volume-low", "audio-volume-medium", "audio-volume-high",
                "microphone-sensitivity-muted", "microphone-sensitivity-low", "microphone-sensitivity-medium",
                "microphone-sensitivity-high"
            };
            int n = muted ? 0 : (3 * level / 100 + 1).clamp (1, 3);
            return NAMES[mic ? n + 4 : n];
        }

        public void stop_repeat () {
            if (repeat_id != 0) {
                GLib.Source.remove (repeat_id);
                repeat_id = 0;
            }
            repeat_keycode = 0;
        }
    }
}
