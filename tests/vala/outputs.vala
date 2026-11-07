/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
using Polo;

Polo.Output monitor (string name, string vendor, string product, string serial,
                     int width, int height, bool internal_panel = false) {
    var o = new Polo.Output ();
    o.name = name;
    o.vendor = vendor;
    o.product = product;
    o.serial = serial;
    o.internal_panel = internal_panel;
    var mode = new OutputMode ();
    mode.width = width;
    mode.height = height;
    mode.rate = 50000;
    mode.preferred = true;
    o.modes.add (mode);
    return o;
}

void parser_test () {
    try {
        string xml;
        FileUtils.get_contents (Environment.get_variable ("POLO_MONITOR_FIXTURE"), out xml);
        var parsed = new Outputs.MonitorsXml (xml);
        var outputs = new GenericArray<Polo.Output> ();
        // Actual X session connector name differs from KMS, but EDID matches.
        outputs.add (monitor ("Virtual-9", "RHT", "0x1234", "0x00000000", 1920, 1080));
        assert (Outputs.Layout.restore (outputs, parsed));
        assert (outputs[0].width == 1920 && outputs[0].primary);
        assert (outputs[0].name == "Virtual-9");
        var roundtrip = new Outputs.MonitorsXml (parsed.serialize ());
        assert (roundtrip.serialize () == parsed.serialize ());
        for (int transform = 0; transform < 8; transform++) {
            parsed.configurations[0].outputs[0].transform = transform;
            parsed.configurations[0].outputs[0].scale = 2;
            roundtrip = new Outputs.MonitorsXml (parsed.serialize ());
            assert (roundtrip.configurations[0].outputs[0].transform == transform);
            assert (roundtrip.configurations[0].outputs[0].scale == 2);
        }
        var reflected = new Outputs.MonitorsXml (xml.replace ("<reflect_y>no", "<reflect_y>yes"));
        assert (reflected.configurations[0].outputs[0].transform == 6);
        outputs[0].serial = "other";
        assert (!Outputs.Layout.restore (outputs, parsed));
        foreach (string bad in new string[] {
            xml.replace ("version=\"1\"", "version=\"2\""),
            xml.replace ("<width>1920", "<width>-1"),
            xml.replace ("<rate>50", "<rate>nan"),
            xml.replace ("<rotation>normal", "<rotation>invalid"),
            xml.replace ("<x>0", "<x>99999999999999999"),
            "<monitors version=\"1\"><configuration>"
        }) {
            bool rejected = false;
            try { new Outputs.MonitorsXml (bad); }
            catch (MarkupError e) { rejected = true; }
            assert (rejected);
        }
    } catch (Error e) {
        error ("%s", e.message);
    }
}

void layout_test () {
    var outputs = new GenericArray<Polo.Output> ();
    var external = monitor ("DP-1", "DEL", "0x1234", "0x00000001", 1920, 1080);
    var laptop = monitor ("eDP-1", "BOE", "0x4321", "0x00000002", 3840, 2160, true);
    laptop.mm_width = 300;
    outputs.add (external);
    outputs.add (laptop);
    Outputs.Layout.arrange (outputs, null, null, 0, true, true);
    assert (outputs[0] == laptop && laptop.primary && laptop.scale == 2);
    assert (external.x == 1920 && external.scale == 1);
    Outputs.Layout.arrange (outputs, null, null, 1, false, true);
    assert (!laptop.enabled && external.primary && external.x == 0);
    Outputs.Layout.arrange (outputs, null, null, 1, false, false);
    assert (laptop.enabled && laptop.primary);
    var saved = new Outputs.MonitorsXml.empty ();
    var config = new Outputs.Configuration ();
    saved.configurations.add (config);
    foreach (var output in outputs) {
        var copy = monitor (output.name, output.vendor, output.product, output.serial, 1920, 1080);
        copy.copy_configuration (output);
        config.outputs.add (copy);
    }
    config.outputs[0].enabled = false;
    config.outputs[1].enabled = true;
    config.outputs[1].primary = true;
    config.outputs[1].transform = 1;
    assert (Outputs.Layout.restore (outputs, saved));
    assert (external.transform == 1 && external.logical_width () == 1080);
    config.outputs[1].width = 9999;
    assert (!Outputs.Layout.restore (outputs, saved));
    config.outputs[1].enabled = false;
    assert (!Outputs.Layout.restore (outputs, saved));
}

