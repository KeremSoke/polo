/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * No output shows black or empty after a display change: with a large
 * picture as the wallpaper, every lit output is captured right after each
 * change (extend, mirror, second only, first only, extend, a scale change,
 * an unplug, a plug) and must show the wallpaper, not a cleared buffer.
 */
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"

struct mode {
	struct zwlr_output_mode_v1 *mode;
	int width, height, refresh;
};

struct head {
	struct zwlr_output_head_v1 *head;
	char name[64];
	struct mode modes[16];
	int n_modes;
	struct zwlr_output_mode_v1 *current;
	int enabled, x, y, transform;
	wl_fixed_t scale;
};

struct client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct zwlr_output_manager_v1 *manager;
	struct head heads[4];
	int n_heads;
	uint32_t serial;
	int dones;
};

enum { PENDING, SUCCEEDED, FAILED, CANCELLED };

static struct head *
head_of(struct client *c, struct zwlr_output_head_v1 *head)
{
	for (int i = 0; i < c->n_heads; i++) {
		if (c->heads[i].head == head)
			return &c->heads[i];
	}
	return NULL;
}

static struct mode *
mode_of(struct client *c, struct zwlr_output_mode_v1 *mode)
{
	for (int i = 0; i < c->n_heads; i++) {
		for (int j = 0; j < c->heads[i].n_modes; j++) {
			if (c->heads[i].modes[j].mode == mode)
				return &c->heads[i].modes[j];
		}
	}
	return NULL;
}

static struct zwlr_output_mode_v1 *
mode_to_set(struct head *h)
{
	return h->current ? h->current : h->modes[0].mode;
}

static void
mode_size(void *data, struct zwlr_output_mode_v1 *mode, int32_t w, int32_t h)
{
	struct mode *m = mode_of(data, mode);

	m->width = w;
	m->height = h;
}

static void
mode_refresh(void *data, struct zwlr_output_mode_v1 *mode, int32_t refresh)
{
	mode_of(data, mode)->refresh = refresh;
}

static void
mode_nothing(void *data, struct zwlr_output_mode_v1 *mode)
{
}

static const struct zwlr_output_mode_v1_listener mode_listener = {
	mode_size,
	mode_refresh,
	mode_nothing,
	mode_nothing,
};

static void
head_name(void *data, struct zwlr_output_head_v1 *head, const char *name)
{
	snprintf(head_of(data, head)->name, 64, "%s", name);
}

static void
head_string(void *data, struct zwlr_output_head_v1 *head, const char *s)
{
}

static void
head_physical_size(void *data, struct zwlr_output_head_v1 *head, int32_t w,
		   int32_t h)
{
}

static void
head_mode(void *data, struct zwlr_output_head_v1 *head,
	  struct zwlr_output_mode_v1 *mode)
{
	struct head *h = head_of(data, head);

	h->modes[h->n_modes++].mode = mode;
	zwlr_output_mode_v1_add_listener(mode, &mode_listener, data);
}

static void
head_enabled(void *data, struct zwlr_output_head_v1 *head, int32_t enabled)
{
	head_of(data, head)->enabled = enabled;
}

static void
head_current_mode(void *data, struct zwlr_output_head_v1 *head,
		  struct zwlr_output_mode_v1 *mode)
{
	head_of(data, head)->current = mode;
}

static void
head_position(void *data, struct zwlr_output_head_v1 *head, int32_t x,
	      int32_t y)
{
	head_of(data, head)->x = x;
	head_of(data, head)->y = y;
}

static void
head_transform(void *data, struct zwlr_output_head_v1 *head, int32_t t)
{
	head_of(data, head)->transform = t;
}

static void
head_scale(void *data, struct zwlr_output_head_v1 *head, wl_fixed_t scale)
{
	head_of(data, head)->scale = scale;
}

static void
head_finished(void *data, struct zwlr_output_head_v1 *head)
{
}

static void
head_adaptive_sync(void *data, struct zwlr_output_head_v1 *head, uint32_t state)
{
}

