/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * xkb-settings.vala against libmatekbd's rules: tab-separated layouts and
 * options, the system configuration for each empty key, and the registry
 * filter.
 */

using Polo.Input;

XkbConfig system_config () {
    return XkbConfig.from_rmlvo ("pc105", "de,fr", ",azerty", "caps:escape,compose:ralt");
}

void test_rmlvo () {
    var config = system_config ();
    assert_cmpstr (config.model, CompareOperator.EQ, "pc105");
    assert_cmpstr (config.layout_names (), CompareOperator.EQ, "de,fr");
    assert_cmpstr (config.variant_names (), CompareOperator.EQ, ",azerty");
    assert_cmpstr (config.option_names (), CompareOperator.EQ, "caps:escape,compose:ralt");

    var empty = XkbConfig.from_rmlvo ("", null, null, "");
    assert_null (empty.model);
    assert_null (empty.layout_names ());
    assert_null (empty.variant_names ());
    assert_null (empty.option_names ());

    var short_variants = XkbConfig.from_rmlvo (null, "us,ru,de", "intl", null);
    assert_cmpstr (short_variants.variant_names (), CompareOperator.EQ, "intl,,");
}

void test_system_fills_empty_keys () {
    var config = XkbConfig.from_settings ("", {}, {}, system_config ());
    assert_true (config.equals (system_config ()));

    var reordered = XkbConfig.from_rmlvo ("pc105", "de,fr", ",azerty", "compose:ralt,caps:escape");
    assert_true (reordered.equals (system_config ()));
    assert_false (XkbConfig.from_rmlvo ("pc105", "fr,de", "azerty,", "caps:escape,compose:ralt")
                  .equals (system_config ()));

    config = XkbConfig.from_settings ("pc104", {}, {}, system_config ());
    assert_cmpstr (config.model, CompareOperator.EQ, "pc104");
    assert_cmpstr (config.layout_names (), CompareOperator.EQ, "de,fr");
    assert_cmpstr (config.option_names (), CompareOperator.EQ, "caps:escape,compose:ralt");
}

void test_settings_form () {
    var config = XkbConfig.from_settings ("", { "us", "ru\tphonetic", "fr\t" },
                                          { "grp\tgrp:alt_shift_toggle", "no-tab" }, system_config ());
    assert_cmpstr (config.model, CompareOperator.EQ, "pc105");
    assert_cmpstr (config.layout_names (), CompareOperator.EQ, "us,ru,fr");
    assert_cmpstr (config.variant_names (), CompareOperator.EQ, ",phonetic,");
    assert_cmpstr (config.option_names (), CompareOperator.EQ, "grp:alt_shift_toggle");
}

void test_filter () {
    var config = XkbConfig.from_settings ("", { "us", "nosuchlayout", "ru\tphonetic", "us\tnosuchvariant" },
                                          {}, system_config ());
    assert_true (config.filter ());
    assert_cmpstr (config.layout_names (), CompareOperator.EQ, "us,ru");
    assert_cmpstr (config.variant_names (), CompareOperator.EQ, ",phonetic");
    assert_false (config.filter ());
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/xkb-settings/rmlvo", test_rmlvo);
    Test.add_func ("/xkb-settings/system", test_system_fills_empty_keys);
    Test.add_func ("/xkb-settings/settings", test_settings_form);
    Test.add_func ("/xkb-settings/filter", test_filter);
    return Test.run ();
}
