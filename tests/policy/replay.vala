/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Replays Marco oracle fixtures against libpolo-policy without a compositor,
 * comparing each step with Marco's recorded state.
 */

using Polo;

/* The model already writes Window.frame; nothing to carry out. */
class FakeAdapter : GLib.Object, WindowAdapter {
    public void configure (Marco.Rect frame) {}
    public void set_activated (bool activated) {}
    public void set_maximized (bool maximized) {}
    public void set_fullscreen (bool fullscreen) {}
    public void take_focus () {}
    public void close () {}
    public void ping () {}
}

errordomain ReplayError {
    FIXTURE,
    MISMATCH
}

class Replay : GLib.Object {
    private Json.Object scenario;
    private Json.Array snapshots;
    private Policy.Model model;
    private GLib.HashTable<string, Window> windows =
        new GLib.HashTable<string, Window> (GLib.str_hash, GLib.str_equal);
    private int64 clock = 1000;
    private int step_index = -1;

    public string name { get; private set; }
    public string description { get; private set; }
    public int steps_total { get; private set; }

    public Replay (Json.Object fixture) throws ReplayError {
        scenario = fixture.get_object_member ("scenario");
        snapshots = fixture.get_array_member ("snapshots");
        name = scenario.get_string_member ("name");
        description = scenario.get_string_member ("description");
        steps_total = (int) scenario.get_array_member ("steps").get_length ();
        model = new Policy.Model (prefs ());
    }

    /* ---- Set-up --------------------------------------------------------- */

    private MarcoPrefs prefs () throws ReplayError {
        var defaults = MarcoPrefs.defaults ();
        var overrides = new GLib.HashTable<string, GLib.Variant> (GLib.str_hash, GLib.str_equal);
        var prefs = scenario.get_object_member ("prefs");
        foreach (unowned string key in prefs.get_members ()) {
            var current = defaults.get_value (key);
            if (current == null) {
                throw new ReplayError.FIXTURE ("unknown pref %s", key);
            }
            var node = prefs.get_member (key);
            if (current.is_of_type (GLib.VariantType.BOOLEAN)) {
                overrides.insert (key, new GLib.Variant.boolean (node.get_boolean ()));
            } else if (current.is_of_type (GLib.VariantType.INT32)) {
                overrides.insert (key, new GLib.Variant.int32 ((int32) node.get_int ()));
            } else {
                overrides.insert (key, new GLib.Variant.string (node.get_string ()));
            }
        }
        try {
            return MarcoPrefs.with_overrides (overrides);
        } catch (PrefsError e) {
            throw new ReplayError.FIXTURE ("%s", e.message);
        }
    }

    private static int[] ints (Json.Array array) {
        int[] values = {};
        foreach (var node in array.get_elements ()) {
            values += (int) node.get_int ();
        }
        return values;
    }

    private static Marco.Rect rect (int x, int y, int width, int height) {
        return Marco.Rect () { x = x, y = y, width = width, height = height };
    }

    /* Decoration extents Marco gave this window in the next snapshot. */
    private Marco.FrameBorders borders_in (int snapshot, string id) {
        var borders = Marco.FrameBorders ();
        var all = snapshots.get_object_element (snapshot).get_object_member ("windows");
        if (all.has_member (id)) {
            var w = all.get_object_member (id);
            if (w.has_member ("extents")) {
                var e = ints (w.get_array_member ("extents"));
                borders = Marco.FrameBorders () {
                    left = e[0], right = e[1], top = e[2], bottom = e[3]
                };
            }
        }
        return borders;
    }

    private void set_up_outputs () {
        int x = 0;
        int i = 0;
        foreach (var node in scenario.get_array_member ("outputs").get_elements ()) {
            var size = ints (node.get_array ());
            model.add_output (new Policy.Output ("output-%d".printf (i++),
                                                 rect (x, 0, size[0], size[1])));
            x += size[0];
        }
        var pointer = ints (scenario.get_array_member ("pointer"));
        model.pointer_moved (pointer[0], pointer[1]);
    }

