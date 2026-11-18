/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The wallpaper fallback. When no client paints an output's
 * background (a background-layer surface covers it), Polo draws
 * org.mate.background, or the output's org.mate.background.monitor
 * settings when use-shared is false, or else the current workspace's
 * org.mate.polo.background.workspace settings when use-shared is false,
 * itself on the BACKGROUND layer, as
 * MateBG draws the X root window: libpolo-bg (lib/bg, ported from mate-bg.c)
 * draws each output's part of the screen into a Backend.PixelBuffer on a
 * worker thread, one buffer per output and per settings some workspace
 * shows there, so switching workspaces only swaps buffers. Without a desktop surface the wallpaper shows when
 * draw-background is true. Under caja's desktop surface it shows when
 * draw-background is false: caja then paints only icons on a transparent
 * surface. Settings changes apply live; slideshows redraw on their timer.
 */

namespace Polo.Settings {
    public class Background : GLib.Object {
        private const string SCHEMA = "org.mate.background";
        private const string MONITOR_SCHEMA = "org.mate.background.monitor";
        private const string MONITOR_PATH = "/org/mate/desktop/background/monitors/";
        private const string WORKSPACE_SCHEMA = "org.mate.polo.background.workspace";
        private const string WORKSPACE_PATH = "/org/mate/polo/background/workspaces/";

        private Backend.Server server;
        private Policy.Model model;
        private Shell.LayerShell layer_shell;
        private Shell.Desktop desktop;
        private GLib.Settings? settings = null;
        private GLib.SettingsSchema? monitor_schema = null;
        private GLib.HashTable<string, GLib.Settings> monitor_settings =
            new GLib.HashTable<string, GLib.Settings> (GLib.str_hash, GLib.str_equal);
        private GLib.SettingsSchema? workspace_schema = null;
        private GLib.GenericArray<GLib.Settings> workspace_settings = new GLib.GenericArray<GLib.Settings> ();

        private class Entry {
            public string id = "";
            public GLib.Settings source;
            public GLib.Settings? drawer = null;
            public uint64 workspaces;
            public Policy.Output output;
            public Backend.PixelBuffer buffer;
            public Backend.PixelBuffer? previous;
            public int previous_width;
            public int previous_height;
            public int x;
            public int y;
            public int width;
            public int height;
            public int scale;
            public bool shown;
        }

        /* One output of one draw; keeps its buffer alive while the worker
         * writes into it, even if the output changes meanwhile. */
        private class Job {
            public Backend.PixelBuffer buffer;
            public unowned uint8[] data;
            public int stride;
            public int width;
            public int height;
            public int scale;
            public Marco.Rect rect;
            public Marco.Rect screen;
            public Bg.Background bg;
        }

        private GLib.GenericArray<Entry> entries = new GLib.GenericArray<Entry> ();
        private uint changed_idle = 0;
        private uint slideshow_timer = 0;
        private GLib.Thread<bool>? worker = null;
        private uint finish_idle = 0;
        private bool redraw_pending = false;
        private bool shown_once = false;

        public Background (Backend.Server server, Policy.Model model, Shell.Desktop desktop,
                           Shell.LayerShell layer_shell) {
            this.server = server;
            this.model = model;
            this.desktop = desktop;
            this.layer_shell = layer_shell;

            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (SCHEMA, true) : null;
            if (schema != null) {
                settings = new GLib.Settings.full (schema, null, null);
                settings.changed.connect (on_settings_changed);
                monitor_schema = source.lookup (MONITOR_SCHEMA, true);
                workspace_schema = source.lookup (WORKSPACE_SCHEMA, true);
                var capabilities = Backend.WallpaperCapabilities.DRAWN | Backend.WallpaperCapabilities.SPANNED;
                if (monitor_schema != null) {
                    capabilities |= Backend.WallpaperCapabilities.PER_OUTPUT;
                }
                server.set_wallpaper_capabilities (capabilities);
            } else {
                Logging.scope (LogDomain.POLO).info ("no %s schema; no wallpaper", SCHEMA);
            }
            layer_shell.arranged.connect (sync);
            model.notify["current-workspace"].connect (on_workspace_changed);
            model.notify["n-workspaces"].connect (on_workspace_changed);
        }