void add_mode (Polo.Output output, int width, int height, int rate) {
    var mode = new OutputMode ();
    mode.width = width;
    mode.height = height;
    mode.rate = rate;
    output.modes.add (mode);
}

Polo.Output? named (GenericArray<Polo.Output> outputs, string name) {
    foreach (var output in outputs) {
        if (output.name == name) {
            return output;
        }
    }
    return null;
}

void layout_hotplug_test () {
    var outputs = new GenericArray<Polo.Output> ();
    var laptop = monitor ("eDP-1", "BOE", "0x0001", "1", 1920, 1080, true);
    var tv = monitor ("HDMI-A-1", "SAM", "0x0002", "2", 1280, 720);
    outputs.add (laptop);
    outputs.add (tv);
    Outputs.Layout.arrange (outputs, null, null, 1, true, false);
    assert (laptop.enabled && !tv.enabled);
    var dell = monitor ("DP-1", "DEL", "0x0003", "3", 2560, 1440);
    outputs.add (dell);
    var added = new GenericArray<Polo.Output> ();
    added.add (dell);
    Outputs.Layout.hotplug (outputs, added, null, null, 1);
    assert (laptop.enabled && laptop.primary && laptop.x == 0);
    assert (!tv.enabled);
    assert (dell.enabled && !dell.primary && dell.x == 1920 && dell.width == 2560);

    laptop.enabled = false;
    dell.enabled = false;
    var none = new GenericArray<Polo.Output> ();
    Outputs.Layout.hotplug (outputs, none, null, null, 1);
    assert (outputs[0].enabled && outputs[0].primary);
}

void display_modes_test () {
    var outputs = new GenericArray<Polo.Output> ();
    var external = monitor ("HDMI-A-1", "DEL", "0x0010", "10", 2560, 1440);
    add_mode (external, 1920, 1080, 60000);
    add_mode (external, 1920, 1080, 75000);
    add_mode (external, 1280, 720, 60000);
    var laptop = monitor ("eDP-1", "BOE", "0x0011", "11", 1920, 1200, true);
    add_mode (laptop, 1920, 1080, 60000);
    add_mode (laptop, 1280, 720, 60000);
    outputs.add (external);
    outputs.add (laptop);
    var lone = new GenericArray<Polo.Output> ();
    lone.add (laptop);
    assert (!Outputs.DisplayModes.available (lone, Outputs.DisplayMode.EXTEND));
    assert (Outputs.DisplayModes.primary_of (outputs) == laptop);

    var only = Outputs.DisplayModes.configure (outputs, Outputs.DisplayMode.PRIMARY_ONLY, 1);
    var p = named (only, "eDP-1"), e = named (only, "HDMI-A-1");
    assert (p.enabled && p.primary && p.width == 1920 && p.height == 1200 && p.x == 0);
    assert (!e.enabled);
    assert (Outputs.DisplayModes.current (only) == Outputs.DisplayMode.PRIMARY_ONLY);

    var secondary = Outputs.DisplayModes.configure (outputs, Outputs.DisplayMode.SECONDARY_ONLY, 1);
    p = named (secondary, "eDP-1");
    e = named (secondary, "HDMI-A-1");
    assert (!p.enabled && e.enabled && e.primary && e.width == 2560 && e.x == 0);
    assert (Outputs.DisplayModes.current (secondary) == Outputs.DisplayMode.SECONDARY_ONLY);

    var extended = Outputs.DisplayModes.configure (outputs, Outputs.DisplayMode.EXTEND, 1);
    p = named (extended, "eDP-1");
    e = named (extended, "HDMI-A-1");
    assert (p.enabled && p.primary && p.x == 0 && e.enabled && !e.primary && e.x == 1920 && e.width == 2560);
    assert (Outputs.DisplayModes.current (extended) == Outputs.DisplayMode.EXTEND);

    int width, height;
    assert (Outputs.DisplayModes.common_size (outputs, out width, out height) && width == 1920 && height == 1080);
    var mirrored = Outputs.DisplayModes.configure (outputs, Outputs.DisplayMode.MIRROR, 0);
    p = named (mirrored, "eDP-1");
    e = named (mirrored, "HDMI-A-1");
    assert (p.enabled && e.enabled && p.primary && !e.primary);
    assert (p.width == 1920 && p.height == 1080 && e.width == 1920 && e.height == 1080);
    assert (e.rate == 75000 && p.x == 0 && e.x == 0 && p.scale == 1 && e.scale == 1);
    assert (Outputs.DisplayModes.current (mirrored) == Outputs.DisplayMode.MIRROR);

    var odd = monitor ("DP-2", "ACR", "0x0012", "12", 1366, 768);
    var with_odd = new GenericArray<Polo.Output> ();
    with_odd.add (laptop);
    with_odd.add (odd);
    assert (!Outputs.DisplayModes.common_size (with_odd, out width, out height));
    assert (!Outputs.DisplayModes.available (with_odd, Outputs.DisplayMode.MIRROR));
    assert (Outputs.DisplayModes.configure (with_odd, Outputs.DisplayMode.MIRROR, 1) == null);
    assert (Outputs.DisplayModes.available (with_odd, Outputs.DisplayMode.EXTEND));

    var desktops = new GenericArray<Polo.Output> ();
    var left = monitor ("DP-1", "DEL", "0x0020", "20", 1920, 1080);
    var right = monitor ("DP-2", "DEL", "0x0020", "21", 1920, 1080);
    right.enabled = true;
    right.primary = true;
    desktops.add (left);
    desktops.add (right);
    assert (Outputs.DisplayModes.primary_of (desktops) == right);
    left.enabled = false;
    right.enabled = false;
    assert (Outputs.DisplayModes.primary_of (desktops) == left);
    assert (Outputs.DisplayModes.current (lone) == -1);
}

