/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A desktop-type window (Caja's desktop) covers the whole output where it
 * asked to be, whatever the panels reserve, before and after they appear
 * or the screen changes.
 */

using Polo;

class FakeAdapter : GLib.Object, WindowAdapter {
    public void configure (Marco.Rect frame) {}
    public void set_activated (bool activated) {}
    public void set_maximized (bool maximized) {}
    public void set_fullscreen (bool fullscreen) {}
    public void take_focus () {}
    public void close () {}
    public void ping () {}
}

Marco.Rect rect (int x, int y, int width, int height) {
    return Marco.Rect () { x = x, y = y, width = width, height = height };
}

Window dock (Policy.Model model, Marco.Rect area, Marco.Side side) {
    var dock = new Window (WindowKind.X11, new FakeAdapter ());
    dock.type_hint = Marco.WindowType.DOCK;
    dock.accepts_focus = false;
    dock.frame = area;
    dock.struts = { Marco.Strut () { rect = area, side = side, edge = Marco.EdgeType.XINERAMA } };
    model.manage (dock);
    model.map (dock);
    return dock;
}

Window desktop_window (Policy.Model model, Marco.Rect area) {
    var w = new Window (WindowKind.X11, new FakeAdapter ());
    w.type_hint = Marco.WindowType.DESKTOP;
    w.accepts_focus = true;
    w.frame = area;
    model.manage (w);
    model.map (w);
    return w;
}

void expect_covering (Window w, Marco.Rect screen, string when) {
    if (w.frame.x != screen.x || w.frame.y != screen.y ||
        w.frame.width != screen.width || w.frame.height != screen.height) {
        GLib.error ("%s: the desktop window is at %d,%d %dx%d, not %d,%d %dx%d", when, w.frame.x, w.frame.y,
                    w.frame.width, w.frame.height, screen.x, screen.y, screen.width, screen.height);
    }
}

void test_before_panels () {
    var screen = rect (0, 0, 1920, 1200);
    var model = new Policy.Model (MarcoPrefs.defaults ());
    model.add_output (new Policy.Output ("eDP-1", screen));
    var w = desktop_window (model, screen);
    expect_covering (w, screen, "mapped alone");
    dock (model, rect (0, 0, 1920, 27), Marco.Side.TOP);
    expect_covering (w, screen, "top panel appeared");
    dock (model, rect (0, 1175, 1920, 25), Marco.Side.BOTTOM);
    expect_covering (w, screen, "bottom panel appeared");
}

void test_after_panels () {
    var screen = rect (0, 0, 1920, 1200);
    var model = new Policy.Model (MarcoPrefs.defaults ());
    model.add_output (new Policy.Output ("eDP-1", screen));
    dock (model, rect (0, 0, 1920, 27), Marco.Side.TOP);
    dock (model, rect (0, 1175, 1920, 25), Marco.Side.BOTTOM);
    var w = desktop_window (model, screen);
    expect_covering (w, screen, "mapped after the panels");
    model.invalidate_work_areas ();
    expect_covering (w, screen, "work areas recomputed");
}

void test_two_panels_on_top () {
    var screen = rect (0, 0, 1920, 1200);
    var model = new Policy.Model (MarcoPrefs.defaults ());
    model.add_output (new Policy.Output ("eDP-1", screen));
    var w = desktop_window (model, screen);
    dock (model, rect (0, 0, 1920, 27), Marco.Side.TOP);
    dock (model, rect (0, 27, 1920, 25), Marco.Side.TOP);
    expect_covering (w, screen, "two panels on top");
}

void test_layer_shell_panels () {
    var screen = rect (0, 0, 1920, 1200);
    var model = new Policy.Model (MarcoPrefs.defaults ());
    model.add_output (new Policy.Output ("eDP-1", screen));
    var struts = new GLib.SList<Marco.Strut?> ();
    model.layer_struts = () => {
        var copy = new GLib.SList<Marco.Strut?> ();
        foreach (var strut in struts) {
            copy.prepend (strut);
        }
        return copy;
    };
    var w = desktop_window (model, screen);
    expect_covering (w, screen, "mapped before any panel");
    struts.prepend (Marco.Strut () { rect = rect (0, 0, 1920, 27), side = Marco.Side.TOP,
                                     edge = Marco.EdgeType.XINERAMA });
    model.invalidate_work_areas ();
    expect_covering (w, screen, "top panel reserved its zone");
    struts.prepend (Marco.Strut () { rect = rect (0, 1175, 1920, 25), side = Marco.Side.BOTTOM,
                                     edge = Marco.EdgeType.XINERAMA });
    model.invalidate_work_areas ();
    expect_covering (w, screen, "bottom panel reserved its zone");
    model.remove_output (model.get_outputs ()[0]);
    model.add_output (new Policy.Output ("eDP-1", screen));
    model.invalidate_work_areas ();
    expect_covering (w, screen, "the screen came back");
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/desktop-window/before-panels", test_before_panels);
    Test.add_func ("/desktop-window/after-panels", test_after_panels);
    Test.add_func ("/desktop-window/two-panels-on-top", test_two_panels_on_top);
    Test.add_func ("/desktop-window/layer-shell-panels", test_layer_shell_panels);
    return Test.run ();
}