        private void on_workspace_changed (GLib.Object object, GLib.ParamSpec pspec) {
            if (pspec.name == "n-workspaces") {
                sync ();
            } else {
                place_all ();
            }
        }

        public void shutdown () {
            layer_shell.arranged.disconnect (sync);
            model.notify["current-workspace"].disconnect (on_workspace_changed);
            model.notify["n-workspaces"].disconnect (on_workspace_changed);
            if (settings != null) {
                settings.changed.disconnect (on_settings_changed);
            }
            monitor_settings.foreach ((id, monitor) => {
                monitor.changed.disconnect (on_settings_changed);
            });
            monitor_settings.remove_all ();
            foreach (var workspace in workspace_settings) {
                workspace.changed.disconnect (on_settings_changed);
            }
            workspace_settings.remove_range (0, workspace_settings.length);
            if (changed_idle != 0) {
                GLib.Source.remove (changed_idle);
                changed_idle = 0;
            }
            if (slideshow_timer != 0) {
                GLib.Source.remove (slideshow_timer);
                slideshow_timer = 0;
            }
            /* The worker writes into the buffers; let it finish, and drop
             * its result so the buffers go before the server. */
            if (worker != null) {
                worker.join ();
                worker = null;
            }
            if (finish_idle != 0) {
                GLib.Source.remove (finish_idle);
                finish_idle = 0;
            }
            entries.remove_range (0, entries.length);
        }

        private void on_settings_changed (string key) {
            if (changed_idle == 0) {
                changed_idle = GLib.Idle.add (() => {
                    changed_idle = 0;
                    sync ();
                    redraw ();
                    return GLib.Source.REMOVE;
                });
            }
        }

        public string monitor_id (Policy.Output output) {
            var head = desktop.backend_output (output);
            return head != null ? desktop.monitor_id (head) : "monitor";
        }

        private unowned GLib.Settings? settings_for_monitor (string id) {
            if (monitor_schema == null) {
                return null;
            }
            unowned GLib.Settings? monitor = monitor_settings.lookup (id);
            if (monitor == null) {
                var created = new GLib.Settings.full (monitor_schema, null, MONITOR_PATH + id + "/");
                created.changed.connect (on_settings_changed);
                monitor_settings.insert (id, created);
                monitor = monitor_settings.lookup (id);
            }
            return monitor;
        }

        private unowned GLib.Settings? monitor_override (string id) {
            unowned GLib.Settings? monitor = settings_for_monitor (id);
            return monitor != null && !monitor.get_boolean ("use-shared") ? monitor : null;
        }

        private unowned GLib.Settings? settings_for_workspace (int workspace) {
            if (workspace_schema == null || workspace < 0) {
                return null;
            }
            while (workspace_settings.length <= workspace) {
                var created = new GLib.Settings.full (workspace_schema, null,
                                                      "%s%u/".printf (WORKSPACE_PATH, workspace_settings.length + 1));
                created.changed.connect (on_settings_changed);
                workspace_settings.add (created);
            }
            return workspace_settings[workspace];
        }

        private GLib.Settings source_for (string id, int workspace) {
            unowned GLib.Settings? monitor = monitor_override (id);
            if (monitor != null) {
                return monitor;
            }
            unowned GLib.Settings? own = settings_for_workspace (workspace);
            return own != null && !own.get_boolean ("use-shared") ? own : settings;
        }

        private GLib.Settings drawer_for (string id) {
            unowned GLib.Settings? monitor = monitor_override (id);
            return monitor ?? settings;
        }

        private static bool shows (Entry entry, int workspace) {
            return workspace >= 0 && workspace < 64 && (entry.workspaces & ((uint64) 1 << workspace)) != 0;
        }

