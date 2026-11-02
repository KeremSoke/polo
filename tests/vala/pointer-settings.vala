/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * device-settings.vala against msd-mouse-manager.c's libinput paths, with
 * fake devices: which device gets which setting, the value mappings, and
 * live changes. GSettings uses the memory backend.
 */

using Polo;
using Polo.Input;

class FakeDevice : GLib.Object, PointerDevice {
    public bool touchpad;
    public Backend.DeviceFeatures available;
    public int calls = 0;

    public bool left_handed;
    public bool left_handed_set = false;
    public double speed = 99;
    public Backend.AccelProfile profile;
    public bool profile_set = false;
    public bool middle;
    public bool middle_set = false;
    public bool dwt;
    public bool dwt_set = false;
    public bool tap;
    public bool tap_set = false;
    public Backend.ClickMethod click;
    public bool click_set = false;
    public Backend.ScrollMethod scroll;
    public bool scroll_set = false;
    public bool natural;
    public bool natural_set = false;
    public bool send_events;
    public bool send_events_set = false;

    public FakeDevice (bool touchpad, Backend.DeviceFeatures available) {
        this.touchpad = touchpad;
        this.available = available;
    }

    public string name {
        get { return touchpad ? "touchpad" : "mouse"; }
    }

    public bool is_touchpad () {
        return touchpad;
    }

    public Backend.DeviceFeatures features () {
        return available;
    }

    public bool set_left_handed (bool value) {
        calls++;
        left_handed = value;
        left_handed_set = true;
        return true;
    }

    public bool set_accel_speed (double value) {
        calls++;
        speed = value;
        return true;
    }

    public bool set_accel_profile (Backend.AccelProfile value) {
        calls++;
        profile = value;
        profile_set = true;
        return true;
    }

    public bool set_middle_emulation (bool value) {
        calls++;
        middle = value;
        middle_set = true;
        return true;
    }

    public bool set_dwt (bool value) {
        calls++;
        dwt = value;
        dwt_set = true;
        return true;
    }

    public bool set_tap (bool value) {
        calls++;
        tap = value;
        tap_set = true;
        return true;
    }

    public bool set_click_method (Backend.ClickMethod value) {
        calls++;
        click = value;
        click_set = true;
        return true;
    }

    public bool set_scroll_method (Backend.ScrollMethod value) {
        calls++;
        scroll = value;
        scroll_set = true;
        return true;
    }

    public bool set_natural_scroll (bool value) {
        calls++;
        natural = value;
        natural_set = true;
        return true;
    }

    public bool set_send_events (bool value) {
        calls++;
        send_events = value;
        send_events_set = true;
        return true;
    }
}

const Backend.DeviceFeatures MOUSE = Backend.DeviceFeatures.LEFT_HANDED | Backend.DeviceFeatures.ACCEL |
                                     Backend.DeviceFeatures.ACCEL_PROFILE |
                                     Backend.DeviceFeatures.MIDDLE_EMULATION |
                                     Backend.DeviceFeatures.SEND_EVENTS;
const Backend.DeviceFeatures TOUCHPAD = MOUSE | Backend.DeviceFeatures.DWT | Backend.DeviceFeatures.TAP |
                                        Backend.DeviceFeatures.CLICK_METHOD |
                                        Backend.DeviceFeatures.SCROLL_METHOD |
                                        Backend.DeviceFeatures.NATURAL_SCROLL;

GLib.Settings mouse_settings;
GLib.Settings touchpad_settings;

void reset_settings () {
    foreach (var settings in new GLib.Settings[] { mouse_settings, touchpad_settings }) {
        foreach (var key in settings.settings_schema.list_keys ()) {
            settings.reset (key);
        }
    }
}

void test_mappings () {
    assert_cmpfloat (DeviceSettings.accel_speed (-1), CompareOperator.EQ, 0);
    assert_cmpfloat (DeviceSettings.accel_speed (1), CompareOperator.EQ, -1);
    assert_cmpfloat (DeviceSettings.accel_speed (10), CompareOperator.EQ, 1);
    assert_cmpfloat (DeviceSettings.accel_speed (5.5), CompareOperator.EQ, 0);
    assert_cmpfloat (DeviceSettings.accel_speed (3), CompareOperator.EQ, (float) (2.0 * 2.0 / 9.0 - 1));

    assert_false (DeviceSettings.touchpad_left_handed (DeviceSettings.Handedness.RIGHT, true));
    assert_true (DeviceSettings.touchpad_left_handed (DeviceSettings.Handedness.LEFT, false));
    assert_true (DeviceSettings.touchpad_left_handed (DeviceSettings.Handedness.MOUSE, true));
    assert_false (DeviceSettings.touchpad_left_handed (DeviceSettings.Handedness.MOUSE, false));

    assert_true (DeviceSettings.click_method (0, 0) == Backend.ClickMethod.BUTTON_AREAS);
    assert_true (DeviceSettings.click_method (3, 0) == Backend.ClickMethod.CLICKFINGER);
    assert_true (DeviceSettings.click_method (0, 2) == Backend.ClickMethod.CLICKFINGER);

    assert_true (DeviceSettings.scroll_method (true, true) == Backend.ScrollMethod.TWO_FINGER);
    assert_true (DeviceSettings.scroll_method (false, true) == Backend.ScrollMethod.EDGE);
    assert_true (DeviceSettings.scroll_method (false, false) == Backend.ScrollMethod.NONE);
}

