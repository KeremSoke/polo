/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The ripple a hot corner or edge shows when it fires, with
 * org.mate.polo.hot-corners animation on: locate pointer's growing circles,
 * centred on the corner point so only the quarter inside the screen shows,
 * or on the point of the edge the pointer hit, as a half circle.
 */

namespace Polo.Effects {
    public class HotCornerRipple : GLib.Object {
        public const int RADIUS = 100;
        private const uint ANIMATION_LENGTH = 750;
        private const uint FPS = 30;
        private const int COLOR_BG = 1;
        private const uint STATE_SELECTED = 1 << 2;

        private Backend.Server server;
        private LocatePointer.ThemeColors theme_colors;
        private Backend.PixelBuffer? buffer = null;
        private int buffer_width = 0;
        private int buffer_height = 0;
        private int buffer_scale = 0;
        private double center_x;
        private double center_y;
        private double view_x;
        private double view_y;
        private int64 start_usec = 0;
        private uint timer = 0;
        private Marco.RGBA color;

        public bool running {
            get { return timer != 0; }
        }

        public HotCornerRipple (Backend.Server server, owned LocatePointer.ThemeColors theme_colors) {
            this.server = server;
            this.theme_colors = (owned) theme_colors;
        }

        public void shutdown () {
            stop ();
            buffer = null;
        }

        public void start (Policy.HotRegion region, double pointer_x, double pointer_y, int scale) {
            stop ();
            unowned Marco.ThemeColors? colors = theme_colors ();
            Marco.RGBA selected = Marco.RGBA ();
            if (colors != null && colors.get (COLOR_BG, STATE_SELECTED, out selected) && selected.alpha > 0) {
                color = selected;
            } else {
                color = Marco.RGBA () { red = 0.7, green = 0.7, blue = 0.7, alpha = 1 };
            }
            int dir_x = region.spot.dir_x ();
            int dir_y = region.spot.dir_y ();
            double px = dir_x < 0 ? region.x : region.x + 1;
            double py = dir_y < 0 ? region.y : region.y + 1;
            if (dir_x == 0) {
                px = pointer_x.clamp (region.x + RADIUS, double.max (region.x + RADIUS, region.x + region.width - RADIUS));
            }
            if (dir_y == 0) {
                py = pointer_y.clamp (region.y + RADIUS, double.max (region.y + RADIUS, region.y + region.height - RADIUS));
            }
            int width = dir_x == 0 ? 2 * RADIUS : RADIUS;
            int height = dir_y == 0 ? 2 * RADIUS : RADIUS;
            view_x = dir_x < 0 ? px : px - RADIUS;
            view_y = dir_y < 0 ? py : py - RADIUS;
            center_x = px - view_x;
            center_y = py - view_y;
            scale = int.max (1, scale);
            if (buffer == null || buffer_width != width || buffer_height != height || buffer_scale != scale) {
                buffer = new Backend.PixelBuffer (server, width * scale, height * scale, scale);
                buffer_width = width;
                buffer_height = height;
                buffer_scale = scale;
            }
            start_usec = GLib.get_monotonic_time ();
            timer = GLib.Timeout.add (1000 / FPS, frame);
            frame ();
        }

        private void stop () {
            if (timer != 0) {
                GLib.Source.remove (timer);
                timer = 0;
            }
            unowned Backend.View? view = buffer != null ? buffer.get_view () : null;
            if (view != null) {
                view.move_to_layer (null);
            }
        }

        private bool frame () {
            double progress = (GLib.get_monotonic_time () - start_usec) / (ANIMATION_LENGTH * 1000.0);
            if (progress >= 1 || server.get_lock_state () != Backend.LockState.UNLOCKED) {
                timer = 0;
                stop ();
                return GLib.Source.REMOVE;
            }
            paint (progress);
            unowned Backend.View? view = buffer.get_view ();
            if (view != null) {
                view.set_position (view_x, view_y);
                view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
            }
            return GLib.Source.CONTINUE;
        }

        private void paint (double progress) {
            int stride;
            unowned uint8[] pixels = buffer.begin (out stride);
            if (pixels.length == 0) {
                return;
            }
            double[] radii, alphas;
            LocatePointer.circles (progress, out radii, out alphas);
            double diameter = 2.0 * RADIUS * buffer_scale;
            double cx = center_x * buffer_scale;
            double cy = center_y * buffer_scale;
            int width = buffer_width * buffer_scale;
            int height = buffer_height * buffer_scale;
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
                    double distance = Math.sqrt (dx * dx + dy * dy);
                    double r = 0, g = 0, b = 0, a = 0;
                    for (int i = 0; i < radii.length; i++) {
                        double coverage = (radii[i] * diameter - distance + 0.5).clamp (0, 1);
                        double alpha = alphas[i] * coverage;
                        r = color.red * alpha + r * (1 - alpha);
                        g = color.green * alpha + g * (1 - alpha);
                        b = color.blue * alpha + b * (1 - alpha);
                        a = alpha + a * (1 - alpha);
                    }
                    int offset = y * stride + x * 4;
                    pixels[offset] = (uint8) (b * 255);
                    pixels[offset + 1] = (uint8) (g * 255);
                    pixels[offset + 2] = (uint8) (r * 255);
                    pixels[offset + 3] = (uint8) (a * 255);
                }
            }
            buffer.commit ();
        }
    }
}
