/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * xdg_activation_v1 against headless Polo: a token minted with the serial
 * of a click raises and focuses another window; one minted without input
 * only marks the window as demanding attention; a token works once.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "xdg-activation-v1-client-protocol.h"

#define STATE_DEMANDS_ATTENTION (1u << 10)

static struct xdg_activation_v1 *activation;

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	(void)data;
	(void)version;
	if (strcmp(interface, xdg_activation_v1_interface.name) == 0)
		activation = wl_registry_bind(registry, name,
					      &xdg_activation_v1_interface, 1);
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove,
};

static void
token_done(void *data, struct xdg_activation_token_v1 *token, const char *name)
{
	char **out = data;

	(void)token;
	*out = strdup(name);
}

static const struct xdg_activation_token_v1_listener token_listener = {
	token_done,
};

static char *
get_token(struct harness *h, bool with_serial, struct wl_surface *surface)
{
	struct xdg_activation_token_v1 *token;
	char *name = NULL;

	token = xdg_activation_v1_get_activation_token(activation);
	xdg_activation_token_v1_add_listener(token, &token_listener, &name);
	if (with_serial)
		xdg_activation_token_v1_set_serial(token, h->last_button_serial,
						   h->seat);
	if (surface)
		xdg_activation_token_v1_set_surface(token, surface);
	xdg_activation_token_v1_set_app_id(token, "test-activation");
	xdg_activation_token_v1_commit(token);
	harness_roundtrip(h);
	xdg_activation_token_v1_destroy(token);
	CHECK(name != NULL && strlen(name) > 0, "no token");
	return name;
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct harness_window *a, *b;
	struct wl_registry *registry;
	const struct harness_window_info *ia;
	char *token;
	int status;

	registry = wl_display_get_registry(h->display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(activation != NULL, "no xdg_activation_v1");

	b = harness_window_create(h, 200, 150, "b");
	a = harness_window_create(h, 600, 400, "a");
	CHECK(h->keyboard_focus == a->surface, "a not focused after mapping");

	token = get_token(h, false, a->surface);
	xdg_activation_v1_activate(activation, token, b->surface);
	harness_roundtrip(h);
	harness_wait_repaint(h);
	CHECK(h->keyboard_focus == a->surface, "worthless token stole focus");
	CHECK(harness_window_info(b)->state & STATE_DEMANDS_ATTENTION,
	      "b not demanding attention");
	CHECK(harness_window_info(a)->stack_index == 1, "b raised");
	free(token);

	ia = harness_window_info(a);
	harness_click(h, ia->x + ia->width / 2, ia->y + ia->height / 2);
	token = get_token(h, true, a->surface);
	xdg_activation_v1_activate(activation, token, b->surface);
	harness_roundtrip(h);
	harness_wait_repaint(h);
	CHECK(h->keyboard_focus == b->surface,
	      "token from a click did not focus b");
	CHECK(harness_window_info(b)->stack_index == 1, "b not raised");
	CHECK(!(harness_window_info(b)->state & STATE_DEMANDS_ATTENTION),
	      "b still demanding attention");

	harness_click(h, ia->x + 5, ia->y + 5);
	CHECK(h->keyboard_focus == a->surface, "click did not focus a");
	xdg_activation_v1_activate(activation, token, b->surface);
	harness_roundtrip(h);
	harness_wait_repaint(h);
	CHECK(h->keyboard_focus == a->surface, "a used token worked twice");
	free(token);

	xdg_activation_v1_destroy(activation);
	wl_registry_destroy(registry);
	harness_window_destroy(a);
	harness_window_destroy(b);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);
	printf("ok\n");
	return 0;
}
