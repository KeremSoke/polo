/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The state of Wayfire's cube (plugins/cube/cube.cpp and cube.hpp, commit
 * e268d4e5): five timed transitions sharing one duration, eased by
 * wf-config's default "circle", and the operations that retarget them.
 * Time is given in milliseconds through tick ().
 *
 * Copyright (c) 2018 Ilia Bozhinov and the Wayfire contributors
 * SPDX-License-Identifier: MIT
 */

namespace Polo.Policy {
    public class CubeTransition {
        public double start = 0.0;
        public double end = 0.0;

        private unowned CubeAnimation animation;

        public CubeTransition (CubeAnimation animation) {
            this.animation = animation;
        }

        public double value {
            get {
                double alpha = animation.progress ();
                return (1 - alpha) * start + alpha * end;
            }
        }

        public void assign (double start, double end) {
            this.start = start;
            this.end = end;
        }

        public void restart_with_end (double end) {
            start = value;
            this.end = end;
        }
    }

    public class CubeAnimation {
        public const double ZOOM_MIN = 0.1;
        public const double ZOOM_MAX = 10.0;
        public const double OFFSET_Y_MAX = 1.5;
        private const double FOVY = 45.0;

        public CubeTransition offset_y;
        public CubeTransition offset_z;
        public CubeTransition rotation;
        public CubeTransition zoom;
        public CubeTransition ease;

        public double duration_ms;
        public int sides { get; private set; default = 0; }
        public double side_angle { get; private set; default = 0.0; }
        public bool active { get; private set; default = false; }
        public bool in_exit { get; private set; default = false; }

        private double now_ms = 0.0;
        private double start_ms = 0.0;

        public CubeAnimation (double duration_ms) {
            this.duration_ms = duration_ms;
            offset_y = new CubeTransition (this);
            offset_z = new CubeTransition (this);
            rotation = new CubeTransition (this);
            zoom = new CubeTransition (this);
            ease = new CubeTransition (this);
            zoom.assign (1, 1);
            start_ms = -double.max (duration_ms, 1);
        }

        public static double rest_offset_z (int sides) {
            double identity = sides > 1 ? 0.5 / Math.tan (Math.PI / sides) : 0.0;
            return identity + 0.5 / Math.tan (FOVY / 2);
        }

        public static int shortest_turn (int from, int to, int sides) {
            if (sides < 2) {
                return 0;
            }
            int steps = ((to - from) % sides + sides) % sides;
            return steps * 2 > sides ? steps - sides : steps;
        }

        public void tick (double now_ms) {
            this.now_ms = now_ms;
        }

        public double progress () {
            double length = double.max (duration_ms, 1);
            double x = ((now_ms - start_ms) / length).clamp (0.0, 1.0);
            return Math.sqrt (2 * x - x * x);
        }

        public bool running () {
            return now_ms - start_ms < double.max (duration_ms, 1);
        }

        private void start () {
            start_ms = now_ms;
        }

        public bool activate (int sides) {
            if (active) {
                return true;
            }
            if (sides < 2) {
                return false;
            }
            this.sides = sides;
            side_angle = 2 * Math.PI / sides;
            active = true;
            double rest = rest_offset_z (sides);
            offset_z.assign (rest, rest);
            return true;
        }

        private void reset () {
            zoom.restart_with_end (1.0);
            offset_z.restart_with_end (rest_offset_z (sides));
            offset_y.restart_with_end (0);
            ease.restart_with_end (0);
        }

        public bool move (int sides, int direction) {
            if (!activate (sides)) {
                return false;
            }
            in_exit = true;
            reset ();
            rotation.restart_with_end (rotation.end - direction * side_angle);
            start ();
            return true;
        }

        public bool grab (int sides, double zoom_out) {
            if (!activate (sides)) {
                return false;
            }
            in_exit = false;
            double now_rotation = rotation.value;
            double now_offset_y = offset_y.value;
            double now_zoom = zoom.value;
            rotation.assign (now_rotation, now_rotation);
            offset_y.assign (now_offset_y, now_offset_y);
            offset_z.restart_with_end (zoom_out + rest_offset_z (this.sides));
            zoom.assign (now_zoom, now_zoom);
            ease.restart_with_end (1);
            start ();
            return true;
        }

        public void release () {
            if (!active) {
                return;
            }
            in_exit = true;
            double now_rotation = rotation.value;
            rotation.assign (now_rotation, -turned () * side_angle);
            reset ();
            start ();
        }

        public void cancel () {
            if (!active) {
                return;
            }
            in_exit = true;
            reset ();
            rotation.restart_with_end (0);
            start ();
        }

        public void turn_while_open (int direction) {
            if (!active || in_exit) {
                return;
            }
            rotation.restart_with_end (rotation.end - direction * side_angle);
            start ();
        }

        public void rotate (double dx, double dy, double speed_horizontal, double speed_vertical) {
            if (!active || in_exit) {
                return;
            }
            zoom.restart_with_end (zoom.end);
            double now_offset_y = offset_y.value;
            double target = (now_offset_y - dy * speed_vertical).clamp (-OFFSET_Y_MAX, OFFSET_Y_MAX);
            offset_y.assign (now_offset_y, target);
            offset_z.restart_with_end (offset_z.end);
            rotation.restart_with_end (rotation.value + dx * speed_horizontal);
            ease.restart_with_end (ease.end);
            start ();
        }

        public void scroll (double amount, double speed) {
            if (!active || in_exit) {
                return;
            }
            offset_y.restart_with_end (offset_y.end);
            offset_z.restart_with_end (offset_z.end);
            rotation.restart_with_end (rotation.end);
            ease.restart_with_end (ease.end);
            double now_zoom = zoom.value;
            double target = now_zoom + double.min (Math.pow (now_zoom, 1.5), ZOOM_MAX) * amount * speed;
            zoom.assign (now_zoom, target.clamp (ZOOM_MIN, ZOOM_MAX));
            start ();
        }

        public int turned () {
            if (side_angle <= 0) {
                return 0;
            }
            return (int) Math.floor (-rotation.value / side_angle + 0.5);
        }

        public bool done () {
            return active && in_exit && !running ();
        }

        public int finish () {
            int landed = sides > 0 ? (turned () % sides + sides) % sides : 0;
            rotation.assign (0, 0);
            active = false;
            in_exit = false;
            return landed;
        }
    }
}