    /* Panels, as tools/marco-oracle/run.py Oracle.dock () creates them. */
    private void set_up_struts () {
        var outputs = scenario.get_array_member ("outputs");
        foreach (var node in scenario.get_array_member ("struts").get_elements ()) {
            var strut = node.get_object ();
            int index = (int) strut.get_int_member ("output");
            int t = (int) strut.get_int_member ("thickness");
            int ox = 0;
            for (int i = 0; i < index; i++) {
                ox += ints (outputs.get_array_element (i))[0];
            }
            var size = ints (outputs.get_array_element (index));
            Marco.Rect area;
            Marco.Side side;
            switch (strut.get_string_member ("edge")) {
                case "top": area = rect (ox, 0, size[0], t); side = Marco.Side.TOP; break;
                case "bottom":
                    area = rect (ox, size[1] - t, size[0], t);
                    side = Marco.Side.BOTTOM;
                    break;
                case "left": area = rect (ox, 0, t, size[1]); side = Marco.Side.LEFT; break;
                default:
                    area = rect (ox + size[0] - t, 0, t, size[1]);
                    side = Marco.Side.RIGHT;
                    break;
            }
            var dock = new Window (WindowKind.X11, new FakeAdapter ());
            dock.label = strut.get_string_member ("id");
            dock.type_hint = Marco.WindowType.DOCK;
            dock.accepts_focus = false;
            dock.frame = area;
            dock.hints.flags = Marco.SizeHintFlags.P_POSITION;
            dock.struts = { Marco.Strut () { rect = area, side = side,
                                             edge = Marco.EdgeType.SCREEN } };
            windows.insert (dock.label, dock);
            model.manage (dock);
            model.map (dock);
        }
    }

    /* ---- Steps ------------------------------------------------------------ */

    private Window window (Json.Object step) throws ReplayError {
        var id = step.get_string_member ("id");
        var w = windows.lookup (id);
        if (w == null) {
            throw new ReplayError.FIXTURE ("no window %s", id);
        }
        return w;
    }

    private static WindowState state_flag (string name) {
        switch (name) {
            case "above": return WindowState.ABOVE;
            case "below": return WindowState.BELOW;
            case "sticky": return WindowState.STICKY;
            case "maximized_vert": return WindowState.MAXIMIZED_V;
            case "maximized_horz": return WindowState.MAXIMIZED_H;
            case "fullscreen": return WindowState.FULLSCREEN;
            case "modal": return WindowState.MODAL;
            case "shaded": return WindowState.SHADED;
            case "skip_taskbar": return WindowState.SKIP_TASKBAR;
            case "skip_pager": return WindowState.SKIP_PAGER;
            case "hidden": return WindowState.MINIMIZED;
            case "demands_attention": return WindowState.DEMANDS_ATTENTION;
            default: return 0;
        }
    }

    private static Marco.WindowType window_type (string name) {
        switch (name) {
            case "desktop": return Marco.WindowType.DESKTOP;
            case "dock": return Marco.WindowType.DOCK;
            case "dialog": return Marco.WindowType.DIALOG;
            case "modal_dialog": return Marco.WindowType.MODAL_DIALOG;
            case "toolbar": return Marco.WindowType.TOOLBAR;
            case "menu": return Marco.WindowType.MENU;
            case "utility": return Marco.WindowType.UTILITY;
            case "splashscreen": return Marco.WindowType.SPLASHSCREEN;
            default: return Marco.WindowType.NORMAL;
        }
    }

    private static Marco.SizeHints raw_hints (uint32 position_flags, Json.Object hints) {
        var w = new RawHints ();
        w.hints.flags = position_flags;
        apply_hints (w, hints);
        return w.hints;
    }

    private class RawHints {
        public Marco.SizeHints hints;
    }

    private static void apply_hints (RawHints w, Json.Object hints) {
        if (hints.has_member ("min")) {
            var v = ints (hints.get_array_member ("min"));
            w.hints.flags |= Marco.SizeHintFlags.P_MIN_SIZE;
            w.hints.min_width = v[0];
            w.hints.min_height = v[1];
        }
        if (hints.has_member ("max")) {
            var v = ints (hints.get_array_member ("max"));
            w.hints.flags |= Marco.SizeHintFlags.P_MAX_SIZE;
            w.hints.max_width = v[0];
            w.hints.max_height = v[1];
        }
        if (hints.has_member ("inc")) {
            var v = ints (hints.get_array_member ("inc"));
            w.hints.flags |= Marco.SizeHintFlags.P_RESIZE_INC;
            w.hints.width_inc = v[0];
            w.hints.height_inc = v[1];
        }
        if (hints.has_member ("base")) {
            var v = ints (hints.get_array_member ("base"));
            w.hints.flags |= Marco.SizeHintFlags.P_BASE_SIZE;
            w.hints.base_width = v[0];
            w.hints.base_height = v[1];
        }
        if (hints.has_member ("aspect")) {
            var aspect = hints.get_array_member ("aspect");
            var min = ints (aspect.get_array_element (0));
            var max = ints (aspect.get_array_element (1));
            w.hints.flags |= Marco.SizeHintFlags.P_ASPECT;
            w.hints.min_aspect = Marco.Aspect () { x = min[0], y = min[1] };
            w.hints.max_aspect = Marco.Aspect () { x = max[0], y = max[1] };
        }
        if (hints.has_member ("gravity")) {
            string[] names = { "forget", "north_west", "north", "north_east", "west", "center",
                               "east", "south_west", "south", "south_east", "static" };
            var gravity = hints.get_string_member ("gravity");
            for (int i = 0; i < names.length; i++) {
                if (names[i] == gravity) {
                    w.hints.flags |= Marco.SizeHintFlags.P_WIN_GRAVITY;
                    w.hints.win_gravity = (Marco.Gravity) i;
                }
            }
        }
    }

