/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What to share, for xdg-desktop-portal-wlr: its monitors and windows in
 * the Alt-Tab popup, with the Alt-Tab outline around the selected one. The
 * portal does not say who asks, so the heading names the focused window's
 * application, which is where the user just pressed Share.
 * Arrows and Tab move, Enter or a click shares, Escape or a click
 * elsewhere declines.
 */

namespace Polo.Shell {
    public delegate Window? WindowForIdentifier (string identifier);

    public delegate void ShareChosen (string? line);

    public class ShareChooser : GLib.Object {
        private const string MONITOR_ICON = "video-display";
        private const int OUTLINE_WIDTH = 5;
        private const double MAX_PREVIEW_SCREEN_FRACTION = 0.33;
        private const double MAX_PREVIEW_SIZE = 300.0;
        private const uint KEY_ESC = 1;
        private const uint KEY_TAB = 15;
        private const uint KEY_ENTER = 28;
        private const uint KEY_KPENTER = 96;
        private const uint KEY_SPACE = 57;
        private const uint KEY_UP = 103;
        private const uint KEY_LEFT = 105;
        private const uint KEY_RIGHT = 106;
        private const uint KEY_DOWN = 108;

        private class Request {
            public GLib.GenericArray<Policy.ShareSource> sources = new GLib.GenericArray<Policy.ShareSource> ();
            public ShareChosen done;
            public int current = 0;
            public uint columns = 1;
            public string heading = "Choose what to share";
        }

        private Backend.Server server;
        private Policy.Model model;
        private Desktop desktop;
        private Outline outline;
        private Request? request = null;
        private WindowForIdentifier? window_for_identifier = null;

        public bool active { get { return request != null; } }

        public ShareChooser (Backend.Server server, Policy.Model model, Desktop desktop) {
            this.server = server;
            this.model = model;
            this.desktop = desktop;
            outline = new Outline (server);
        }

        public void set_window_lookup (owned WindowForIdentifier? lookup) {
            window_for_identifier = (owned) lookup;
        }

        public void shutdown () {
            finish (null);
            window_for_identifier = null;
        }

        public void choose (string[] lines, owned ShareChosen done) {
            finish (null);
            var shell_ui = server.get_shell_ui ();
            var next = new Request ();
            next.done = (owned) done;
            foreach (unowned string line in lines) {
                var source = Policy.ShareSource.parse (line);
                if (source != null) {
                    next.sources.add (source);
                }
            }
            if (next.sources.length == 0 || !shell_ui.is_bound () || desktop.grab_active) {
                Logging.scope (LogDomain.PROTOCOL).info ("screen share: nothing to choose from or no popup");
                next.done (null);
                return;
            }
            next.columns = (uint) int.max (model.prefs.alt_tab_max_columns, 1);
            var asking = model.focused != null ? app_name (model.focused) : null;
            if (asking != null) {
                next.heading = "Choose what to share with %s".printf (asking);
            }
            request = next;
            shell_ui.switcher_select.connect (on_switcher_select);
            shell_ui.unbound.connect (on_unbound);
            show ();
        }

        private static string? app_name (Window window) {
            string? app_id = window.app_id;
            if (app_id != null && app_id != "") {
                foreach (unowned string id in new string[] { app_id, app_id.down () }) {
                    var info = new GLib.DesktopAppInfo (id + ".desktop");
                    if (info != null) {
                        return info.get_name ();
                    }
                }
            }
            return window.title != null && window.title != "" ? window.title : null;
        }

        private Window? window_of (Policy.ShareSource source) {
            if (source.kind != Policy.ShareSource.Kind.WINDOW) {
                return null;
            }
            return window_for_identifier != null ? window_for_identifier (source.key) : null;
        }

        private void highlight () {
            var source = request.sources[request.current];
            if (source.kind == Policy.ShareSource.Kind.MONITOR) {
                foreach (var output in model.get_outputs ()) {
                    if (output.name == source.key) {
                        outline.show (output.rect, OUTLINE_WIDTH);
                        return;
                    }
                }
            } else {
                var window = window_of (source);
                if (window != null && model.showing_on_its_workspace (window)) {
                    outline.show (window.outer_rect (), OUTLINE_WIDTH);
                    return;
                }
            }
            outline.hide ();
        }

