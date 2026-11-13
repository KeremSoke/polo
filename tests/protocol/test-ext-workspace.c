/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_workspace_manager_v1 as libxfce4windowing uses it: one group on every
 * output, a handle per workspace, requests held until commit, live names and
 * counts, Marco's fixed-count rules for create and remove, and the xfce
 * toplevel extension's workspace_enter, workspace_leave and move_to_workspace;
 * an unplugged monitor leaves the group before its wl_output goes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ext-workspace-v1-client-protocol.h"
#include "harness.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "xfce-foreign-toplevel-management-private-v1-client-protocol.h"

#define MAX_WS 16
#define ACTIVE EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE

struct ws {
	struct ext_workspace_handle_v1 *handle;
	char id[32];
	char name[64];
	uint32_t col, row, state, caps;
	bool removed, in_group;
	bool on_toplevel;
};

static struct ws list[MAX_WS];
static int n_ws;
static int dones, group_caps, output_enters;
static int events, leave_at, done_after_leave, removed_at;
static bool unplugging;
static struct ext_workspace_group_handle_v1 *group;
static struct zwlr_foreign_toplevel_handle_v1 *wlr_handle;

static struct ws *
of(struct ext_workspace_handle_v1 *handle)
{
	for (int i = 0; i < n_ws; i++)
		if (list[i].handle == handle)
			return &list[i];
	CHECK(0, "unknown workspace handle");
	return NULL;
}

static struct ws *
named(const char *id)
{
	for (int i = 0; i < n_ws; i++)
		if (!list[i].removed && strcmp(list[i].id, id) == 0)
			return &list[i];
	return NULL;
}

static int
alive(void)
{
	int n = 0;

	for (int i = 0; i < n_ws; i++)
		n += !list[i].removed;
	return n;
}

static void
ws_id(void *data, struct ext_workspace_handle_v1 *h, const char *id)
{
	snprintf(of(h)->id, sizeof of(h)->id, "%s", id);
}

static void
ws_name(void *data, struct ext_workspace_handle_v1 *h, const char *name)
{
	snprintf(of(h)->name, sizeof of(h)->name, "%s", name);
}

static void
ws_coordinates(void *data, struct ext_workspace_handle_v1 *h,
	       struct wl_array *coordinates)
{
	uint32_t *c = coordinates->data;

	CHECK(coordinates->size == 2 * sizeof(uint32_t), "two coordinates");
	of(h)->col = c[0];
	of(h)->row = c[1];
}

static void
ws_state(void *data, struct ext_workspace_handle_v1 *h, uint32_t state)
{
	of(h)->state = state;
}

static void
ws_capabilities(void *data, struct ext_workspace_handle_v1 *h, uint32_t caps)
{
	of(h)->caps = caps;
}

static void
ws_removed(void *data, struct ext_workspace_handle_v1 *h)
{
	CHECK(!of(h)->in_group, "removed before workspace_leave");
	of(h)->removed = true;
}

static const struct ext_workspace_handle_v1_listener ws_listener = {
	.id = ws_id,
	.name = ws_name,
	.coordinates = ws_coordinates,
	.state = ws_state,
	.capabilities = ws_capabilities,
	.removed = ws_removed,
};

static void
group_capabilities(void *data, struct ext_workspace_group_handle_v1 *g,
		   uint32_t caps)
{
	group_caps = caps;
}

static void
group_output_enter(void *data, struct ext_workspace_group_handle_v1 *g,
		   struct wl_output *output)
{
	output_enters++;
}

static void
group_output_leave(void *data, struct ext_workspace_group_handle_v1 *g,
		   struct wl_output *output)
{
	if (unplugging && !leave_at)
		leave_at = ++events;
}

static void
group_workspace_enter(void *data, struct ext_workspace_group_handle_v1 *g,
		      struct ext_workspace_handle_v1 *w)
{
	of(w)->in_group = true;
}

static void
group_workspace_leave(void *data, struct ext_workspace_group_handle_v1 *g,
		      struct ext_workspace_handle_v1 *w)
{
	of(w)->in_group = false;
}

static void
group_removed(void *data, struct ext_workspace_group_handle_v1 *g)
{
}

static const struct ext_workspace_group_handle_v1_listener group_listener = {
	.capabilities = group_capabilities,
	.output_enter = group_output_enter,
	.output_leave = group_output_leave,
	.workspace_enter = group_workspace_enter,
	.workspace_leave = group_workspace_leave,
	.removed = group_removed,
};

static void
manager_group(void *data, struct ext_workspace_manager_v1 *m,
	      struct ext_workspace_group_handle_v1 *g)
{
	group = g;
	ext_workspace_group_handle_v1_add_listener(g, &group_listener, NULL);
}

static void
manager_workspace(void *data, struct ext_workspace_manager_v1 *m,
		  struct ext_workspace_handle_v1 *w)
{
	CHECK(n_ws < MAX_WS, "too many workspaces");
	list[n_ws++].handle = w;
	ext_workspace_handle_v1_add_listener(w, &ws_listener, NULL);
}

