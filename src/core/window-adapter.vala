/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * How the policy acts on a window, whatever protocol it comes from.
 * Implemented by the XDG, XWayland, layer-shell, and policy-replay
 * adapters. No backend types appear here.
 */

namespace Polo {
    public interface WindowAdapter : GLib.Object {
        /*
         * Make the window's outer frame occupy this rectangle. A protocol
         * where the client resizes asynchronously (xdg-shell) asks for the
         * size now and moves once the client shows it.
         */
        public abstract void configure (Marco.Rect frame);

        public abstract void set_activated (bool activated);
        public abstract void set_maximized (bool maximized);
        public abstract void set_fullscreen (bool fullscreen);

        /* The edges the window is tiled against, as WindowState.TILED_*. */
        public virtual void set_tiled (WindowState edges) {}

        /* Updates window.borders for the current window state. */
        public virtual void calc_borders () {}

        /* Give the window keyboard focus on every seat. */
        public abstract void take_focus ();

        /* Ask the client to close. */
        public abstract void close ();

        public abstract void ping ();
    }
}
