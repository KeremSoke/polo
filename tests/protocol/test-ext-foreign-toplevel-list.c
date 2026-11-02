/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_foreign_toplevel_list_v1 against headless Polo: identifiers
 * (unique, at most 32 printable bytes, the same in every list, never
 * reused), title, app_id, done only for changes this protocol shows,
 * closed, a list bound late, and stop/finished.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "harness.h"

struct handle {
	struct ext_foreign_toplevel_handle_v1 *proxy;
	char identifier[64], title[64], app_id[64];
	int dones;
	bool closed;
};

struct list {
	struct ext_foreign_toplevel_list_v1 *proxy;
	struct handle *handles[16];
	int n_handles;
	bool finished;
};

static void
h_closed(void *data, struct ext_foreign_toplevel_handle_v1 *proxy)
{
	struct handle *h = data;

	CHECK(!h->closed, "closed twice");
	h->closed = true;
}

static void
h_done(void *data, struct ext_foreign_toplevel_handle_v1 *proxy)
{
	struct handle *h = data;

	CHECK(!h->closed, "done after closed");
	h->dones++;
}

static void
h_title(void *data, struct ext_foreign_toplevel_handle_v1 *proxy,
	const char *title)
{
	struct handle *h = data;

	snprintf(h->title, sizeof h->title, "%s", title);
}

static void
h_app_id(void *data, struct ext_foreign_toplevel_handle_v1 *proxy,
	 const char *app_id)
{
	struct handle *h = data;

	snprintf(h->app_id, sizeof h->app_id, "%s", app_id);
}

static void
h_identifier(void *data, struct ext_foreign_toplevel_handle_v1 *proxy,
	     const char *identifier)
{
	struct handle *h = data;

	CHECK(h->identifier[0] == '\0', "identifier sent twice");
	snprintf(h->identifier, sizeof h->identifier, "%s", identifier);
}

static const struct ext_foreign_toplevel_handle_v1_listener handle_listener = {
	h_closed, h_done, h_title, h_app_id, h_identifier,
};

static void
l_toplevel(void *data, struct ext_foreign_toplevel_list_v1 *proxy,
	   struct ext_foreign_toplevel_handle_v1 *toplevel)
{
	struct list *l = data;
	struct handle *h = calloc(1, sizeof *h);

	CHECK(l->n_handles < 16, "too many toplevels");
	h->proxy = toplevel;
	ext_foreign_toplevel_handle_v1_add_listener(toplevel, &handle_listener,
						    h);
	l->handles[l->n_handles++] = h;
}

static void
l_finished(void *data, struct ext_foreign_toplevel_list_v1 *proxy)
{
	struct list *l = data;

	l->finished = true;
}

static const struct ext_foreign_toplevel_list_v1_listener list_listener = {
	l_toplevel,
	l_finished,
};

static uint32_t list_name;

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface, ext_foreign_toplevel_list_v1_interface.name) == 0)
		list_name = name;
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove,
};

static struct list *
list_bind(struct harness *h, struct wl_registry *registry)
{
	struct list *l = calloc(1, sizeof *l);

	l->proxy = wl_registry_bind(registry, list_name,
				    &ext_foreign_toplevel_list_v1_interface, 1);
	ext_foreign_toplevel_list_v1_add_listener(l->proxy, &list_listener, l);
	harness_roundtrip(h);
	harness_roundtrip(h);
	return l;
}

static struct handle *
find(struct list *l, const char *title)
{
	int i;

	for (i = 0; i < l->n_handles; i++) {
		if (!l->handles[i]->closed &&
		    strcmp(l->handles[i]->title, title) == 0)
			return l->handles[i];
	}
	return NULL;
}

static void
check_identifier(const struct handle *h)
{
	size_t n = strlen(h->identifier);
	size_t i;

	CHECK(n > 0 && n <= 32, "identifier \"%s\" has %zu bytes",
	      h->identifier, n);
	for (i = 0; i < n; i++)
		CHECK(isprint((unsigned char)h->identifier[i]) &&
			      h->identifier[i] != ' ',
		      "identifier \"%s\" is not printable ASCII",
		      h->identifier);
}

static void
settle(struct harness *h)
{
	int i;

	for (i = 0; i < 6; i++)
		harness_roundtrip(h);
}

