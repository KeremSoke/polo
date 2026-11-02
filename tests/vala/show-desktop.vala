/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's show desktop: windows hide without being minimized, per workspace;
 * activating or mapping one minimizes the rest and leaves the mode; desktops,
 * docks, their transients and the skip list stay.
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

Policy.Model new_model (string skip = "") {
    var overrides = new GLib.HashTable<string, GLib.Variant> (GLib.str_hash, GLib.str_equal);
    overrides.insert ("general/show-desktop-skip-list", new GLib.Variant.string (skip));
    MarcoPrefs prefs;
    try {
        prefs = MarcoPrefs.with_overrides (overrides);
    } catch (PrefsError e) {
        GLib.error ("%s", e.message);
    }
    var model = new Policy.Model (prefs);
    model.add_output (new Policy.Output ("eDP-1", Marco.Rect () { width = 1920, height = 1080 }));
    return model;
}

Window map (Policy.Model model, string label, Marco.WindowType? type = null, Window? parent = null) {
    var w = new Window (WindowKind.X11, new FakeAdapter ());
    w.label = label;
    w.app_id = label;
    w.type_hint = type;
    w.frame = Marco.Rect () { x = 100, y = 100, width = 400, height = 300 };
    model.manage (w);
    if (parent != null) {
        model.set_transient_for (w, parent);
    }
    model.map (w);
    return w;
}

void test_hide_and_return () {
    var model = new_model ();
    var a = map (model, "a");
    var b = map (model, "b");
    model.minimize (b);
    model.show_desktop ();
    assert (model.showing_desktop);
    assert (!model.is_showing (a) && !a.has_state (WindowState.MINIMIZED));
    assert (model.focused == null);
    model.unshow_desktop ();
    assert (model.is_showing (a));
    assert (!model.is_showing (b) && b.has_state (WindowState.MINIMIZED));
}

void test_activation_leaves () {
    var model = new_model ();
    var a = map (model, "a");
    var b = map (model, "b");
    model.show_desktop ();
    model.activate (a);
    assert (!model.showing_desktop);
    assert (model.is_showing (a) && model.focused == a);
    assert (b.has_state (WindowState.MINIMIZED));
}

void test_new_window_leaves () {
    var model = new_model ();
    var a = map (model, "a");
    model.show_desktop ();
    var c = map (model, "c");
    assert (!model.showing_desktop);
    assert (a.has_state (WindowState.MINIMIZED) && model.is_showing (c));
}

void test_desktop_components_stay () {
    var model = new_model ("Keep");
    var desktop = map (model, "desktop", Marco.WindowType.DESKTOP);
    var dock = map (model, "dock", Marco.WindowType.DOCK);
    var keep = map (model, "keep");
    var a = map (model, "a");
    model.show_desktop ();
    assert (model.is_showing (desktop) && model.is_showing (dock) && model.is_showing (keep));
    assert (!model.is_showing (a));
    assert (model.focused == desktop);

    var dialog = map (model, "dialog", Marco.WindowType.DIALOG, desktop);
    assert (model.showing_desktop && model.is_showing (dialog));
    assert (!a.has_state (WindowState.MINIMIZED));
}

void test_per_workspace () {
    var model = new_model ();
    var a = map (model, "a");
    model.show_desktop ();
    model.switch_workspace (1);
    assert (!model.showing_desktop);
    var b = map (model, "b");
    assert (model.is_showing (b));
    model.switch_workspace (0);
    assert (model.showing_desktop && !model.is_showing (a));
    assert (!a.has_state (WindowState.MINIMIZED));
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/show-desktop/hide-and-return", test_hide_and_return);
    Test.add_func ("/show-desktop/activation", test_activation_leaves);
    Test.add_func ("/show-desktop/new-window", test_new_window_leaves);
    Test.add_func ("/show-desktop/components", test_desktop_components_stay);
    Test.add_func ("/show-desktop/per-workspace", test_per_workspace);
    return Test.run ();
}
