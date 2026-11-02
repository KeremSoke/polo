/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The workspace.c and screen.c port: the live workspace count, names, sticky
 * windows and their home workspace, transients following their parents, and
 * meta_workspace_activate_with_focus ().
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

MarcoPrefs prefs_with (string key, GLib.Variant value) {
    var overrides = new GLib.HashTable<string, GLib.Variant> (GLib.str_hash, GLib.str_equal);
    overrides.insert (key, value);
    try {
        return MarcoPrefs.with_overrides (overrides);
    } catch (PrefsError e) {
        GLib.error ("%s", e.message);
    }
}

Policy.Model new_model (int workspaces = 4) {
    var model = new Policy.Model (prefs_with ("general/num-workspaces", new GLib.Variant.int32 (workspaces)));
    model.add_output (new Policy.Output ("eDP-1", Marco.Rect () { width = 1920, height = 1080 }));
    return model;
}

Window map (Policy.Model model, string label, Window? parent = null) {
    var w = new Window (WindowKind.X11, new FakeAdapter ());
    w.label = label;
    w.frame = Marco.Rect () { x = 100, y = 100, width = 400, height = 300 };
    model.manage (w);
    if (parent != null) {
        model.set_transient_for (w, parent);
    }
    model.map (w);
    return w;
}

string mru (Policy.Model model, int workspace) {
    string[] labels = {};
    foreach (var w in model.get_mru (workspace)) {
        labels += w.label;
    }
    return string.joinv (" ", labels);
}

void set_count (Policy.Model model, int count) {
    model.prefs = prefs_with ("general/num-workspaces", new GLib.Variant.int32 (count));
}

void test_shrink () {
    var model = new_model ();
    var keep = map (model, "keep");
    var a = map (model, "a");
    var b = map (model, "b");
    model.move_to_workspace (a, 3);
    model.move_to_workspace (b, 2);
    model.move_to_workspace (keep, 1);
    model.switch_workspace (3);

    set_count (model, 2);
    assert (model.n_workspaces == 2);
    assert (model.current_workspace == 1);
    assert (a.workspace == 1 && b.workspace == 1 && keep.workspace == 1);
    assert (mru (model, 1) == "b a keep");
    assert (model.is_showing (a));

    set_count (model, 5);
    assert (model.n_workspaces == 5 && model.current_workspace == 1);
    assert (mru (model, 4) == "");
}

void test_shrink_order () {
    var model = new_model ();
    var x = map (model, "x");
    var y = map (model, "y");
    var z = map (model, "z");
    model.move_to_workspace (y, 3);
    model.move_to_workspace (x, 3);
    model.move_to_workspace (z, 3);
    set_count (model, 2);
    assert (mru (model, 1) == "y x z");
}

void test_growing_keeps_sticky () {
    var model = new_model (2);
    var sticky = map (model, "sticky");
    model.set_state (sticky, WindowState.STICKY, true);
    set_count (model, 3);
    assert (mru (model, 2) == "sticky");
}

void test_sticky_home () {
    var model = new_model ();
    var w = map (model, "w");
    map (model, "other");
    model.set_state (w, WindowState.STICKY, true);
    assert (w.workspace == -1 && w.home_workspace == 0);
    assert (mru (model, 2) == "w");

    /* Unsticking on the home workspace keeps the MRU position. */
    assert (mru (model, 0) == "other w");
    model.set_state (w, WindowState.STICKY, false);
    assert (w.workspace == 0 && mru (model, 0) == "other w" && mru (model, 2) == "");

    /* Elsewhere it goes to the active workspace. */
    model.set_state (w, WindowState.STICKY, true);
    model.switch_workspace (2);
    model.set_state (w, WindowState.STICKY, false);
    assert (w.workspace == 2 && w.home_workspace == 2);
    assert (mru (model, 2) == "w" && mru (model, 0) == "other");
}

void test_sticky_home_relocated () {
    var model = new_model ();
    var w = map (model, "w");
    model.move_to_workspace (w, 3);
    model.set_state (w, WindowState.STICKY, true);
    set_count (model, 2);
    assert (w.workspace == -1 && w.home_workspace == 1);
    assert (mru (model, 0) == "w" && mru (model, 1) == "w");
}

void test_transients () {
    var model = new_model ();
    var parent = map (model, "parent");
    var dialog = map (model, "dialog", parent);
    model.move_to_workspace (parent, 2);
    assert (dialog.workspace == 2);
    model.move_to_workspace (dialog, 3);
    assert (parent.workspace == 3);

    model.set_state (parent, WindowState.STICKY, true);
    assert (dialog.workspace == -1);
    model.set_state (parent, WindowState.STICKY, false);
    assert (dialog.workspace == 0 && parent.workspace == 0);

    model.move_to_workspace (parent, 1);
    var child = map (model, "child", parent);
    assert (child.workspace == 1);
    model.set_state (parent, WindowState.STICKY, true);
    var sticky_child = map (model, "sticky-child", parent);
    assert (sticky_child.workspace == -1 && sticky_child.home_workspace == 1);
}

void test_activate () {
    var model = new_model ();
    var a = map (model, "a");
    var b = map (model, "b");
    model.move_to_workspace (b, 1);
    var c = map (model, "c");
    model.move_to_workspace (c, 1);

    model.switch_workspace (1, b);
    assert (model.focused == b && model.previous_workspace == 0);
    assert (model.stack_index (b) > model.stack_index (c));

    model.moving_window = b;
    model.switch_workspace (2);
    assert (b.workspace == 2 && model.current_workspace == 2 && model.previous_workspace == 1);
    model.moving_window = null;

    model.set_state (b, WindowState.STICKY, true);
    model.moving_window = b;
    model.switch_workspace (0);
    assert (b.workspace == -1);
    model.moving_window = null;
    assert (a != null);
}

void test_names_and_layout () {
    var model = new Policy.Model (prefs_with ("workspace-names/name-2", new GLib.Variant.string ("Mail")));
    assert (model.workspace_name (1) == "Mail");
    assert (model.workspace_name (0).contains ("1"));

    var grid = new_model (6);
    assert (grid.workspace_neighbor (Policy.Motion.DOWN) == 0);
    grid.set_workspace_layout (0, 2, false, Policy.Corner.TOPLEFT);
    assert (grid.workspace_layout ().rows == 2 && grid.workspace_layout ().cols == 3);
    assert (grid.workspace_neighbor (Policy.Motion.DOWN) == 3);
    Test.expect_message ("Polo", GLib.LogLevelFlags.LEVEL_WARNING, "*makes no sense*");
    grid.set_workspace_layout (0, 0, false, Policy.Corner.TOPLEFT);
    Test.assert_expected_messages ();
    assert (grid.workspace_layout ().rows == 2);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/workspaces/shrink", test_shrink);
    Test.add_func ("/workspaces/shrink-order", test_shrink_order);
    Test.add_func ("/workspaces/grow", test_growing_keeps_sticky);
    Test.add_func ("/workspaces/sticky-home", test_sticky_home);
    Test.add_func ("/workspaces/sticky-relocated", test_sticky_home_relocated);
    Test.add_func ("/workspaces/transients", test_transients);
    Test.add_func ("/workspaces/activate", test_activate);
    Test.add_func ("/workspaces/names-layout", test_names_and_layout);
    return Test.run ();
}
