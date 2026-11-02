/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Closing windows: orphaned transients and the per-workspace MRU lists.
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

Policy.Model new_model () {
    var model = new Policy.Model (MarcoPrefs.defaults ());
    model.add_output (new Policy.Output ("eDP-1", Marco.Rect () { width = 1920, height = 1080 }));
    return model;
}

Window map (Policy.Model model, string label, Window? parent = null) {
    var w = new Window (WindowKind.XDG, new FakeAdapter ());
    w.label = label;
    w.mwm_decorated = false;
    w.frame = Marco.Rect () { width = 300, height = 200 };
    model.manage (w);
    if (parent != null) {
        model.set_transient_for (w, parent);
    }
    model.map (w);
    return w;
}

string mru (Policy.Model model, int workspace = -1) {
    string[] names = {};
    foreach (var w in model.get_mru (workspace)) {
        names += w.label;
    }
    return string.joinv (" ", names);
}

void test_orphans () {
    var model = new_model ();
    var root = map (model, "root");
    var parent = map (model, "parent", root);
    var dialog = map (model, "dialog", parent);
    var lone = map (model, "lone");
    var orphan = map (model, "orphan", lone);
    assert (dialog.window_type == Marco.WindowType.DIALOG);
    assert (dialog.has_state (WindowState.SKIP_TASKBAR));

    model.unmanage (parent);
    assert (dialog.transient_for == root);
    assert (dialog.window_type == Marco.WindowType.DIALOG);
    assert (model.stack_index (dialog) > model.stack_index (root));

    model.unmanage (lone);
    assert (orphan.transient_for == null);
    assert (orphan.window_type == Marco.WindowType.NORMAL);
    assert (!orphan.has_state (WindowState.SKIP_TASKBAR));
}

void test_mru () {
    var model = new_model ();
    var a = map (model, "a");
    var b = map (model, "b");
    var c = map (model, "c");
    assert (mru (model) == "c b a");

    model.focus (a);
    assert (mru (model) == "a c b");

    model.move_to_workspace (c, 1);
    assert (mru (model) == "a b");
    assert (mru (model, 1) == "c");

    model.set_state (b, WindowState.STICKY, true);
    assert (mru (model, 1) == "b c");
    model.mru_to_back (a);
    assert (mru (model) == "b a");

    model.switch_workspace (1);
    model.set_state (b, WindowState.STICKY, false);
    assert (b.workspace == 1);
    assert (mru (model, 0) == "a");
    assert (mru (model, 1) == "b c");

    model.unmanage (b);
    assert (mru (model, 1) == "c");
    model.unmanage (c);
    assert (mru (model, 1) == "");
    assert (mru (model, 0) == "a");
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/unmanage/orphans", test_orphans);
    Test.add_func ("/unmanage/mru", test_mru);
    return Test.run ();
}
