/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * interface-mirror.vala against msd-xsettings-manager.c's translations:
 * the first copy, live changes, the toolbar and DPI translations, values
 * GNOME's schemas refuse, and mirror-gtk-settings. GSettings uses the
 * memory backend.
 */

const string MATE = "org.mate.interface";
const string FONTS = "org.mate.font-rendering";
const string MOUSE = "org.mate.peripherals-mouse";
const string GNOME = "org.gnome.desktop.interface";
const string WM = "org.gnome.desktop.wm.preferences";

GLib.Settings? installed (string id) {
    var source = GLib.SettingsSchemaSource.get_default ();
    var schema = source != null ? source.lookup (id, true) : null;
    return schema != null ? new GLib.Settings.full (schema, null, null) : null;
}

bool have_schemas () {
    foreach (unowned string id in new string[] { MATE, FONTS, MOUSE, GNOME, WM,
                                                 Polo.Settings.InterfaceMirror.GENERAL_SCHEMA }) {
        if (installed (id) == null) {
            Test.skip ("%s is not installed".printf (id));
            return false;
        }
    }
    return true;
}

void reset () {
    foreach (unowned string id in new string[] { MATE, FONTS, MOUSE, GNOME, WM,
                                                 Polo.Settings.InterfaceMirror.GENERAL_SCHEMA }) {
        var settings = installed (id);
        foreach (unowned string key in settings.settings_schema.list_keys ()) {
            settings.reset (key);
        }
    }
}

bool until (owned GLib.SourceFunc condition) {
    var context = GLib.MainContext.default ();
    for (int i = 0; i < 200; i++) {
        while (context.pending ()) {
            context.iteration (false);
        }
        if (condition ()) {
            return true;
        }
        GLib.Thread.usleep (5000);
    }
    return false;
}

void test_first_copy () {
    if (!have_schemas ()) {
        return;
    }
    reset ();
    var mate = installed (MATE);
    var fonts = installed (FONTS);
    var gnome = installed (GNOME);
    mate.set_string ("gtk-theme", "Menta");
    mate.set_string ("toolbar-style", "both_horiz");
    mate.set_string ("toolbar-icons-size", "small-toolbar");
    mate.set_string ("gtk-decoration-layout", "menu:minimize,close");
    fonts.set_double ("dpi", 120);
    fonts.set_string ("hinting", "full");
    installed (MOUSE).set_int ("cursor-size", 48);
    var mirror = new Polo.Settings.InterfaceMirror (installed (Polo.Settings.InterfaceMirror.GENERAL_SCHEMA));
    assert_cmpstr (gnome.get_string ("gtk-theme"), CompareOperator.EQ, "Menta");
    assert_cmpstr (gnome.get_string ("toolbar-style"), CompareOperator.EQ, "both-horiz");
    assert_cmpstr (gnome.get_string ("toolbar-icons-size"), CompareOperator.EQ, "small");
    assert_cmpstr (installed (WM).get_string ("button-layout"), CompareOperator.EQ, "menu:minimize,close");
    assert_cmpfloat (gnome.get_double ("text-scaling-factor"), CompareOperator.EQ, 1.25);
    assert_cmpstr (gnome.get_string ("font-hinting"), CompareOperator.EQ, "full");
    assert_cmpint (gnome.get_int ("cursor-size"), CompareOperator.EQ, 48);
    mirror.shutdown ();
}

void test_dpi () {
    if (!have_schemas ()) {
        return;
    }
    reset ();
    var fonts = installed (FONTS);
    var gnome = installed (GNOME);
    gnome.set_double ("text-scaling-factor", 2);
    var mirror = new Polo.Settings.InterfaceMirror (null);
    assert_cmpfloat (gnome.get_double ("text-scaling-factor"), CompareOperator.EQ, 1);
    fonts.set_double ("dpi", 1000);
    assert_true (until (() => gnome.get_double ("text-scaling-factor") == 3));
    fonts.set_double ("dpi", 10);
    assert_true (until (() => gnome.get_double ("text-scaling-factor") == 50.0 / 96));
    mirror.shutdown ();
}

void test_live_changes () {
    if (!have_schemas ()) {
        return;
    }
    reset ();
    var mate = installed (MATE);
    var gnome = installed (GNOME);
    var mirror = new Polo.Settings.InterfaceMirror (null);
    mate.set_string ("icon-theme", "mate");
    assert_true (until (() => gnome.get_string ("icon-theme") == "mate"));
    mate.set_boolean ("gtk-enable-animations", false);
    assert_true (until (() => !gnome.get_boolean ("enable-animations")));
    installed (MOUSE).set_string ("cursor-theme", "mate-black");
    assert_true (until (() => gnome.get_string ("cursor-theme") == "mate-black"));
    mirror.shutdown ();
    mate.set_string ("icon-theme", "after-shutdown");
    until (() => false);
    assert_cmpstr (gnome.get_string ("icon-theme"), CompareOperator.EQ, "mate");
}

void test_refused () {
    if (!have_schemas ()) {
        return;
    }
    reset ();
    var mate = installed (MATE);
    var gnome = installed (GNOME);
    gnome.set_string ("toolbar-style", "text");
    mate.set_string ("toolbar-style", "sideways");
    var mirror = new Polo.Settings.InterfaceMirror (null);
    assert_cmpstr (gnome.get_string ("toolbar-style"), CompareOperator.EQ, "text");
    mirror.shutdown ();
}

void test_opt_out () {
    if (!have_schemas ()) {
        return;
    }
    reset ();
    var general = installed (Polo.Settings.InterfaceMirror.GENERAL_SCHEMA);
    var mate = installed (MATE);
    var gnome = installed (GNOME);
    general.set_boolean (Polo.Settings.InterfaceMirror.ENABLED_KEY, false);
    mate.set_string ("gtk-theme", "Off");
    var mirror = new Polo.Settings.InterfaceMirror (general);
    assert_cmpstr (gnome.get_string ("gtk-theme"), CompareOperator.NE, "Off");
    mate.set_string ("gtk-theme", "StillOff");
    until (() => false);
    assert_cmpstr (gnome.get_string ("gtk-theme"), CompareOperator.NE, "StillOff");
    general.set_boolean (Polo.Settings.InterfaceMirror.ENABLED_KEY, true);
    assert_true (until (() => gnome.get_string ("gtk-theme") == "StillOff"));
    mirror.shutdown ();
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/interface-mirror/first-copy", test_first_copy);
    Test.add_func ("/interface-mirror/dpi", test_dpi);
    Test.add_func ("/interface-mirror/live-changes", test_live_changes);
    Test.add_func ("/interface-mirror/refused", test_refused);
    Test.add_func ("/interface-mirror/opt-out", test_opt_out);
    return Test.run ();
}
