/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Button accelerators for the desktop cube's cube-drag key: Marco's
 * modifiers followed by an X button number.
 */

using Polo;
using Polo.Input;

const Marco.VirtualModifier CONTROL = Marco.VirtualModifier.CONTROL_MASK;
const Marco.VirtualModifier ALT = Marco.VirtualModifier.ALT_MASK;
const Marco.VirtualModifier SUPER = Marco.VirtualModifier.SUPER_MASK;
const Marco.VirtualModifier SHIFT = Marco.VirtualModifier.SHIFT_MASK;

void assert_parses (string text, uint32 button, Marco.VirtualModifier mask) {
    ButtonAccelerator accelerator;
    assert (Accelerators.parse_button (text, out accelerator));
    assert (accelerator.button == button);
    assert (accelerator.mask == mask);
}

void assert_refused (string text) {
    ButtonAccelerator accelerator;
    assert (!Accelerators.parse_button (text, out accelerator));
    assert (accelerator.is_disabled ());
}

void test_parse () {
    assert_parses ("<Control><Alt>Button1", Accelerators.BTN_LEFT, CONTROL | ALT);
    assert_parses ("<Primary><Alt>Button1", Accelerators.BTN_LEFT, CONTROL | ALT);
    assert_parses ("<Mod4>Button2", Accelerators.BTN_MIDDLE, SUPER);
    assert_parses ("<Super>Button3", Accelerators.BTN_RIGHT, SUPER);
    assert_parses ("<Shift><Control>button8", Accelerators.BTN_SIDE, SHIFT | CONTROL);
    assert_parses ("<Meta>Button9", Accelerators.BTN_EXTRA, ALT);
}

void test_disabled () {
    foreach (var text in new string[] { "disabled", "" }) {
        ButtonAccelerator accelerator;
        assert (Accelerators.parse_button (text, out accelerator));
        assert (accelerator.is_disabled ());
    }
    ButtonAccelerator none;
    assert (Accelerators.parse_button (null, out none) && none.is_disabled ());
}

void test_refused () {
    assert_refused ("Button1");
    assert_refused ("<Control><Alt>");
    assert_refused ("<Control><Alt>a");
    assert_refused ("<Control><Alt>Button");
    assert_refused ("<Control><Alt>Button4");
    assert_refused ("<Control><Alt>Button7");
    assert_refused ("<Control><Alt>Button0");
    assert_refused ("<Control><Alt>Button-1");
    assert_refused ("<Control><Alt>Button1x");
    assert_refused ("<Bogus>Button1");
    assert_refused ("<Control><Alt>Button1<Shift>");
}

void test_match () {
    ButtonAccelerator drag;
    assert (Accelerators.parse_button ("<Control><Alt>Button1", out drag));
    var control_alt = Backend.Modifiers.CONTROL | Backend.Modifiers.MOD1;
    assert (Accelerators.matches_button (drag, Accelerators.BTN_LEFT, control_alt));
    assert (Accelerators.matches_button (drag, Accelerators.BTN_LEFT,
                                         control_alt | Backend.Modifiers.LOCK | Backend.Modifiers.MOD2));
    assert (!Accelerators.matches_button (drag, Accelerators.BTN_RIGHT, control_alt));
    assert (!Accelerators.matches_button (drag, Accelerators.BTN_LEFT, Backend.Modifiers.CONTROL));
    assert (!Accelerators.matches_button (drag, Accelerators.BTN_LEFT, control_alt | Backend.Modifiers.SHIFT));
    assert (!Accelerators.matches_button (drag, Accelerators.BTN_LEFT, 0));

    ButtonAccelerator super_drag;
    assert (Accelerators.parse_button ("<Super>Button1", out super_drag));
    assert (Accelerators.matches_button (super_drag, Accelerators.BTN_LEFT, Backend.Modifiers.MOD4));

    ButtonAccelerator off;
    assert (Accelerators.parse_button ("disabled", out off));
    assert (!Accelerators.matches_button (off, 0, 0));
    assert (!Accelerators.matches_button (off, Accelerators.BTN_LEFT, control_alt));
}