    private void map (Json.Object step, int snapshot) {
        var w = new Window (WindowKind.X11, new FakeAdapter ());
        w.label = step.get_string_member ("id");
        var type_name = step.get_string_member ("type");
        w.type_hint = window_type (type_name);
        if (!step.get_null_member ("transient_for")) {
            w.transient_for = windows.lookup (step.get_string_member ("transient_for"));
        }
        w.accepts_focus = step.get_boolean_member ("input");
        w.borders = borders_in (snapshot, w.label);

        var size = ints (step.get_array_member ("size"));
        w.frame.width = size[0] + w.borders.left + w.borders.right;
        w.frame.height = size[1] + w.borders.top + w.borders.bottom;
        if (!step.get_null_member ("position")) {
            var position = ints (step.get_array_member ("position"));
            w.frame.x = position[0];
            w.frame.y = position[1];
            w.hints.flags |= step.get_string_member ("position_source") == "user"
                ? Marco.SizeHintFlags.US_POSITION : Marco.SizeHintFlags.P_POSITION;
        }
        model.set_size_hints (w, raw_hints (w.hints.flags & (Marco.SizeHintFlags.US_POSITION |
                                                             Marco.SizeHintFlags.P_POSITION),
                                            step.get_object_member ("hints")));

        foreach (var node in step.get_array_member ("state").get_elements ()) {
            var flag = state_flag (node.get_string ());
            w.set_state_flags (flag, true);
            w.wm_state_skip_taskbar |= flag == WindowState.SKIP_TASKBAR;
            w.wm_state_skip_pager |= flag == WindowState.SKIP_PAGER;
        }
        if (w.type_hint == Marco.WindowType.MODAL_DIALOG) {
            w.set_state_flags (WindowState.MODAL, true);
        }

        var user_time = step.get_member ("user_time");
        if (user_time.is_null ()) {
            w.user_time = Window.USER_TIME_UNSET;
        } else if (user_time.get_value_type () == typeof (string)) {
            w.user_time = clock++;
        } else {
            w.user_time = user_time.get_int ();
        }

        windows.insert (w.label, w);
        model.manage (w);
        model.map (w);
    }

    /* The topmost showing window under a point, like the X server's pick. */
    private Window? window_at (double x, double y) {
        var stack = model.get_stack ();
        for (int i = stack.length - 1; i >= 0; i--) {
            var w = stack[i];
            if (model.is_showing (w) &&
                x >= w.frame.x && x < w.frame.x + w.frame.width &&
                y >= w.frame.y && y < w.frame.y + w.frame.height) {
                return w;
            }
        }
        return null;
    }

    private void click (Json.Object step) throws ReplayError {
        var target = window (step);
        bool titlebar = step.get_string_member ("where") == "titlebar";
        int x, y;
        if (titlebar) {
            var at = step.get_null_member ("at") ? new int[] { target.frame.width / 2,
                                                              target.borders.top / 2 }
                                                 : ints (step.get_array_member ("at"));
            x = target.frame.x + at[0];
            y = target.frame.y + at[1];
        } else {
            var client = target.client_rect ();
            var at = step.get_null_member ("at") ? new int[] { client.width / 2,
                                                              client.height / 2 }
                                                 : ints (step.get_array_member ("at"));
            x = client.x + at[0];
            y = client.y + at[1];
        }
        model.pointer_moved (x, y);
        var hit = window_at (x, y);
        model.pointer_entered (hit, clock++);
        if (hit != null) {
            bool on_frame = !(hit.client_rect ().x <= x && x < hit.client_rect ().x +
                              hit.client_rect ().width && hit.client_rect ().y <= y &&
                              y < hit.client_rect ().y + hit.client_rect ().height);
            model.button_pressed (hit, on_frame, clock++);
        }
    }

