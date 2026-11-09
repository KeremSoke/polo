/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The wallpaper fallback. When no client paints an output's
 * background (caja's desktop, or a background-layer surface), Polo draws
 * org.mate.background itself on the BACKGROUND layer, as MateBG draws the
 * X root window: libpolo-bg (lib/bg, ported from mate-bg.c) draws each
 * output's part of the screen into a Backend.PixelBuffer on a worker
 * thread. Settings changes apply live; slideshows redraw on their timer.
 */

namespace Polo.Settings {
    public class Background : GLib.Object {
        private const string SCHEMA = "org.mate.background";

        private Backend.Server server;
        private Policy.Model model;
        private Shell.LayerShell layer_shell;
        private Shell.Desktop desktop;
        private GLib.Settings? settings = null;

        private class Entry {
            public Policy.Output output;
            public Backend.PixelBuffer buffer;
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
        }

        private GLib.GenericArray<Entry> entries = new GLib.GenericArray<Entry> ();
        private uint changed_idle = 0;
        private uint slideshow_timer = 0;
        private GLib.Thread<bool>? worker = null;
        private uint finish_idle = 0;
        private bool redraw_pending = false;

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
            } else {
                Logging.scope (LogDomain.POLO).info ("no %s schema; no wallpaper", SCHEMA);
            }
            layer_shell.arranged.connect (sync);
        }

        public void shutdown () {
            layer_shell.arranged.disconnect (sync);
            if (settings != null) {
                settings.changed.disconnect (on_settings_changed);
            }
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

        /* ---- Outputs ---------------------------------------------------- */

        public unowned Backend.View? wallpaper_view (Policy.Output output) {
            var entry = entry_for (output);
            return entry != null && entry.buffer != null ? entry.buffer.get_view () : null;
        }

        public Backend.PixelBuffer? wallpaper_buffer (Policy.Output output) {
            var entry = entry_for (output);
            return entry != null ? entry.buffer : null;
        }

        private Entry? entry_for (Policy.Output output) {
            foreach (var entry in entries) {
                if (entry.output == output) {
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
            var outputs = model.get_outputs ();
            for (int i = entries.length - 1; i >= 0; i--) {
                bool found = false;
                foreach (var output in outputs) {
                    found = found || output == entries[i].output;
                }
                if (!found) {
                    entries.remove_index (i);
                }
            }
            foreach (var output in outputs) {
                var head = desktop.backend_output (output);
                if (head == null) {
                    continue;
                }
                int scale = int.max (head.get_scale (), 1);
                int width = output.rect.width * scale;
                int height = output.rect.height * scale;
                var entry = entry_for (output);
                if (entry != null && entry.width == width && entry.height == height &&
                    entry.scale == scale) {
                    if (entry.x != output.rect.x || entry.y != output.rect.y) {
                        entry.x = output.rect.x;
                        entry.y = output.rect.y;
                        changed = true;
                    }
                    continue;
                }
                if (entry == null) {
                    entry = new Entry ();
                    entry.output = output;
                    entries.add (entry);
                }
                if (width <= 0 || height <= 0) {
                    continue;
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
            place_all ();
            if (changed) {
                redraw ();
            }
        }

        private bool should_show (Entry entry) {
            return settings != null && settings.get_boolean ("draw-background") &&
                   !layer_shell.covers_output (entry.output);
        }

        private void place_all () {
            foreach (var entry in entries) {
                unowned Backend.View? view = entry.buffer != null ? entry.buffer.get_view () : null;
                if (view == null) {
                    continue;
                }
                bool show = should_show (entry);
                if (show) {
                    view.set_position (entry.output.rect.x, entry.output.rect.y);
                    view.move_to_layer (server.get_layer (Backend.LayerKind.BACKGROUND));
                } else {
                    view.move_to_layer (null);
                }
                if (show != entry.shown) {
                    entry.shown = show;
                    Logging.scope (LogDomain.POLO).debug ("wallpaper on %s %s", entry.output.name,
                                                          show ? "shown" : "hidden");
                }
            }
        }

        /* ---- Drawing ---------------------------------------------------- */

        /* Marco/MateBG: a missing picture falls back to the key's default,
         * then to colours only. */
        private string? picture_filename () {
            var filename = settings.get_string ("picture-filename");
            if (filename == "") {
                return null;
            }
            if (!GLib.FileUtils.test (filename, GLib.FileTest.IS_REGULAR)) {
                var fallback = settings.get_default_value ("picture-filename");
                filename = fallback != null ? fallback.get_string () : "";
                if (!GLib.FileUtils.test (filename, GLib.FileTest.IS_REGULAR)) {
                    return null;
                }
            }
            return filename;
        }

        private Bg.Background describe () {
            var bg = new Bg.Background ();
            bg.set_color ((Bg.ColorType) settings.get_enum ("color-shading-type"),
                          settings.get_string ("primary-color"),
                          settings.get_string ("secondary-color"));
            bg.set_placement ((Bg.Placement) settings.get_enum ("picture-options"));
            bg.set_filename (picture_filename ());
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

            var jobs = new GLib.GenericArray<Job> ();
            foreach (var entry in entries) {
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
                if (job.data.length > 0) {
                    jobs.add (job);
                }
            }
            if (jobs.length == 0) {
                return;
            }

            var bg = describe ();
            var screen = screen_of (model.get_outputs ());
            uint timeout = uint.MAX;
            worker = new GLib.Thread<bool> ("polo-background", () => {
                foreach (var job in jobs) {
                    bg.draw_output (job.data, job.width, job.height, job.stride, job.scale,
                                    job.rect.x - screen.x, job.rect.y - screen.y,
                                    screen.width, screen.height);
                }
                timeout = bg.get_timeout ();
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
