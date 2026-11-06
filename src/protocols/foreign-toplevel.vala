/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwlr_foreign_toplevel_manager_v1: mirrors every mapped
 * Polo.Window into a Backend.ForeignToplevel and routes task-bar requests
 * into the policy. The C shim (lib/backend/protocols/wlr-foreign-toplevel.c)
 * owns the protocol mechanics.
 *
 * Windows with SKIP_TASKBAR are listed too, as Marco lists them in
 * _NET_CLIENT_LIST; filtering is the panel's job. Requests behave as they
 * do for an X11 pager talking to Marco through libwnck: activation is a
 * pager activation, and libwnck switches to the window's workspace first;
 * unminimize is an activation too (wnck_window_unminimize ()).
 */

namespace Polo.Protocols {
    public class ForeignToplevels : GLib.Object {
        private Backend.Server server;
        private Policy.Model model;
        private Shell.Desktop desktop;

        /* What was last sent, so done () goes out only for real changes. */
        private class Entry {
            public Window window;
            public Backend.ForeignToplevel handle;
            public unowned ForeignToplevels owner;
            public string? title;
            public string? app_id;
            public Backend.ForeignToplevelState state;
            public Backend.ForeignToplevelExtraState extra_state;
            public string outputs = "";
            public Window? parent;

            public Entry (Window window, Backend.ForeignToplevel handle, ForeignToplevels owner) {
                this.window = window;
                this.handle = handle;
                this.owner = owner;
            }

            /* Handlers are methods so they can be disconnected. */
            public string? workspace_key = null;

            public void changed () {
                owner.update (this);
            }

            public void on_maximize (bool on) {
                owner.model.request_state (window, WindowState.MAXIMIZED, on);
            }

            public void on_minimize (bool on) {
                if (on) {
                    owner.model.minimize (window);
                } else {
                    owner.activate (window);
                }
            }

            public void on_activate () {
                owner.activate (window);
            }

            public void on_close () {
                owner.model.close (window);
            }

            public void on_fullscreen (bool on, Backend.Output? output) {
                if (on && output != null && window.has_feature (WindowFeatures.FULLSCREEN)) {
                    var destination = owner.desktop.policy_output (output);
                    if (destination != null) {
                        owner.model.move_to_output (window, destination);
                    }
                }
                owner.model.request_state (window, WindowState.FULLSCREEN, on);
            }

            public void on_shade (bool on) {
                owner.model.request_state (window, WindowState.SHADED, on);
            }

            public void on_sticky (bool on) {
                owner.model.set_state (window, WindowState.STICKY, on);
            }

            public void on_above (bool on) {
                if (on) {
                    owner.model.set_state (window, WindowState.BELOW, false);
                }
                owner.model.set_state (window, WindowState.ABOVE, on);
            }

            public void on_below (bool on) {
                if (on) {
                    owner.model.set_state (window, WindowState.ABOVE, false);
                }
                owner.model.set_state (window, WindowState.BELOW, on);
            }

            public void on_move () {
                owner.begin_keyboard (window, Input.GrabOp.KEYBOARD_MOVING);
            }

            public void on_resize () {
                owner.begin_keyboard (window, Input.GrabOp.KEYBOARD_RESIZING_UNKNOWN);
            }

            public void on_move_to_workspace (Backend.Workspace target) {
                owner.move_to_workspace (window, target);
            }

            public void on_move_to_output (Backend.Output output) {
                var destination = owner.desktop.policy_output (output);
                if (destination != null) {
                    owner.model.move_to_output (window, destination);
                }
            }
        }

        private Workspaces? workspaces = null;

        public void set_workspaces (Workspaces? workspaces) {
            if (this.workspaces != null) {
                this.workspaces.changed.disconnect (sync);
            }
            this.workspaces = workspaces;
            if (workspaces != null) {
                workspaces.changed.connect (sync);
                sync ();
            }
        }

        private void begin_keyboard (Window window, Input.GrabOp op) {
            var seat = desktop.default_seat ();
            if (seat != null) {
                desktop.move_resize.begin_keyboard (seat, window, op, model.last_user_time);
            }
        }

        private void move_to_workspace (Window window, Backend.Workspace target) {
            if (workspaces != null) {
                int index = workspaces.index_of (target);
                if (index >= 0) {
                    model.move_to_workspace (window, index);
                }
            }
        }

        private GLib.HashTable<Window, Entry> entries =
            new GLib.HashTable<Window, Entry> (GLib.direct_hash, GLib.direct_equal);