static const struct zwlr_output_head_v1_listener head_listener = {
	.name = head_name,
	.description = head_string,
	.physical_size = head_physical_size,
	.mode = head_mode,
	.enabled = head_enabled,
	.current_mode = head_current_mode,
	.position = head_position,
	.transform = head_transform,
	.scale = head_scale,
	.finished = head_finished,
	.make = head_string,
	.model = head_string,
	.serial_number = head_string,
	.adaptive_sync = head_adaptive_sync,
};

static void
manager_head(void *data, struct zwlr_output_manager_v1 *manager,
	     struct zwlr_output_head_v1 *head)
{
	struct client *c = data;

	c->heads[c->n_heads++].head = head;
	zwlr_output_head_v1_add_listener(head, &head_listener, c);
}

static void
manager_done(void *data, struct zwlr_output_manager_v1 *manager,
	     uint32_t serial)
{
	struct client *c = data;

	c->serial = serial;
	c->dones++;
}

static void
manager_finished(void *data, struct zwlr_output_manager_v1 *manager)
{
}

static const struct zwlr_output_manager_v1_listener manager_listener = {
	manager_head,
	manager_done,
	manager_finished,
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct client *c = data;

	if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0 &&
	    version >= 4) {
		c->manager = wl_registry_bind(
			registry, name, &zwlr_output_manager_v1_interface, 4);
		zwlr_output_manager_v1_add_listener(c->manager,
						    &manager_listener, c);
	}
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static struct client *
connect_client(struct harness *h)
{
	struct client *c = calloc(1, sizeof *c);

	c->display = wl_display_connect(h->socket);
	CHECK(c->display, "cannot connect");
	c->registry = wl_display_get_registry(c->display);
	wl_registry_add_listener(c->registry, &registry_listener, c);
	wl_display_roundtrip(c->display);
	CHECK(c->manager, "no zwlr_output_manager_v1 version 4");
	wl_display_roundtrip(c->display);
	wl_display_roundtrip(c->display);
	return c;
}

static void
client_free(struct client *c)
{
	for (int i = 0; i < c->n_heads; i++) {
		for (int j = 0; j < c->heads[i].n_modes; j++)
			wl_proxy_destroy(
				(struct wl_proxy *)c->heads[i].modes[j].mode);
		wl_proxy_destroy((struct wl_proxy *)c->heads[i].head);
	}
	if (c->manager)
		wl_proxy_destroy((struct wl_proxy *)c->manager);
	wl_proxy_destroy((struct wl_proxy *)c->registry);
	wl_display_disconnect(c->display);
	free(c);
}

static void
config_succeeded(void *data, struct zwlr_output_configuration_v1 *config)
{
	*(int *)data = SUCCEEDED;
}

static void
config_failed(void *data, struct zwlr_output_configuration_v1 *config)
{
	*(int *)data = FAILED;
}

static void
config_cancelled(void *data, struct zwlr_output_configuration_v1 *config)
{
	*(int *)data = CANCELLED;
}

static const struct zwlr_output_configuration_v1_listener config_listener = {
	config_succeeded,
	config_failed,
	config_cancelled,
};

struct layout {
	int on[2];
	int x[2];
	int scale[2];
};

static int
apply_layout(struct harness *h, struct client *c, const struct layout *l)
{
	struct zwlr_output_configuration_v1 *config =
		zwlr_output_manager_v1_create_configuration(c->manager,
							    c->serial);
	struct zwlr_output_configuration_head_v1 *chs[2] = { 0 };
	int result = PENDING;

	zwlr_output_configuration_v1_add_listener(config, &config_listener,
						  &result);
	for (int i = 0; i < 2; i++) {
		if (!l->on[i]) {
			zwlr_output_configuration_v1_disable_head(
				config, c->heads[i].head);
			continue;
		}
		chs[i] = zwlr_output_configuration_v1_enable_head(
			config, c->heads[i].head);
		zwlr_output_configuration_head_v1_set_mode(
			chs[i], mode_to_set(&c->heads[i]));
		zwlr_output_configuration_head_v1_set_position(chs[i], l->x[i],
							       0);
		zwlr_output_configuration_head_v1_set_scale(
			chs[i], wl_fixed_from_int(l->scale[i]));
	}
	zwlr_output_configuration_v1_apply(config);
	for (int i = 0; i < 200 && result == PENDING; i++) {
		wl_display_roundtrip(c->display);
		harness_roundtrip(h);
	}
	zwlr_output_configuration_v1_destroy(config);
	for (int i = 0; i < 2; i++) {
		if (chs[i])
			wl_proxy_destroy((struct wl_proxy *)chs[i]);
	}
	wl_display_roundtrip(c->display);
	return result;
}

