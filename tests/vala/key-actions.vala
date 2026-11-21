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

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/key-actions/previous-workspace", test_previous_workspace);
    return Test.run ();
}
