/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

using Polo;

void test_monitor () {
    var source = Policy.ShareSource.parse ("Monitor: DP-1 Dell Inc. U2720Q");
    assert (source != null);
    assert (source.kind == Policy.ShareSource.Kind.MONITOR);
    assert (source.key == "DP-1");
    assert (source.label == "Dell Inc. U2720Q");
    assert (source.line == "Monitor: DP-1 Dell Inc. U2720Q");

    source = Policy.ShareSource.parse ("Monitor: headless0");
    assert (source != null && source.key == "headless0" && source.label == "headless0");
}

void test_window () {
    var source = Policy.ShareSource.parse ("Window: Mozilla Firefox (0123456789abcdef0123456789abcdef)");
    assert (source != null);
    assert (source.kind == Policy.ShareSource.Kind.WINDOW);
    assert (source.key == "0123456789abcdef0123456789abcdef");
    assert (source.label == "Mozilla Firefox");

    source = Policy.ShareSource.parse ("Window: notes (draft) (abc)");
    assert (source != null && source.key == "abc" && source.label == "notes (draft)");

    source = Policy.ShareSource.parse ("Window: 0123456789abcdef0123456789abcdef");
    assert (source != null && source.key == "0123456789abcdef0123456789abcdef");
    assert (source.label == source.key);
}

void test_rejected () {
    assert (Policy.ShareSource.parse ("") == null);
    assert (Policy.ShareSource.parse ("Region: 0,0 10x10") == null);
    assert (Policy.ShareSource.parse ("Monitor: ") == null);
    assert (Policy.ShareSource.parse ("Window:abc") == null);
    assert (Policy.ShareSource.parse ("Window: ") == null);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/screen-share/monitor", test_monitor);
    Test.add_func ("/screen-share/window", test_window);
    Test.add_func ("/screen-share/rejected", test_rejected);
    return Test.run ();
}