static void
manager_done(void *data, struct ext_workspace_manager_v1 *m)
{
	dones++;
	if (leave_at && !done_after_leave)
		done_after_leave = ++events;
}

static void
manager_finished(void *data, struct ext_workspace_manager_v1 *m)
{
}

static const struct ext_workspace_manager_v1_listener manager_listener = {
	.workspace_group = manager_group,
	.workspace = manager_workspace,
	.done = manager_done,
	.finished = manager_finished,
};

static void
ft_title(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, const char *s)
{
}
static void
ft_app_id(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, const char *s)
{
}
static void
ft_output(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
	  struct wl_output *o)
{
}
static void
ft_state(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_array *a)
{
}
static void
ft_done(void *d, struct zwlr_foreign_toplevel_handle_v1 *h)
{
}
static void
ft_closed(void *d, struct zwlr_foreign_toplevel_handle_v1 *h)
{
}
static void
ft_parent(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
	  struct zwlr_foreign_toplevel_handle_v1 *p)
{
}

static const struct zwlr_foreign_toplevel_handle_v1_listener ft_listener = {
	ft_title, ft_app_id, ft_output, ft_output,
	ft_state, ft_done,   ft_closed, ft_parent,
};

static void
toplevel_new(void *data, struct zwlr_foreign_toplevel_manager_v1 *m,
	     struct zwlr_foreign_toplevel_handle_v1 *h)
{
	wlr_handle = h;
	zwlr_foreign_toplevel_handle_v1_add_listener(h, &ft_listener, NULL);
}

static void
toplevel_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *m)
{
}

static const struct zwlr_foreign_toplevel_manager_v1_listener
	toplevels_listener = {
		toplevel_new,
		toplevel_finished,
	};

static void
x_state(void *d, struct xfce_foreign_toplevel_handle_v1 *h, struct wl_array *a)
{
}
static void
x_icon_name(void *d, struct xfce_foreign_toplevel_handle_v1 *h, const char *n)
{
}
static void
x_icon_size(void *d, struct xfce_foreign_toplevel_handle_v1 *h, uint32_t size,
	    uint32_t scale)
{
}
static void
x_no_icon(void *d, struct xfce_foreign_toplevel_handle_v1 *h)
{
}
static void
x_enter(void *d, struct xfce_foreign_toplevel_handle_v1 *h,
	struct ext_workspace_handle_v1 *w)
{
	of(w)->on_toplevel = true;
}
static void
x_leave(void *d, struct xfce_foreign_toplevel_handle_v1 *h,
	struct ext_workspace_handle_v1 *w)
{
	of(w)->on_toplevel = false;
}

static const struct xfce_foreign_toplevel_handle_v1_listener x_listener = {
	x_state, x_icon_name, x_icon_size, x_no_icon, x_enter, x_leave,
};

