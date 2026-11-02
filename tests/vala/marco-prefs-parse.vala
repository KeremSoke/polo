/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The prefs.c and ui.c parsers in lib/marco and the acceptance rules of
 * Polo.MarcoPrefsTracker.
 */

using Polo;
using Polo.Marco;

const ButtonFunction L = ButtonFunction.LAST;

void check_side (ButtonFunction[] got, bool[] spacers, ButtonFunction[] want, bool[]? want_spacers = null) {
    for (int i = 0; i < want.length; i++) {
        assert (got[i] == want[i]);
    }
    assert (got[want.length] == L);
    for (int i = 0; i <= want.length; i++) {
        bool expected = want_spacers != null && i < want_spacers.length && want_spacers[i];
        assert (spacers[i] == expected);
    }
}

void check_layout (string? str, bool rtl, ButtonFunction[] left, ButtonFunction[] right,
                   bool[]? left_spacers = null, bool[]? right_spacers = null) {
    ButtonLayout layout;
    parse_button_layout (str, rtl, out layout);
    check_side (layout.left_buttons, layout.left_buttons_has_spacer, left, left_spacers);
    check_side (layout.right_buttons, layout.right_buttons_has_spacer, right, right_spacers);
}

void test_button_layout () {
    var M = ButtonFunction.MENU;
    var MIN = ButtonFunction.MINIMIZE;
    var MAX = ButtonFunction.MAXIMIZE;
    var C = ButtonFunction.CLOSE;

    check_layout (MarcoPrefs.defaults ().button_layout, false, { M }, { MIN, MAX, C });
    check_layout ("", false, {}, {});
    check_layout (null, false, {}, {});
    check_layout (":", false, {}, {});
    check_layout ("close", false, { C }, {});
    check_layout (":close", false, {}, { C });
    check_layout ("spacer,close:", false, { C }, {});
    check_layout ("menu,spacer,close:", false, { M, C }, {}, { true });
    check_layout ("close,spacer:", false, { C }, {}, { true });
    check_layout ("close,close,minimize:", false, { C, MIN }, {});
    check_layout ("foo,close,MENU:", false, { C }, {});
    check_layout ("shade:", false, { ButtonFunction.SHADE, ButtonFunction.UNSHADE }, {});
    check_layout ("above,stick:", false,
                  { ButtonFunction.ABOVE, ButtonFunction.UNABOVE, ButtonFunction.STICK,
                    ButtonFunction.UNSTICK }, {});
    check_layout ("close:minimize:maximize", false, { C }, {});
    check_layout ("menu,appmenu,minimize,maximize,close,shade,above,stick:", false,
                  { M, ButtonFunction.APPMENU, MIN, MAX, C, ButtonFunction.SHADE,
                    ButtonFunction.UNSHADE, ButtonFunction.ABOVE, ButtonFunction.UNABOVE,
                    ButtonFunction.STICK, ButtonFunction.UNSTICK }, {});
    check_layout ("menu:minimize,maximize,close", true, { C, MAX, MIN }, { M });
    check_layout ("menu,spacer,close:", true, {}, { C, M }, null, { true });

    ButtonLayout a, b;
    parse_button_layout ("menu:close", false, out a);
    parse_button_layout ("menu,foo:close,close", false, out b);
    assert (a.equal (b));
    parse_button_layout ("menu,spacer:close", false, out b);
    assert (!a.equal (b));
}

void check_accel (string? accel, bool ok, string? name = null, uint code = 0,
                  VirtualModifier mods = 0) {
    string? got_name;
    uint got_code;
    VirtualModifier got_mods;
    assert (parse_accelerator (accel, out got_name, out got_code, out got_mods) == ok);
    if (ok) {
        assert (got_name == name);
        assert (got_code == code);
        assert (got_mods == mods);
    }
}

void test_accelerator () {
    check_accel ("disabled", true);
    check_accel ("", true);
    check_accel (null, true);
    check_accel ("<Alt>F10", true, "F10", 0, VirtualModifier.ALT_MASK);
    check_accel ("<Control><Alt>Delete", true, "Delete", 0,
                 VirtualModifier.CONTROL_MASK | VirtualModifier.ALT_MASK);
    check_accel ("<Primary>a", true, "a", 0, VirtualModifier.CONTROL_MASK);
    check_accel ("<ctrl><shft>x", true, "x", 0,
                 VirtualModifier.CONTROL_MASK | VirtualModifier.SHIFT_MASK);
    check_accel ("<CTL><SHIFT>x", true, "x", 0,
                 VirtualModifier.CONTROL_MASK | VirtualModifier.SHIFT_MASK);
    check_accel ("<Mod4>e", true, "e", 0, VirtualModifier.MOD4_MASK);
    check_accel ("<mod2><Mod3><MOD5>k", true, "k", 0,
                 VirtualModifier.MOD2_MASK | VirtualModifier.MOD3_MASK | VirtualModifier.MOD5_MASK);
    check_accel ("<Mod1>Tab", true, "Tab", 0, VirtualModifier.ALT_MASK);
    check_accel ("<Meta><Hyper>z", true, "z", 0,
                 VirtualModifier.META_MASK | VirtualModifier.HYPER_MASK);
    check_accel ("<Super>Up", true, "Up", 0, VirtualModifier.SUPER_MASK);
    check_accel ("<Foo>a", true, ">a", 0, 0);
    check_accel ("Print", true, "Print", 0, 0);
    check_accel ("0x41", true, null, 0x41, 0);
    check_accel ("0x", false);
    check_accel ("<Super>", false);
    check_accel ("<Release>a", false);
    check_accel ("<Alt>0x41", false);
    check_accel ("<Alt", false);
    check_accel ("<Mod6>a", true, ">a", 0, 0);
}