        /* ---- Outputs ---------------------------------------------------- */

        public unowned Backend.View? wallpaper_view (Policy.Output output, int workspace = -1) {
            var entry = entry_for (output, workspace);
            return entry != null && entry.buffer != null ? entry.buffer.get_view () : null;
        }

        public Backend.PixelBuffer? wallpaper_buffer (Policy.Output output) {
            var entry = entry_for (output, -1);
            return entry != null ? entry.buffer : null;
        }

        private Entry? entry_for (Policy.Output output, int workspace) {
            int index = workspace < 0 ? model.current_workspace : workspace;
            foreach (var entry in entries) {
                if (entry.output == output && shows (entry, index)) {
                    return entry;
                }
            }
            return null;
        }

        private Entry? find (Policy.Output output, GLib.Settings source) {
            foreach (var entry in entries) {
                if (entry.output == output && entry.source == source) {
                    return entry;
                }
            }
            return null;
        }

        private Entry? shown_on (Policy.Output output) {
            foreach (var entry in entries) {
                if (entry.output == output && entry.shown && entry.buffer != null && entry.buffer.is_ready ()) {
                    return entry;
                }
            }
            return null;
        }

        private void sync () {
            if (settings == null) {
                return;
            }
            bool changed = false;
            var fresh = new GLib.GenericArray<Entry> ();
            var outputs = model.get_outputs ();
            int n_workspaces = int.min (model.n_workspaces, 64);
            foreach (var entry in entries) {
                entry.workspaces = 0;
            }
            foreach (var output in outputs) {
                var head = desktop.backend_output (output);
                if (head == null) {
                    continue;
                }
                int scale = int.max (head.get_scale (), 1);
                int width = output.rect.width * scale;
                int height = output.rect.height * scale;
                string id = monitor_id (output);
                var drawer = drawer_for (id);
                var stand_in = shown_on (output);
                for (int workspace = 0; workspace < n_workspaces; workspace++) {
                    var source = source_for (id, workspace);
                    var entry = find (output, source);
                    if (entry != null && entry.workspaces != 0) {
                        entry.workspaces |= (uint64) 1 << workspace;
                        continue;
                    }
                    if (entry == null) {
                        entry = new Entry ();
                        entry.output = output;
                        entry.source = source;
                        entries.add (entry);
                    }
                    entry.workspaces = (uint64) 1 << workspace;
                    if (entry.id != id || entry.drawer != drawer) {
                        entry.id = id;
                        entry.drawer = drawer;
                        changed = true;
                    }
                    if (entry.width == width && entry.height == height && entry.scale == scale) {
                        if (entry.x != output.rect.x || entry.y != output.rect.y) {
                            entry.x = output.rect.x;
                            entry.y = output.rect.y;
                            changed = true;
                        }
                        continue;
                    }
                    if (width <= 0 || height <= 0) {
                        continue;
                    }
                    bool current = workspace == model.current_workspace;
                    if (entry.buffer != null && entry.buffer.is_ready ()) {
                        entry.previous = entry.buffer;
                        entry.previous_width = entry.width / entry.scale;
                        entry.previous_height = entry.height / entry.scale;
                    } else if (entry.buffer == null && current && stand_in != null && stand_in != entry) {
                        entry.previous = stand_in.buffer;
                        entry.previous_width = stand_in.width / stand_in.scale;
                        entry.previous_height = stand_in.height / stand_in.scale;
                    } else if (entry.previous == null && current) {
                        fresh.add (entry);
                    }
                    entry.x = output.rect.x;
                    entry.y = output.rect.y;
                    entry.width = width;
                    entry.height = height;
                    entry.scale = scale;
                    entry.buffer = new Backend.PixelBuffer (server, width, height, scale);
                    entry.buffer.ready.connect (place_all);
                    changed = true;
                }
            }
            for (int i = entries.length - 1; i >= 0; i--) {
                if (entries[i].workspaces == 0) {
                    hide (entries[i]);
                    entries.remove_index (i);
                }
            }
            if (shown_once && fresh.length > 0) {
                var jobs = make_jobs (fresh);
                draw_jobs (jobs);
                foreach (var job in jobs) {
                    job.buffer.commit ();
                }
            }
            place_all ();
            if (changed) {
                redraw ();
            }
        }