        private void show () {
            var shell_ui = server.get_shell_ui ();
            var output = model.output_at (model.pointer_x, model.pointer_y);
            if (output == null && model.get_outputs ().length > 0) {
                output = model.get_outputs ()[0];
            }
            double landscape = MAX_PREVIEW_SIZE;
            double portrait = MAX_PREVIEW_SIZE;
            if (output != null) {
                landscape = double.min (MAX_PREVIEW_SIZE,
                                        MAX_PREVIEW_SCREEN_FRACTION * output.rect.width / request.columns);
                portrait = double.min (MAX_PREVIEW_SIZE,
                                       MAX_PREVIEW_SCREEN_FRACTION * output.rect.height / request.columns);
            }
            for (int i = 0; i < request.sources.length; i++) {
                var source = request.sources[i];
                uint id = i + 1;
                var window = window_of (source);
                var xdg = window != null ? window.adapter as XdgWindow : null;
                if (xdg == null) {
                    shell_ui.switcher_entry (id, source.label, null, 0);
                    if (source.kind == Policy.ShareSource.Kind.MONITOR) {
                        shell_ui.switcher_icon_name (id, MONITOR_ICON);
                    }
                    continue;
                }
                shell_ui.switcher_entry (id, window.title ?? source.label, window.app_id, 0);
                shell_ui.switcher_icon (id, xdg.toplevel);
                unowned Backend.View? view = xdg.toplevel.get_view ();
                if (view != null) {
                    shell_ui.switcher_thumbnail (id, view, landscape, portrait);
                }
            }
            shell_ui.switcher_heading (request.heading);
            shell_ui.show_switcher (output != null ? desktop.backend_output (output) : null, 1, request.columns,
                                    model.prefs.alt_tab_expand_to_fit_title, (uint) model.prefs.icon_size);
            highlight ();
        }

        private void move (int delta) {
            int n = (int) request.sources.length;
            int target = request.current + delta;
            if (delta == 1 || delta == -1) {
                target = (target + n) % n;
            } else if (target < 0 || target >= n) {
                return;
            }
            request.current = target;
            server.get_shell_ui ().update_selected (request.current + 1);
            highlight ();
        }

        public Backend.FilterResult key (Backend.Seat seat, Backend.KeyEvent event) {
            if (request == null) {
                return Backend.FilterResult.PASS;
            }
            if (!event.pressed) {
                return Backend.FilterResult.CONSUME;
            }
            bool shift = Backend.Modifiers.SHIFT in event.modifiers;
            switch (event.keycode) {
                case KEY_ESC:
                    finish (null);
                    break;
                case KEY_ENTER:
                case KEY_KPENTER:
                case KEY_SPACE:
                    finish (request.sources[request.current].line);
                    break;
                case KEY_TAB:
                    move (shift ? -1 : 1);
                    break;
                case KEY_RIGHT:
                    move (1);
                    break;
                case KEY_LEFT:
                    move (-1);
                    break;
                case KEY_DOWN:
                    move ((int) request.columns);
                    break;
                case KEY_UP:
                    move (0 - (int) request.columns);
                    break;
                default:
                    break;
            }
            return Backend.FilterResult.CONSUME;
        }

        public void button (Backend.ButtonEvent event) {
            if (request == null || !event.pressed) {
                return;
            }
            if (event.view != null) {
                unowned Backend.Client? client = event.view.get_client ();
                if (client != null && client == server.get_shell_ui ().get_client ()) {
                    return;
                }
            }
            finish (null);
        }

        private void on_switcher_select (uint id) {
            if (request == null || id < 1 || id > request.sources.length) {
                return;
            }
            finish (request.sources[(int) id - 1].line);
        }

        private void on_unbound () {
            finish (null);
        }

        private void finish (string? line) {
            if (request == null) {
                return;
            }
            string? chosen = line;
            var done = (owned) request.done;
            var shell_ui = server.get_shell_ui ();
            shell_ui.switcher_select.disconnect (on_switcher_select);
            shell_ui.unbound.disconnect (on_unbound);
            shell_ui.hide_switcher ();
            outline.hide ();
            request = null;
            done (chosen);
        }
    }
}
