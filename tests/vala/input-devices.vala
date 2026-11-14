/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * device-list.vala: the type org.mate.InputDevices1 gives each libinput
 * device, and the dictionary for it.
 */

using Polo.Input;

static void test_kind () {
    assert (DeviceList.kind (false, true, true, false, false) == "touchpad");
    assert (DeviceList.kind (false, true, false, false, false) == "pointer");
    assert (DeviceList.kind (true, false, false, false, false) == "keyboard");
    assert (DeviceList.kind (true, true, false, false, false) == "pointer");
    assert (DeviceList.kind (false, true, false, true, false) == "touchscreen");
    assert (DeviceList.kind (false, false, false, true, false) == "touchscreen");
    assert (DeviceList.kind (false, true, false, false, true) == "tablet");
    assert (DeviceList.kind (false, false, true, false, false) == "other");
    assert (DeviceList.kind (false, false, false, false, false) == "other");
}

static void test_describe () {
    var tap_only = DeviceList.describe ("Tap-only touchpad", "touchpad", false);
    assert (tap_only.lookup_value ("name", GLib.VariantType.STRING).get_string () == "Tap-only touchpad");
    assert (tap_only.lookup_value ("type", GLib.VariantType.STRING).get_string () == "touchpad");
    assert (!tap_only.lookup_value ("has-left-button", GLib.VariantType.BOOLEAN).get_boolean ());

    var clickpad = DeviceList.describe ("Clickpad", "touchpad", true);
    assert (clickpad.lookup_value ("has-left-button", GLib.VariantType.BOOLEAN).get_boolean ());

    var mouse = DeviceList.describe ("Mouse", "pointer", true);
    assert (mouse.lookup_value ("has-left-button", GLib.VariantType.BOOLEAN).get_boolean ());

    var keyboard = DeviceList.describe ("Keyboard", "keyboard", false);
    assert (keyboard.lookup_value ("has-left-button", null) == null);
    assert (keyboard.get_type_string () == "a{sv}");
}

static void test_empty () {
    var list = new DeviceList ();
    var value = list.to_variant ();
    assert (value.get_type_string () == "aa{sv}");
    assert (value.n_children () == 0);
}

int main (string[] args) {
    GLib.Test.init (ref args);
    GLib.Test.add_func ("/input-devices/kind", test_kind);
    GLib.Test.add_func ("/input-devices/describe", test_describe);
    GLib.Test.add_func ("/input-devices/empty", test_empty);
    return GLib.Test.run ();
}
