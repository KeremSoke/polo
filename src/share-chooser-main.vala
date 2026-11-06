/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * polo-share-chooser: xdg-desktop-portal-wlr's dmenu chooser. Reads the
 * sources on stdin, lets Polo show them, and prints the one picked;
 * nothing on stdout means declined.
 */

int main (string[] args) {
    string[] lines = {};
    string? line;
    while ((line = GLib.stdin.read_line ()) != null) {
        if (line.strip () != "") {
            lines += line;
        }
    }
    try {
        var bus = GLib.Bus.get_sync (GLib.BusType.SESSION);
        var reply = bus.call_sync ("org.mate.Polo", "/org/mate/Polo", "org.mate.Polo.ScreenShare1", "Choose",
                                   new GLib.Variant ("(^as)", lines), new GLib.VariantType ("(s)"),
                                   GLib.DBusCallFlags.NO_AUTO_START, int.MAX);
        string choice;
        reply.get ("(s)", out choice);
        if (choice != "") {
            stdout.printf ("%s\n", choice);
        }
        return 0;
    } catch (GLib.Error e) {
        stderr.printf ("polo-share-chooser: %s\n", e.message);
        return 1;
    }
}