    private void run_step (Json.Object step, int snapshot) throws ReplayError {
        switch (step.get_string_member ("kind")) {
            case "map":
                map (step, snapshot);
                break;
            case "unmap":
            case "destroy":
                model.unmanage (window (step));
                break;
            case "click":
                click (step);
                break;
            case "pointer":
                int px, py;
                if (!step.get_null_member ("id")) {
                    var client = window (step).client_rect ();
                    px = client.x + client.width / 2;
                    py = client.y + client.height / 2;
                } else {
                    var to = ints (step.get_array_member ("to"));
                    px = to[0];
                    py = to[1];
                }
                model.pointer_moved (px, py);
                model.pointer_entered (window_at (px, py), clock++);
                break;
            case "minimize":
                model.minimize (window (step));
                break;
            case "key":
                if (step.get_string_member ("combo") == "<Super>Left" && model.focused != null) {
                    model.toggle_tiled (model.focused, Marco.TileMode.LEFT);
                } else if (step.get_string_member ("combo") == "<Alt>Tab") {
                    var next = Policy.TabChain.get_tab_next (model, Policy.TabList.NORMAL, model.current_workspace,
                                                               model.focused, false);
                    if (next != null) {
                        model.activate (next, Policy.ActivationSource.PAGER, clock++);
                    }
                }
                break;
            case "request_state":
                var w = window (step);
                WindowState flags = 0;
                foreach (var node in step.get_array_member ("state").get_elements ()) {
                    flags |= state_flag (node.get_string ());
                }
                var action = step.get_string_member ("action");
                bool on = action == "add" || (action == "toggle" && !w.has_state (flags));
                model.set_state (w, flags, on);
                break;
            case "configure":
                var w = window (step);
                bool move = !step.get_null_member ("position");
                bool resize = !step.get_null_member ("size");
                int[] position = move ? ints (step.get_array_member ("position")) : new int[] { 0, 0 };
                int[] size = resize ? ints (step.get_array_member ("size")) : new int[] { 0, 0 };
                model.configure_request (w, move, position[0], position[1], resize, size[0], size[1]);
                break;
            case "set_hints":
                var w = window (step);
                model.set_size_hints (w, raw_hints (w.hints.flags & (Marco.SizeHintFlags.US_POSITION |
                                                                     Marco.SizeHintFlags.P_POSITION),
                                                    step.get_object_member ("hints")));
                break;
            case "move_to_workspace":
                model.move_to_workspace (window (step), (int) step.get_int_member ("workspace"));
                break;
            case "switch_workspace":
                model.switch_workspace ((int) step.get_int_member ("workspace"));
                break;
            case "activate":
                var source = step.get_string_member ("source") == "application"
                    ? Policy.ActivationSource.APPLICATION : Policy.ActivationSource.PAGER;
                var time = step.get_member ("time");
                int64 when = time.get_value_type () == typeof (string) ? clock++ : time.get_int ();
                model.activate (window (step), source, when);
                break;
            case "wait":
                break;
            default:
                throw new ReplayError.FIXTURE ("unknown step %s", step.get_string_member ("kind"));
        }
    }

    /* ---- Comparison ----------------------------------------------------------- */

    private static string show (Json.Node? node) {
        return node == null ? "(none)" : Json.to_string (node, false);
    }

    private static bool same (Json.Node? a, Json.Node? b) {
        return show (a) == show (b);
    }

    private void compare (int snapshot) throws ReplayError {
        var expected = snapshots.get_object_element (snapshot);
        Json.Object actual;
        try {
            var parser = new Json.Parser ();
            parser.load_from_data (model.dump_json ());
            actual = parser.get_root ().get_object ();
        } catch (GLib.Error e) {
            throw new ReplayError.FIXTURE ("bad model JSON: %s", e.message);
        }

        string where = step_index < 0 ? "initial state"
            : "step %d (%s)".printf (step_index + 1, scenario.get_array_member ("steps")
                                     .get_object_element (step_index).get_string_member ("kind"));

        foreach (var field in new string[] { "active", "current_workspace", "n_workspaces",
                                             "stacking" }) {
            if (!same (expected.get_member (field), actual.get_member (field))) {
                throw new ReplayError.MISMATCH ("%s: %s is %s, Marco %s", where, field,
                                                show (actual.get_member (field)),
                                                show (expected.get_member (field)));
            }
        }

        var expected_windows = expected.get_object_member ("windows");
        var actual_windows = actual.get_object_member ("windows");
        foreach (unowned string id in expected_windows.get_members ()) {
            var e = expected_windows.get_object_member (id);
            bool e_mapped = e.get_boolean_member ("mapped");
            var a = actual_windows.has_member (id) ? actual_windows.get_object_member (id) : null;
            bool a_mapped = a != null && a.get_boolean_member ("mapped");
            if (e_mapped != a_mapped) {
                throw new ReplayError.MISMATCH ("%s: %s mapped is %s, Marco %s", where, id,
                                                a_mapped.to_string (), e_mapped.to_string ());
            }
            if (!e_mapped) {
                continue;
            }
            foreach (var field in new string[] { "frame", "workspace", "state", "visible" }) {
                if (!same (e.get_member (field), a.get_member (field))) {
                    throw new ReplayError.MISMATCH ("%s: %s %s is %s, Marco %s", where, id,
                                                    field, show (a.get_member (field)),
                                                    show (e.get_member (field)));
                }
            }
        }
    }