        public ForeignToplevels (Backend.Server server, Policy.Model model, Shell.Desktop desktop) {
            this.server = server;
            this.model = model;
            this.desktop = desktop;
            model.stack_changed.connect (sync);
        }

        public void shutdown () {
            model.stack_changed.disconnect (sync);
            foreach (var entry in entries.get_values ()) {
                forget (entry);
            }
            entries.remove_all ();
        }

        /* The listed windows are the mapped ones, minimized included. */
        private void sync () {
            var mapped = new GLib.HashTable<Window, bool> (GLib.direct_hash, GLib.direct_equal);
            foreach (var window in model.get_stack ()) {
                if (!window.has_state (WindowState.MAPPED)) {
                    continue;
                }
                mapped.insert (window, true);
                if (!entries.contains (window)) {
                    add (window);
                }
            }
            foreach (var window in entries.get_keys ()) {
                if (!mapped.contains (window)) {
                    forget (entries[window]);
                    entries.remove (window);
                }
            }
            /* Parents may have appeared or gone. */
            foreach (var entry in entries.get_values ()) {
                update (entry);
            }
        }

        private void add (Window window) {
            var entry = new Entry (window, new Backend.ForeignToplevel (server), this);
            entries.insert (window, entry);
            var adapter = window.adapter as Shell.XdgWindow;
            if (adapter != null) {
                entry.handle.set_window (adapter.toplevel);
            }
            window.notify["title"].connect (entry.changed);
            window.notify["workspace"].connect (entry.changed);
            window.notify["app-id"].connect (entry.changed);
            window.notify["state"].connect (entry.changed);
            window.notify["transient-for"].connect (entry.changed);
            window.geometry_changed.connect (entry.changed);
            entry.handle.request_maximize.connect (entry.on_maximize);
            entry.handle.request_minimize.connect (entry.on_minimize);
            entry.handle.request_activate.connect (entry.on_activate);
            entry.handle.request_close.connect (entry.on_close);
            entry.handle.request_fullscreen.connect (entry.on_fullscreen);
            entry.handle.request_shade.connect (entry.on_shade);
            entry.handle.request_sticky.connect (entry.on_sticky);
            entry.handle.request_above.connect (entry.on_above);
            entry.handle.request_below.connect (entry.on_below);
            entry.handle.request_move.connect (entry.on_move);
            entry.handle.request_resize.connect (entry.on_resize);
            entry.handle.request_move_to_output.connect (entry.on_move_to_output);
            entry.handle.request_move_to_workspace.connect (entry.on_move_to_workspace);
        }

        private void forget (Entry entry) {
            var window = entry.window;
            window.notify["title"].disconnect (entry.changed);
            window.notify["workspace"].disconnect (entry.changed);
            window.notify["app-id"].disconnect (entry.changed);
            window.notify["state"].disconnect (entry.changed);
            window.notify["transient-for"].disconnect (entry.changed);
            window.geometry_changed.disconnect (entry.changed);
            entry.handle.request_maximize.disconnect (entry.on_maximize);
            entry.handle.request_minimize.disconnect (entry.on_minimize);
            entry.handle.request_activate.disconnect (entry.on_activate);
            entry.handle.request_close.disconnect (entry.on_close);
            entry.handle.request_fullscreen.disconnect (entry.on_fullscreen);
            entry.handle.request_shade.disconnect (entry.on_shade);
            entry.handle.request_sticky.disconnect (entry.on_sticky);
            entry.handle.request_above.disconnect (entry.on_above);
            entry.handle.request_below.disconnect (entry.on_below);
            entry.handle.request_move.disconnect (entry.on_move);
            entry.handle.request_resize.disconnect (entry.on_resize);
            entry.handle.request_move_to_output.disconnect (entry.on_move_to_output);
            entry.handle.close ();
        }

        private static Backend.ForeignToplevelExtraState extra_state_of (Window window) {
            var state = Backend.ForeignToplevelExtraState.NONE;
            if (window.has_state (WindowState.SHADED)) {
                state |= Backend.ForeignToplevelExtraState.SHADED;
            }
            if (window.has_state (WindowState.SKIP_PAGER)) {
                state |= Backend.ForeignToplevelExtraState.SKIP_PAGER;
            }
            if (window.has_state (WindowState.SKIP_TASKBAR)) {
                state |= Backend.ForeignToplevelExtraState.SKIP_TASKLIST;
            }
            if (window.has_state (WindowState.STICKY)) {
                state |= Backend.ForeignToplevelExtraState.STICKY;
            }
            if (window.has_state (WindowState.ABOVE)) {
                state |= Backend.ForeignToplevelExtraState.ABOVE;
            }
            if (window.has_state (WindowState.BELOW)) {
                state |= Backend.ForeignToplevelExtraState.BELOW;
            }
            if (window.has_state (WindowState.DEMANDS_ATTENTION) || window.has_state (WindowState.URGENT)) {
                state |= Backend.ForeignToplevelExtraState.DEMANDS_ATTENTION;
            }
            return state;
        }