static struct ext_workspace_manager_v1 *manager;
static struct zwlr_foreign_toplevel_manager_v1 *toplevels;
static struct xfce_foreign_toplevel_manager_private_v1 *xfce;

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface, ext_workspace_manager_v1_interface.name) == 0)
		manager = wl_registry_bind(
			registry, name, &ext_workspace_manager_v1_interface, 1);
	else if (strcmp(interface,
			zwlr_foreign_toplevel_manager_v1_interface.name) == 0)
		toplevels = wl_registry_bind(
			registry, name,
			&zwlr_foreign_toplevel_manager_v1_interface, 3);
	else if (strcmp(interface,
			xfce_foreign_toplevel_manager_private_v1_interface
				.name) == 0)
		xfce = wl_registry_bind(
			registry, name,
			&xfce_foreign_toplevel_manager_private_v1_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	if (unplugging && !removed_at)
		removed_at = ++events;
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
settle(struct harness *h)
{
	struct timespec ts = { 0, 20 * 1000000L };

	for (int i = 0; i < 5; i++) {
		harness_roundtrip(h);
		nanosleep(&ts, NULL);
	}
}

int
main(int argc, char **argv)
{
	setenv("POLO_TEST_MANAGED_OUTPUTS", "1", 1);
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(manager && toplevels && xfce, "missing globals");
	ext_workspace_manager_v1_add_listener(manager, &manager_listener, NULL);
	zwlr_foreign_toplevel_manager_v1_add_listener(
		toplevels, &toplevels_listener, NULL);
	settle(h);

	/* What a client sees on binding. */
	CHECK(group && group_caps == 1, "group capabilities %d", group_caps);
	CHECK(output_enters == 1, "%d output_enter", output_enters);
	CHECK(n_ws == 4 && dones >= 1, "%d workspaces, %d done", n_ws, dones);
	for (int i = 0; i < 4; i++) {
		char id[32];

		snprintf(id, sizeof id, "polo-ws-%d", i + 1);
		CHECK(strcmp(list[i].id, id) == 0, "id %s", list[i].id);
		CHECK(list[i].col == (uint32_t)i && list[i].row == 0,
		      "coordinates %u,%u", list[i].col, list[i].row);
		CHECK(list[i].in_group, "%s not in the group", id);
		CHECK(list[i].caps == 13, "capabilities %u", list[i].caps);
		CHECK((list[i].state == ACTIVE) == (i == 0), "state of %s", id);
		CHECK(strstr(list[i].name, id + 8) != NULL, "name %s",
		      list[i].name);
	}

	/* Activation waits for commit. */
	ext_workspace_handle_v1_activate(list[2].handle);
	settle(h);
	CHECK(list[0].state == ACTIVE, "activated before commit");
	ext_workspace_manager_v1_commit(manager);
	settle(h);
	CHECK(list[2].state == ACTIVE && list[0].state == 0,
	      "activate did not switch");
	ext_workspace_handle_v1_activate(list[0].handle);
	ext_workspace_manager_v1_commit(manager);
	settle(h);

	/* Names and counts follow the settings. */
	harness_set_setting(h, "org.mate.Marco.workspace-names", "name-2",
			    "'Mail'");
	settle(h);
	CHECK(strcmp(list[1].name, "Mail") == 0, "name %s", list[1].name);

	/* Create adds one, with its name. */
	ext_workspace_group_handle_v1_create_workspace(group, "Fifth");
	ext_workspace_manager_v1_commit(manager);
	settle(h);
	CHECK(alive() == 5 && named("polo-ws-5") != NULL, "no fifth workspace");
	CHECK(strcmp(named("polo-ws-5")->name, "Fifth") == 0, "name %s",
	      named("polo-ws-5")->name);
	CHECK(strcmp(harness_get_setting(h, "org.mate.Marco.general",
					 "num-workspaces"),
		     "5") == 0,
	      "num-workspaces not written");

	/* Remove: never one in the middle, nor the last with a window on it. */
	struct harness_window *w = harness_window_create(h, 300, 200, "alpha");

	settle(h);
	CHECK(wlr_handle != NULL, "window not listed");
	struct xfce_foreign_toplevel_handle_v1 *x =
		xfce_foreign_toplevel_manager_private_v1_get_xfce_foreign_toplevel_handle(
			xfce, wlr_handle);

	xfce_foreign_toplevel_handle_v1_add_listener(x, &x_listener, NULL);
	settle(h);
	CHECK(list[0].on_toplevel && !list[1].on_toplevel,
	      "no workspace_enter for the first workspace");

	ext_workspace_handle_v1_remove(list[1].handle);
	ext_workspace_manager_v1_commit(manager);
	settle(h);
	CHECK(alive() == 5, "removed a workspace in the middle");

	struct ws *fifth = named("polo-ws-5");

	xfce_foreign_toplevel_handle_v1_move_to_workspace(x, fifth->handle);
	settle(h);
	CHECK(harness_window_info(w)->workspace == 4,
	      "move_to_workspace did not move alpha");
	CHECK(fifth->on_toplevel && !list[0].on_toplevel,
	      "workspace_enter and leave did not follow");
	ext_workspace_handle_v1_remove(fifth->handle);
	ext_workspace_manager_v1_commit(manager);
	settle(h);
	CHECK(alive() == 5, "removed a workspace with a window");

	xfce_foreign_toplevel_handle_v1_move_to_workspace(x, list[0].handle);
	settle(h);
	CHECK(list[0].on_toplevel && !fifth->on_toplevel, "not back");
	ext_workspace_handle_v1_remove(fifth->handle);
	ext_workspace_manager_v1_commit(manager);
	settle(h);
	CHECK(fifth->removed && !fifth->in_group && alive() == 4,
	      "the empty last workspace stayed");

	/* A lower count from the settings removes handles. */
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "2");
	settle(h);
	CHECK(alive() == 2, "%d workspaces after the count dropped", alive());

	/* A monitor unplugged leaves the group before its wl_output goes:
	 * libxfce4windowing keeps a freed monitor otherwise. */
	static const int modes[][3] = { { 1280, 720, 60000 } };
	int enters = output_enters;

	harness_add_output(h, "HDMI-A-1", "DEL", 0x4321, 9, "Workspaces", false,
			   527, 296, modes, 1);
	settle(h);
	CHECK(h->n_outputs == 2 && output_enters > enters,
	      "the group did not enter the new output");
	unplugging = true;
	harness_remove_output(h, "HDMI-A-1");
	settle(h);
	CHECK(removed_at, "the new output's global was not removed");
	CHECK(leave_at && leave_at < removed_at,
	      "output_leave came %s the output's global_remove",
	      leave_at ? "after" : "never before");
	CHECK(done_after_leave && done_after_leave < removed_at,
	      "no done between output_leave and global_remove");

	xfce_foreign_toplevel_handle_v1_destroy(x);
	harness_window_destroy(w);
	zwlr_foreign_toplevel_handle_v1_destroy(wlr_handle);
	zwlr_foreign_toplevel_manager_v1_stop(toplevels);
	xfce_foreign_toplevel_manager_private_v1_destroy(xfce);
	for (int i = 0; i < n_ws; i++)
		ext_workspace_handle_v1_destroy(list[i].handle);
	ext_workspace_group_handle_v1_destroy(group);
	ext_workspace_manager_v1_stop(manager);
	settle(h);
	wl_registry_destroy(registry);
	return harness_stop(h);
}
