/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Every libinput device, as org.mate.InputDevices1 lists them for clients
 * that cannot see input devices on Wayland (mate-control-center's mouse
 * capplet asks whether a touchpad exists and whether it can click).
 */

namespace Polo.Input {
    public class DeviceList : GLib.Object {
        private const uint32 BTN_LEFT = 0x110;

        private GLib.GenericArray<Backend.InputDevice> devices = new GLib.GenericArray<Backend.InputDevice> ();

        public void add (Backend.InputDevice device) {
            devices.add (device);
            device.removed.connect (() => devices.remove (device));
        }

        public void clear () {
            devices.remove_range (0, devices.length);
        }

        public static string kind (bool keyboard, bool pointer, bool touchpad, bool touch, bool tablet) {
            if (pointer && touchpad) {
                return "touchpad";
            }
            if (tablet) {
                return "tablet";
            }
            if (touch) {
                return "touchscreen";
            }
            if (pointer) {
                return "pointer";
            }
            if (keyboard) {
                return "keyboard";
            }
            return "other";
        }

        public static GLib.Variant describe (string name, string kind, bool has_left_button) {
            var entry = new GLib.VariantBuilder (new GLib.VariantType ("a{sv}"));
            entry.add ("{sv}", "name", new GLib.Variant.string (name));
            entry.add ("{sv}", "type", new GLib.Variant.string (kind));
            if (kind == "pointer" || kind == "touchpad") {
                entry.add ("{sv}", "has-left-button", new GLib.Variant.boolean (has_left_button));
            }
            return entry.end ();
        }

        public GLib.Variant to_variant () {
            var list = new GLib.VariantBuilder (new GLib.VariantType ("aa{sv}"));
            foreach (var device in devices) {
                string type = kind (device.has_capability (Backend.DeviceCapability.KEYBOARD),
                                    device.has_capability (Backend.DeviceCapability.POINTER),
                                    device.is_touchpad (),
                                    device.has_capability (Backend.DeviceCapability.TOUCH),
                                    device.has_capability (Backend.DeviceCapability.TABLET_TOOL));
                list.add_value (describe (device.get_name (), type, device.has_button (BTN_LEFT)));
            }
            return list.end ();
        }
    }
}