void test_schema_defaults () {
    GLib.SettingsSchemaSource source;
    try {
        source = new GLib.SettingsSchemaSource.from_directory (GLib.Environment.get_variable ("POLO_SCHEMA_DIR"),
                                                               null, true);
    } catch (GLib.Error e) {
        GLib.error ("cannot read Polo's schemas: %s", e.message);
    }
    var keys = source.lookup ("org.mate.polo.keybindings", false);
    assert (keys != null);
    ButtonAccelerator drag;
    assert (Accelerators.parse_button (keys.get_key ("cube-drag").get_default_value ().get_string (), out drag));
    assert (drag.button == Accelerators.BTN_LEFT && drag.mask == (CONTROL | ALT));
    foreach (var name in new string[] { "cube", "cube-rotate-left", "cube-rotate-right" }) {
        Accelerator accelerator;
        assert (Accelerators.parse (keys.get_key (name).get_default_value ().get_string (), out accelerator));
    }
    Accelerator cube;
    Accelerators.parse (keys.get_key ("cube").get_default_value ().get_string (), out cube);
    assert (cube.keysym == Backend.Keysym.from_name ("c") && cube.mask == SUPER);

    var effects = source.lookup ("org.mate.polo.effects", false);
    assert (effects != null);
    assert (!effects.get_key ("cube").get_default_value ().get_boolean ());
    assert (effects.get_key ("cube-duration-ms").get_default_value ().get_uint32 () == 350);
    assert (effects.get_key ("cube-zoom").get_default_value ().get_double () == 0.1);
    assert (effects.get_key ("cube-speed-zoom").get_default_value ().get_double () == 0.07);
    assert (effects.get_key ("cube-speed-spin-horizontal").get_default_value ().get_double () == 0.02);
    assert (effects.get_key ("cube-speed-spin-vertical").get_default_value ().get_double () == 0.02);
    assert (!effects.get_key ("cube-natural-drag").get_default_value ().get_boolean ());
    foreach (var name in new string[] { "open-animation", "close-animation", "minimize-animation" }) {
        assert (effects.get_key (name).get_default_value ().get_string () == "none");
    }
    assert (effects.get_key ("open-animation").range_check (new GLib.Variant.string ("spin")));
    assert (!effects.get_key ("open-animation").range_check (new GLib.Variant.string ("fire")));
    assert (effects.get_key ("minimize-animation").range_check (new GLib.Variant.string ("squeezimize")));
    assert (effects.get_key ("animation-duration-ms").get_default_value ().get_uint32 () == 400);
    assert (effects.get_key ("zap-duration-ms").get_default_value ().get_uint32 () == 250);
    assert (effects.get_key ("spin-duration-ms").get_default_value ().get_uint32 () == 250);
    assert (effects.get_key ("spin-rotations").get_default_value ().get_int32 () == 1);
    assert (effects.get_key ("squeezimize-duration-ms").get_default_value ().get_uint32 () == 150);
    assert (effects.get_key ("cube-deform").get_default_value ().get_string () == "none");
    assert (effects.get_key ("cube-deform").range_check (new GLib.Variant.string ("star")));
    assert (!effects.get_key ("cube-deform").range_check (new GLib.Variant.string ("sphere")));
    assert (effects.get_key ("cube-light").get_default_value ().get_boolean ());
    assert (effects.get_key ("cube-background").get_default_value ().get_string () == "solid");
    foreach (var mode in new string[] { "solid", "wallpaper", "blurred-wallpaper", "skydome", "cubemap" }) {
        assert (effects.get_key ("cube-background").range_check (new GLib.Variant.string (mode)));
    }
    assert (effects.get_key ("cube-background-color").get_default_value ().get_string () == "#1a1a1a");
    assert (effects.get_key ("cube-skydome-mirror").get_default_value ().get_boolean ());
    assert (effects.get_key ("cube-mesh-spacing").get_default_value ().get_uint32 () == 32);
    assert (!effects.get_key ("cube-mesh-spacing").range_check (new GLib.Variant.uint32 (4)));
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/accelerators/button/parse", test_parse);
    Test.add_func ("/accelerators/button/disabled", test_disabled);
    Test.add_func ("/accelerators/button/refused", test_refused);
    Test.add_func ("/accelerators/button/match", test_match);
    Test.add_func ("/accelerators/schema-defaults", test_schema_defaults);
    return Test.run ();
}
