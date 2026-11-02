/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The stack.c port: group transients, layers and the default focus window,
 * which the oracle corpus does not reach.
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

Window map (Policy.Model model, string label, Marco.WindowType? type = null, Window? parent = null,
            uint64 group = 0) {
    var w = new Window (WindowKind.X11, new FakeAdapter ());
    w.label = label;
    w.type_hint = type;
    w.group = group;
    w.frame = Marco.Rect () { x = 100, y = 100, width = 400, height = 300 };
    model.manage (w);
    if (parent != null) {
        model.set_transient_for (w, parent);
    }
    model.map (w);
    return w;
}

string order (Policy.Model model) {
    string[] names = {};
    foreach (var w in model.get_stack ()) {
        if (w.window_type != Marco.WindowType.DOCK) {
            names += w.label;
        }
    }
    return string.joinv (" ", names);
}

void test_group_transient () {
    var model = new_model ();
    var a1 = map (model, "a1", null, null, 7);
    var b = map (model, "b", null, null, 9);
    var a2 = map (model, "a2", null, null, 7);
    var toolbox = map (model, "toolbox", Marco.WindowType.UTILITY, null, 7);
    assert (order (model) == "a1 b a2 toolbox");

    model.raise (a1);
    assert (order (model) == "b a2 a1 toolbox");
    model.raise (b);
    assert (order (model) == "a2 a1 toolbox b");
    assert (model.stack_index (toolbox) > model.stack_index (a1));
    assert (model.stack_index (toolbox) > model.stack_index (a2));

    var lone = map (model, "lone", Marco.WindowType.UTILITY);
    model.raise (b);
    assert (model.stack_index (b) > model.stack_index (lone));
    assert (a1 != null && a2 != null);
}

void test_transient_raise () {
    var model = new_model ();
    var parent = map (model, "parent");
    var first = map (model, "first", null, parent);
    var second = map (model, "second", null, parent);
    var other = map (model, "other");
    assert (order (model) == "parent first second other");

    model.raise (first);
    assert (order (model) == "other parent second first");
    assert (model.stack_index (parent) > model.stack_index (other));
    assert (model.stack_index (first) > model.stack_index (second));
}

void test_layers () {
    var model = new_model ();
    var dock = map (model, "dock", Marco.WindowType.DOCK);
    var normal = map (model, "normal");
    assert (dock.layer == Marco.StackLayer.DOCK);
    assert (normal.layer == Marco.StackLayer.NORMAL);

    model.set_state (dock, WindowState.BELOW, true);
    assert (dock.layer == Marco.StackLayer.BOTTOM);
    assert (model.stack_index (dock) < model.stack_index (normal));
    model.set_state (dock, WindowState.BELOW, false);

    model.focus (normal);
    model.set_state (normal, WindowState.FULLSCREEN, true);
    assert (normal.layer == Marco.StackLayer.FULLSCREEN);
    assert (model.stack_index (normal) > model.stack_index (dock));

    var dialog = map (model, "dialog", null, normal);
    model.focus (dialog);
    assert (normal.layer == Marco.StackLayer.FULLSCREEN);
    assert (dialog.layer == Marco.StackLayer.FULLSCREEN);

    var unrelated = map (model, "unrelated");
    model.focus (unrelated);
    assert (normal.layer == Marco.StackLayer.NORMAL);
    assert (model.stack_index (normal) < model.stack_index (dock));

    model.set_state (unrelated, WindowState.ABOVE, true);
    assert (unrelated.layer == Marco.StackLayer.TOP);
}

void test_default_focus () {
    var model = new_model ();
    var stack = model.get_stack_object ();
    var dock = map (model, "dock", Marco.WindowType.DOCK);
    assert (stack.get_default_focus_window (-1, null) == dock);

    var parent = map (model, "parent", null, null, 3);
    var sibling = map (model, "sibling", null, null, 3);
    var other = map (model, "other");
    var dialog = map (model, "dialog", null, parent, 3);
    assert (stack.get_default_focus_window (-1, null) == dialog);
    assert (stack.get_default_focus_window (-1, dialog) == parent);
    assert (stack.get_default_focus_window (-1, other) == dialog);
    model.raise (other);
    assert (stack.get_default_focus_window (-1, sibling) == dialog);
    assert (stack.get_default_focus_window (-1, null, true, 5, 5) == dock);

    model.minimize (other);
    assert (stack.get_default_focus_window (-1, null) != other);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/stack/group-transient", test_group_transient);
    Test.add_func ("/stack/transient-raise", test_transient_raise);
    Test.add_func ("/stack/layers", test_layers);
    Test.add_func ("/stack/default-focus", test_default_focus);
    return Test.run ();
}
