/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The zwlr_layer_shell_v1 arranger: everything the protocol
 * leaves to the compositor — anchor placement, exclusive-zone struts,
 * keyboard focus policy, output assignment. The C shim
 * (lib/backend/protocols/layer-shell.c) owns the protocol mechanics:
 * double-buffered state, configure serials and acks, validation errors and
 * the get_popup plumbing.
 */

namespace Polo.Shell {
    public class LayerShell : GLib.Object {
        /* zwlr_layer_surface_v1.anchor bits. */
        private const uint ANCHOR_TOP = 1;
        private const uint ANCHOR_BOTTOM = 2;
        private const uint ANCHOR_LEFT = 4;
        private const uint ANCHOR_RIGHT = 8;

        private const uint KBD_EXCLUSIVE = 1;
        private const uint KBD_ON_DEMAND = 2;

        private Backend.Server server;
        private Policy.Model model;
        private Desktop desktop;

        /* Everything known about one live layer surface. Its handlers are
         * methods (not delegate fields) so that signal connect/disconnect
         * compiles without warnings. */
        private class Entry {
            public Backend.LayerSurface handle;
            public unowned LayerShell shell;
            public Policy.Output? output;
            public bool closed;
            public bool announced;
            public bool was_mapped;

            public unowned Backend.View view {
                get { return handle.get_view (); }
            }

            public Entry (Backend.LayerSurface handle, LayerShell shell) {
                this.handle = handle;
                this.shell = shell;
            }

            public void commit_handler () {
                shell.on_committed (this);
            }

            public void destroy_handler () {
                shell.on_destroyed (this);
            }
        }

        private GLib.GenericArray<Entry> entries = new GLib.GenericArray<Entry> ();
        /* Struts of mapped surfaces with an exclusive zone, rebuilt by
         * arrange (). They are per-output struts (Marco
         * _GNOME_WM_STRUT_AREA), on every workspace. */
        private GLib.GenericArray<Marco.Strut?> struts = new GLib.GenericArray<Marco.Strut?> ();

        public LayerShell (Backend.Server server, Policy.Model model, Desktop desktop) {
            this.server = server;
            this.model = model;
            this.desktop = desktop;
            /* In the body: construct blocks run before these assignments. */
            server.new_layer_surface.connect (on_new_layer_surface);
            model.focus_changed.connect (on_focus_changed);
        }

        public void shutdown () {
            server.new_layer_surface.disconnect (on_new_layer_surface);
            model.focus_changed.disconnect (on_focus_changed);
            foreach (var entry in entries) {
                entry.handle.committed.disconnect (entry.commit_handler);
                entry.handle.destroyed.disconnect (entry.destroy_handler);
            }
            entries.remove_range (0, entries.length);
            struts.remove_range (0, struts.length);
        }

        private void on_new_layer_surface (Backend.LayerSurface handle) {
            var entry = new Entry (handle, this);
            handle.committed.connect (entry.commit_handler);
            handle.destroyed.connect (entry.destroy_handler);
            entries.add (entry);
            Logging.scope (LogDomain.PROTOCOL).debug (
                "layer surface \"%s\" (layer %u)", handle.get_namespace (),
                handle.layer);
        }

        /* ---- Output assignment ------------------------------------------ */

        private Policy.Output? policy_output (Backend.Output? backend_output) {
            if (backend_output == null) {
                return null;
            }
            return desktop.policy_output (backend_output);
        }

        private Policy.Output? assign_output (Entry entry) {
            Backend.Output? requested = entry.handle.requested_output;
            Backend.Output? backend_output = requested;
            var output = policy_output (backend_output);
            if (output == null && model.get_outputs ().length > 0) {
                /* Default: the first (primary) output, as with Marco. */
                output = model.get_outputs ()[0];
                backend_output = desktop.backend_output (output);
            }
            if (output == null) {
                return null;
            }
            entry.output = output;
            entry.handle.set_output (backend_output);
            return output;
        }

        /* Surfaces on an output that went away are told to close (they
         * are not moved); their clients destroy them and on_destroyed
         * cleans up. */
        private void close_orphans () {
            var outputs = model.get_outputs ();
            foreach (var entry in entries) {
                if (entry.output == null) {
                    continue;
                }
                bool found = false;
                for (int i = 0; i < outputs.length && !found; i++) {
                    found = outputs[i] == entry.output;
                }
                if (!found) {
                    entry.output = null;
                    entry.handle.set_output (null);
                    entry.closed = true;
                    entry.handle.close ();
                }
            }
        }

