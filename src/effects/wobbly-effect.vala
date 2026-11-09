/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Effects {
    public class WobblyEffect : GLib.Object, Effect {
        private GLib.Settings settings;
        private Polo.Fx.Wobbly simulation = new Polo.Fx.Wobbly ();
        private Polo.Fx.Origin start_origin;
        private Polo.Fx.Size start_size;
        private Polo.Fx.Origin origin;
        private Polo.Fx.Size size;
        private Polo.Fx.Size grid_extent;
        private uint cols;
        private uint rows;
        private double anchor_u = 0.5;
        private double anchor_v = 0.0;
        private bool prepared = false;
        private bool active = false;
        private unowned BackendTarget? live = null;
        private double hold = 0.0;
        private bool grabbed = false;
        private Polo.Fx.Origin rest_origin;
        private Polo.Fx.Size rest_size;
        private double budget = 0.0;
        private double budget_elapsed = 0.0;
        private double budget_pending = 0.0;

        public int priority { get { return 1; } }
        public unowned string settings_key { get { return "wobbly-windows"; } }
        public bool deforms { get { return true; } }

        public WobblyEffect (GLib.Settings settings) {
            this.settings = settings;
        }

        public bool prepare (Target target, Polo.Marco.Rect old_rect,
                             Polo.Marco.Rect new_rect, bool resizing,
                             double grab_u = 0.5, double grab_v = 0.0) {
            if (resizing ? !settings.get_boolean ("wobbly-resize") :
                           !settings.get_boolean ("wobbly-move") ||
                old_rect.width <= 0 || old_rect.height <= 0 || new_rect.width <= 0 || new_rect.height <= 0) {
                prepared = false;
                return false;
            }
            start_origin = { old_rect.x, old_rect.y };
            start_size = { old_rect.width, old_rect.height };
            grid_extent = {
                int.max (old_rect.width, new_rect.width),
                int.max (old_rect.height, new_rect.height)
            };
            anchor_u = grab_u;
            anchor_v = grab_v;
            set_target (new_rect);

            uint spacing = uint.max (1, settings.get_uint ("wobbly-mesh-spacing"));
            do {
                cols = (uint) (grid_extent.width + (int) spacing - 1) / spacing + 1;
                rows = (uint) (grid_extent.height + (int) spacing - 1) / spacing + 1;
                spacing++;
            } while ((uint64) cols * rows > Polo.Fx.GRID_MAX_VERTICES);
            prepared = target.ensure_grid (cols, rows);
            var backend_target = target as BackendTarget;
            if (prepared && backend_target != null) {
                prepared = backend_target.prepare_component_grids (cols, rows);
            }
            return prepared;
        }

        public void set_target (Polo.Marco.Rect rect) {
            origin = { rect.x, rect.y };
            size = { rect.width, rect.height };
        }

        private Polo.Fx.WobblyParameters parameters () {
            Polo.Fx.WobblyParameters value =
                Polo.Fx.wobbly_parameters_for_level (settings.get_int ("wobbly-level"));
            double stiffness = settings.get_double ("wobbly-stiffness");
            double drag = settings.get_double ("wobbly-drag");
            double move_factor = settings.get_double ("wobbly-move-factor");
            if (stiffness >= 0.0) value.stiffness = stiffness;
            if (drag >= 0.0) value.drag = drag;
            if (move_factor >= 0.0) value.move_factor = move_factor;
            value.min_velocity = settings.get_double ("wobbly-min-velocity");
            value.max_velocity = settings.get_double ("wobbly-max-velocity");
            value.stop_velocity = settings.get_double ("wobbly-stop-velocity");
            value.min_acceleration = settings.get_double ("wobbly-min-acceleration");
            value.max_acceleration = settings.get_double ("wobbly-max-acceleration");
            value.stop_acceleration = settings.get_double ("wobbly-stop-acceleration");
            value.x_tessellation = (int) cols;
            value.y_tessellation = (int) rows;
            return value;
        }

        public void hold_for (double seconds) {
            hold = double.max (hold, seconds);
        }

        public void grab () {
            grabbed = true;
        }

        public void release () {
            grabbed = false;
            if (budget_pending > 0.0) {
                start_budget (budget_pending);
                budget_pending = 0.0;
            }
        }

        public void limit_to (double seconds) {
            hold_for (0.5);
            if (grabbed) {
                budget_pending = seconds;
            } else {
                start_budget (seconds);
            }
        }

        private void start_budget (double seconds) {
            budget = seconds;
            budget_elapsed = 0.0;
        }

        public void start (Target target) {
            active = false;
            live = prepared ? target as BackendTarget : null;
            rest_origin = start_origin;
            rest_size = start_size;
        }

        public void update (double dt) {
            Polo.Marco.Rect rect = {};
            if (live == null || !live.get_surface_rect (out rect)) {
                active = false;
                hold = 0.0;
                grabbed = false;
                return;
            }
            set_target (rect);
            bool changed = origin.x != rest_origin.x || origin.y != rest_origin.y ||
                           size.width != rest_size.width || size.height != rest_size.height;
            if (!active && changed && (grabbed || hold > 0.0)) {
                simulation.start (rest_origin, rest_size, parameters (), anchor_u, anchor_v);
                /* A tile or maximize, even one a drag ends in, leaves a ripple
                 * that would creep out for seconds. */
                simulation.set_ease_out (true);
                active = true;
            }
            if (active) {
                active = simulation.advance (origin, size, double.max (0.0, dt) * 1000.0);
            }
            rest_origin = origin;
            rest_size = size;
            if (!active && hold > 0.0) {
                hold -= double.max (0.0, dt);
            }
            if (budget > 0.0) {
                budget_elapsed += double.max (0.0, dt);
                simulation.set_fade (budget_elapsed * 1000.0, budget * 1000.0);
                if (budget_elapsed >= budget) {
                    active = false;
                    hold = 0.0;
                    budget = 0.0;
                    simulation.set_fade (0.0, 0.0);
                }
            }
        }

        public bool apply (Target target) {
            if (!active || target.grid == null || target.grid.cols != cols ||
                target.grid.rows != rows) {
                return false;
            }
            simulation.fill_grid (target.grid);
            return true;
        }

        public bool is_done () {
            return !active && !grabbed && hold <= 0.0;
        }

        public void cancel () {
            active = false;
            prepared = false;
            live = null;
        }

        public void reverse () {
            /* Wobbly is a settling spring and has no separate reverse action. */
        }
    }
}
