/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_workspace_manager_v1 over the model's workspaces: names, grid
 * coordinates, active and urgent states, and Marco's fixed count: creating
 * raises num-workspaces and only the last, empty workspace can go.
 */

namespace Polo.Protocols {
    public delegate void WorkspacePreference (string schema, string key, GLib.Variant value);

    public class Workspaces : GLib.Object {
        private const uint STATE_ACTIVE = 1;
        private const uint STATE_URGENT = 2;
        private const uint CAPABILITIES = 1 | 4 | 8;
        private const int MAX_WORKSPACES = 36;

        private class Entry {
            public Backend.Workspace handle;
            public int index;
        }

        private Backend.Server server;
        private Policy.Model model;
        private WorkspacePreference write_preference;
        private GLib.GenericArray<Entry> entries = new GLib.GenericArray<Entry> ();
        private uint queued = 0;

        public Workspaces (Backend.Server server, Policy.Model model, owned WorkspacePreference write_preference) {
            this.server = server;
            this.model = model;
            this.write_preference = (owned) write_preference;
            server.get_workspace_manager ().create_workspace.connect (on_create);
            model.notify["n-workspaces"].connect (queue_sync);
            model.notify["current-workspace"].connect (queue_sync);
            model.notify["prefs"].connect (queue_sync);
            model.stack_changed.connect (queue_sync);
            model.focus_changed.connect (queue_sync);
            server.outputs_changed.connect (queue_sync);
            sync ();
        }

        public void shutdown () {
            server.get_workspace_manager ().create_workspace.disconnect (on_create);
            model.notify["n-workspaces"].disconnect (queue_sync);
            model.notify["current-workspace"].disconnect (queue_sync);
            model.notify["prefs"].disconnect (queue_sync);
            model.stack_changed.disconnect (queue_sync);
            model.focus_changed.disconnect (queue_sync);
            server.outputs_changed.disconnect (queue_sync);
            if (queued != 0) {
                GLib.Source.remove (queued);
                queued = 0;
            }
        }

        /* The handle of a workspace, for task bars' workspace_enter. */
        public unowned Backend.Workspace? handle_of (int index) {
            return index >= 0 && index < entries.length ? entries[index].handle : null;
        }

        public int index_of (Backend.Workspace handle) {
            foreach (var entry in entries) {
                if (entry.handle == handle) {
                    return entry.index;
                }
            }
            return -1;
        }

        private void queue_sync () {
            if (queued == 0) {
                queued = GLib.Idle.add (() => {
                    queued = 0;
                    sync ();
                    return GLib.Source.REMOVE;
                });
            }
        }

        private uint state_of (int index) {
            uint state = index == model.current_workspace ? STATE_ACTIVE : 0;
            foreach (var window in model.get_windows ()) {
                if ((window.workspace == index || window.workspace == -1) &&
                    (window.has_state (WindowState.URGENT) || window.has_state (WindowState.DEMANDS_ATTENTION))) {
                    state |= STATE_URGENT;
                    break;
                }
            }
            return state;
        }

        private void sync () {
            var manager = server.get_workspace_manager ();
            var layout = model.workspace_layout ();
            while (entries.length > model.n_workspaces) {
                var last = entries[entries.length - 1];
                last.handle.activate.disconnect (on_activate);
                last.handle.remove.disconnect (on_remove);
                last.handle.remove_workspace ();
                entries.remove_index (entries.length - 1);
            }
            for (int index = 0; index < model.n_workspaces; index++) {
                uint col = 0;
                uint row = 0;
                for (int cell = 0; cell < layout.grid.length; cell++) {
                    if (layout.grid[cell] == index) {
                        col = (uint) (cell % layout.cols);
                        row = (uint) (cell / layout.cols);
                    }
                }
                if (index >= entries.length) {
                    var entry = new Entry () {
                        index = index,
                        handle = manager.add ("polo-ws-%d".printf (index + 1), model.workspace_name (index),
                                              col, row, state_of (index), CAPABILITIES)
                    };
                    entry.handle.activate.connect (on_activate);
                    entry.handle.remove.connect (on_remove);
                    entries.add (entry);
                } else {
                    var handle = entries[index].handle;
                    handle.set_name (model.workspace_name (index));
                    handle.set_coordinates (col, row);
                    handle.set_state (state_of (index));
                }
            }
            changed ();
            manager.done ();
        }

        /* Handles may have come or gone; task bars look them up again. */
        public signal void changed ();

        private void on_activate (Backend.Workspace handle) {
            model.switch_workspace (index_of (handle));
        }

        /* Marco's count is fixed: only the last one, if nothing is on it. */
        private void on_remove (Backend.Workspace handle) {
            int index = index_of (handle);
            if (index != model.n_workspaces - 1 || model.n_workspaces == 1) {
                return;
            }
            foreach (var window in model.get_windows ()) {
                if (window.workspace == index) {
                    return;
                }
            }
            write_preference ("org.mate.Marco.general", "num-workspaces",
                              new GLib.Variant.int32 (model.n_workspaces - 1));
        }

        private void on_create (string name) {
            if (model.n_workspaces >= MAX_WORKSPACES) {
                return;
            }
            int count = model.n_workspaces + 1;
            if (name != "") {
                write_preference ("org.mate.Marco.workspace-names", "name-%d".printf (count),
                                  new GLib.Variant.string (name));
            }
            write_preference ("org.mate.Marco.general", "num-workspaces", new GLib.Variant.int32 (count));
        }
    }
}
