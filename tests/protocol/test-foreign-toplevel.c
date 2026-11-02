/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * zwlr_foreign_toplevel_manager_v1 v3 against headless Polo, acting as
 * a task bar: listing, title, app_id, state, output_enter/leave (also for
 * outputs bound late), parent, closed, a manager bound late, every request
 * (through the policy) and the set_rectangle protocol error.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

/* Polo.WindowState bits (src/core/window.vala). */
#define STATE_MINIMIZED (1u << 1)
#define STATE_MAXIMIZED ((1u << 2) | (1u << 3))
#define STATE_FULLSCREEN (1u << 4)
#define STATE_ACTIVE (1u << 12)
#define STATE_SKIP_TASKBAR (1u << 13)

#define FT_MAXIMIZED (1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED)
#define FT_MINIMIZED (1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED)
#define FT_ACTIVATED (1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
#define FT_FULLSCREEN (1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN)

/* ---- A task bar ---------------------------------------------------------- */

struct toplevel {
	struct taskbar *bar;
	struct zwlr_foreign_toplevel_handle_v1 *handle;
	char title[64], app_id[64];
	uint32_t state; /* FT_* bits, as of the last done */
	uint32_t pending_state;
	struct wl_output *outputs[4]; /* entered, as of now */
	int n_outputs;
	struct zwlr_foreign_toplevel_handle_v1 *parent;
	int dones;
	bool closed;
};

struct taskbar {
	struct zwlr_foreign_toplevel_manager_v1 *manager;
	struct toplevel *toplevels[16];
	int n_toplevels;
	bool finished;
};

static void
th_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
	 const char *title)
{
	struct toplevel *t = data;

	snprintf(t->title, sizeof t->title, "%s", title);
}

static void
th_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
	  const char *app_id)
{
	struct toplevel *t = data;

	snprintf(t->app_id, sizeof t->app_id, "%s", app_id);
}

static void
th_output_enter(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
		struct wl_output *output)
{
	struct toplevel *t = data;

	CHECK(t->n_outputs < 4, "too many outputs");
	t->outputs[t->n_outputs++] = output;
}

static void
th_output_leave(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
		struct wl_output *output)
{
	struct toplevel *t = data;
	int i;

	for (i = 0; i < t->n_outputs; i++) {
		if (t->outputs[i] == output) {
			t->outputs[i] = t->outputs[--t->n_outputs];
			return;
		}
	}
	CHECK(false, "output_leave for an output never entered");
}

static void
th_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
	 struct wl_array *states)
{
	struct toplevel *t = data;
	uint32_t *s;

	t->pending_state = 0;
	wl_array_for_each(s, states) t->pending_state |= 1u << *s;
}

static void
th_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle)
{
	struct toplevel *t = data;

	t->state = t->pending_state;
	t->dones++;
}

static void
th_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle)
{
	struct toplevel *t = data;

	t->closed = true;
}

static void
th_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
	  struct zwlr_foreign_toplevel_handle_v1 *parent)
{
	struct toplevel *t = data;

	t->parent = parent;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener handle_listener = {
	th_title, th_app_id, th_output_enter, th_output_leave,
	th_state, th_done,   th_closed,       th_parent,
};

static void
manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager,
		 struct zwlr_foreign_toplevel_handle_v1 *handle)
{
	struct taskbar *bar = data;
	struct toplevel *t = calloc(1, sizeof *t);

	CHECK(bar->n_toplevels < 16, "too many toplevels");
	t->bar = bar;
	t->handle = handle;
	zwlr_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener,
						     t);
	bar->toplevels[bar->n_toplevels++] = t;
}

static void
manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager)
{
	struct taskbar *bar = data;

	bar->finished = true;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener
	manager_listener = {
		manager_toplevel,
		manager_finished,
	};

static uint32_t manager_name;
static struct wl_output *late_output;
static uint32_t output0_name;
static uint32_t compositor_name;

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface,
		   zwlr_foreign_toplevel_manager_v1_interface.name) == 0)
		manager_name = name;
	else if (strcmp(interface, wl_output_interface.name) == 0 &&
		 !output0_name)
		output0_name = name;
	else if (strcmp(interface, wl_compositor_interface.name) == 0)
		compositor_name = name;
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove,
};

