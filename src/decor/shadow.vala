/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Decor {
    /* Marco's frame shadow, split into four pieces so memory scales with its perimeter. */
    public class Shadow : GLib.Object {
        private Backend.Server server;
        private Backend.PixelBuffer?[] pieces = new Backend.PixelBuffer?[Marco.ShadowPiece.COUNT];
        private Marco.Rect[] piece_rects = new Marco.Rect[Marco.ShadowPiece.COUNT];
        /* Shadow corner relative to the frame window's corner. */
        public int x { get; private set; }
        public int y { get; private set; }
        private bool drawn = false;
        private Marco.ShadowType drawn_type;
        private Marco.FrameFlags drawn_flags;
        private int drawn_width;
        private int drawn_height;
        private uint drawn_serial;
        private int drawn_scale;

        /* Emitted when a piece's view is ready to place. */
        public signal void ready ();

        public Shadow (Backend.Server server) {
            this.server = server;
        }

        /* Returns a piece's buffer, or null when empty, and its bounds from the shadow corner. */
        public unowned Backend.PixelBuffer? get_piece (Marco.ShadowPiece piece, out Marco.Rect rect) {
            rect = piece_rects[piece];
            return pieces[piece];
        }

        /* Redraws only when an input changes; returns true when it redraws. */
        public bool update (Marco.ShadowType type, Marco.FrameGeometry geometry, Marco.FrameFlags flags,
                            uint serial, int scale = 1) {
            if (drawn && drawn_type == type && drawn_flags == flags && drawn_width == geometry.width &&
                drawn_height == geometry.height && drawn_serial == serial && drawn_scale == scale) {
                return false;
            }
            var mask = Marco.ShadowMask.create (type, Marco.SHADOW_OPACITY, geometry, flags, scale);
            if (mask == null) {
                clear ();
                return false;
            }
            int mask_x, mask_y, width, height;
            mask.get_bounds (out mask_x, out mask_y, out width, out height);
            x = mask_x;
            y = mask_y;
            for (int i = 0; i < Marco.ShadowPiece.COUNT; i++) {
                draw_piece (mask, (Marco.ShadowPiece) i, scale);
            }
            drawn = true;
            drawn_type = type;
            drawn_flags = flags;
            drawn_width = geometry.width;
            drawn_height = geometry.height;
            drawn_serial = serial;
            drawn_scale = scale;
            return true;
        }

        private void draw_piece (Marco.ShadowMask mask, Marco.ShadowPiece piece, int scale) {
            Marco.Rect rect = {};
            mask.get_piece (piece, out rect.x, out rect.y, out rect.width, out rect.height);
            piece_rects[piece] = rect;
            if (rect.width <= 0 || rect.height <= 0) {
                pieces[piece] = null;
                return;
            }
            int width, height, buffer_scale;
            if (pieces[piece] != null) {
                pieces[piece].get_size (out width, out height, out buffer_scale);
                if (width != rect.width * scale || height != rect.height * scale || buffer_scale != scale) {
                    pieces[piece] = null;
                }
            }
            if (pieces[piece] == null) {
                pieces[piece] = new Backend.PixelBuffer (server, rect.width * scale, rect.height * scale, scale);
                pieces[piece].ready.connect (on_piece_ready);
            }
            int stride;
            unowned uint8[] pixels = pieces[piece].begin (out stride);
            if (pixels.length == 0) {
                return;
            }
            mask.render (rect.x, rect.y, pixels, rect.width * scale, rect.height * scale, stride);
            pieces[piece].commit ();
        }

        private void on_piece_ready () {
            ready ();
        }

        public void clear () {
            for (int i = 0; i < Marco.ShadowPiece.COUNT; i++) {
                pieces[i] = null;
            }
            drawn = false;
        }
    }
}
