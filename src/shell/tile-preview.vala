/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's tile preview (ui/tile-preview.c): a translucent rectangle with an
 * opaque one pixel border, beneath the focused window.
 */

namespace Polo {
    public class TilePreview : GLib.Object {
        private const uint8 RED = 0x35;
        private const uint8 GREEN = 0x84;
        private const uint8 BLUE = 0xe4;
        private const uint8 ALPHA = 0x40;

        private Backend.Server server;
        private Backend.PixelBuffer? buffer = null;
        private Marco.Rect rect;
        private bool placed = false;

        public signal void changed ();

        public unowned Backend.View? view {
            get { return placed ? buffer.get_view () : null; }
        }

        public TilePreview (Backend.Server server) {
            this.server = server;
        }

        public void show (Marco.Rect area) {
            if (buffer != null && rect.x == area.x && rect.y == area.y &&
                rect.width == area.width && rect.height == area.height) {
                return;
            }
            hide ();
            if (area.width <= 0 || area.height <= 0) {
                return;
            }
            rect = area;
            buffer = new Backend.PixelBuffer (server, rect.width, rect.height, 1);
            int stride;
            unowned uint8[] pixels = buffer.begin (out stride);
            if (pixels.length > 0) {
                for (int y = 0; y < rect.height; y++) {
                    for (int x = 0; x < rect.width; x++) {
                        bool border = x == 0 || y == 0 || x == rect.width - 1 || y == rect.height - 1;
                        uint alpha = border ? 0xff : ALPHA;
                        int offset = y * stride + x * 4;
                        pixels[offset] = (uint8) (BLUE * alpha / 0xff);
                        pixels[offset + 1] = (uint8) (GREEN * alpha / 0xff);
                        pixels[offset + 2] = (uint8) (RED * alpha / 0xff);
                        pixels[offset + 3] = (uint8) alpha;
                    }
                }
            }
            buffer.commit ();
            if (buffer.is_ready ()) {
                place ();
            } else {
                buffer.ready.connect (place);
            }
        }

        private void place () {
            unowned Backend.View? buffer_view = buffer != null ? buffer.get_view () : null;
            if (buffer_view == null) {
                return;
            }
            buffer_view.set_position (rect.x, rect.y);
            placed = true;
            changed ();
        }

        public void hide () {
            if (buffer == null) {
                return;
            }
            buffer.ready.disconnect (place);
            unowned Backend.View? buffer_view = buffer.get_view ();
            if (buffer_view != null) {
                buffer_view.move_to_layer (null);
            }
            buffer = null;
            placed = false;
        }
    }
}