void test_new_devices () {
    reset_settings ();
    var settings = new DeviceSettings (mouse_settings, touchpad_settings);
    var mouse = new FakeDevice (false, MOUSE);
    var touchpad = new FakeDevice (true, TOUCHPAD);
    settings.add (mouse);
    settings.add (touchpad);

    assert_true (mouse.left_handed_set && !mouse.left_handed);
    assert_cmpfloat (mouse.speed, CompareOperator.EQ, 0);
    assert_true (mouse.middle_set && mouse.middle);
    assert_true (mouse.profile_set && mouse.profile == Backend.AccelProfile.DEFAULT);
    assert_false (mouse.tap_set || mouse.dwt_set || mouse.click_set || mouse.scroll_set ||
                  mouse.natural_set || mouse.send_events_set);

    assert_true (touchpad.left_handed_set && !touchpad.left_handed);
    assert_true (touchpad.tap_set && !touchpad.tap);
    assert_true (touchpad.dwt_set && !touchpad.dwt);
    assert_true (touchpad.click_set && touchpad.click == Backend.ClickMethod.BUTTON_AREAS);
    assert_true (touchpad.scroll_set && touchpad.scroll == Backend.ScrollMethod.TWO_FINGER);
    assert_true (touchpad.natural_set && !touchpad.natural);
    assert_true (touchpad.send_events_set && touchpad.send_events);
    assert_false (touchpad.middle_set);

    var bare = new FakeDevice (false, 0);
    settings.add (bare);
    assert_cmpint (bare.calls, CompareOperator.EQ, 0);
    settings.shutdown ();
}

void test_live_changes () {
    reset_settings ();
    var settings = new DeviceSettings (mouse_settings, touchpad_settings);
    var mouse = new FakeDevice (false, MOUSE);
    var touchpad = new FakeDevice (true, TOUCHPAD);
    settings.add (mouse);
    settings.add (touchpad);

    mouse_settings.set_boolean ("left-handed", true);
    assert_true (mouse.left_handed && touchpad.left_handed);
    touchpad_settings.set_enum ("left-handed", DeviceSettings.Handedness.RIGHT);
    assert_true (mouse.left_handed && !touchpad.left_handed);

    touchpad_settings.set_double ("motion-acceleration", 10);
    assert_cmpfloat (touchpad.speed, CompareOperator.EQ, 1);
    assert_cmpfloat (mouse.speed, CompareOperator.EQ, 0);
    mouse_settings.set_double ("motion-acceleration", 1);
    assert_cmpfloat (mouse.speed, CompareOperator.EQ, -1);

    mouse_settings.set_enum ("accel-profile", 2);
    assert_true (mouse.profile == Backend.AccelProfile.FLAT);
    assert_true (touchpad.profile == Backend.AccelProfile.DEFAULT);

    mouse_settings.set_boolean ("middle-button-enabled", false);
    assert_false (mouse.middle);
    assert_false (touchpad.middle_set);

    touchpad_settings.set_boolean ("tap-to-click", true);
    assert_true (touchpad.tap);
    touchpad_settings.set_boolean ("disable-while-typing", true);
    assert_true (touchpad.dwt);
    touchpad_settings.set_int ("two-finger-click", 3);
    assert_true (touchpad.click == Backend.ClickMethod.CLICKFINGER);
    touchpad_settings.set_boolean ("vertical-two-finger-scrolling", false);
    assert_true (touchpad.scroll == Backend.ScrollMethod.EDGE);
    touchpad_settings.set_boolean ("vertical-edge-scrolling", false);
    assert_true (touchpad.scroll == Backend.ScrollMethod.NONE);
    touchpad_settings.set_boolean ("natural-scroll", true);
    assert_true (touchpad.natural);
    touchpad_settings.set_boolean ("touchpad-enabled", false);
    assert_false (touchpad.send_events);
    assert_false (mouse.send_events_set);

    settings.remove (touchpad);
    int calls = touchpad.calls;
    touchpad_settings.set_boolean ("tap-to-click", false);
    assert_cmpint (touchpad.calls, CompareOperator.EQ, calls);
    settings.shutdown ();
}

int main (string[] args) {
    GLib.Environment.set_variable ("GSETTINGS_BACKEND", "memory", true);
    Test.init (ref args);
    mouse_settings = new GLib.Settings (DeviceSettings.MOUSE_SCHEMA);
    touchpad_settings = new GLib.Settings (DeviceSettings.TOUCHPAD_SCHEMA);
    Test.add_func ("/device-settings/mappings", test_mappings);
    Test.add_func ("/device-settings/new-devices", test_new_devices);
    Test.add_func ("/device-settings/live-changes", test_live_changes);
    return Test.run ();
}
