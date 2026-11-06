/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * media-keys.vala against msd's acme.h and gpm-button.c: hard-coded and
 * configured keys, modifiers, live changes, disabled keys, the keys that
 * work while locked, volume repeat, do_sound_action ()'s steps and the
 * volume icons; mpris.vala's player names and methods. The media-keys
 * schema is a fixture, on the memory backend.
 */

using Polo;
using Polo.Input;

class Recorder : GLib.Object, MediaKeyTarget {
    public GLib.GenericArray<MediaKey?> runs = new GLib.GenericArray<MediaKey?> ();

    public void run (MediaKey key) {
        runs.add (key);
    }
}

GLib.Settings fixture () {
    try {
        var dir = GLib.Environment.get_variable ("POLO_MEDIA_KEYS_SCHEMAS");
        var source = new GLib.SettingsSchemaSource.from_directory (dir, null, false);
        return new GLib.Settings.full (source.lookup (MediaKeys.SCHEMA, false), null, null);
    } catch (GLib.Error e) {
        GLib.error ("fixture: %s", e.message);
    }
}

Backend.KeyEvent press (string keysym, Backend.Modifiers mods = 0, uint32 keycode = 100) {
    uint32 sym = Backend.Keysym.from_name (keysym);
    assert_true (sym != 0);
    return Backend.KeyEvent () { keycode = keycode, keysym = sym, base_keysym = sym, modifiers = mods, pressed = true };
}

Backend.KeyEvent release (Backend.KeyEvent event) {
    var released = event;
    released.pressed = false;
    return released;
}

void expect (MediaKeys keys, Backend.KeyEvent event, MediaKey? wanted) {
    MediaKey? found = keys.match (event);
    if (wanted == null) {
        assert_true (found == null);
    } else {
        assert_true (found != null);
        assert_cmpint ((int) found, CompareOperator.EQ, (int) wanted);
    }
}

void test_table () {
    var settings = fixture ();
    var keys = new MediaKeys (settings, new Recorder ());
    expect (keys, press ("XF86AudioRaiseVolume"), MediaKey.VOLUME_UP);
    expect (keys, press ("XF86AudioRaiseVolume", Backend.Modifiers.MOD1), MediaKey.VOLUME_UP_QUIET);
    expect (keys, press ("XF86AudioMute", Backend.Modifiers.MOD1), MediaKey.MUTE_QUIET);
    expect (keys, press ("l", Backend.Modifiers.CONTROL | Backend.Modifiers.MOD1), MediaKey.SCREENSAVER);
    expect (keys, press ("l", Backend.Modifiers.CONTROL | Backend.Modifiers.MOD1 | Backend.Modifiers.LOCK),
            MediaKey.SCREENSAVER);
    expect (keys, press ("l", Backend.Modifiers.CONTROL), null);
    expect (keys, press ("XF86MonBrightnessUp"), MediaKey.BRIGHTNESS_UP);
    expect (keys, press ("XF86KbdLightOnOff"), MediaKey.KBD_BRIGHTNESS_TOGGLE);
    expect (keys, press ("XF86AudioRewind"), MediaKey.REWIND);
    expect (keys, press ("XF86Sleep"), MediaKey.SUSPEND_BUTTON);
    expect (keys, press ("XF86Suspend"), MediaKey.HIBERNATE_BUTTON);
    expect (keys, press ("XF86PowerOff"), MediaKey.POWER_BUTTON);
    expect (keys, press ("XF86Calculator"), null);
    keys.shutdown ();
}

void test_changes () {
    var settings = fixture ();
    var keys = new MediaKeys (settings, new Recorder ());
    settings.set_string ("play", "<Control>F9");
    expect (keys, press ("XF86AudioPlay"), null);
    expect (keys, press ("F9", Backend.Modifiers.CONTROL), MediaKey.PLAY);
    settings.set_string ("play", "disabled");
    expect (keys, press ("F9", Backend.Modifiers.CONTROL), null);
    settings.set_string ("help", "F1");
    expect (keys, press ("F1"), MediaKey.HELP);
    settings.reset ("play");
    settings.reset ("help");
    expect (keys, press ("XF86AudioPlay"), MediaKey.PLAY);
    keys.shutdown ();
}

void test_locked () {
    var recorder = new Recorder ();
    var keys = new MediaKeys (fixture (), recorder);
    var lock_key = press ("l", Backend.Modifiers.CONTROL | Backend.Modifiers.MOD1);
    assert_true (keys.filter (lock_key, true) == Backend.FilterResult.PASS);
    assert_cmpuint (recorder.runs.length, CompareOperator.EQ, 0);
    assert_true (keys.filter (press ("XF86AudioMute"), true) == Backend.FilterResult.CONSUME);
    assert_true (keys.filter (press ("XF86AudioPlay"), true) == Backend.FilterResult.CONSUME);
    assert_true (keys.filter (press ("XF86MonBrightnessDown"), true) == Backend.FilterResult.CONSUME);
    assert_true (keys.filter (press ("XF86PowerOff"), true) == Backend.FilterResult.PASS);
    assert_true (keys.filter (lock_key, false) == Backend.FilterResult.CONSUME);
    assert_cmpuint (recorder.runs.length, CompareOperator.EQ, 4);
    assert_true (recorder.runs[3] == MediaKey.SCREENSAVER);
    assert_true (keys.filter (release (lock_key), false) == Backend.FilterResult.PASS);
    keys.shutdown ();
}