        private bool should_show (Entry entry) {
            if (entry.drawer == null || layer_shell.covers_output (entry.output)) {
                return false;
            }
            return !layer_shell.has_desktop (entry.output) || !entry.drawer.get_boolean ("draw-background");
        }

        private Backend.BackgroundOwner owner (Entry entry, bool show) {
            if (show) {
                return Backend.BackgroundOwner.COMPOSITOR;
            }
            if (layer_shell.covers_output (entry.output) ||
                (layer_shell.has_desktop (entry.output) &&
                 (entry.drawer == null || entry.drawer.get_boolean ("draw-background")))) {
                return Backend.BackgroundOwner.CLIENT;
            }
            return Backend.BackgroundOwner.NONE;
        }

        private void hide (Entry entry) {
            unowned Backend.View? view = entry.buffer != null ? entry.buffer.get_view () : null;
            if (view != null) {
                view.move_to_layer (null);
            }
            if (entry.previous != null) {
                unowned Backend.View? old = entry.previous.get_view ();
                if (old != null) {
                    old.move_to_layer (null);
                }
                entry.previous = null;
            }
            entry.shown = false;
        }

        private void place_all () {
            int workspace = model.current_workspace;
            foreach (var entry in entries) {
                if (!shows (entry, workspace)) {
                    hide (entry);
                }
            }
            foreach (var entry in entries) {
                if (!shows (entry, workspace)) {
                    continue;
                }
                bool show = should_show (entry);
                desktop.set_background_owner (entry.output, owner (entry, show));
                unowned Backend.View? view = entry.buffer != null ? entry.buffer.get_view () : null;
                if (view != null && entry.previous != null) {
                    unowned Backend.View? old = entry.previous.get_view ();
                    if (old != null) {
                        old.move_to_layer (null);
                    }
                    entry.previous = null;
                }
                bool stand_in = view == null && entry.previous != null;
                if (stand_in) {
                    view = entry.previous.get_view ();
                }
                if (view == null) {
                    continue;
                }
                if (show) {
                    var rect = entry.output.rect;
                    view.set_position (rect.x, rect.y);
                    if (stand_in) {
                        view.set_effect_transform (rect.x, rect.y,
                                                   (double) rect.width / entry.previous_width,
                                                   (double) rect.height / entry.previous_height, 0, 0, 0);
                    }
                    view.move_to_layer (server.get_layer (Backend.LayerKind.BACKGROUND));
                    layer_shell.restack_desktop (entry.output);
                } else {
                    view.move_to_layer (null);
                }
                shown_once = shown_once || show;
                if (show != entry.shown) {
                    entry.shown = show;
                    Logging.scope (LogDomain.POLO).debug ("wallpaper on %s (%s) %s", entry.output.name, entry.id,
                                                          show ? "shown" : "hidden");
                }
            }
        }

        /* ---- Drawing ---------------------------------------------------- */

        /* Marco/MateBG: a missing picture falls back to the key's default,
         * then to colours only. */
        private string? picture_filename (GLib.Settings source) {
            var filename = source.get_string ("picture-filename");
            if (filename == "") {
                return null;
            }
            if (!GLib.FileUtils.test (filename, GLib.FileTest.IS_REGULAR)) {
                var fallback = source.get_default_value ("picture-filename");
                filename = fallback != null ? fallback.get_string () : "";
                if (!GLib.FileUtils.test (filename, GLib.FileTest.IS_REGULAR)) {
                    return null;
                }
            }
            return filename;
        }