static struct taskbar *
taskbar_bind(struct harness *h, struct wl_registry *registry)
{
	struct taskbar *bar = calloc(1, sizeof *bar);

	bar->manager = wl_registry_bind(
		registry, manager_name,
		&zwlr_foreign_toplevel_manager_v1_interface, 3);
	zwlr_foreign_toplevel_manager_v1_add_listener(bar->manager,
						      &manager_listener, bar);
	harness_roundtrip(h);
	harness_roundtrip(h);
	return bar;
}

static struct toplevel *
find(struct taskbar *bar, const char *title)
{
	int i;

	for (i = 0; i < bar->n_toplevels; i++) {
		if (!bar->toplevels[i]->closed &&
		    strcmp(bar->toplevels[i]->title, title) == 0)
			return bar->toplevels[i];
	}
	return NULL;
}

static bool
entered(struct toplevel *t, struct wl_output *output)
{
	int i;

	for (i = 0; i < t->n_outputs; i++) {
		if (t->outputs[i] == output)
			return true;
	}
	return false;
}

/* Protocol state and the model only: no repaint is needed (and a
 * minimized window would never send its frame callback). */
static void
settle(struct harness *h)
{
	int i;

	for (i = 0; i < 6; i++)
		harness_roundtrip(h);
}

/* ---- Protocol error, on a fresh connection --------------------------------
 */

