/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wayfire's animate plugin on Polo's effect engine: fade, zoom, zap and spin
 * when a window opens or closes, zoom and squeezimize when it minimises.
 * The open, close and zoom animations are a whole-window transform about
 * the frame's centre plus alpha (Policy.WindowAnimation), so they work on
 * Pixman too; squeezimize bends the window with G1 as Magic Lamp does. A
 * closing window animates as ghosts, views that keep its surfaces' last
 * content after the client has destroyed them, with its frame and shadow
 * handed over until the end. Pixman drops a surface's image as soon as
 * its client destroys the buffer, so closing animates on GL only.
 */

namespace Polo.Effects {
    public interface FrameTarget : GLib.Object {
        public abstract void show_frame (Policy.AnimationFrame frame);
    }

    public class GhostTarget : Target, FrameTarget {
        private Window held;
        private GLib.GenericArray<Backend.View> ghosts = new GLib.GenericArray<Backend.View> ();
        private GLib.GenericArray<Backend.View> painted = new GLib.GenericArray<Backend.View> ();
        public Marco.Rect frame;
        public Backend.LayerKind layer;

        public GhostTarget (Window window, Marco.Rect frame, Backend.LayerKind layer) {
            Object (window: window);
            held = window;
            this.frame = frame;
            this.layer = layer;
        }

        public delegate void Release ();

        private Release? release = null;

        public bool add (Backend.View source) {
            var ghost = source.ghost ();
            if (ghost == null) {
                return false;
            }
            ghosts.add (ghost);
            foreach (var view in ghost.mirror_views ()) {
                painted.add (view);
            }
            return true;
        }

        public void adopt (Backend.View view) {
            ghosts.add (view);
            painted.add (view);
        }

        public void on_release (owned Release release) {
            this.release = (owned) release;
        }

        public bool empty { get { return ghosts.length == 0; } }

        public unowned GLib.GenericArray<Backend.View> views { get { return ghosts; } }

        public void show_frame (Policy.AnimationFrame f) {
            double cx = frame.x + frame.width / 2.0, cy = frame.y + frame.height / 2.0;
            foreach (var ghost in ghosts) {
                ghost.set_effect_transform (cx, cy, f.scale_x, f.scale_y, f.angle, f.dx, f.dy);
            }
            foreach (var view in painted) {
                view.set_alpha ((float) f.alpha.clamp (0.0, 1.0));
            }
        }

        public override bool deform () {
            return false;
        }

        public override void undeform () {
        }

        protected override void dispose () {
            foreach (var ghost in ghosts) {
                ghost.clear_effect_transform ();
                ghost.move_to_layer (null);
            }
            ghosts.remove_range (0, ghosts.length);
            painted.remove_range (0, painted.length);
            if (release != null) {
                release ();
                release = null;
            }
            base.dispose ();
        }
    }

    public class WindowAnimationEffect : GLib.Object, Effect {
        protected Policy.WindowAnimation animation;
        private unowned FrameTarget? live = null;

        public int priority { get { return 2; } }
        public unowned string settings_key { get { return ""; } }
        public bool deforms { get { return false; } }
        public double shown { get { return animation.shown; } }

        public WindowAnimationEffect (Policy.WindowAnimation animation) {
            this.animation = animation;
        }

        public void start (Target target) {
            live = target as FrameTarget;
        }

        public void update (double dt) {
            animation.advance (dt * 1000.0);
        }

        public bool apply (Target target) {
            if (live == null) {
                return false;
            }
            live.show_frame (animation.frame ());
            return true;
        }

        public bool is_done () {
            return live == null || animation.done;
        }

        public void cancel () {
            live = null;
        }

        public void reverse () {
            animation.reverse ();
        }
    }

    public class ZoomMinimize : WindowAnimationEffect, MinimizeEffect {
        public bool minimizing { get { return !animation.showing; } }

        public ZoomMinimize (Policy.WindowAnimation animation) {
            base (animation);
        }
    }

    public class SqueezimizeEffect : GLib.Object, Effect, MinimizeEffect {
        private Polo.Fx.Squeezimize squeeze;
        private double duration_ms;
        private uint cols;
        private uint rows;
        private bool prepared = false;
        private double progress = 0.0;
        private bool restoring = false;

        public int priority { get { return 2; } }
        public unowned string settings_key { get { return ""; } }
        public bool deforms { get { return true; } }
        public bool minimizing { get { return !restoring; } }

        public SqueezimizeEffect (double duration_ms) {
            this.duration_ms = duration_ms;
        }

        public bool prepare (BackendTarget target, LampTarget destination, bool restore) {
            Polo.Marco.Rect drawn;
            if (!target.get_surface_rect (out drawn) || drawn.width <= 0 || drawn.height <= 0) {
                prepared = false;
                return false;
            }
            squeeze = Polo.Fx.Squeezimize () {
                direction = (Polo.Fx.MagicLampDirection) (int) destination.edge,
                origin = Polo.Fx.Vec2 () { x = drawn.x, y = drawn.y },
                size = Polo.Fx.Vec2 () { x = drawn.width, y = drawn.height },
                target = Polo.Fx.Vec2 () { x = destination.rect.x, y = destination.rect.y },
                target_size = Polo.Fx.Vec2 () { x = destination.rect.width, y = destination.rect.height }
            };
            squeeze.grid_size (out cols, out rows);
            restoring = restore;
            progress = restore ? 1.0 : 0.0;
            prepared = target.ensure_grid (cols, rows) && target.prepare_component_grids (cols, rows);
            return prepared;
        }

        public void start (Target target) {
        }

        public void update (double dt) {
            progress = Polo.Fx.Squeezimize.advance (progress, restoring, dt * 1000.0, duration_ms);
        }

        public bool apply (Target target) {
            if (!prepared || target.grid == null || target.grid.cols != cols || target.grid.rows != rows) {
                return false;
            }
            squeeze.progress = progress;
            squeeze.fill_grid (target.grid);
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
