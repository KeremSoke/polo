/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Input idleness without a call per event: libweston wakes the compositor on
 * every input event and declares it idle IDLE_SECONDS after the last one, so
 * its wake signal is a coalesced "activity" and its idle signal dates the
 * last input. PoloBackendIdleWatch times one timeout against that.
 */

#include <libweston/libweston.h>

#include "polo-backend-private.h"

#define IDLE_SECONDS 1

struct _PoloBackendIdleWatch {
	GObject parent_instance;

	PoloBackendServer *server; /* NULL once the server stops */
	struct wl_list link;
	struct wl_event_source *timer;
	guint timeout_msec;
	gboolean respect_inhibitors;
	gboolean idled;
};

enum {
	SIGNAL_IDLED,
	SIGNAL_RESUMED,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(PoloBackendIdleWatch, polo_backend_idle_watch,
		    G_TYPE_OBJECT)

static gboolean
inhibited(PoloBackendServer *server)
{
	return server->session_idle_inhibited ||
		polo_backend_server_get_idle_inhibited(server);
}

static guint
idle_msec(PoloBackendServer *server)
{
	if (!server->input_idle)
		return 0;
	return (guint)((g_get_monotonic_time() - server->last_input_usec) /
		       1000);
}

static void
arm(PoloBackendIdleWatch *watch, guint after_msec)
{
	if (!watch->timer)
		return;
	wl_event_source_timer_update(
		watch->timer,
		watch->timeout_msec == 0 ? 0 : MAX(after_msec, 1));
}

static void
arm_remaining(PoloBackendIdleWatch *watch)
{
	guint idle = idle_msec(watch->server);

	arm(watch, watch->timeout_msec > idle ? watch->timeout_msec - idle : 1);
}

static int
on_timer(void *data)
{
	PoloBackendIdleWatch *watch = data;
	guint idle;

	if (!watch->server || watch->idled || watch->timeout_msec == 0)
		return 0;
	idle = idle_msec(watch->server);
	if (idle < watch->timeout_msec) {
		arm(watch, watch->timeout_msec - idle);
		return 0;
	}
	if (watch->respect_inhibitors && inhibited(watch->server)) {
		arm(watch, watch->timeout_msec);
		return 0;
	}
	watch->idled = TRUE;
	g_signal_emit(watch, signals[SIGNAL_IDLED], 0);
	return 0;
}

/* Every watch matching respecting_only, held across the handlers. */
static GPtrArray *
collect(PoloBackendServer *server, gboolean respecting_only)
{
	GPtrArray *watches = g_ptr_array_new_with_free_func(g_object_unref);
	PoloBackendIdleWatch *watch;

	wl_list_for_each(watch, &server->idle_watches, link)
	{
		if (!respecting_only || watch->respect_inhibitors)
			g_ptr_array_add(watches, g_object_ref(watch));
	}
	return watches;
}

static void
reset(PoloBackendServer *server, gboolean respecting_only)
{
	GPtrArray *watches = collect(server, respecting_only);

	for (guint i = 0; i < watches->len; i++) {
		PoloBackendIdleWatch *watch = watches->pdata[i];
		gboolean was_idled = watch->idled;

		if (!watch->server)
			continue;
		watch->idled = FALSE;
		arm(watch, watch->timeout_msec);
		if (was_idled)
			g_signal_emit(watch, signals[SIGNAL_RESUMED], 0);
	}
	g_ptr_array_unref(watches);
}

static void
on_wake(struct wl_listener *listener, void *data)
{
	PoloBackendServer *server =
		wl_container_of(listener, server, idle_wake_listener);

	server->input_idle = FALSE;
	server->last_input_usec = g_get_monotonic_time();
	reset(server, FALSE);
	polo_backend_server_emit_activity(server);
}

static void
on_idle(struct wl_listener *listener, void *data)
{
	PoloBackendServer *server =
		wl_container_of(listener, server, idle_idle_listener);
	PoloBackendIdleWatch *watch;

	server->input_idle = TRUE;
	server->last_input_usec =
		g_get_monotonic_time() - IDLE_SECONDS * G_USEC_PER_SEC;
	wl_list_for_each(watch, &server->idle_watches, link)
	{
		if (!watch->idled)
			arm_remaining(watch);
	}
}

void
polo_backend_idle_inhibitors_changed(PoloBackendServer *server, gboolean added)
{
	PoloBackendIdleWatch *watch;

	if (added) {
		reset(server, TRUE);
		return;
	}
	wl_list_for_each(watch, &server->idle_watches, link)
	{
		if (watch->respect_inhibitors && !watch->idled)
			arm(watch, watch->timeout_msec);
	}
}

void
polo_backend_server_set_session_idle_inhibited(PoloBackendServer *server,
					       gboolean inhibited)
{
	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	inhibited = !!inhibited;
	if (server->session_idle_inhibited == inhibited)
		return;
	server->session_idle_inhibited = inhibited;
	polo_backend_idle_inhibitors_changed(server, inhibited);
}

gint64
polo_backend_server_get_last_input_usec(PoloBackendServer *server)
{
	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), 0);
	return server->input_idle ? server->last_input_usec
				  : g_get_monotonic_time();
}