        private Bg.Background describe (GLib.Settings source) {
            var bg = new Bg.Background ();
            bg.set_color ((Bg.ColorType) source.get_enum ("color-shading-type"),
                          source.get_string ("primary-color"),
                          source.get_string ("secondary-color"));
            bg.set_placement ((Bg.Placement) source.get_enum ("picture-options"));
            bg.set_filename (picture_filename (source));
            return bg;
        }

        private static Marco.Rect screen_of (GLib.GenericArray<Policy.Output> outputs) {
            if (outputs.length == 0) {
                return Marco.Rect ();
            }
            int x1 = int.MAX, y1 = int.MAX, x2 = int.MIN, y2 = int.MIN;
            foreach (var output in outputs) {
                x1 = int.min (x1, output.rect.x);
                y1 = int.min (y1, output.rect.y);
                x2 = int.max (x2, output.rect.x + output.rect.width);
                y2 = int.max (y2, output.rect.y + output.rect.height);
            }
            return Marco.Rect () { x = x1, y = y1, width = x2 - x1, height = y2 - y1 };
        }

        private Marco.Rect screen_for (GLib.Settings source) {
            var outputs = new GLib.GenericArray<Policy.Output> ();
            foreach (var entry in entries) {
                if (entry.source == source) {
                    outputs.add (entry.output);
                }
            }
            return screen_of (outputs);
        }

        private GLib.GenericArray<Job> make_jobs (GLib.GenericArray<Entry> targets) {
            var described = new GLib.HashTable<GLib.Settings, Bg.Background> (GLib.direct_hash, GLib.direct_equal);
            var jobs = new GLib.GenericArray<Job> ();
            foreach (var entry in targets) {
                if (entry.buffer == null) {
                    continue;
                }
                var job = new Job ();
                int stride;
                job.buffer = entry.buffer;
                job.data = entry.buffer.begin (out stride);
                job.stride = stride;
                job.width = entry.width;
                job.height = entry.height;
                job.scale = entry.scale;
                job.rect = entry.output.rect;
                var source = entry.source;
                job.bg = described[source];
                if (job.bg == null) {
                    job.bg = describe (source);
                    described[source] = job.bg;
                }
                job.screen = source != settings && source == entry.drawer ? entry.output.rect : screen_for (source);
                if (job.data.length > 0) {
                    jobs.add (job);
                }
            }
            return jobs;
        }

        private static uint draw_jobs (GLib.GenericArray<Job> jobs) {
            uint timeout = uint.MAX;
            foreach (var job in jobs) {
                job.bg.draw_output (job.data, job.width, job.height, job.stride, job.scale,
                                    job.rect.x - job.screen.x, job.rect.y - job.screen.y,
                                    job.screen.width, job.screen.height);
                timeout = uint.min (timeout, job.bg.get_timeout ());
            }
            return timeout;
        }

        private void redraw () {
            if (settings == null || entries.length == 0) {
                return;
            }
            if (worker != null) {
                redraw_pending = true;
                return;
            }
            if (slideshow_timer != 0) {
                GLib.Source.remove (slideshow_timer);
                slideshow_timer = 0;
            }

            var jobs = make_jobs (entries);
            if (jobs.length == 0) {
                return;
            }

            worker = new GLib.Thread<bool> ("polo-background", () => {
                uint timeout = draw_jobs (jobs);
                finish_idle = GLib.Idle.add (() => {
                    finish_idle = 0;
                    finish (jobs, timeout);
                    return GLib.Source.REMOVE;
                });
                return true;
            });
        }

        private void finish (GLib.GenericArray<Job> jobs, uint timeout) {
            if (worker == null) {
                return; /* shut down meanwhile */
            }
            worker.join ();
            worker = null;
            foreach (var job in jobs) {
                job.buffer.commit ();
            }
            place_all ();
            if (redraw_pending) {
                redraw_pending = false;
                redraw ();
            } else if (timeout != uint.MAX) {
                slideshow_timer = GLib.Timeout.add_seconds (uint.max (timeout, 1), () => {
                    slideshow_timer = 0;
                    redraw ();
                    return GLib.Source.REMOVE;
                });
            }
        }
    }
}
