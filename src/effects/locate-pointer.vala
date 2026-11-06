/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate-settings-daemon 1.28's msd-locate-pointer: with
 * org.mate.peripherals-mouse locate-pointer on, pressing and releasing a
 * Control key with nothing else pressed in between (Lock, NumLock and Super
 * may be held, as msd's grabs allow) draws circles around the pointer.
 * msd grabs the key on X; here it is only watched, so clients still see
 * Control. The animation is msd's composited one: 750 ms at 30 frames a
 * second in a 101 pixel square that follows the pointer, four filled
 * circles in the theme's selected background colour, each growing from the
 * centre and fading. Nothing is drawn while the session is locked.
 */

namespace Polo.Effects {
    public class LocatePointer : GLib.Object {
        public const string SCHEMA = "org.mate.peripherals-mouse";
        private const string KEY = "locate-pointer";
        private const uint ANIMATION_LENGTH = 750;
        private const uint FPS = 30;
        private const int WINDOW_SIZE = 101;
        private const int N_CIRCLES = 4;
        private const double CIRCLES_PROGRESS_INTERVAL = 0.5 / N_CIRCLES;
        private const int COLOR_BG = 1;
        private const uint STATE_SELECTED = 1 << 2;

        public delegate unowned Marco.ThemeColors? ThemeColors ();

        private Backend.Server server;
        private Policy.Model model;
        private GLib.Settings? settings;
        private ThemeColors theme_colors;
        private uint32 armed = 0;
        private Backend.PixelBuffer? buffer = null;
        private int buffer_scale = 0;
        private int64 start_usec = 0;
        private uint timer = 0;
        private Marco.RGBA color;

        public bool running {
            get { return timer != 0; }
        }

        public LocatePointer (Backend.Server server, Policy.Model model, GLib.Settings? settings,
                              owned ThemeColors theme_colors) {
            this.server = server;
            this.model = model;
            this.settings = settings;
            this.theme_colors = (owned) theme_colors;
        }

        public void shutdown () {
            stop ();
        }

        private bool enabled () {
            return settings != null && settings.settings_schema.has_key (KEY) && settings.get_boolean (KEY);
        }

        private static bool is_control (uint32 keysym) {
            return keysym == Backend.Keysym.from_name ("Control_L") || keysym == Backend.Keysym.from_name ("Control_R");
        }

        private static bool allowed_modifiers (Backend.Modifiers mods) {
            var others = mods & ~(Backend.Modifiers.LOCK | Backend.Modifiers.MOD2 | Backend.Modifiers.MOD4 |
                                  Backend.Modifiers.CONTROL);
            return others == 0;
        }

        public void key (Backend.KeyEvent event) {
            if (event.pressed) {
                armed = is_control (event.keysym) && allowed_modifiers (event.modifiers) ? event.keycode + 1 : 0;
                return;
            }
            if (armed != 0 && event.keycode + 1 == armed) {
                armed = 0;
                if (enabled () && server.get_lock_state () == Backend.LockState.UNLOCKED) {
                    start ();
                }
            }
        }

        public void button () {
            armed = 0;
        }

        public void start () {
            stop ();
            unowned Marco.ThemeColors? colors = theme_colors ();
            Marco.RGBA selected = Marco.RGBA ();
            if (colors != null && colors.get (COLOR_BG, STATE_SELECTED, out selected) && selected.alpha > 0) {
                color = selected;
            } else {
                color = Marco.RGBA () { red = 0.7, green = 0.7, blue = 0.7, alpha = 1 };
            }
            var output = model.output_at (model.pointer_x, model.pointer_y);
            int scale = output != null ? int.max (1, output.scale) : 1;
            if (buffer == null || buffer_scale != scale) {
                buffer = new Backend.PixelBuffer (server, WINDOW_SIZE * scale, WINDOW_SIZE * scale, scale);
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
            if (progress >= 1) {
                timer = 0;
                stop ();
                return GLib.Source.REMOVE;
            }
            paint (progress);
            unowned Backend.View? view = buffer.get_view ();
            if (view != null) {
                view.set_position (model.pointer_x - WINDOW_SIZE / 2, model.pointer_y - WINDOW_SIZE / 2);
                view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
            }
            return GLib.Source.CONTINUE;
        }

        public static void circles (double progress, out double[] radii, out double[] alphas) {
            double[] r = {};
            double[] a = {};
            for (int i = 0; i <= N_CIRCLES; i++) {
                if (progress < 0) {
                    break;
                }
                double circle = double.min (1, progress * 2);
                progress -= CIRCLES_PROGRESS_INTERVAL;
                if (circle >= 1) {
                    continue;
                }
                r += circle / 2;
                a += 1 - circle;
            }
            radii = r;
            alphas = a;
        }

        private void paint (double progress) {
            int stride;
            unowned uint8[] pixels = buffer.begin (out stride);
            if (pixels.length == 0) {
                return;
            }
            int size = WINDOW_SIZE * buffer_scale;
            double[] radii, alphas;
            circles (progress, out radii, out alphas);
            double center = size / 2.0;
            for (int y = 0; y < size; y++) {
                for (int x = 0; x < size; x++) {
                    double dx = x + 0.5 - center, dy = y + 0.5 - center;
                    double distance = Math.sqrt (dx * dx + dy * dy);
                    double r = 0, g = 0, b = 0, a = 0;
                    for (int i = 0; i < radii.length; i++) {
                        double coverage = (radii[i] * size - distance + 0.5).clamp (0, 1);
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