static void
write_picture(const char *path)
{
	cairo_surface_t *surface =
		cairo_image_surface_create(CAIRO_FORMAT_RGB24, 3000, 2000);
	cairo_t *cr = cairo_create(surface);

	cairo_set_source_rgb(cr, 1, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);
	CHECK(cairo_surface_write_to_png(surface, path) == CAIRO_STATUS_SUCCESS,
	      "cannot write %s", path);
	cairo_surface_destroy(surface);
}

static int failures;

static void
expect_wallpaper(struct harness *h, const char *what)
{
	harness_roundtrip(h);
	for (int i = 0; i < h->n_outputs; i++) {
		int width, height, dark = 0, total = 0;
		uint32_t *pixels =
			harness_capture_output(h, i, &width, &height);

		if (!pixels)
			continue;
		for (int y = 0; y < height; y += 16) {
			for (int x = 0; x < width; x += 16) {
				uint32_t p = pixels[y * width + x] & 0xffffff;

				total++;
				dark += ((p >> 16) & 0xff) < 64;
			}
		}
		free(pixels);
		if (total == 0 || dark * 100 / total >= 2) {
			fprintf(stderr,
				"%s: output %d is %d%% dark right after the "
				"change\n",
				what, i, total ? dark * 100 / total : 100);
			failures++;
		}
	}
}

int
main(int argc, char **argv)
{
	char path[512], value[600];

	setenv("POLO_TEST_MANAGED_OUTPUTS", "1", 1);
	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	if (argc > 2 && strcmp(argv[2], "gl") == 0) {
		setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
		setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
	}
	struct harness *h = harness_start(argc, argv);
	struct client *c = connect_client(h);

	CHECK(c->n_heads == 2, "expected two heads, got %d", c->n_heads);
	snprintf(path, sizeof path, "%s/large.png", h->runtime_dir);
	write_picture(path);
	snprintf(value, sizeof value, "'%s'", path);
	harness_set_setting(h, "org.mate.background", "picture-options",
			    "'zoom'");
	harness_set_setting(h, "org.mate.background", "picture-filename",
			    value);
	for (int i = 0; i < 200; i++) {
		int width, height;
		uint32_t *pixels =
			harness_capture_output(h, 0, &width, &height);
		bool red = pixels &&
			(pixels[(height / 2) * width + width / 2] & 0xffffff) ==
				0xff0000;

		free(pixels);
		if (red)
			break;
		harness_roundtrip(h);
		usleep(20000);
	}
	expect_wallpaper(h, "start");

	struct mode *first = mode_of(c, mode_to_set(&c->heads[0]));
	int w = first->width;
	const struct layout steps[] = {
		{ { 1, 1 }, { 0, w }, { 1, 1 } },
		{ { 1, 1 }, { 0, 0 }, { 1, 1 } },
		{ { 0, 1 }, { 0, 0 }, { 1, 1 } },
		{ { 1, 0 }, { 0, 0 }, { 1, 1 } },
		{ { 1, 1 }, { 0, w }, { 1, 1 } },
		{ { 1, 1 }, { 0, w }, { 1, 2 } },
	};
	const char *names[] = { "extend",     "mirror", "second only",
				"first only", "extend", "scale 2" };

	for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; i++) {
		CHECK(apply_layout(h, c, &steps[i]) == SUCCEEDED,
		      "%s was not applied", names[i]);
		expect_wallpaper(h, names[i]);
	}

	harness_connect_output(h, c->heads[1].name, false);
	for (int i = 0; i < 20; i++)
		harness_roundtrip(h);
	expect_wallpaper(h, "unplug");
	harness_connect_output(h, c->heads[1].name, true);
	for (int i = 0; i < 20; i++)
		harness_roundtrip(h);
	expect_wallpaper(h, "plug");

	unlink(path);
	CHECK(failures == 0, "%d dark outputs", failures);
	client_free(c);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