void check_modifier (string? accel, bool ok, VirtualModifier mods = 0) {
    VirtualModifier got;
    assert (parse_modifier (accel, out got) == ok);
    if (ok) {
        assert (got == mods);
    }
}

void test_modifier () {
    check_modifier (MarcoPrefs.defaults ().mouse_button_modifier, true, VirtualModifier.ALT_MASK);
    check_modifier ("<Super>", true, VirtualModifier.SUPER_MASK);
    check_modifier ("<Control><Shift>", true,
                    VirtualModifier.CONTROL_MASK | VirtualModifier.SHIFT_MASK);
    check_modifier ("disabled", true, 0);
    check_modifier ("", true, 0);
    check_modifier (null, true, 0);
    check_modifier ("<Alt>a", false);
    check_modifier ("0x40", false);
    check_modifier ("<Release><Alt>", false);
    check_modifier ("<Foo>", false);
}

void test_font () {
    var prefs = MarcoPrefs.defaults ();
    assert (parse_font (prefs.titlebar_font) != null);
    assert (parse_font ("Sans Bold 10") == "Sans Bold 10");
    assert (parse_font ("sans bold 10") == parse_font ("sans Bold 10"));
    assert (parse_font ("") != null);
    assert (parse_font (null) == null);
}

/* Every default Marco ships must parse. */
void test_defaults () {
    var prefs = MarcoPrefs.defaults ();
    int count = 0;
    foreach (unowned string key in MarcoPrefs.KEYS) {
        if (!key.has_prefix ("window-keybindings/") && !key.has_prefix ("global-keybindings/")) {
            continue;
        }
        string? name;
        uint code;
        VirtualModifier mods;
        unowned string text = prefs.get_value (key).get_string ();
        if (!parse_accelerator (text, out name, out code, out mods)) {
            GLib.error ("default of %s (\"%s\") does not parse", key, text);
        }
        count++;
    }
    assert (count > 100);
}

GLib.HashTable<string, GLib.Variant> changes (string key, GLib.Variant value) {
    var t = new GLib.HashTable<string, GLib.Variant> (GLib.str_hash, GLib.str_equal);
    t[key] = value;
    return t;
}

void test_tracker () {
    var tracker = new MarcoPrefsTracker (false);
    string[] rejected = {};
    tracker.rejected.connect ((key) => { rejected += key; });

    assert (tracker.update (changes ("general/focus-mode", new GLib.Variant.string ("sloppy")))
            == PrefsChange.FOCUS);
    assert (tracker.current.focus_mode == FocusMode.SLOPPY);

    assert (tracker.update (changes ("general/focus-mode", new GLib.Variant.string ("sloppy")))
            == PrefsChange.NONE);

    tracker.update (changes ("general/num-workspaces", new GLib.Variant.int32 (37)));
    tracker.update (changes ("general/num-workspaces", new GLib.Variant.int32 (0)));
    assert (tracker.current.num_workspaces == MarcoPrefs.defaults ().num_workspaces);
    tracker.update (changes ("general/num-workspaces", new GLib.Variant.int32 (36)));
    assert (tracker.current.num_workspaces == 36);
    assert (tracker.update (changes ("general/mouse-button-modifier",
                                     new GLib.Variant.string ("<Alt>a"))) == PrefsChange.NONE);
    assert (tracker.current.mouse_button_modifier == "<Alt>");
    assert (tracker.update (changes ("general/mouse-button-modifier",
                                     new GLib.Variant.string ("<Super>")))
            == PrefsChange.MOUSE_BUTTONS);
    assert (tracker.current.mouse_button_modifier == "<Super>");

    assert (tracker.update (changes ("general/button-layout",
                                     new GLib.Variant.string ("menu,foo:minimize,maximize,close")))
            == PrefsChange.NONE);
    assert (tracker.update (changes ("general/button-layout", new GLib.Variant.string (":close")))
            == PrefsChange.BUTTON_LAYOUT);

    assert (tracker.update (changes ("window-keybindings/close", new GLib.Variant.string ("<Alt>0x41")))
            == PrefsChange.NONE);
    assert (tracker.current.get_keybinding ("window-keybindings", "close") == "<Alt>F4");

    assert (tracker.update (changes ("general/focus-mode", new GLib.Variant.string ("nonsense")))
            == PrefsChange.NONE);
    assert (tracker.current.focus_mode == FocusMode.SLOPPY);
    assert (tracker.update (changes ("general/no-such-key", new GLib.Variant.boolean (true)))
            == PrefsChange.NONE);
    assert (tracker.update (changes ("general/num-workspaces", new GLib.Variant.string ("4")))
            == PrefsChange.NONE);
    assert (rejected.length == 7);

    var both = changes ("general/num-workspaces", new GLib.Variant.int32 (6));
    both["window-keybindings/close"] = new GLib.Variant.string ("<Super>q");
    assert (tracker.update (both) == (PrefsChange.WORKSPACES | PrefsChange.KEYBINDINGS));
    assert (tracker.current.num_workspaces == 6);
    assert (tracker.current.mouse_button_modifier == "<Super>");
    assert (tracker.current.focus_mode == FocusMode.SLOPPY);
}

int main (string[] args) {
    GLib.Test.init (ref args);
    GLib.Test.add_func ("/marco-prefs-parse/button-layout", test_button_layout);
    GLib.Test.add_func ("/marco-prefs-parse/accelerator", test_accelerator);
    GLib.Test.add_func ("/marco-prefs-parse/modifier", test_modifier);
    GLib.Test.add_func ("/marco-prefs-parse/font", test_font);
    GLib.Test.add_func ("/marco-prefs-parse/defaults", test_defaults);
    GLib.Test.add_func ("/marco-prefs-parse/tracker", test_tracker);
    return GLib.Test.run ();
}