void test_repeat () {
    var recorder = new Recorder ();
    var keys = new MediaKeys (fixture (), recorder);
    var up = press ("XF86AudioRaiseVolume", 0, 115);
    keys.filter (up, false);
    var context = GLib.MainContext.default ();
    int64 end = GLib.get_monotonic_time () + 900 * 1000;
    while (GLib.get_monotonic_time () < end) {
        context.iteration (false);
        GLib.Thread.usleep (2000);
    }
    uint held = recorder.runs.length;
    assert_cmpuint (held, CompareOperator.GT, 2);
    keys.filter (release (up), false);
    end = GLib.get_monotonic_time () + 300 * 1000;
    while (GLib.get_monotonic_time () < end) {
        context.iteration (false);
        GLib.Thread.usleep (2000);
    }
    assert_cmpuint (recorder.runs.length, CompareOperator.EQ, held);

    recorder.runs.length = 0;
    var mute = press ("XF86AudioMute", 0, 113);
    keys.filter (mute, false);
    end = GLib.get_monotonic_time () + 700 * 1000;
    while (GLib.get_monotonic_time () < end) {
        context.iteration (false);
        GLib.Thread.usleep (2000);
    }
    assert_cmpuint (recorder.runs.length, CompareOperator.EQ, 1);
    keys.shutdown ();
}

void test_sound_step () {
    uint32 normal = 65536;
    uint32 step = normal * 6 / 100;
    uint32 volume;
    bool muted;
    MediaKeys.sound_step (MediaKey.VOLUME_UP, 1000, false, 0, normal, 6, out volume, out muted);
    assert_cmpuint (volume, CompareOperator.EQ, 1000 + step);
    assert_false (muted);
    MediaKeys.sound_step (MediaKey.VOLUME_UP, normal - 10, false, 0, normal, 6, out volume, out muted);
    assert_cmpuint (volume, CompareOperator.EQ, normal);
    MediaKeys.sound_step (MediaKey.VOLUME_UP, 5000, true, 0, normal, 6, out volume, out muted);
    assert_cmpuint (volume, CompareOperator.EQ, 5000);
    assert_false (muted);
    MediaKeys.sound_step (MediaKey.VOLUME_UP, 0, true, 0, normal, 6, out volume, out muted);
    assert_cmpuint (volume, CompareOperator.EQ, step);
    MediaKeys.sound_step (MediaKey.VOLUME_DOWN, step, false, 0, normal, 6, out volume, out muted);
    assert_cmpuint (volume, CompareOperator.EQ, 0);
    assert_true (muted);
    MediaKeys.sound_step (MediaKey.VOLUME_DOWN, 20000, true, 0, normal, 6, out volume, out muted);
    assert_cmpuint (volume, CompareOperator.EQ, 20000 - step);
    assert_false (muted);
    MediaKeys.sound_step (MediaKey.MUTE, 20000, false, 0, normal, 6, out volume, out muted);
    assert_cmpuint (volume, CompareOperator.EQ, 20000);
    assert_true (muted);
}

void test_icons () {
    assert_cmpstr (MediaKeys.volume_icon (false, true, 50), CompareOperator.EQ, "audio-volume-muted");
    assert_cmpstr (MediaKeys.volume_icon (false, false, 0), CompareOperator.EQ, "audio-volume-low");
    assert_cmpstr (MediaKeys.volume_icon (false, false, 40), CompareOperator.EQ, "audio-volume-medium");
    assert_cmpstr (MediaKeys.volume_icon (false, false, 100), CompareOperator.EQ, "audio-volume-high");
    assert_cmpstr (MediaKeys.volume_icon (true, true, 100), CompareOperator.EQ, "microphone-sensitivity-muted");
    assert_cmpstr (MediaKeys.volume_icon (true, false, 70), CompareOperator.EQ, "microphone-sensitivity-high");
}

void test_mpris () {
    assert_cmpstr (Mpris.player_name ("org.mpris.MediaPlayer2.vlc"), CompareOperator.EQ, "vlc");
    assert_cmpstr (Mpris.player_name ("org.mpris.MediaPlayer2.vlc.instance7389"), CompareOperator.EQ,
                   "vlc.instance7389");
    assert_true (Mpris.player_name ("org.mate.Polo") == null);
    assert_cmpstr (Mpris.method_for ("Play"), CompareOperator.EQ, "PlayPause");
    assert_cmpstr (Mpris.method_for ("Stop"), CompareOperator.EQ, "Stop");
    assert_true (Mpris.method_for ("Shuffle") == null);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/media-keys/table", test_table);
    Test.add_func ("/media-keys/changes", test_changes);
    Test.add_func ("/media-keys/locked", test_locked);
    Test.add_func ("/media-keys/repeat", test_repeat);
    Test.add_func ("/media-keys/sound-step", test_sound_step);
    Test.add_func ("/media-keys/icons", test_icons);
    Test.add_func ("/media-keys/mpris", test_mpris);
    return Test.run ();
}