void
polo_backend_server_blank_outputs(PoloBackendServer *server)
{
	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	if (server->compositor)
		weston_compositor_sleep(server->compositor);
}

void
polo_backend_server_unblank_outputs(PoloBackendServer *server)
{
	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	if (server->compositor &&
	    server->compositor->state == WESTON_COMPOSITOR_SLEEPING)
		weston_compositor_wake(server->compositor);
}

void
polo_backend_idle_init(PoloBackendServer *server)
{
	wl_list_init(&server->idle_watches);
	server->last_input_usec = g_get_monotonic_time();
	server->input_idle = FALSE;
	server->compositor->idle_time = IDLE_SECONDS;
	server->idle_wake_listener.notify = on_wake;
	wl_signal_add(&server->compositor->wake_signal,
		      &server->idle_wake_listener);
	server->idle_idle_listener.notify = on_idle;
	wl_signal_add(&server->compositor->idle_signal,
		      &server->idle_idle_listener);
	server->idle_ready = TRUE;
}

void
polo_backend_idle_fini(PoloBackendServer *server)
{
	PoloBackendIdleWatch *watch, *next;

	if (!server->idle_ready)
		return;
	server->idle_ready = FALSE;
	wl_list_remove(&server->idle_wake_listener.link);
	wl_list_remove(&server->idle_idle_listener.link);
	wl_list_for_each_safe(watch, next, &server->idle_watches, link)
	{
		g_clear_pointer(&watch->timer, wl_event_source_remove);
		wl_list_remove(&watch->link);
		wl_list_init(&watch->link);
		watch->server = NULL;
	}
}

static void
polo_backend_idle_watch_finalize(GObject *object)
{
	PoloBackendIdleWatch *watch = POLO_BACKEND_IDLE_WATCH(object);

	g_clear_pointer(&watch->timer, wl_event_source_remove);
	wl_list_remove(&watch->link);
	G_OBJECT_CLASS(polo_backend_idle_watch_parent_class)->finalize(object);
}

static void
polo_backend_idle_watch_class_init(PoloBackendIdleWatchClass *klass)
{
	G_OBJECT_CLASS(klass)->finalize = polo_backend_idle_watch_finalize;
	signals[SIGNAL_IDLED] = g_signal_new("idled", G_TYPE_FROM_CLASS(klass),
					     G_SIGNAL_RUN_LAST, 0, NULL, NULL,
					     NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_RESUMED] = g_signal_new(
		"resumed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_backend_idle_watch_init(PoloBackendIdleWatch *watch)
{
	wl_list_init(&watch->link);
}

PoloBackendIdleWatch *
polo_backend_idle_watch_new(PoloBackendServer *server, guint timeout_msec,
			    gboolean respect_inhibitors)
{
	PoloBackendIdleWatch *watch;

	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), NULL);
	watch = g_object_new(POLO_BACKEND_TYPE_IDLE_WATCH, NULL);
	watch->timeout_msec = timeout_msec;
	watch->respect_inhibitors = !!respect_inhibitors;
	if (!server->idle_ready)
		return watch;
	watch->server = server;
	wl_list_insert(&server->idle_watches, &watch->link);
	watch->timer = wl_event_loop_add_timer(
		wl_display_get_event_loop(server->display), on_timer, watch);
	arm_remaining(watch);
	return watch;
}

void
polo_backend_idle_watch_set_timeout(PoloBackendIdleWatch *watch,
				    guint timeout_msec)
{
	g_return_if_fail(POLO_BACKEND_IS_IDLE_WATCH(watch));
	if (watch->timeout_msec == timeout_msec)
		return;
	watch->timeout_msec = timeout_msec;
	if (!watch->server)
		return;
	if (watch->idled) {
		watch->idled = FALSE;
		arm_remaining(watch);
		g_signal_emit(watch, signals[SIGNAL_RESUMED], 0);
		return;
	}
	arm_remaining(watch);
}

gboolean
polo_backend_idle_watch_get_idled(PoloBackendIdleWatch *watch)
{
	g_return_val_if_fail(POLO_BACKEND_IS_IDLE_WATCH(watch), FALSE);
	return watch->idled;
}
