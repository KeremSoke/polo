/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The screenshot keybinding commands. MATE's defaults run mate-screenshot,
 * which reads the screen through X and so sees only XWayland windows; Polo
 * runs polo-screenshot for exactly those defaults and leaves any other
 * command as the user set it.
 */

namespace Polo.Policy.Screenshot {
    public const string WRAPPER = "polo-screenshot";

    public static string command_for (string command, Window? focused) {
        string[] argv;
        try {
            GLib.Shell.parse_argv (command, out argv);
        } catch (GLib.ShellError e) {
            return command;
        }
        if (argv.length == 0 || argv[0] != "mate-screenshot" || argv.length > 2) {
            return command;
        }
        if (argv.length == 1) {
            return WRAPPER;
        }
        switch (argv[1]) {
        case "-w":
        case "--window":
            if (focused == null) {
                return WRAPPER;
            }
            var r = focused.outer_rect ();
            return "%s --geometry=%s".printf (
                WRAPPER, GLib.Shell.quote ("%d,%d %dx%d".printf (r.x, r.y, r.width, r.height)));
        case "-a":
        case "--area":
            return WRAPPER + " --area";
        default:
            return command;
        }
    }
}
