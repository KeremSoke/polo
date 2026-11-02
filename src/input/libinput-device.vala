/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Input {
    public class LibinputDevice : GLib.Object, PointerDevice {
        public Backend.InputDevice device { get; construct; }

        public LibinputDevice (Backend.InputDevice device) {
            Object (device: device);
        }

        public string name {
            get { return device.get_name (); }
        }

        public bool is_touchpad () {
            return device.is_touchpad ();
        }

        public Backend.DeviceFeatures features () {
            return device.get_features ();
        }

        public bool set_left_handed (bool left_handed) {
            return device.set_left_handed (left_handed);
        }

        public bool set_accel_speed (double speed) {
            return device.set_accel_speed (speed);
        }

        public bool set_accel_profile (Backend.AccelProfile profile) {
            return device.set_accel_profile (profile);
        }

        public bool set_middle_emulation (bool enabled) {
            return device.set_middle_emulation (enabled);
        }

        public bool set_dwt (bool enabled) {
            return device.set_dwt (enabled);
        }

        public bool set_tap (bool enabled) {
            return device.set_tap (enabled);
        }

        public bool set_click_method (Backend.ClickMethod method) {
            return device.set_click_method (method);
        }

        public bool set_scroll_method (Backend.ScrollMethod method) {
            return device.set_scroll_method (method);
        }

        public bool set_natural_scroll (bool enabled) {
            return device.set_natural_scroll (enabled);
        }

        public bool set_send_events (bool enabled) {
            return device.set_send_events (enabled);
        }
    }
}
