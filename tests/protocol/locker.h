/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An ext_session_lock_v1 client for the lock tests, on its own connection.
 */

#ifndef POLO_TEST_LOCKER_H
#define POLO_TEST_LOCKER_H

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <linux/input.h>

#include "ext-session-lock-v1-client-protocol.h"
#include "harness.h"

struct locker {
	struct wl_display *display;
	struct wl_registry *registry;
	struct ext_session_lock_manager_v1 *manager;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_output *output;
	struct ext_session_lock_v1 *lock;
	bool locked;
	bool finished;
	uint32_t serial;
	int32_t width, height;
	bool configured;
	struct wl_proxy *owned[8];
	int n_owned;
};

static inline void
own(struct locker *l, void *proxy)
{
	l->owned[l->n_owned++] = proxy;
}

static inline void
locker_global(void *data, struct wl_registry *registry, uint32_t name,
	      const char *interface, uint32_t version)
{
	struct locker *l = data;

	if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0)
		l->manager = wl_registry_bind(
			registry, name, &ext_session_lock_manager_v1_interface,
			1);
	else if (strcmp(interface, wl_compositor_interface.name) == 0)
		l->compositor = wl_registry_bind(registry, name,
						 &wl_compositor_interface, 4);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		l->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, wl_output_interface.name) == 0 && !l->output)
		l->output = wl_registry_bind(registry, name,
					     &wl_output_interface, 1);
}

static inline void
locker_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener locker_registry_listener = {
	locker_global,
	locker_global_remove,
};

static inline void
lock_locked(void *data, struct ext_session_lock_v1 *lock)
{
	((struct locker *)data)->locked = true;
}

static inline void
lock_finished(void *data, struct ext_session_lock_v1 *lock)
{
	((struct locker *)data)->finished = true;
}

static const struct ext_session_lock_v1_listener lock_listener = {
	lock_locked,
	lock_finished,
};

static inline void
surface_configure(void *data, struct ext_session_lock_surface_v1 *surface,
		  uint32_t serial, uint32_t width, uint32_t height)
{
	struct locker *l = data;

	l->serial = serial;
	l->width = (int32_t)width;
	l->height = (int32_t)height;
	l->configured = true;
}

static const struct ext_session_lock_surface_v1_listener surface_listener = {
	surface_configure,
};

static inline struct locker *
locker_new(struct harness *h)
{
	struct locker *l = calloc(1, sizeof *l);

	l->display = wl_display_connect(h->socket);
	CHECK(l->display, "cannot connect a locker");
	l->registry = wl_display_get_registry(l->display);
	wl_registry_add_listener(l->registry, &locker_registry_listener, l);
	wl_display_roundtrip(l->display);
	CHECK(l->manager, "no ext_session_lock_manager_v1");
	return l;
}

static inline void
locker_lock(struct locker *l)
{
	l->lock = ext_session_lock_manager_v1_lock(l->manager);
	ext_session_lock_v1_add_listener(l->lock, &lock_listener, l);
	wl_display_flush(l->display);
}

/* Close without unlocking, as a crash would. */
static inline void
locker_free(struct locker *l)
{
	void *proxies[] = { l->lock,       l->output,  l->shm,
			    l->compositor, l->manager, l->registry };

	for (int i = l->n_owned - 1; i >= 0; i--)
		wl_proxy_destroy(l->owned[i]);
	for (size_t i = 0; i < sizeof proxies / sizeof proxies[0]; i++) {
		if (proxies[i])
			wl_proxy_destroy(proxies[i]);
	}
	wl_display_disconnect(l->display);
	free(l);
}

static inline const char *
state(struct harness *h)
{
	return harness_get_setting(h, "polo-test", "session-lock");
}

static inline void
pump(struct harness *h, struct locker *l)
{
	struct timespec tick = { 0, 10 * 1000000L };

	nanosleep(&tick, NULL);
	if (l && wl_display_get_error(l->display) == 0)
		wl_display_roundtrip(l->display);
	harness_roundtrip(h);
}

static inline bool
wait_state(struct harness *h, struct locker *l, const char *wanted)
{
	for (int i = 0; i < 300 && strcmp(state(h), wanted) != 0; i++)
		pump(h, l);
	return strcmp(state(h), wanted) == 0;
}

static inline bool
wait_locked(struct harness *h, struct locker *l)
{
	for (int i = 0; i < 300 && !l->locked; i++)
		pump(h, l);
	return l->locked;
}

static inline bool
key_reaches(struct harness *h, struct wl_surface *surface)
{
	h->last_key_surface = NULL;
	harness_key(h, KEY_A, true);
	harness_key(h, KEY_A, false);
	harness_roundtrip(h);
	return h->last_key_surface == surface;
}

static inline void
unlock(struct harness *h, struct locker *l)
{
	ext_session_lock_v1_unlock_and_destroy(l->lock);
	l->lock = NULL;
	wl_display_roundtrip(l->display);
	CHECK(wait_state(h, NULL, "unlocked"),
	      "unlock_and_destroy did not unlock");
	locker_free(l);
}

#endif