        /* ---- Arranging ---------------------------------------------------- */

        private void on_committed (Entry entry) {
            if (entry.closed) {
                return;
            }
            if (entry.output == null && assign_output (entry) == null) {
                return; /* No outputs yet; the next commit retries. */
            }

            update_focus (entry);
            arrange ();
            if (!entry.announced && entry.handle.is_mapped ()) {
                entry.announced = true;
                unowned Backend.Client? client = entry.view.get_client ();
                if (client != null) {
                    desktop.client_mapped (client.get_pid ());
                }
            }
        }

        private void on_destroyed (Entry entry) {
            entries.remove (entry);
            if (focus_holder == entry) {
                focus_holder = null;
                hand_back_focus ();
            }
            arrange ();
        }

        public void arrange () {
            close_orphans ();
            struts.remove_range (0, struts.length);
            foreach (var output in model.get_outputs ()) {
                var usable = output.rect;
                for (int layer = 3; layer >= 0; layer--) {
                    foreach (var entry in entries) {
                        if (entry.output == output && entry.handle.layer == layer &&
                            entry.handle.exclusive_zone > 0) {
                            place (entry, ref usable, output);
                        }
                    }
                }
                foreach (var entry in entries) {
                    if (entry.output == output && entry.handle.exclusive_zone == 0) {
                        place (entry, ref usable, output);
                    }
                }
                foreach (var entry in entries) {
                    if (entry.output == output && entry.handle.exclusive_zone < 0) {
                        var full = output.rect;
                        place (entry, ref full, output);
                    }
                }
            }
            model.invalidate_work_areas ();
            arranged ();
        }

        /* After every layout pass, including output changes. */
        public signal void arranged ();

        /*
         * Whether a client paints this output's background: a mapped
         * background-layer surface anchored to all four edges, or a caja
         * desktop (bottom layer, namespace "desktop", which draws its own
         * background). The wallpaper fallback hides then.
         */
        public bool panel_at (int x, int y, out uint anchor) {
            anchor = 0;
            int best_layer = -1;
            foreach (var entry in entries) {
                var handle = entry.handle;
                if (!handle.mapped || handle.layer == 0 || (int) handle.layer <= best_layer ||
                    handle.get_namespace () == "desktop") {
                    continue;
                }
                double px, py;
                int width, height;
                entry.view.get_position (out px, out py);
                handle.get_size (out width, out height);
                if (x >= (int) px && x < (int) px + width && y >= (int) py && y < (int) py + height) {
                    anchor = handle.anchor;
                    best_layer = (int) handle.layer;
                }
            }
            return best_layer >= 0;
        }

        public bool covers_output (Policy.Output output) {
            foreach (var entry in entries) {
                var handle = entry.handle;
                if (entry.output != output || !handle.mapped) {
                    continue;
                }
                if (handle.layer == 0 && handle.anchor == (ANCHOR_TOP | ANCHOR_BOTTOM |
                                                           ANCHOR_LEFT | ANCHOR_RIGHT)) {
                    return true;
                }
                if (handle.layer == 1 && handle.get_namespace () == "desktop") {
                    return true;
                }
            }
            return false;
        }

        /* Configures and positions one surface inside bounds; a mapped
         * surface with an exclusive zone then reserves its strip. */
        private void place (Entry entry, ref Marco.Rect bounds, Policy.Output output) {
            var handle = entry.handle;
            uint anchor = handle.anchor;

            /* A dimension anchored to both opposite edges spans the bounds
             * minus the margins; otherwise the client's size stands (0 is
             * only allowed for spanning dimensions). */
            uint width = handle.desired_width;
            uint height = handle.desired_height;
            if ((anchor & (ANCHOR_LEFT | ANCHOR_RIGHT)) == (ANCHOR_LEFT | ANCHOR_RIGHT)) {
                width = (uint) int.max (0, bounds.width - handle.margin_left - handle.margin_right);
            }
            if ((anchor & (ANCHOR_TOP | ANCHOR_BOTTOM)) == (ANCHOR_TOP | ANCHOR_BOTTOM)) {
                height = (uint) int.max (0, bounds.height - handle.margin_top - handle.margin_bottom);
            }
            handle.configure (width, height);

            unowned Backend.View? view = entry.view;
            if (!handle.mapped || view == null) {
                return;
            }

            int x, y, surface_width, surface_height;
            handle.get_size (out surface_width, out surface_height);

            bool left = (anchor & ANCHOR_LEFT) != 0, right = (anchor & ANCHOR_RIGHT) != 0;
            if (left && !right) {
                x = bounds.x + handle.margin_left;
            } else if (right && !left) {
                x = bounds.x + bounds.width - surface_width - handle.margin_right;
            } else if (left && right) {
                x = bounds.x + handle.margin_left +
                    (bounds.width - handle.margin_left - handle.margin_right - surface_width) / 2;
            } else {
                x = bounds.x + (bounds.width - surface_width) / 2;
            }
            bool top = (anchor & ANCHOR_TOP) != 0, bottom = (anchor & ANCHOR_BOTTOM) != 0;
            if (top && !bottom) {
                y = bounds.y + handle.margin_top;
            } else if (bottom && !top) {
                y = bounds.y + bounds.height - surface_height - handle.margin_bottom;
            } else if (top && bottom) {
                y = bounds.y + handle.margin_top +
                    (bounds.height - handle.margin_top - handle.margin_bottom - surface_height) / 2;
            } else {
                y = bounds.y + (bounds.height - surface_height) / 2;
            }

            view.set_position (x, y);
            view.move_to_layer (server.get_layer (backend_layer (handle.layer)));

            if (handle.exclusive_zone > 0) {
                reserve (entry, ref bounds, output);
            }
        }

