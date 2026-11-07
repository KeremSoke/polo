/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The open, close and minimise animations of Wayfire's animate plugin
 * (plugins/animate/basic_animations.hpp, spin.hpp, zap.hpp, commit e268d4e5)
 * as a progress through the duration and the transform it gives. Fade and
 * zoom ease with wf-config's "circle" and flip their transitions to hide;
 * spin and zap run linearly, backwards to hide. A reverse goes back along
 * the same curve from where it is, as duration_t::reverse () does.
 *
 * Copyright (c) 2018 Ilia Bozhinov and the Wayfire contributors
 * Copyright (c) 2024 Scott Moreau
 * SPDX-License-Identifier: MIT
 */

namespace Polo.Policy {
    public enum WindowAnimationKind {
        NONE,
        FADE,
        ZOOM,
        ZAP,
        SPIN;

        public static WindowAnimationKind parse (string? name) {
            switch (name) {
                case "fade":
                    return FADE;
                case "zoom":
                    return ZOOM;
                case "zap":
                    return ZAP;
                case "spin":
                    return SPIN;
                default:
                    return NONE;
            }
        }

        public bool eased () {
            return this == FADE || this == ZOOM;
        }
    }

    public struct AnimationFrame {
        public double alpha;
        public double scale_x;
        public double scale_y;
        public double angle;
        public double dx;
        public double dy;
    }

    public class WindowAnimation {
        public const double ZOOM_FROM = 1.0 / 3.0;
        public const double SPIN_FROM = 0.01;

        public WindowAnimationKind kind { get; private set; }
        public double duration_ms;
        public int rotations = 1;
        public double zoom_from = ZOOM_FROM;
        public double offset_x = 0.0;
        public double offset_y = 0.0;

        private double position;
        private bool forward = true;
        private bool flipped;

        public WindowAnimation (WindowAnimationKind kind, double duration_ms, bool showing) {
            this.kind = kind;
            this.duration_ms = duration_ms;
            if (kind.eased ()) {
                position = 0.0;
                flipped = !showing;
            } else {
                position = showing ? 0.0 : 1.0;
                forward = showing;
                flipped = false;
            }
        }

        public bool showing { get { return forward != flipped; } }

        private double ease (double x) {
            return kind.eased () ? Math.sqrt (2 * x - x * x) : x;
        }

        private double uneased (double y) {
            return kind.eased () ? 1 - Math.sqrt (double.max (1 - y * y, 0)) : y;
        }

        public double shown {
            get {
                double eased = ease (position);
                return flipped ? 1 - eased : eased;
            }
        }

        public void start_from (double amount) {
            amount = amount.clamp (0.0, 1.0);
            position = uneased (flipped ? 1 - amount : amount);
        }

        public void advance (double ms) {
            double step = double.max (ms, 0.0) / double.max (duration_ms, 1.0);
            position = (position + (forward ? step : -step)).clamp (0.0, 1.0);
        }

        public bool done { get { return forward ? position >= 1.0 : position <= 0.0; } }

        public void reverse () {
            forward = !forward;
        }

        public AnimationFrame frame () {
            double s = shown;
            var result = AnimationFrame () { alpha = s, scale_x = 1, scale_y = 1, angle = 0, dx = 0, dy = 0 };
            switch (kind) {
                case WindowAnimationKind.ZOOM:
                    double zoom = zoom_from + (1 - zoom_from) * s;
                    result.alpha = double.min (1.0, s);
                    result.scale_x = zoom;
                    result.scale_y = zoom;
                    result.dx = offset_x * (1 - s);
                    result.dy = offset_y * (1 - s);
                    break;
                case WindowAnimationKind.SPIN:
                    result.angle = s * Math.PI * 2 * rotations;
                    result.scale_x = SPIN_FROM + s * (1 - SPIN_FROM);
                    result.scale_y = result.scale_x;
                    break;
                case WindowAnimationKind.ZAP:
                    double one = s.clamp (0.0, 1.0 / 3) * 3;
                    double two = (s.clamp (1.0 / 3, 2.0 / 3) - 1.0 / 3) * 3;
                    double three = (s.clamp (2.0 / 3, 1.0) - 2.0 / 3) * 3;
                    result.alpha = one;
                    result.scale_x = SPIN_FROM + two * (1 - SPIN_FROM);
                    result.scale_y = SPIN_FROM + three * (1 - SPIN_FROM);
                    break;
                default:
                    break;
            }
            return result;
        }

        public void zoom_into (Marco.Rect window, Marco.Rect target) {
            if (target.width <= 0 || target.height <= 0 || window.width <= 0 || window.height <= 0) {
                return;
            }
            offset_x = (target.x + target.width / 2) - (window.x + window.width / 2);
            offset_y = (target.y + target.height / 2) - (window.y + window.height / 2);
            zoom_from = double.min ((double) target.width / window.width, (double) target.height / window.height);
        }
    }
}
