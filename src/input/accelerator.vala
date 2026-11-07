/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco accelerator strings to key bindings: ui.c meta_ui_parse_accelerator
 * (lib/marco/prefs.c) with GTK's key name lookup, and Marco's modifier
 * devirtualisation for a standard keymap.
 */

namespace Polo.Input {
    public struct Accelerator {
        public uint32 keysym;
        /* An X keycode (evdev + 8) for "0x.." accelerators, else 0. */
        public uint32 keycode;
        public Marco.VirtualModifier mask;

        public bool is_disabled () {
            return keysym == 0 && keycode == 0;
        }
    }

    public struct ButtonAccelerator {
        /* An evdev button code (BTN_LEFT ...), 0 when disabled. */
        public uint32 button;
        public Marco.VirtualModifier mask;

        public bool is_disabled () {
            return button == 0;
        }
    }

    namespace Accelerators {
        /*
         * meta_ui_parse_accelerator () and gtk_accelerator_parse (): the key
         * name must name a keysym, which is lowercased; "disabled" and ""
         * give a disabled accelerator.
         */
        public bool parse (string? text, out Accelerator accelerator) {
            accelerator = Accelerator ();
            string? key_name;
            uint keycode;
            Marco.VirtualModifier mask;
            if (!Marco.parse_accelerator (text, out key_name, out keycode, out mask)) {
                return false;
            }
            if (key_name != null) {
                uint32 keysym = Backend.Keysym.from_name (key_name);
                if (keysym == 0) {
                    return false;
                }
                accelerator.keysym = Backend.Keysym.to_lower (keysym);
            }
            accelerator.keycode = keycode;
            accelerator.mask = canonical (mask);
            return true;
        }

        public const uint32 BTN_LEFT = 0x110;
        public const uint32 BTN_RIGHT = 0x111;
        public const uint32 BTN_MIDDLE = 0x112;
        public const uint32 BTN_SIDE = 0x113;
        public const uint32 BTN_EXTRA = 0x114;

        public uint32 evdev_button (int number) {
            switch (number) {
                case 1:
                    return BTN_LEFT;
                case 2:
                    return BTN_MIDDLE;
                case 3:
                    return BTN_RIGHT;
                case 8:
                    return BTN_SIDE;
                case 9:
                    return BTN_EXTRA;
                default:
                    return 0;
            }
        }

        public bool parse_button (string? text, out ButtonAccelerator accelerator) {
            accelerator = ButtonAccelerator ();
            if (text == null || text == "" || text == "disabled") {
                return true;
            }
            int end = text.last_index_of_char ('>') + 1;
            string name = text.substring (end);
            if (!name.down ().has_prefix ("button")) {
                return false;
            }
            int number;
            if (!int.try_parse (name.substring ("button".length), out number)) {
                return false;
            }
            uint32 button = evdev_button (number);
            if (button == 0) {
                return false;
            }
            Marco.VirtualModifier mask;
            bool parsed = Marco.parse_modifier (text.substring (0, end), out mask);
            if (!parsed || mask == 0) {
                return false;
            }
            accelerator.button = button;
            accelerator.mask = canonical (mask);
            return true;
        }

        public bool matches_button (ButtonAccelerator accelerator, uint32 button, Backend.Modifiers mods) {
            return !accelerator.is_disabled () && button == accelerator.button &&
                   from_real (mods) == accelerator.mask;
        }

        /*
         * meta_display_devirtualize_modifiers () with the usual modmap:
         * Super and Hyper on Mod4, Meta on Mod1 (Alt).
         */
        public Marco.VirtualModifier canonical (Marco.VirtualModifier mask) {
            var result = mask & (Marco.VirtualModifier.SHIFT_MASK | Marco.VirtualModifier.CONTROL_MASK |
                                 Marco.VirtualModifier.ALT_MASK | Marco.VirtualModifier.MOD3_MASK |
                                 Marco.VirtualModifier.MOD5_MASK);
            if ((mask & (Marco.VirtualModifier.SUPER_MASK | Marco.VirtualModifier.HYPER_MASK |
                         Marco.VirtualModifier.MOD4_MASK)) != 0) {
                result |= Marco.VirtualModifier.SUPER_MASK;
            }
            if ((mask & Marco.VirtualModifier.META_MASK) != 0) {
                result |= Marco.VirtualModifier.ALT_MASK;
            }
            return result;
        }

        /* An event's real modifiers, without Lock and NumLock (Marco's ignored_modifier_mask). */
        public Marco.VirtualModifier from_real (Backend.Modifiers mods) {
            Marco.VirtualModifier result = 0;
            if (Backend.Modifiers.SHIFT in mods) {
                result |= Marco.VirtualModifier.SHIFT_MASK;
            }
            if (Backend.Modifiers.CONTROL in mods) {
                result |= Marco.VirtualModifier.CONTROL_MASK;
            }
            if (Backend.Modifiers.MOD1 in mods) {
                result |= Marco.VirtualModifier.ALT_MASK;
            }
            if (Backend.Modifiers.MOD3 in mods) {
                result |= Marco.VirtualModifier.MOD3_MASK;
            }
            if (Backend.Modifiers.MOD4 in mods) {
                result |= Marco.VirtualModifier.SUPER_MASK;
            }
            if (Backend.Modifiers.MOD5 in mods) {
                result |= Marco.VirtualModifier.MOD5_MASK;
            }
            return result;
        }
    }
}