        /* The edge an exclusive zone applies to: the v5 exclusive_edge, or
         * the one edge implied by the anchor (a single edge, or an edge
         * plus both perpendicular ones). Corners without exclusive_edge
         * reserve nothing. */
        private static uint exclusive_edge (Backend.LayerSurface handle) {
            if (handle.exclusive_edge != 0) {
                return handle.exclusive_edge;
            }
            switch (handle.anchor) {
                case ANCHOR_TOP:
                case ANCHOR_TOP | ANCHOR_LEFT | ANCHOR_RIGHT:
                    return ANCHOR_TOP;
                case ANCHOR_BOTTOM:
                case ANCHOR_BOTTOM | ANCHOR_LEFT | ANCHOR_RIGHT:
                    return ANCHOR_BOTTOM;
                case ANCHOR_LEFT:
                case ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_BOTTOM:
                    return ANCHOR_LEFT;
                case ANCHOR_RIGHT:
                case ANCHOR_RIGHT | ANCHOR_TOP | ANCHOR_BOTTOM:
                    return ANCHOR_RIGHT;
                default:
                    return 0;
            }
        }

        /* Shrinks bounds by the zone plus the margin on its edge and
         * records a strut from the output edge to the end of the reserved
         * strip, so stacked panels add up in Marco's work area. */
        private void reserve (Entry entry, ref Marco.Rect bounds, Policy.Output output) {
            var handle = entry.handle;
            var full = output.rect;
            var strut = Marco.Strut ();
            strut.edge = Marco.EdgeType.XINERAMA;
            var r = bounds;
            int thickness;

            switch (exclusive_edge (handle)) {
                case ANCHOR_TOP:
                    thickness = int.min (handle.exclusive_zone + handle.margin_top, bounds.height);
                    strut.side = Marco.Side.TOP;
                    r.y = full.y;
                    r.height = bounds.y - full.y + thickness;
                    bounds.y += thickness;
                    bounds.height -= thickness;
                    break;
                case ANCHOR_BOTTOM:
                    thickness = int.min (handle.exclusive_zone + handle.margin_bottom, bounds.height);
                    strut.side = Marco.Side.BOTTOM;
                    r.y = bounds.y + bounds.height - thickness;
                    r.height = full.y + full.height - r.y;
                    bounds.height -= thickness;
                    break;
                case ANCHOR_LEFT:
                    thickness = int.min (handle.exclusive_zone + handle.margin_left, bounds.width);
                    strut.side = Marco.Side.LEFT;
                    r.x = full.x;
                    r.width = bounds.x - full.x + thickness;
                    bounds.x += thickness;
                    bounds.width -= thickness;
                    break;
                case ANCHOR_RIGHT:
                    thickness = int.min (handle.exclusive_zone + handle.margin_right, bounds.width);
                    strut.side = Marco.Side.RIGHT;
                    r.x = bounds.x + bounds.width - thickness;
                    r.width = full.x + full.width - r.x;
                    bounds.width -= thickness;
                    break;
                default:
                    return;
            }
            strut.rect = r;

            struts.add (strut);
        }