static void
list_free(struct list *l)
{
	int i;

	for (i = 0; i < l->n_handles; i++) {
		ext_foreign_toplevel_handle_v1_destroy(l->handles[i]->proxy);
		free(l->handles[i]);
	}
	ext_foreign_toplevel_list_v1_destroy(l->proxy);
	free(l);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct wl_registry *registry;
	struct list *first, *late;
	struct harness_window *a, *b, *c;
	struct handle *ha, *hb, *hc, *la, *lb;
	char old_identifier[64];
	int dones, status;

	h = harness_start(argc, argv);
	registry = wl_display_get_registry(h->display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(list_name != 0, "no ext_foreign_toplevel_list_v1");

	first = list_bind(h, registry);
	CHECK(first->n_handles == 0, "toplevels before any window");

	a = harness_window_create(h, 400, 300, "alpha");
	b = harness_window_create(h, 400, 300, "beta");
	settle(h);
	ha = find(first, "alpha");
	hb = find(first, "beta");
	CHECK(ha && hb, "windows not listed");
	CHECK(ha->dones > 0 && hb->dones > 0, "no done");
	check_identifier(ha);
	check_identifier(hb);
	CHECK(strcmp(ha->identifier, hb->identifier) != 0,
	      "two windows share identifier %s", ha->identifier);
	CHECK(strcmp(ha->app_id, "org.mate.PoloTest") == 0, "app_id %s",
	      ha->app_id);

	/* A list bound later: same windows, same identifiers, oldest first. */
	late = list_bind(h, registry);
	CHECK(late->n_handles == 2, "late list sees %d toplevels",
	      late->n_handles);
	la = late->handles[0];
	lb = late->handles[1];
	CHECK(strcmp(la->title, "alpha") == 0 && strcmp(lb->title, "beta") == 0,
	      "late list order %s, %s", la->title, lb->title);
	CHECK(strcmp(la->identifier, ha->identifier) == 0 &&
		      strcmp(lb->identifier, hb->identifier) == 0,
	      "identifiers differ between lists");

	/* A title change reaches every list, with done. */
	dones = ha->dones;
	xdg_toplevel_set_title(a->xdg_toplevel, "alpha2");
	wl_surface_commit(a->surface);
	settle(h);
	CHECK(strcmp(ha->title, "alpha2") == 0 &&
		      strcmp(la->title, "alpha2") == 0,
	      "title not updated: %s / %s", ha->title, la->title);
	CHECK(ha->dones == dones + 1, "title change: %d dones",
	      ha->dones - dones);

	/* Activation is not part of this protocol: no done for it. */
	dones = ha->dones;
	c = harness_window_create(h, 200, 100, "gamma");
	settle(h);
	CHECK(ha->dones == dones, "activation change sent %d dones",
	      ha->dones - dones);
	hc = find(first, "gamma");
	CHECK(hc != NULL, "gamma not listed");

	/* Closing sends closed everywhere; identifiers are never reused. */
	snprintf(old_identifier, sizeof old_identifier, "%s", hb->identifier);
	harness_window_destroy(b);
	settle(h);
	CHECK(hb->closed && lb->closed, "beta not closed in both lists");
	b = harness_window_create(h, 400, 300, "beta");
	settle(h);
	hb = find(first, "beta");
	CHECK(hb != NULL, "new beta not listed");
	CHECK(strcmp(hb->identifier, old_identifier) != 0,
	      "identifier %s reused", old_identifier);

	/* stop: finished, and no new toplevels afterwards. */
	ext_foreign_toplevel_list_v1_stop(late->proxy);
	settle(h);
	CHECK(late->finished, "no finished after stop");
	{
		int before = late->n_handles;
		struct harness_window *d =
			harness_window_create(h, 100, 100, "delta");

		settle(h);
		CHECK(late->n_handles == before, "toplevel event after stop");
		CHECK(find(first, "delta") != NULL, "delta not listed");
		harness_window_destroy(d);
	}

	harness_window_destroy(a);
	harness_window_destroy(b);
	harness_window_destroy(c);
	settle(h);
	CHECK(ha->closed && hc->closed, "toplevels not closed");

	list_free(first);
	list_free(late);
	wl_registry_destroy(registry);

	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
