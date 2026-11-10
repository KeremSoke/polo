/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_session_lock_v1: locked only after the curtain is up, no input for
 * other surfaces while locked, a second lock finished, a crashed locker
 * leaving the session locked, and protocol misuse.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <linux/input.h>

#include "ext-session-lock-v1-client-protocol.h"
#include "harness.h"
#include "locker.h"

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct harness_window *w = harness_window_create(h, 300, 200, "secret");

	CHECK(strcmp(state(h), "unlocked") == 0, "locked from the start");
	CHECK(key_reaches(h, w->surface), "the window does not get keys");

	/* Lock, then a lock surface. */
	struct locker *a = locker_new(h);

	locker_lock(a);
	CHECK(wait_locked(h, a), "no locked event");
	CHECK(strcmp(state(h), "locked") == 0, "not locked after locked");
	CHECK(!key_reaches(h, w->surface),
	      "a key reached a window while locked");

	struct wl_surface *surface =
		wl_compositor_create_surface(a->compositor);
	struct ext_session_lock_surface_v1 *lock_surface =
		ext_session_lock_v1_get_lock_surface(a->lock, surface,
						     a->output);

	ext_session_lock_surface_v1_add_listener(lock_surface,
						 &surface_listener, a);
	wl_display_roundtrip(a->display);
	CHECK(a->configured && a->width > 0 && a->height > 0,
	      "the lock surface was not configured");
	ext_session_lock_surface_v1_ack_configure(lock_surface, a->serial);
	struct wl_buffer *buffer =
		harness_buffer_create(a->shm, a->width, a->height);

	own(a, buffer);
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);
	wl_display_roundtrip(a->display);
	CHECK(wl_display_get_error(a->display) == 0,
	      "a valid lock surface was refused");
	pump(h, a);
	CHECK(!key_reaches(h, w->surface),
	      "a key reached a window with a lock surface up");

	/* A second lock while locked. */
	struct locker *b = locker_new(h);

	locker_lock(b);
	wl_display_roundtrip(b->display);
	CHECK(b->finished && !b->locked, "a second lock was not finished");
	ext_session_lock_v1_destroy(b->lock);
	b->lock = NULL;
	locker_free(b);
	CHECK(strcmp(state(h), "locked") == 0, "the second lock unlocked");

	ext_session_lock_surface_v1_destroy(lock_surface);
	wl_surface_destroy(surface);
	unlock(h, a);
	CHECK(key_reaches(h, w->surface),
	      "focus did not come back after unlock");

	/* The locker crashes: the session stays locked. */
	struct locker *c = locker_new(h);

	locker_lock(c);
	CHECK(wait_locked(h, c), "no locked event for the crashing locker");
	locker_free(c);
	CHECK(wait_state(h, NULL, "abandoned"), "a crashed locker unlocked");
	CHECK(!key_reaches(h, w->surface),
	      "a key reached a window after the locker crashed");

	struct locker *d = locker_new(h);

	locker_lock(d);
	CHECK(wait_locked(h, d), "a new locker could not take over");
	unlock(h, d);

	/* Destroy before locked: nothing was locked. */
	struct locker *e = locker_new(h);

	locker_lock(e);
	ext_session_lock_v1_destroy(e->lock);
	e->lock = NULL;
	wl_display_roundtrip(e->display);
	CHECK(wait_state(h, NULL, "unlocked"),
	      "destroy before locked stayed locked");
	locker_free(e);

	/* Destroy after locked is an error, and the session stays locked. */
	struct locker *f = locker_new(h);

	locker_lock(f);
	CHECK(wait_locked(h, f), "no locked event for the misbehaving locker");
	ext_session_lock_v1_destroy(f->lock);
	f->lock = NULL;
	wl_display_roundtrip(f->display);
	CHECK(wl_display_get_error(f->display) != 0,
	      "destroy after locked was allowed");
	CHECK(wait_state(h, NULL, "abandoned"),
	      "destroy after locked unlocked");
	locker_free(f);

	/* unlock_and_destroy before locked is an error. */
	struct locker *g = locker_new(h);

	locker_lock(g);
	CHECK(wait_locked(h, g), "no locked event after abandoned");
	unlock(h, g);
	struct locker *u = locker_new(h);

	u->lock = ext_session_lock_manager_v1_lock(u->manager);
	ext_session_lock_v1_add_listener(u->lock, &lock_listener, u);
	ext_session_lock_v1_unlock_and_destroy(u->lock);
	u->lock = NULL;
	wl_display_roundtrip(u->display);
	CHECK(wl_display_get_error(u->display) != 0,
	      "unlock_and_destroy before locked was allowed");
	locker_free(u);
	CHECK(wait_state(h, NULL, "abandoned"),
	      "an early unlock_and_destroy unlocked");

	/* A buffer of the wrong size is an error. */
	struct locker *m = locker_new(h);

	locker_lock(m);
	CHECK(wait_locked(h, m), "no locked event for the last locker");
	surface = wl_compositor_create_surface(m->compositor);
	lock_surface = ext_session_lock_v1_get_lock_surface(m->lock, surface,
							    m->output);
	ext_session_lock_surface_v1_add_listener(lock_surface,
						 &surface_listener, m);
	wl_display_roundtrip(m->display);
	own(m, surface);
	own(m, lock_surface);
	buffer = harness_buffer_create(m->shm, 10, 10);
	own(m, buffer);
	ext_session_lock_surface_v1_ack_configure(lock_surface, m->serial);
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);
	wl_display_roundtrip(m->display);
	CHECK(wl_display_get_error(m->display) != 0,
	      "a lock surface of the wrong size was allowed");
	locker_free(m);
	CHECK(wait_state(h, NULL, "abandoned"), "the killed locker unlocked");

	struct locker *z = locker_new(h);

	locker_lock(z);
	CHECK(wait_locked(h, z), "no locked event for cleanup");
	unlock(h, z);

	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
