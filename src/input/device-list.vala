/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Every libinput device the compositor manages, and the kind each one is
 * listed as for MATE's input preferences.
 */

namespace Polo.Input {
    public class DeviceList : GLib.Object {

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
    }
}
