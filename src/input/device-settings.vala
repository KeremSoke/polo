/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate-settings-daemon 1.28's mouse plugin (msd-mouse-manager.c), its
 * libinput paths: what msd writes to xf86-input-libinput's device
 * properties goes to the same libinput settings here. A touchpad is what
 * device_is_touchpad () finds, a device with tap fingers. Not mapped:
 * motion-threshold (msd's libinput path ignores it too), the tap-button-*
 * keys (msd sets them for synaptics only), and the horizontal-*-scrolling
 * keys, which xf86-input-libinput applies in the X driver where libinput has
 * no such setting, so horizontal scrolling follows the scroll method.
 * double-click, drag-threshold, and locate-pointer are client settings.
 */

namespace Polo.Input {
    /* What this file needs of a libinput device. */
    public interface PointerDevice : GLib.Object {
        public abstract string name { get; }
        public abstract bool is_touchpad ();
        public abstract Backend.DeviceFeatures features ();
        public abstract bool set_left_handed (bool left_handed);
        public abstract bool set_accel_speed (double speed);
        public abstract bool set_accel_profile (Backend.AccelProfile profile);
        public abstract bool set_middle_emulation (bool enabled);
        public abstract bool set_dwt (bool enabled);
        public abstract bool set_tap (bool enabled);
        public abstract bool set_click_method (Backend.ClickMethod method);
        public abstract bool set_scroll_method (Backend.ScrollMethod method);
        public abstract bool set_natural_scroll (bool enabled);
        public abstract bool set_send_events (bool enabled);
    }

    public class DeviceSettings : GLib.Object {
        public const string MOUSE_SCHEMA = "org.mate.peripherals-mouse";
        public const string TOUCHPAD_SCHEMA = "org.mate.peripherals-touchpad";

        /* org.mate.peripherals-touchpad.Handedness */
        public enum Handedness {
            RIGHT = 0,
            LEFT = 1,
            MOUSE = 2
        }

        private GLib.Settings? mouse;
        private GLib.Settings? touchpad;
        private GLib.GenericArray<PointerDevice> devices = new GLib.GenericArray<PointerDevice> ();

        public DeviceSettings (GLib.Settings? mouse, GLib.Settings? touchpad) {
            this.mouse = mouse;
            this.touchpad = touchpad;
            if (mouse != null) {
                mouse.changed.connect (mouse_callback);
            }
            if (touchpad != null) {
                touchpad.changed.connect (touchpad_callback);
            }
        }

        public void shutdown () {
            if (mouse != null) {
                mouse.changed.disconnect (mouse_callback);
            }
            if (touchpad != null) {
                touchpad.changed.disconnect (touchpad_callback);
            }
            devices.remove_range (0, devices.length);
        }

        public void add (PointerDevice device) {
            devices.add (device);
            if (mouse != null && touchpad != null) {
                set_mouse_settings (device);
            }
        }

        public void remove (PointerDevice device) {
            devices.remove (device);
        }

        /* set_motion_libinput (): the panel's 1..10 onto libinput's -1..1; unset (-1) is 0. */
        public static double accel_speed (double motion_acceleration) {
            float accel;
            if (motion_acceleration == -1.0) {
                accel = 0.0f;
            } else {
                accel = (float) ((motion_acceleration - 1.0) * 2.0 / 9.0 - 1);
            }
            return accel;
        }

        public static bool touchpad_left_handed (Handedness handedness, bool mouse_left_handed) {
            switch (handedness) {
                case Handedness.RIGHT:
                    return false;
                case Handedness.LEFT:
                    return true;
                default:
                    return mouse_left_handed;
            }
        }

        public static Backend.ClickMethod click_method (int two_finger_click, int three_finger_click) {
            bool want_clickfinger = two_finger_click != 0 || three_finger_click != 0;
            return want_clickfinger ? Backend.ClickMethod.CLICKFINGER : Backend.ClickMethod.BUTTON_AREAS;
        }

        /* set_scrolling_libinput (): one method at a time, two-finger first. */
        public static Backend.ScrollMethod scroll_method (bool vertical_two_finger, bool vertical_edge) {
            if (vertical_two_finger) {
                return Backend.ScrollMethod.TWO_FINGER;
            }
            return vertical_edge ? Backend.ScrollMethod.EDGE : Backend.ScrollMethod.NONE;
        }

        /* ---- set_mouse_settings () and the callbacks --------------------- */

        private void set_mouse_settings (PointerDevice device) {
            bool mouse_left_handed = mouse.get_boolean ("left-handed");
            set_left_handed (device, mouse_left_handed, touchpad_handedness (mouse_left_handed));
            set_motion (device);
            set_middle_button (device, mouse.get_boolean ("middle-button-enabled"));
            set_disable_w_typing (device, touchpad.get_boolean ("disable-while-typing"));
            set_tap_to_click (device);
            set_click_actions (device);
            set_scrolling (device);
            set_natural_scroll (device);
            set_touchpad_enabled (device, touchpad.get_boolean ("touchpad-enabled"));
            set_accel_profile (device);
        }

        private bool touchpad_handedness (bool mouse_left_handed) {
            return touchpad_left_handed ((Handedness) touchpad.get_enum ("left-handed"), mouse_left_handed);
        }

        private void mouse_callback (string key) {
            if (touchpad == null) {
                return;
            }
            switch (key) {
                case "left-handed":
                    bool mouse_left_handed = mouse.get_boolean (key);
                    bool touchpad_lh = touchpad_handedness (mouse_left_handed);
                    foreach (var device in devices) {
                        set_left_handed (device, mouse_left_handed, touchpad_lh);
                    }
                    break;
                case "motion-acceleration":
                case "motion-threshold":
                    foreach (var device in devices) {
                        set_motion (device);
                    }
                    break;
                case "accel-profile":
                    foreach (var device in devices) {
                        set_accel_profile (device);
                    }
                    break;
                case "middle-button-enabled":
                    bool middle_button = mouse.get_boolean (key);
                    foreach (var device in devices) {
                        set_middle_button (device, middle_button);
                    }
                    break;
                default:
                    break;
            }
        }

        private void touchpad_callback (string key) {
            if (mouse == null) {
                return;
            }
            switch (key) {
                case "disable-while-typing":
                    bool state = touchpad.get_boolean (key);
                    foreach (var device in devices) {
                        set_disable_w_typing (device, state);
                    }
                    break;
                case "left-handed":
                    bool mouse_left_handed = mouse.get_boolean (key);
                    bool touchpad_lh = touchpad_handedness (mouse_left_handed);
                    foreach (var device in devices) {
                        set_left_handed (device, mouse_left_handed, touchpad_lh);
                    }
                    break;
                case "tap-to-click":
                case "tap-button-one-finger":
                case "tap-button-two-finger":
                case "tap-button-three-finger":
                    foreach (var device in devices) {
                        set_tap_to_click (device);
                    }
                    break;
                case "two-finger-click":
                case "three-finger-click":
                    foreach (var device in devices) {
                        set_click_actions (device);
                    }
                    break;
                case "vertical-edge-scrolling":
                case "horizontal-edge-scrolling":
                case "vertical-two-finger-scrolling":
                case "horizontal-two-finger-scrolling":
                    foreach (var device in devices) {
                        set_scrolling (device);
                    }
                    break;
                case "natural-scroll":
                    foreach (var device in devices) {
                        set_natural_scroll (device);
                    }
                    break;
                case "touchpad-enabled":
                    bool state = touchpad.get_boolean (key);
                    foreach (var device in devices) {
                        set_touchpad_enabled (device, state);
                    }
                    break;
                case "motion-acceleration":
                case "motion-threshold":
                    foreach (var device in devices) {
                        set_motion (device);
                    }
                    break;
                case "accel-profile":
                    foreach (var device in devices) {
                        set_accel_profile (device);
                    }
                    break;
                default:
                    break;
            }
        }

        /* ---- One device, as each set_*_libinput () ----------------------- */

        private static bool has (PointerDevice device, Backend.DeviceFeatures feature) {
            return feature in device.features ();
        }

        private void set_left_handed (PointerDevice device, bool mouse_left_handed, bool touchpad_left_handed) {
            if (has (device, Backend.DeviceFeatures.LEFT_HANDED)) {
                device.set_left_handed (device.is_touchpad () ? touchpad_left_handed : mouse_left_handed);
            }
        }

        private void set_motion (PointerDevice device) {
            if (has (device, Backend.DeviceFeatures.ACCEL)) {
                var settings = device.is_touchpad () ? touchpad : mouse;
                device.set_accel_speed (accel_speed (settings.get_double ("motion-acceleration")));
            }
        }

        private void set_middle_button (PointerDevice device, bool middle_button) {
            if (!device.is_touchpad () && has (device, Backend.DeviceFeatures.MIDDLE_EMULATION)) {
                device.set_middle_emulation (middle_button);
            }
        }

        private void set_disable_w_typing (PointerDevice device, bool state) {
            if (device.is_touchpad () && has (device, Backend.DeviceFeatures.DWT)) {
                device.set_dwt (state);
            }
        }

        private void set_tap_to_click (PointerDevice device) {
            if (device.is_touchpad ()) {
                device.set_tap (touchpad.get_boolean ("tap-to-click"));
            }
        }

        private void set_click_actions (PointerDevice device) {
            if (device.is_touchpad () && has (device, Backend.DeviceFeatures.CLICK_METHOD)) {
                device.set_click_method (click_method (touchpad.get_int ("two-finger-click"),
                                                       touchpad.get_int ("three-finger-click")));
            }
        }

        private void set_scrolling (PointerDevice device) {
            if (device.is_touchpad () && has (device, Backend.DeviceFeatures.SCROLL_METHOD)) {
                device.set_scroll_method (scroll_method (touchpad.get_boolean ("vertical-two-finger-scrolling"),
                                                         touchpad.get_boolean ("vertical-edge-scrolling")));
            }
        }

        private void set_natural_scroll (PointerDevice device) {
            if (device.is_touchpad () && has (device, Backend.DeviceFeatures.NATURAL_SCROLL)) {
                device.set_natural_scroll (touchpad.get_boolean ("natural-scroll"));
            }
        }

        private void set_touchpad_enabled (PointerDevice device, bool state) {
            if (device.is_touchpad ()) {
                device.set_send_events (state);
            }
        }

        /* set_accel_profile_libinput (): the enums share adaptive's and flat's places. */
        private void set_accel_profile (PointerDevice device) {
            if (has (device, Backend.DeviceFeatures.ACCEL_PROFILE)) {
                var settings = device.is_touchpad () ? touchpad : mouse;
                device.set_accel_profile ((Backend.AccelProfile) settings.get_enum ("accel-profile"));
            }
        }
    }
}