        /* Layer-shell layer -> compositor layer. */
        private static Backend.LayerKind backend_layer (uint layer) {
            switch (layer) {
                case 0: /* background */
                    return Backend.LayerKind.BACKGROUND;
                case 1: /* bottom */
                    return Backend.LayerKind.BOTTOM;
                case 3: /* overlay */
                    return Backend.LayerKind.OVERLAY;
                default: /* top (and anything else) */
                    return Backend.LayerKind.DOCK;
            }
        }

        /* Policy.Model.LayerStruts. */
        public GLib.SList<Marco.Strut?> all_struts () {
            var list = new GLib.SList<Marco.Strut?> ();
            foreach (var strut in struts) {
                list.prepend (strut);
            }
            return list;
        }

        /* ---- Keyboard ---------------------------------------------------- */

        /*
         * An exclusive surface in the top or overlay layer takes the
         * keyboard when it maps and keeps it while mapped
         * (Desktop.set_keyboard_focus refuses everything else). An
         * on-demand surface takes it when it maps in the top or overlay
         * layer, as Wayfire does, or when clicked, and gives it back on a
         * press anywhere outside it and its popups: a popup cannot see
         * clicks on other clients, so wl_keyboard.leave is how it learns
         * to close. While a surface holds the keyboard no window is
         * focused, as when Marco focuses a desktop or dock window.
         * Afterwards the previously focused window gets it back.
         */
        private Entry? focus_holder = null;
        private Window? previous_focus = null;

        private bool locked () {
            return server.get_lock_state () != Backend.LockState.UNLOCKED;
        }

        private void take_focus (Entry entry) {
            if (focus_holder == null) {
                previous_focus = model.focused;
            }
            focus_holder = entry;
            model.focus (null);
            desktop.set_keyboard_focus (entry.view);
        }

        public unowned Backend.View? exclusive_view () {
            if (focus_holder != null &&
                focus_holder.handle.keyboard_interactivity == KBD_EXCLUSIVE) {
                return focus_holder.view;
            }
            return null;
        }

        private static bool can_hold_exclusive (Backend.LayerSurface handle) {
            return handle.mapped && handle.keyboard_interactivity == KBD_EXCLUSIVE &&
                   handle.layer >= 2; /* top or overlay */
        }

        private void update_focus (Entry entry) {
            var handle = entry.handle;
            bool just_mapped = handle.mapped && !entry.was_mapped;
            entry.was_mapped = handle.mapped;
            if (focus_holder == entry) {
                bool keep = handle.mapped &&
                    (handle.keyboard_interactivity == KBD_ON_DEMAND || can_hold_exclusive (handle));
                if (!keep) {
                    focus_holder = null;
                    hand_back_focus ();
                }
            } else if (can_hold_exclusive (handle) && exclusive_view () == null) {
                take_focus (entry);
            } else if (just_mapped && handle.keyboard_interactivity == KBD_ON_DEMAND && handle.layer >= 2 &&
                       exclusive_view () == null && !locked ()) {
                take_focus (entry);
            }
        }

        private void hand_back_focus () {
            /* Another exclusive surface may be waiting. */
            foreach (var entry in entries) {
                if (can_hold_exclusive (entry.handle)) {
                    take_focus (entry);
                    return;
                }
            }
            var window = previous_focus;
            previous_focus = null;
            if (window != null && model.is_managed (window) && model.is_showing (window)) {
                model.focus (window);
            } else {
                model.focus_default ();
            }
            desktop.restore_focus ();
        }

        private Entry? owner (Backend.View view) {
            foreach (var entry in entries) {
                if (entry.handle.owns_view (view)) {
                    return entry;
                }
            }
            return null;
        }

        /* A press on a view that is not a window, or on nothing. */
        public void click (Backend.View? view) {
            if (exclusive_view () != null) {
                return;
            }
            var entry = view != null ? owner (view) : null;
            if (entry != null && entry == focus_holder) {
                return;
            }
            if (entry != null && entry.handle.keyboard_interactivity == KBD_ON_DEMAND && entry.handle.mapped) {
                take_focus (entry);
            } else if (focus_holder != null) {
                focus_holder = null;
                hand_back_focus ();
            }
        }

        /* A click on a window: an on-demand surface lets go. */
        public void window_clicked () {
            if (focus_holder != null && exclusive_view () == null) {
                /* The policy focuses the clicked window next. */
                focus_holder = null;
                previous_focus = null;
                desktop.restore_focus ();
            }
        }

        private void on_focus_changed () {
            if (model.focused != null && focus_holder != null && exclusive_view () == null) {
                focus_holder = null;
                previous_focus = null;
            }
        }
    }
}
