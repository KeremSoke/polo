/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * device-list.vala: the kind each libinput device is listed as.
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

int main (string[] args) {
    GLib.Test.init (ref args);
    GLib.Test.add_func ("/input-devices/kind", test_kind);
    return GLib.Test.run ();
}
