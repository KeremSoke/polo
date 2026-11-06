/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Magic Lamp: a minimising window funnels into its target (D13) over
 * magic-lamp-duration-ms, and a restored one comes back out of it. A
 * minimise that is restored meanwhile, or the other way round, turns back
 * from where it is, taking as long as it has run so far. Without
 * deformation (Pixman) it is MagicLampFade, a 150 ms fade instead.
 */

namespace Polo.Effects {
    public interface MinimizeEffect : Effect {
        public abstract bool minimizing { get; }
    }

    public class MagicLampFade : GLib.Object, Effect, MinimizeEffect {
        private const double DURATION_MS = 150.0;

        private unowned BackendTarget? live = null;
        private double progress;
        private bool restoring;

        public int priority { get { return 2; } }
        public unowned string settings_key { get { return "magic-lamp"; } }
        public bool deforms { get { return false; } }
        public bool minimizing { get { return !restoring; } }

        public MagicLampFade (bool restore) {
            restoring = restore;
            progress = restore ? 1.0 : 0.0;
        }

        public void start (Target target) {
            live = target as BackendTarget;
        }

        public void update (double dt) {
            progress = Polo.Fx.MagicLamp.advance (progress, restoring, double.max (0.0, dt) * 1000.0, DURATION_MS);
        }

        public bool apply (Target target) {
            if (live == null) {
                return false;
            }
            live.set_alpha (1.0 - progress);
            return true;
        }

        public bool is_done () {
            return live == null || (restoring ? progress <= 0.0 : progress >= 1.0);
        }

        public void cancel () {
            live = null;
        }

        public void reverse () {
            restoring = !restoring;
        }
    }

    public class MagicLampEffect : GLib.Object, Effect, MinimizeEffect {
        private GLib.Settings settings;
        private Polo.Fx.MagicLamp lamp;
        private uint cols;
        private uint rows;
        private bool prepared = false;
        private double progress = 0.0;
        private bool restoring = false;

        public int priority { get { return 2; } }
        public unowned string settings_key { get { return "magic-lamp"; } }
        public bool deforms { get { return true; } }

        public bool minimizing { get { return !restoring; } }

        public MagicLampEffect (GLib.Settings settings) {
            this.settings = settings;
        }

        public bool prepare (BackendTarget target, LampTarget destination, bool restore) {
            Polo.Marco.Rect drawn;
            if (!target.get_surface_rect (out drawn) || drawn.width <= 0 || drawn.height <= 0) {
                prepared = false;
                return false;
            }
            lamp = Polo.Fx.MagicLamp () {
                direction = (Polo.Fx.MagicLampDirection) (int) destination.edge,
                origin = Polo.Fx.Vec2 () { x = drawn.x, y = drawn.y },
                size = Polo.Fx.Vec2 () { x = drawn.width, y = drawn.height },
                target = Polo.Fx.Vec2 () { x = destination.rect.x, y = destination.rect.y },
                target_size = Polo.Fx.Vec2 () { x = destination.rect.width, y = destination.rect.height }
            };
            lamp.grid_size (out cols, out rows);
            restoring = restore;
            progress = restore ? 1.0 : 0.0;
            prepared = target.ensure_grid (cols, rows) && target.prepare_component_grids (cols, rows);
            return prepared;
        }

        public void start (Target target) {
        }

        public void update (double dt) {
            progress = Polo.Fx.MagicLamp.advance (progress, restoring, double.max (0.0, dt) * 1000.0,
                                                  settings.get_uint ("magic-lamp-duration-ms"));
        }

        public bool apply (Target target) {
            if (!prepared || target.grid == null || target.grid.cols != cols || target.grid.rows != rows) {
                return false;
            }
            lamp.progress = progress;
            lamp.fill_grid (target.grid);
            return true;
        }

        public bool is_done () {
            return !prepared || (restoring ? progress <= 0.0 : progress >= 1.0);
        }

        public void cancel () {
            prepared = false;
        }

        public void reverse () {
            restoring = !restoring;
        }
    }
}