    /* Run every step; throws MISMATCH at the first difference from Marco. */
    public void run () throws ReplayError {
        set_up_outputs ();
        set_up_struts ();
        compare (0);
        var steps = scenario.get_array_member ("steps");
        for (step_index = 0; step_index < steps.get_length (); step_index++) {
            run_step (steps.get_object_element (step_index), step_index + 1);
            model.focus_policy.flush_autoraise ();
            compare (step_index + 1);
        }
    }
}

int main (string[] args) {
    string? fixtures_dir = null;
    string? passing_file = null;
    string? only = null;
    bool verbose = false;
    GLib.OptionEntry[] options = {
        { "fixtures", 0, 0, GLib.OptionArg.FILENAME, ref fixtures_dir, "Fixture directory", "DIR" },
        { "passing", 0, 0, GLib.OptionArg.FILENAME, ref passing_file,
          "Scenarios that must pass", "FILE" },
        { "only", 0, 0, GLib.OptionArg.STRING, ref only, "Run one scenario", "NAME" },
        { "verbose", 'v', 0, GLib.OptionArg.NONE, ref verbose, "Show every mismatch", null },
        { null }
    };
    var context = new GLib.OptionContext ("- replay Marco oracle fixtures against libpolo-policy");
    context.add_main_entries (options, null);
    try {
        context.parse (ref args);
    } catch (GLib.OptionError e) {
        stderr.printf ("%s\n", e.message);
        return 2;
    }
    if (fixtures_dir == null) {
        stderr.printf ("--fixtures is required\n");
        return 2;
    }

    var must_pass = new GLib.HashTable<string, bool> (GLib.str_hash, GLib.str_equal);
    if (passing_file != null) {
        try {
            string text;
            GLib.FileUtils.get_contents (passing_file, out text);
            foreach (var line in text.split ("\n")) {
                var name = line.strip ();
                if (name != "" && !name.has_prefix ("#")) {
                    must_pass.insert (name, true);
                }
            }
        } catch (GLib.FileError e) {
            stderr.printf ("%s\n", e.message);
            return 2;
        }
    }

    string[] names = {};
    try {
        var dir = GLib.Dir.open (fixtures_dir);
        unowned string? entry;
        while ((entry = dir.read_name ()) != null) {
            if (entry.has_suffix (".expected.json")) {
                names += entry;
            }
        }
    } catch (GLib.FileError e) {
        stderr.printf ("%s\n", e.message);
        return 2;
    }
    GLib.qsort_with_data<string> (names, sizeof (string), (a, b) => GLib.strcmp (a, b));

    int passed = 0, failed = 0, regressions = 0;
    string[] newly_passing = {};
    foreach (var file in names) {
        var name = file.substring (0, file.length - ".expected.json".length);
        if (only != null && name != only) {
            continue;
        }
        string status;
        string detail = "";
        try {
            var parser = new Json.Parser ();
            parser.load_from_file (GLib.Path.build_filename (fixtures_dir, file));
            var replay = new Replay (parser.get_root ().get_object ());
            replay.run ();
            status = "pass";
            passed++;
            if (passing_file != null && !must_pass.contains (name)) {
                newly_passing += name;
            }
        } catch (ReplayError.MISMATCH e) {
            status = must_pass.contains (name) ? "FAIL (regression)" : "fail";
            detail = e.message;
            failed++;
            if (must_pass.contains (name)) {
                regressions++;
            }
        } catch (GLib.Error e) {
            status = "ERROR";
            detail = e.message;
            failed++;
            regressions++;
        }
        print ("%-42s %s\n", name, status);
        if (detail != "" && (verbose || status != "fail")) {
            print ("    %s\n", detail);
        }
    }

    print ("\n%d passed, %d failed, %d scenarios", passed, failed, passed + failed);
    if (regressions > 0) {
        print (", %d REGRESSIONS", regressions);
    }
    print ("\n");
    foreach (var name in newly_passing) {
        print ("now passing (add to %s): %s\n", passing_file, name);
    }
    return regressions > 0 ? 1 : 0;
}
