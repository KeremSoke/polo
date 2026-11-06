/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

using Polo;

Policy.Model make_model () {
    var model = new Policy.Model (MarcoPrefs.defaults ());
    model.add_output (new Policy.Output ("left", Marco.Rect () {
        x = 0, y = 0, width = 1000, height = 800
    }));
    model.add_output (new Policy.Output ("right", Marco.Rect () {
        x = 1000, y = 0, width = 1000, height = 800
    }));
    return model;
}

void test_previous_workspace () {
    var model = make_model ();
    model.switch_workspace (2);
    assert (model.previous_workspace == 0);
    model.switch_workspace (1);
    assert (model.previous_workspace == 2);
}

class FakeAdapter : GLib.Object, WindowAdapter {
    public void configure (Marco.Rect frame) {}
    public void set_activated (bool activated) {}
    public void set_maximized (bool maximized) {}
    public void set_fullscreen (bool fullscreen) {}
    public void take_focus () {}
    public void close () {}
    public void ping () {}
}

void test_screenshot_commands () {
    var window = new Window (WindowKind.XDG, new FakeAdapter ());
    window.frame = Marco.Rect () { x = 10, y = 20, width = 300, height = 200 };

    assert (Policy.Screenshot.command_for ("mate-screenshot", null) == "polo-screenshot");
    assert (Policy.Screenshot.command_for ("mate-screenshot --window", window) ==
            "polo-screenshot --geometry='10,20 300x200'");
    assert (Policy.Screenshot.command_for ("mate-screenshot -w", null) == "polo-screenshot");
    assert (Policy.Screenshot.command_for ("mate-screenshot --area", null) == "polo-screenshot --area");
    assert (Policy.Screenshot.command_for ("mate-screenshot --delay=3", null) == "mate-screenshot --delay=3");
    assert (Policy.Screenshot.command_for ("flameshot gui", window) == "flameshot gui");
    assert (Policy.Screenshot.command_for ("'unterminated", window) == "'unterminated");
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/key-actions/previous-workspace", test_previous_workspace);
    Test.add_func ("/key-actions/screenshot-commands", test_screenshot_commands);
    return Test.run ();
}
