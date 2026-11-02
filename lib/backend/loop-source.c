/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Run libweston's wl_event_loop inside the GLib main loop, as Mutter
 * does: GLib owns the process' poll(), the Wayland loop's epoll fd is one
 * of its sources.
 */

#include <wayland-server-core.h>

#include "polo-backend-private.h"

typedef struct {
	GSource source;
	struct wl_display *display;
	struct wl_event_loop *loop;
} PoloLoopSource;

static gboolean
loop_source_prepare(GSource *source, gint *timeout)
{
	PoloLoopSource *self = (PoloLoopSource *)source;

	/* Idle callbacks (libweston schedules repaints this way) may have
	 * been added from GLib callbacks; run them before sleeping, then
	 * push out everything queued for clients */
	wl_event_loop_dispatch_idle(self->loop);
	wl_display_flush_clients(self->display);

	*timeout = -1;
	return FALSE;
}

static gboolean
loop_source_dispatch(GSource *source, GSourceFunc callback, gpointer data)
{
	PoloLoopSource *self = (PoloLoopSource *)source;

	wl_event_loop_dispatch(self->loop, 0);
	return G_SOURCE_CONTINUE;
}

static GSourceFuncs loop_source_funcs = {
	.prepare = loop_source_prepare,
	.dispatch = loop_source_dispatch,
};

GSource *
polo_backend_loop_source_new(struct wl_display *display)
{
	PoloLoopSource *self;
	GSource *source;

	source = g_source_new(&loop_source_funcs, sizeof(PoloLoopSource));
	self = (PoloLoopSource *)source;
	self->display = display;
	self->loop = wl_display_get_event_loop(display);

	g_source_set_name(source, "polo-wayland");
	g_source_add_unix_fd(source, wl_event_loop_get_fd(self->loop),
			     G_IO_IN | G_IO_ERR);
	g_source_set_can_recurse(source, TRUE);

	return source;
}
