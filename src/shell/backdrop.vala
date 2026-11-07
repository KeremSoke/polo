/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What Expo and the cube show behind their mirrors on one output: a solid
 * colour, or the wallpaper's last frame, shrunk to blur it cheaply, dimmed,
 * and shown zoomed back to the output's size.
 */

namespace Polo.Shell {
    public class Backdrop {
        public const uint32 DEFAULT_COLOUR = 0x1a1a1a;
        private const int BLUR_FACTOR = 4;

        /* "#rrggbb" as 0xrrggbb, else the fallback. */
        public static uint32 parse_colour (string? text, uint32 fallback = DEFAULT_COLOUR) {
            if (text == null || text.length != 7 || text[0] != '#') {
                return fallback;
            }
            uint32 colour = 0;
            for (int i = 1; i < 7; i++) {
                int digit = text[i].xdigit_value ();
                if (digit < 0) {
                    return fallback;
                }
                colour = colour << 4 | digit;
            }
            return colour;
        }

        /* mode: "solid", "wallpaper" or "blurred-wallpaper"; the view goes at the output's origin, scaled by zoom. */
        public static Backend.PixelBuffer make (Backend.Server server, Desktop desktop, Policy.Output output,
                                                string mode, uint32 colour, uint blur, double dim, out double zoom) {
            zoom = 1.0;
            if (mode == "wallpaper" || mode == "blurred-wallpaper") {
                var shown = wallpaper (server, desktop, output, mode == "blurred-wallpaper", blur, dim, out zoom);
                if (shown != null) {
                    return shown;
                }
            }
            var head = desktop.backend_output (output);
            int scale = head != null ? int.max (head.get_scale (), 1) : 1;
            var buffer = new Backend.PixelBuffer (server, output.rect.width * scale, output.rect.height * scale, scale);
            int stride;
            unowned uint8[] pixels = buffer.begin (out stride);
            for (int i = 0; i + 3 < pixels.length; i += 4) {
                pixels[i] = (uint8) (colour & 0xff);
                pixels[i + 1] = (uint8) (colour >> 8 & 0xff);
                pixels[i + 2] = (uint8) (colour >> 16 & 0xff);
                pixels[i + 3] = 0xff;
            }
            buffer.set_accepts_input (false);
            buffer.commit ();
            return buffer;
        }

        private static Backend.PixelBuffer? wallpaper (Backend.Server server, Desktop desktop, Policy.Output output,
                                                       bool blur, uint radius, double dim, out double zoom) {
            zoom = 1.0;
            var source = desktop.background.wallpaper_buffer (output);
            if (source == null) {
                return null;
            }
            int width, height, scale, stride;
            source.get_size (out width, out height, out scale);
            unowned uint8[] pixels_in = source.peek (out stride);
            if (pixels_in.length < stride * height || width <= 0 || height <= 0) {
                return null;
            }
            int factor = int.min (blur ? BLUR_FACTOR : 1, int.min (width, height));
            int small_width = width / factor, small_height = height / factor;
            var buffer = new Backend.PixelBuffer (server, small_width, small_height, 1);
            int small_stride;
            unowned uint8[] pixels = buffer.begin (out small_stride);
            if (pixels.length == 0) {
                return null;
            }
            Fx.downscale (pixels_in, width, height, stride, factor, pixels, small_stride);
            if (blur) {
                Fx.blur (pixels, small_width, small_height, small_stride,
                         int.max ((int) (radius * scale / factor), 1));
            }
            Fx.dim (pixels, small_width, small_height, small_stride, dim);
            buffer.set_accepts_input (false);
            buffer.commit ();
            zoom = (double) output.rect.width / small_width;
            return buffer;
        }
    }
}
