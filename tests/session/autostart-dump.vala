/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

int main (string[] args) {
    if (args.length > 1 && args[1] == "--restart") {
        return restart (args[2:args.length]);
    }
    if (args.length > 1 && args[1] == "--live") {
        return live (args[2]);
    }
    string[] lines = {};
    foreach (var app in Polo.Autostart.session_apps ()) {
        lines += "%s\t%s\t%d\t%d\t%d".printf (app.id, app.phase.to_name (), (int) app.is_disabled (),
                                              (int) app.is_conditionally_disabled (), (int) (app.delay > 0));
        stderr.printf ("%s\t%d\t%d\t%d\n", app.id, (int) app.autorestart, app.delay, (int) app.required);
    }
    GLib.qsort_with_data<string> (lines, sizeof (string), (a, b) => GLib.strcmp (a, b));
    foreach (unowned string line in lines) {
        print ("%s\n", line);
    }
    return 0;
}

int restart (string[] seconds) {
    var policy = new Polo.RestartPolicy ();
    foreach (unowned string second in seconds) {
        print ("%d\n", policy.next_delay (int.parse (second) * GLib.TimeSpan.SECOND));
    }
    return 0;
}

int live (string path) {
    var app = Polo.AutostartApp.load (path);
    var loop = new GLib.MainLoop ();
    app.is_conditionally_disabled ();
    app.condition_changed.connect ((condition) => {
        print ("changed %d\n", (int) condition);
        stdout.flush ();
    });
    print ("disabled %d\n", (int) app.is_conditionally_disabled ());
    stdout.flush ();
    new GLib.IOChannel.unix_new (0).add_watch (GLib.IOCondition.IN | GLib.IOCondition.HUP, (source, condition) => {
        string line;
        size_t ending;
        try {
            if (source.read_line (out line, null, out ending) != GLib.IOStatus.NORMAL) {
                loop.quit ();
                return false;
            }
        } catch (GLib.Error e) {
            loop.quit ();
            return false;
        }
        var parts = line.strip ().split (" ");
        new GLib.Settings (parts[0]).set_boolean (parts[1], parts[2] == "true");
        return true;
    });
    loop.run ();
    return 0;
}
