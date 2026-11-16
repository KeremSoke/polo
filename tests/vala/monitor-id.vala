/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
using Polo;
using Polo.Outputs;

Polo.Output head (string connector, string make, string model, string serial = "", string serial_text = "",
                  int x = 0) {
    var o = new Polo.Output ();
    o.name = connector;
    o.make = make;
    o.model = model;
    o.serial = serial;
    o.serial_text = serial_text;
    o.x = x;
    return o;
}

string id_of (Polo.Output o, Polo.Output[] connected) {
    var list = new GLib.GenericArray<Polo.Output> ();
    foreach (var c in connected) {
        list.add (c);
    }
    return MonitorId.for_output (o, list);
}

void test_unique () {
    var dell = head ("HDMI-A-1", "Dell Inc.", "DELL SE2722H", "0x4a4c3e31");
    var panel = head ("eDP-1", "BOE", "0x0BCA");
    assert (id_of (dell, { dell, panel }) == "Dell_Inc.-DELL_SE2722H");
    assert (id_of (panel, { dell, panel }) == "BOE-0x0BCA");
}

void test_empty_parts () {
    var no_model = head ("DP-1", "Acer", "");
    var no_make = head ("DP-2", "", "X27");
    var nothing = head ("DP-3", "", "");
    assert (id_of (no_model, { no_model }) == "Acer");
    assert (id_of (no_make, { no_make }) == "X27");
    assert (id_of (nothing, { nothing }) == "monitor");
}

void test_sanitize () {
    var odd = head ("DP-1", "Ünï Corp/Ltd", "27\" 4K+ (v2)");
    assert (id_of (odd, { odd }) == "__n___Corp_Ltd-27__4K___v2_");
    assert (MonitorId.sanitize ("a-b_c.d") == "a-b_c.d");
}

void test_twins_by_serial () {
    var left = head ("DP-1", "Dell Inc.", "U2720Q", "0x00000101");
    var right = head ("DP-2", "Dell Inc.", "U2720Q", "0x00000202");
    assert (id_of (left, { left, right }) == "Dell_Inc.-U2720Q-s00000101");
    assert (id_of (right, { left, right }) == "Dell_Inc.-U2720Q-s00000202");
}

void test_twins_by_descriptor_serial () {
    var a = head ("DP-1", "LG", "27UL850", "0x00000000", "905NTAB1");
    var b = head ("DP-2", "LG", "27UL850", "0x00000000", "905NTAB2");
    assert (id_of (a, { a, b }) == "LG-27UL850-s905NTAB1");
    assert (id_of (b, { a, b }) == "LG-27UL850-s905NTAB2");
}

void test_twins_without_serials () {
    var a = head ("DP-1", "AOC", "24B2W", "0x00000000");
    var b = head ("HDMI-A-1", "AOC", "24B2W", "");
    assert (id_of (a, { a, b }) == "AOC-24B2W@DP-1");
    assert (id_of (b, { a, b }) == "AOC-24B2W@HDMI-A-1");
}

void test_three_of_a_kind () {
    var a = head ("DP-1", "HP", "E24", "0x00000001");
    var b = head ("DP-2", "HP", "E24", "0x00000001");
    var c = head ("DP-3", "HP", "E24", "0x00000003");
    Polo.Output[] all = { a, b, c };
    assert (id_of (a, all) == "HP-E24@DP-1");
    assert (id_of (b, all) == "HP-E24@DP-2");
    assert (id_of (c, all) == "HP-E24-s00000003");
}

void test_position_does_not_matter () {
    var left = head ("DP-1", "Dell Inc.", "U2720Q", "0x00000101", "", 0);
    var right = head ("DP-2", "Dell Inc.", "U2720Q", "0x00000202", "", 3840);
    string before_left = id_of (left, { left, right });
    string before_right = id_of (right, { left, right });
    left.x = 3840;
    right.x = 0;
    assert (id_of (left, { right, left }) == before_left);
    assert (id_of (right, { right, left }) == before_right);
}

void test_twin_arrives () {
    var first = head ("DP-1", "Dell Inc.", "U2720Q", "0x00000101");
    assert (id_of (first, { first }) == "Dell_Inc.-U2720Q");
    var second = head ("DP-2", "Dell Inc.", "U2720Q", "0x00000202");
    assert (id_of (first, { first, second }) == "Dell_Inc.-U2720Q-s00000101");
}

int main (string[] args) {
    GLib.Test.init (ref args);
    GLib.Test.add_func ("/monitor-id/unique", test_unique);
    GLib.Test.add_func ("/monitor-id/empty-parts", test_empty_parts);
    GLib.Test.add_func ("/monitor-id/sanitize", test_sanitize);
    GLib.Test.add_func ("/monitor-id/twins-by-serial", test_twins_by_serial);
    GLib.Test.add_func ("/monitor-id/twins-by-descriptor-serial", test_twins_by_descriptor_serial);
    GLib.Test.add_func ("/monitor-id/twins-without-serials", test_twins_without_serials);
    GLib.Test.add_func ("/monitor-id/three-of-a-kind", test_three_of_a_kind);
    GLib.Test.add_func ("/monitor-id/position-does-not-matter", test_position_does_not_matter);
    GLib.Test.add_func ("/monitor-id/twin-arrives", test_twin_arrives);
    return GLib.Test.run ();
}