        private static Backend.ForeignToplevelState state_of (Window window) {
            var state = Backend.ForeignToplevelState.NONE;
            if (window.has_state (WindowState.MAXIMIZED_H) || window.has_state (WindowState.MAXIMIZED_V)) {
                state |= Backend.ForeignToplevelState.MAXIMIZED;
            }
            if (window.has_state (WindowState.MINIMIZED)) {
                state |= Backend.ForeignToplevelState.MINIMIZED;
            }
            if (window.has_state (WindowState.ACTIVE)) {
                state |= Backend.ForeignToplevelState.ACTIVATED;
            }
            if (window.has_state (WindowState.FULLSCREEN)) {
                state |= Backend.ForeignToplevelState.FULLSCREEN;
            }
            return state;
        }

        /* The outputs the frame overlaps, as backend outputs. */
        private Backend.Output[] outputs_of (Window window, out string key) {
            Backend.Output[] result = {};
            var names = new GLib.StringBuilder ();
            foreach (var output in model.get_outputs ()) {
                var r = output.rect;
                var f = window.frame;
                if (f.x < r.x + r.width && r.x < f.x + f.width &&
                    f.y < r.y + r.height && r.y < f.y + f.height) {
                    var backend_output = desktop.backend_output (output);
                    if (backend_output != null) {
                        result += backend_output;
                        names.append (output.name).append_c (',');
                    }
                }
            }
            key = names.str;
            return result;
        }

        private void update (Entry entry) {
            var window = entry.window;
            var handle = entry.handle;
            bool changed = false;

            if (window.title != entry.title) {
                entry.title = window.title;
                handle.set_title (window.title ?? "");
                changed = true;
            }
            if (window.app_id != entry.app_id) {
                entry.app_id = window.app_id;
                handle.set_app_id (window.app_id ?? "");
                changed = true;
            }
            var extra_state = extra_state_of (window);
            if (extra_state != entry.extra_state) {
                entry.extra_state = extra_state;
                handle.set_extra_state (extra_state);
                changed = true;
            }
            var state = state_of (window);
            if (state != entry.state) {
                entry.state = state;
                handle.set_state (state);
                changed = true;
            }
            string key;
            var outputs = outputs_of (window, out key);
            if (key != entry.outputs) {
                entry.outputs = key;
                handle.set_outputs (outputs);
                changed = true;
            }
            if (workspaces != null) {
                string workspace_key = "%d/%d".printf (window.workspace, model.n_workspaces);
                if (workspace_key != entry.workspace_key) {
                    entry.workspace_key = workspace_key;
                    Backend.Workspace[] on = {};
                    for (int i = 0; i < model.n_workspaces; i++) {
                        unowned Backend.Workspace? workspace = workspaces.handle_of (i);
                        if (workspace != null && (window.workspace == -1 || window.workspace == i)) {
                            on += workspace;
                        }
                    }
                    handle.set_workspaces (on);
                }
            }
            Window? parent = window.transient_for;
            Entry? parent_entry = parent != null ? entries.lookup (parent) : null;
            if (parent_entry == null) {
                parent = null;
            }
            if (parent != entry.parent) {
                entry.parent = parent;
                handle.set_parent (parent_entry != null ? parent_entry.handle : null);
                changed = true;
            }
            if (changed) {
                handle.done ();
            }
        }

        /* A pager activation; libwnck switches workspace first. */
        private void activate (Window window) {
            if (window.workspace != -1 && window.workspace != model.current_workspace) {
                model.switch_workspace (window.workspace);
            }
            model.activate (window, Policy.ActivationSource.PAGER, 0);
        }

        public Window? window_for_identifier (string identifier) {
            foreach (var entry in entries.get_values ()) {
                if (entry.handle.get_identifier () == identifier) {
                    return entry.window;
                }
            }
            return null;
        }

        /* The task-bar button of a window. */
        public bool minimize_target (Window window, out Marco.Rect rect) {
            rect = Marco.Rect ();
            var entry = entries.lookup (window);
            return entry != null &&
                   entry.handle.get_rectangle (out rect.x, out rect.y, out rect.width, out rect.height);
        }
    }
}