class FakeAdapter : GLib.Object, WindowAdapter {
    public Marco.Rect configured;
    public void configure (Marco.Rect rect) { configured = rect; }
    public void set_activated (bool value) {}
    public void set_maximized (bool value) {}
    public void set_fullscreen (bool value) {}
    public void take_focus () {}
    public void close () {}
    public void ping () {}
}

void hotplug_test () {
    var model = new Policy.Model (MarcoPrefs.defaults ());
    var left = new Policy.Output ("left", Marco.Rect () { x = 0, y = 0, width = 1000, height = 800 });
    var right = new Policy.Output ("right", Marco.Rect () { x = 1000, y = 0, width = 1200, height = 900 });
    left.primary = true;
    model.add_output (left);
    model.add_output (right);
    var adapter = new FakeAdapter ();
    var window = new Window (WindowKind.XDG, adapter);
    window.frame = Marco.Rect () { x = 0, y = 0, width = 400, height = 300 };
    model.manage (window);
    model.map (window);
    model.move (window, false, 1900, 700);
    assert (window.frame.x == 1800 && window.frame.y == 600);
    model.remove_output (right);
    assert (window.frame.x == 600 && window.frame.y == 500);
    assert (adapter.configured.x == 600 && window.user_rect.x == 600);
    model.set_state (window, WindowState.FULLSCREEN, true);
    assert (window.frame.width == 1000 && window.frame.height == 800);
    model.reconfigure_output (left, Marco.Rect () { x = -800, y = 0, width = 800, height = 600 });
    assert (window.frame.x == -800 && window.frame.width == 800 && window.frame.height == 600);
    model.remove_output (left); // All monitors absent must not crash.
    assert (model.get_outputs ().length == 0);
}

void persistence_test () {
    try {
        string dir = DirUtils.make_tmp ("polo-monitors-XXXXXX");
        string path = Path.build_filename (dir, "mate", "polo", "monitors.xml");
        var saved = new Outputs.MonitorsXml.empty ();
        saved.configurations.add (new Outputs.Configuration ());
        var loop = new MainLoop ();
        saved.save.begin (path, (obj, result) => {
            try {
                saved.save.end (result);
                string data;
                FileUtils.get_contents (path, out data);
                assert (data == saved.serialize ());
            } catch (Error e) { error ("%s", e.message); }
            loop.quit ();
        });
        loop.run ();
        FileUtils.unlink (path);
        DirUtils.remove (Path.get_dirname (path));
        DirUtils.remove (Path.build_filename (dir, "mate"));
        DirUtils.remove (dir);
    } catch (Error e) { error ("%s", e.message); }
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/outputs/parser", parser_test);
    Test.add_func ("/outputs/layout", layout_test);
    Test.add_func ("/outputs/hotplug", hotplug_test);
    Test.add_func ("/outputs/layout-hotplug", layout_hotplug_test);
    Test.add_func ("/outputs/display-modes", display_modes_test);
    Test.add_func ("/outputs/persistence", persistence_test);
    return Test.run ();
}