static void
test_invalid_rectangle(struct harness *h)
{
	struct wl_display *display = wl_display_connect(h->socket);
	struct wl_registry *registry;
	struct zwlr_foreign_toplevel_manager_v1 *manager;
	struct taskbar bar = { 0 };
	struct wl_compositor *compositor = NULL;
	struct wl_surface *surface;
	int i;

	CHECK(display != NULL, "connect: %s", strerror(errno));
	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	compositor = wl_registry_bind(registry, compositor_name,
				      &wl_compositor_interface, 4);
	manager = wl_registry_bind(registry, manager_name,
				   &zwlr_foreign_toplevel_manager_v1_interface,
				   3);
	zwlr_foreign_toplevel_manager_v1_add_listener(manager,
						      &manager_listener, &bar);
	wl_display_roundtrip(display);
	CHECK(bar.n_toplevels > 0, "raw task bar sees no toplevels");
	surface = wl_compositor_create_surface(compositor);
	zwlr_foreign_toplevel_handle_v1_set_rectangle(bar.toplevels[0]->handle,
						      surface, 0, 0, -5, 10);
	for (i = 0; i < 10 && wl_display_get_error(display) == 0; i++)
		if (wl_display_roundtrip(display) < 0)
			break;
	CHECK(wl_display_get_error(display) == EPROTO,
	      "negative rectangle not rejected");
	for (i = 0; i < bar.n_toplevels; i++) {
		zwlr_foreign_toplevel_handle_v1_destroy(
			bar.toplevels[i]->handle);
		free(bar.toplevels[i]);
	}
	wl_surface_destroy(surface);
	zwlr_foreign_toplevel_manager_v1_destroy(manager);
	wl_compositor_destroy(compositor);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct wl_registry *registry;
	struct taskbar *bar, *late;
	struct harness_window *a, *b, *c;
	struct toplevel *ta, *tb, *tc;
	int status, i;

	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	h = harness_start(argc, argv);
	registry = wl_display_get_registry(h->display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(manager_name != 0, "no zwlr_foreign_toplevel_manager_v1");
	bar = taskbar_bind(h, registry);
	CHECK(bar->n_toplevels == 0, "toplevels before any window");

	/* Listing: title, app_id, output, activated. */
	a = harness_window_create(h, 400, 300, "alpha");
	settle(h);
	ta = find(bar, "alpha");
	CHECK(ta != NULL && ta->dones > 0, "alpha not listed");
	CHECK(strcmp(ta->app_id, "org.mate.PoloTest") == 0, "app_id %s",
	      ta->app_id);
	CHECK(ta->n_outputs == 1 && entered(ta, h->outputs[0]),
	      "alpha not on output 0 (%d outputs)", ta->n_outputs);
	CHECK(ta->state == FT_ACTIVATED, "alpha state 0x%x", ta->state);

	b = harness_window_create(h, 400, 300, "beta");
	settle(h);
	tb = find(bar, "beta");
	CHECK(tb != NULL, "beta not listed");
	CHECK(tb->state == FT_ACTIVATED && ta->state == 0,
	      "activation did not move: alpha 0x%x beta 0x%x", ta->state,
	      tb->state);

	/* Title changes follow. */
	xdg_toplevel_set_title(a->xdg_toplevel, "alpha2");
	wl_surface_commit(a->surface);
	settle(h);
	CHECK(strcmp(ta->title, "alpha2") == 0, "title %s", ta->title);

	/* A manager bound later gets everything, oldest first. */
	late = taskbar_bind(h, registry);
	CHECK(late->n_toplevels == 2, "late manager sees %d toplevels",
	      late->n_toplevels);
	CHECK(strcmp(late->toplevels[0]->title, "alpha2") == 0 &&
		      strcmp(late->toplevels[1]->title, "beta") == 0,
	      "late manager order %s, %s", late->toplevels[0]->title,
	      late->toplevels[1]->title);
	CHECK(late->toplevels[1]->state == FT_ACTIVATED &&
		      late->toplevels[0]->n_outputs == 1,
	      "late manager state incomplete");

	/* Minimize: hidden, focus goes to alpha. */
	zwlr_foreign_toplevel_handle_v1_set_minimized(tb->handle);
	settle(h);
	CHECK(tb->state == FT_MINIMIZED, "beta state 0x%x", tb->state);
	CHECK(ta->state == FT_ACTIVATED, "alpha did not get focus: 0x%x",
	      ta->state);
	CHECK(harness_window_info(b)->state & STATE_MINIMIZED,
	      "policy did not minimize beta");

	/* Unminimize is a pager activation: shown, focused, raised. */
	zwlr_foreign_toplevel_handle_v1_unset_minimized(tb->handle);
	settle(h);
	CHECK(tb->state == FT_ACTIVATED, "beta state 0x%x", tb->state);
	CHECK(h->keyboard_focus == b->surface, "beta has no keyboard focus");

	/* Activate a lower window: focused and raised. */
	zwlr_foreign_toplevel_handle_v1_activate(ta->handle, h->seat);
	settle(h);
	CHECK(ta->state == FT_ACTIVATED && tb->state == 0,
	      "activate: alpha 0x%x beta 0x%x", ta->state, tb->state);
	CHECK(harness_window_info(a)->stack_index >
		      harness_window_info(b)->stack_index,
	      "activate did not raise alpha");

	/* Maximize through the policy. */
	zwlr_foreign_toplevel_handle_v1_set_maximized(ta->handle);
	settle(h);
	CHECK(ta->state == (FT_ACTIVATED | FT_MAXIMIZED), "alpha 0x%x",
	      ta->state);
	CHECK((harness_window_info(a)->state & STATE_MAXIMIZED) ==
		      STATE_MAXIMIZED,
	      "policy did not maximize alpha");
	zwlr_foreign_toplevel_handle_v1_unset_maximized(ta->handle);
	settle(h);
	CHECK(ta->state == FT_ACTIVATED, "alpha 0x%x", ta->state);

	/* Fullscreen on the second output: moves there. */
	zwlr_foreign_toplevel_handle_v1_set_fullscreen(ta->handle,
						       h->outputs[1]);
	settle(h);
	CHECK(ta->state & FT_FULLSCREEN, "alpha 0x%x", ta->state);
	CHECK(ta->n_outputs == 1 && entered(ta, h->outputs[1]),
	      "fullscreen alpha not (only) on output 1");
	CHECK(harness_window_info(a)->x == HARNESS_OUTPUT_WIDTH,
	      "fullscreen alpha at x %d", harness_window_info(a)->x);
	zwlr_foreign_toplevel_handle_v1_unset_fullscreen(ta->handle);
	settle(h);
	CHECK(!(ta->state & FT_FULLSCREEN), "alpha 0x%x", ta->state);

	/* An xdg minimize from the client itself shows up too. */
	xdg_toplevel_set_minimized(b->xdg_toplevel);
	settle(h);
	CHECK(tb->state & FT_MINIMIZED, "xdg minimize not reported: 0x%x",
	      tb->state);
	zwlr_foreign_toplevel_handle_v1_activate(tb->handle, h->seat);
	settle(h);

	/* Parent (v3): a dialog of alpha. */
	c = harness_window_create(h, 200, 100, "gamma");
	xdg_toplevel_set_parent(c->xdg_toplevel, a->xdg_toplevel);
	wl_surface_commit(c->surface);
	settle(h);
	tc = find(bar, "gamma");
	CHECK(tc != NULL, "gamma not listed");
	CHECK(tc->parent == ta->handle, "gamma parent %p, alpha %p",
	      (void *)tc->parent, (void *)ta->handle);

	/* An output bound after the handles exist gets its output_enter. */
	late_output = wl_registry_bind(registry, output0_name,
				       &wl_output_interface, 4);
	settle(h);
	CHECK(entered(tc, late_output), "late wl_output not entered");

	/* Close asks the client. */
	zwlr_foreign_toplevel_handle_v1_close(tb->handle);
	settle(h);
	CHECK(b->close_requested, "close did not reach beta");

	/* An unmapped window is closed; mapped again, it is a new toplevel. */
	{
		struct harness_window *d =
			harness_window_create(h, 160, 120, "delta");
		struct toplevel *td, *td2;

		settle(h);
		td = find(bar, "delta");
		CHECK(td && !td->closed, "delta not listed");
		harness_window_unmap(d);
		settle(h);
		CHECK(td->closed, "unmapped delta not closed");
		CHECK(!harness_window_info(d)->known,
		      "unmapped delta still known");
		harness_window_remap(d, 160, 120);
		settle(h);
		td2 = find(bar, "delta");
		CHECK(td2 && td2 != td && !td2->closed,
		      "remapped delta not listed");
		harness_window_destroy(d);
		settle(h);
		CHECK(td2->closed, "destroyed delta not closed");
	}

	/* A destroyed window is closed; its children lose it as parent and
	 * become normal windows again. */
	CHECK(harness_window_info(c)->state & STATE_SKIP_TASKBAR,
	      "gamma, a dialog, not skipping the task bar");
	harness_window_destroy(a);
	settle(h);
	CHECK(ta->closed, "alpha not closed");
	CHECK(tc->parent == NULL, "gamma still has a parent");
	CHECK(!(harness_window_info(c)->state & STATE_SKIP_TASKBAR),
	      "orphaned gamma still skips the task bar");

	test_invalid_rectangle(h);

	/* stop ends the manager with finished. */
	zwlr_foreign_toplevel_manager_v1_stop(late->manager);
	settle(h);
	CHECK(late->finished, "no finished after stop");

	harness_window_destroy(b);
	harness_window_destroy(c);
	settle(h);
	CHECK(tb->closed && tc->closed, "toplevels not closed");

	for (struct taskbar *t = bar; t; t = t == bar ? late : NULL) {
		for (i = 0; i < t->n_toplevels; i++) {
			zwlr_foreign_toplevel_handle_v1_destroy(
				t->toplevels[i]->handle);
			free(t->toplevels[i]);
		}
		/* No destroy request: this only frees the proxy. */
		zwlr_foreign_toplevel_manager_v1_destroy(t->manager);
		free(t);
	}
	wl_output_destroy(late_output);
	wl_registry_destroy(registry);

	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
