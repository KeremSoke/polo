/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PoloBackendPointerGrab: a weston_pointer_grab whose events become GObject
 * signals, so interactive move and resize are written in Vala. Pattern:
 * Weston kiosk-shell-grab.c.
 */

#include <libweston/libweston.h>

#include "polo-backend-private.h"

struct _PoloBackendPointerGrab {
	GObject parent_instance;

	struct weston_pointer_grab grab;
	struct weston_pointer *pointer; /* NULL once ended */
	struct wl_listener pointer_destroy_listener;
};

enum {
	SIGNAL_MOTION,
	SIGNAL_BUTTON,
	SIGNAL_ENDED,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(PoloBackendPointerGrab, polo_backend_pointer_grab,
		    G_TYPE_OBJECT)

static guint32
msec(const struct timespec *ts)
{
	return (guint32)(ts->tv_sec * 1000 + ts->tv_nsec / 1000000);
}

/*
 * End the grab. Emits "ended" once and drops the reference taken at start;
 * the caller must hold its own reference across this call.
 */
static void
grab_finish(PoloBackendPointerGrab *self, gboolean cancelled,
	    gboolean end_weston_grab)
{
	struct weston_pointer *pointer = self->pointer;

	if (!pointer)
		return;

	self->pointer = NULL;
	wl_list_remove(&self->pointer_destroy_listener.link);
	if (end_weston_grab && pointer->grab == &self->grab)
		weston_pointer_end_grab(pointer);

	g_signal_emit(self, signals[SIGNAL_ENDED], 0, cancelled);
	g_object_unref(self);
}

static PoloBackendPointerGrab *
from_grab(struct weston_pointer_grab *grab)
{
	PoloBackendPointerGrab *self = wl_container_of(grab, self, grab);

	return self;
}

static void
grab_focus(struct weston_pointer_grab *grab)
{
}

static void
grab_motion(struct weston_pointer_grab *grab,
	    const struct weston_pointer_motion_event *event)
{
	PoloBackendPointerGrab *self = from_grab(grab);
	struct weston_pointer *pointer = grab->pointer;

	weston_pointer_move(pointer, event);
	g_signal_emit(self, signals[SIGNAL_MOTION], 0, pointer->pos.c.x,
		      pointer->pos.c.y, msec(&event->base.ts));
}

static void
grab_button(struct weston_pointer_grab *grab,
	    const struct weston_pointer_button_event *event)
{
	PoloBackendPointerGrab *self = from_grab(grab);

	g_signal_emit(self, signals[SIGNAL_BUTTON], 0, event->button,
		      event->button_state == WL_POINTER_BUTTON_STATE_PRESSED,
		      grab->pointer->button_count);
}

static void
grab_axis(struct weston_pointer_grab *grab,
	  const struct weston_pointer_axis_event *event)
{
}

static void
grab_axis_source(struct weston_pointer_grab *grab, uint32_t source)
{
}

static void
grab_frame(struct weston_pointer_grab *grab)
{
}

static void
grab_cancel(struct weston_pointer_grab *grab)
{
	PoloBackendPointerGrab *self = from_grab(grab);

	g_object_ref(self);
	grab_finish(self, TRUE, TRUE);
	g_object_unref(self);
}

static const struct weston_pointer_grab_interface grab_interface = {
	grab_focus,       grab_motion, grab_button, grab_axis,
	grab_axis_source, grab_frame,  grab_cancel,
};

static void
pointer_destroyed(struct wl_listener *listener, void *data)
{
	PoloBackendPointerGrab *self =
		wl_container_of(listener, self, pointer_destroy_listener);

	g_object_ref(self);
	grab_finish(self, TRUE, FALSE);
	g_object_unref(self);
}

PoloBackendPointerGrab *
polo_backend_pointer_grab_start(struct weston_pointer *pointer)
{
	PoloBackendPointerGrab *self =
		g_object_new(POLO_BACKEND_TYPE_POINTER_GRAB, NULL);

	/* This reference belongs to the active grab; the caller gets one
	 * of its own. */
	g_object_ref(self);
	self->pointer = pointer;
	self->pointer_destroy_listener.notify = pointer_destroyed;
	wl_signal_add(&pointer->destroy_signal,
		      &self->pointer_destroy_listener);
	/* start_grab calls the interface's focus hook straight away. */
	self->grab.interface = &grab_interface;
	weston_pointer_start_grab(pointer, &self->grab);
	weston_pointer_clear_focus(pointer);

	return self;
}

void
polo_backend_pointer_grab_end(PoloBackendPointerGrab *grab)
{
	g_return_if_fail(POLO_BACKEND_IS_POINTER_GRAB(grab));

	g_object_ref(grab);
	grab_finish(grab, FALSE, TRUE);
	g_object_unref(grab);
}

static void
polo_backend_pointer_grab_class_init(PoloBackendPointerGrabClass *klass)
{
	signals[SIGNAL_MOTION] = g_signal_new(
		"motion", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 3, G_TYPE_DOUBLE, G_TYPE_DOUBLE,
		G_TYPE_UINT);
	signals[SIGNAL_BUTTON] = g_signal_new(
		"button", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_BOOLEAN,
		G_TYPE_UINT);
	signals[SIGNAL_ENDED] = g_signal_new(
		"ended", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
polo_backend_pointer_grab_init(PoloBackendPointerGrab *self)
{
}
