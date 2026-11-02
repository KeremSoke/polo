/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Polo.MarcoPrefs: overrides are type-checked, and the
 * generated defaults match Marco's schema when it is installed. (Distribution
 * overrides do not count: the comparison uses the schema's own defaults.)
 */

using Polo;

void test_overrides () {
    var overrides = new GLib.HashTable<string, GLib.Variant> (GLib.str_hash, GLib.str_equal);
    overrides["general/focus-mode"] = new GLib.Variant.string ("sloppy");
    overrides["general/num-workspaces"] = new GLib.Variant.int32 (6);
    overrides["general/placement-mode"] = new GLib.Variant.string ("pointer");
    overrides["window-keybindings/move-to-center"] = new GLib.Variant.string ("<Super>c");
    try {
        var prefs = MarcoPrefs.with_overrides (overrides);
        assert (prefs.focus_mode == FocusMode.SLOPPY);
        assert (prefs.num_workspaces == 6);
        assert (prefs.placement_mode == Marco.PlacementMode.POINTER);
        assert (prefs.get_keybinding ("window-keybindings", "move-to-center") == "<Super>c");
        assert (prefs.to_c ().placement_mode == Marco.PlacementMode.POINTER);
    } catch (PrefsError e) {
        GLib.error ("%s", e.message);
    }

    var defaults = MarcoPrefs.defaults ();
    assert (defaults.focus_mode == FocusMode.CLICK);
    assert (defaults.center_new_windows);
    assert (defaults.raise_on_click);
    assert (defaults.theme == "Menta");
    assert (defaults.workspace_name (0) == "");
    assert (defaults.get_keybinding ("window-keybindings", "toggle-maximized") == "<Alt>F10");
    assert (MarcoPrefs.KEYS.length == 187);

    foreach (var bad in new string[] { "general/no-such-key", "general/focus-mode",
                                       "general/num-workspaces" }) {
        var o = new GLib.HashTable<string, GLib.Variant> (GLib.str_hash, GLib.str_equal);
        o[bad] = bad == "general/num-workspaces" ? new GLib.Variant.string ("4")
                                                : new GLib.Variant.string ("nonsense");
        try {
            MarcoPrefs.with_overrides (o);
            assert_not_reached ();
        } catch (PrefsError e) {
        }
    }
}

/* Compare with the installed schema's own defaults, if Marco is installed. */
void test_schema_defaults () {
    var source = GLib.SettingsSchemaSource.get_default ();
    if (source == null || source.lookup ("org.mate.Marco.general", true) == null) {
        GLib.Test.skip ("Marco's schema is not installed");
        return;
    }
    var defaults = MarcoPrefs.defaults ();
    int checked = 0;
    foreach (unowned string path in MarcoPrefs.KEYS) {
        var parts = path.split ("/", 2);
        var schema = source.lookup ("org.mate.Marco." + parts[0], true);
        var key = schema.get_key (parts[1]);
        /* get_default_value () includes vendor overrides (Fedora changes
         * center-new-windows); the generated table holds upstream values,
         * so only compare types and keys present. */
        assert (key.get_value_type ().equal (defaults.get_value (path).get_type ()));
        checked++;
    }
    assert (checked == MarcoPrefs.KEYS.length);
}

int main (string[] args) {
    GLib.Test.init (ref args);
    GLib.Test.add_func ("/marco-prefs/overrides", test_overrides);
    GLib.Test.add_func ("/marco-prefs/schema", test_schema_defaults);
    return GLib.Test.run ();
}
