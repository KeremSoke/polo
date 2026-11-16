/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate_output_manager_v1 on fake monitors: capabilities, ids (twins told
 * apart by serial), EDID identity, display names, the primary set with a
 * configuration, temporary apply (monitors.xml untouched until a normal
 * apply), mirroring, the wl_output side (work area, background owner),
 * finished on unplug, and the protocol errors.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "mate-output-v1-client-protocol.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"

static const int modes[][3] = { { 1024, 768, 60000 } };

struct head {
	struct zwlr_output_head_v1 *wlr;
	struct mate_output_head_v1 *mate;
	struct zwlr_output_mode_v1 *mode;
	char name[64], id[256], display_name[128], mirror_of[256];
	char vendor[8];
	uint32_t product, serial;
	int internal, primary, enabled, finished, dones;
};

struct out {
	struct wl_output *wl;
	struct mate_output_v1 *mate;
	char id[256];
	int primary, x, y, width, height, owner, dones;
};

struct client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct zwlr_output_manager_v1 *wlr;
	struct mate_output_manager_v1 *mate;
	uint32_t capabilities;
	int has_capabilities;
	struct head heads[8];
	int n_heads;
	struct out outs[8];
	int n_outs;
	uint32_t serial;
};

static struct head *
head_of_wlr(struct client *c, struct zwlr_output_head_v1 *wlr)
{
	for (int i = 0; i < c->n_heads; i++) {
		if (c->heads[i].wlr == wlr)
			return &c->heads[i];
	}
	return NULL;
}

static struct head *
head_of_mate(struct client *c, struct mate_output_head_v1 *mate)
{
	for (int i = 0; i < c->n_heads; i++) {
		if (c->heads[i].mate == mate)
			return &c->heads[i];
	}
	return NULL;
}

static struct head *
head_named(struct client *c, const char *name)
{
	for (int i = 0; i < c->n_heads; i++) {
		if (strcmp(c->heads[i].name, name) == 0 &&
		    !c->heads[i].finished)
			return &c->heads[i];
	}
	CHECK(false, "no head %s", name);
	return NULL;
}

/* ---- wlr heads --------------------------------------------------------- */

static void
wlr_name(void *data, struct zwlr_output_head_v1 *wlr, const char *name)
{
	snprintf(head_of_wlr(data, wlr)->name, 64, "%s", name);
}

static void
wlr_string(void *data, struct zwlr_output_head_v1 *wlr, const char *s)
{
}

static void
wlr_size(void *data, struct zwlr_output_head_v1 *wlr, int32_t w, int32_t h)
{
}

static void
mode_size(void *data, struct zwlr_output_mode_v1 *m, int32_t w, int32_t h)
{
}

static void
mode_refresh(void *data, struct zwlr_output_mode_v1 *m, int32_t r)
{
}

static void
mode_nothing(void *data, struct zwlr_output_mode_v1 *m)
{
}

static const struct zwlr_output_mode_v1_listener mode_listener = {
	mode_size, mode_refresh, mode_nothing, mode_nothing
};

static void
wlr_mode(void *data, struct zwlr_output_head_v1 *wlr,
	 struct zwlr_output_mode_v1 *mode)
{
	head_of_wlr(data, wlr)->mode = mode;
	zwlr_output_mode_v1_add_listener(mode, &mode_listener, data);
}

static void
wlr_enabled(void *data, struct zwlr_output_head_v1 *wlr, int32_t enabled)
{
	head_of_wlr(data, wlr)->enabled = enabled;
}

static void
wlr_current(void *data, struct zwlr_output_head_v1 *wlr,
	    struct zwlr_output_mode_v1 *mode)
{
}

static void
wlr_position(void *data, struct zwlr_output_head_v1 *wlr, int32_t x, int32_t y)
{
}

static void
wlr_int(void *data, struct zwlr_output_head_v1 *wlr, int32_t v)
{
}

static void
wlr_scale(void *data, struct zwlr_output_head_v1 *wlr, wl_fixed_t scale)
{
}

static void
wlr_finished(void *data, struct zwlr_output_head_v1 *wlr)
{
}

static void
wlr_adaptive_sync(void *data, struct zwlr_output_head_v1 *wlr, uint32_t s)
{
}

static const struct zwlr_output_head_v1_listener wlr_head_listener = {
	.name = wlr_name,
	.description = wlr_string,
	.physical_size = wlr_size,
	.mode = wlr_mode,
	.enabled = wlr_enabled,
	.current_mode = wlr_current,
	.position = wlr_position,
	.transform = wlr_int,
	.scale = wlr_scale,
	.finished = wlr_finished,
	.make = wlr_string,
	.model = wlr_string,
	.serial_number = wlr_string,
	.adaptive_sync = wlr_adaptive_sync,
};

static void
wlr_manager_head(void *data, struct zwlr_output_manager_v1 *manager,
		 struct zwlr_output_head_v1 *wlr)
{
	struct client *c = data;
	struct head *h = &c->heads[c->n_heads++];

	memset(h, 0, sizeof *h);
	h->wlr = wlr;
	zwlr_output_head_v1_add_listener(wlr, &wlr_head_listener, c);
}

static void
wlr_manager_done(void *data, struct zwlr_output_manager_v1 *manager,
		 uint32_t serial)
{
	((struct client *)data)->serial = serial;
}

static void
wlr_manager_finished(void *data, struct zwlr_output_manager_v1 *manager)
{
}

static const struct zwlr_output_manager_v1_listener wlr_manager_listener = {
	wlr_manager_head, wlr_manager_done, wlr_manager_finished
};

/* ---- mate heads -------------------------------------------------------- */

static void
mh_id(void *data, struct mate_output_head_v1 *m, const char *id)
{
	snprintf(head_of_mate(data, m)->id, 256, "%s", id);
}

static void
mh_identity(void *data, struct mate_output_head_v1 *m, const char *vendor,
	    uint32_t product, uint32_t serial, const char *serial_string)
{
	struct head *h = head_of_mate(data, m);

	snprintf(h->vendor, sizeof h->vendor, "%s", vendor);
	h->product = product;
	h->serial = serial;
}

static void
mh_edid(void *data, struct mate_output_head_v1 *m, int32_t fd, uint32_t size)
{
	close(fd);
}

static void
mh_display_name(void *data, struct mate_output_head_v1 *m, const char *name)
{
	snprintf(head_of_mate(data, m)->display_name, 128, "%s", name);
}

static void
mh_internal(void *data, struct mate_output_head_v1 *m, uint32_t internal)
{
	head_of_mate(data, m)->internal = internal;
}

static void
mh_primary(void *data, struct mate_output_head_v1 *m, uint32_t primary)
{
	head_of_mate(data, m)->primary = primary;
}

static void
mh_mirror_of(void *data, struct mate_output_head_v1 *m, const char *id)
{
	snprintf(head_of_mate(data, m)->mirror_of, 256, "%s", id);
}

static void
mh_done(void *data, struct mate_output_head_v1 *m)
{
	head_of_mate(data, m)->dones++;
}

static void
mh_finished(void *data, struct mate_output_head_v1 *m)
{
	head_of_mate(data, m)->finished = 1;
}

static const struct mate_output_head_v1_listener mate_head_listener = {
	mh_id,      mh_identity,  mh_edid, mh_display_name, mh_internal,
	mh_primary, mh_mirror_of, mh_done, mh_finished,
};

/* ---- mate outputs ------------------------------------------------------ */

static struct out *
out_of(struct client *c, struct mate_output_v1 *m)
{
	for (int i = 0; i < c->n_outs; i++) {
		if (c->outs[i].mate == m)
			return &c->outs[i];
	}
	return NULL;
}

static void
mo_id(void *data, struct mate_output_v1 *m, const char *id)
{
	snprintf(out_of(data, m)->id, 256, "%s", id);
}

static void
mo_identity(void *data, struct mate_output_v1 *m, const char *vendor,
	    uint32_t product, uint32_t serial, const char *serial_string)
{
}

static void
mo_edid(void *data, struct mate_output_v1 *m, int32_t fd, uint32_t size)
{
	close(fd);
}

static void
mo_string(void *data, struct mate_output_v1 *m, const char *s)
{
}

static void
mo_uint(void *data, struct mate_output_v1 *m, uint32_t v)
{
}

static void
mo_primary(void *data, struct mate_output_v1 *m, uint32_t primary)
{
	out_of(data, m)->primary = primary;
}

static void
mo_workarea(void *data, struct mate_output_v1 *m, int32_t x, int32_t y,
	    uint32_t width, uint32_t height)
{
	struct out *o = out_of(data, m);

	o->x = x;
	o->y = y;
	o->width = width;
	o->height = height;
}

static void
mo_owner(void *data, struct mate_output_v1 *m, uint32_t owner)
{
	out_of(data, m)->owner = owner;
}

static void
mo_done(void *data, struct mate_output_v1 *m)
{
	out_of(data, m)->dones++;
}

static const struct mate_output_v1_listener mate_out_listener = {
	mo_id,      mo_identity, mo_edid,     mo_string, mo_uint,
	mo_primary, mo_string,   mo_workarea, mo_owner,  mo_done,
};

static void
capabilities(void *data, struct mate_output_manager_v1 *m, uint32_t caps)
{
	struct client *c = data;

	c->capabilities = caps;
	c->has_capabilities = 1;
}

static void
manager_finished(void *data, struct mate_output_manager_v1 *m)
{
}

static const struct mate_output_manager_v1_listener mate_manager_listener = {
	capabilities, manager_finished
};

/* ---- client ------------------------------------------------------------ */

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct client *c = data;

	if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
		c->wlr = wl_registry_bind(registry, name,
					  &zwlr_output_manager_v1_interface, 4);
		zwlr_output_manager_v1_add_listener(c->wlr,
						    &wlr_manager_listener, c);
	} else if (strcmp(interface, mate_output_manager_v1_interface.name) ==
		   0) {
		c->mate = wl_registry_bind(
			registry, name, &mate_output_manager_v1_interface, 1);
		mate_output_manager_v1_add_listener(c->mate,
						    &mate_manager_listener, c);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		struct out *o = &c->outs[c->n_outs++];

		memset(o, 0, sizeof *o);
		o->wl = wl_registry_bind(registry, name, &wl_output_interface,
					 2);
	}
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
settle(struct harness *h, struct client *c)
{
	for (int i = 0; i < 3; i++) {
		harness_roundtrip(h);
		wl_display_roundtrip(c->display);
	}
}

static struct client *
connect_client(struct harness *h)
{
	struct client *c = calloc(1, sizeof *c);

	c->display = wl_display_connect(h->socket);
	CHECK(c->display, "cannot connect");
	c->registry = wl_display_get_registry(c->display);
	wl_registry_add_listener(c->registry, &registry_listener, c);
	wl_display_roundtrip(c->display);
	CHECK(c->wlr, "no zwlr_output_manager_v1");
	CHECK(c->mate, "no mate_output_manager_v1");
	wl_display_roundtrip(c->display);
	for (int i = 0; i < c->n_heads; i++) {
		c->heads[i].mate = mate_output_manager_v1_get_head(
			c->mate, c->heads[i].wlr);
		mate_output_head_v1_add_listener(c->heads[i].mate,
						 &mate_head_listener, c);
	}
	for (int i = 0; i < c->n_outs; i++) {
		c->outs[i].mate = mate_output_manager_v1_get_output(
			c->mate, c->outs[i].wl);
		mate_output_v1_add_listener(c->outs[i].mate, &mate_out_listener,
					    c);
	}
	settle(h, c);
	return c;
}

static void
disconnect_client(struct client *c)
{
	for (int i = 0; i < c->n_heads; i++) {
		mate_output_head_v1_destroy(c->heads[i].mate);
		if (c->heads[i].mode)
			zwlr_output_mode_v1_destroy(c->heads[i].mode);
		zwlr_output_head_v1_destroy(c->heads[i].wlr);
	}
	for (int i = 0; i < c->n_outs; i++) {
		mate_output_v1_destroy(c->outs[i].mate);
		wl_output_destroy(c->outs[i].wl);
	}
	mate_output_manager_v1_destroy(c->mate);
	zwlr_output_manager_v1_destroy(c->wlr);
	wl_registry_destroy(c->registry);
	wl_display_disconnect(c->display);
	free(c);
}

enum { PENDING, SUCCEEDED, FAILED, CANCELLED };

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
	config_succeeded, config_failed, config_cancelled
};

/* Every head on at its mode, in a row (or the two named ones at 0,0 when
 * mirror is set), with primary and temporary as asked. */
static int
apply(struct harness *h, struct client *c, const char *primary, int temporary,
      const char *mirror_a, const char *mirror_b)
{
	struct zwlr_output_configuration_v1 *config =
		zwlr_output_manager_v1_create_configuration(c->wlr, c->serial);
	struct mate_output_configuration_v1 *mate =
		mate_output_manager_v1_get_configuration(c->mate, config);
	struct zwlr_output_configuration_head_v1 *chs[8] = { 0 };
	int result = PENDING, x = 0;

	zwlr_output_configuration_v1_add_listener(config, &config_listener,
						  &result);
	for (int i = 0; i < c->n_heads; i++) {
		struct head *hd = &c->heads[i];
		int mirrored = mirror_a &&
			(strcmp(hd->name, mirror_a) == 0 ||
			 strcmp(hd->name, mirror_b) == 0);

		if (hd->finished)
			continue;
		chs[i] = zwlr_output_configuration_v1_enable_head(config,
								  hd->wlr);
		zwlr_output_configuration_head_v1_set_mode(chs[i], hd->mode);
		zwlr_output_configuration_head_v1_set_position(
			chs[i], mirrored ? 0 : x + (mirror_a ? 1024 : 0), 0);
		if (!mirrored)
			x += 1024;
		if (primary && strcmp(hd->name, primary) == 0)
			mate_output_configuration_v1_set_primary(mate, chs[i]);
	}
	if (temporary)
		mate_output_configuration_v1_set_temporary(mate);
	zwlr_output_configuration_v1_apply(config);
	for (int i = 0; i < 200 && result == PENDING; i++)
		settle(h, c);
	mate_output_configuration_v1_destroy(mate);
	zwlr_output_configuration_v1_destroy(config);
	for (int i = 0; i < 8; i++) {
		if (chs[i])
			zwlr_output_configuration_head_v1_destroy(chs[i]);
	}
	settle(h, c);
	return result;
}

static char saved_xml[16384];

static int
read_monitors_xml(struct harness *h, char *buffer, size_t size)
{
	char path[512];
	FILE *file;
	size_t length;

	snprintf(path, sizeof path, "%s/mate/polo/monitors.xml", h->config_dir);
	file = fopen(path, "r");
	if (!file)
		return 0;
	length = fread(buffer, 1, size - 1, file);
	buffer[length] = '\0';
	fclose(file);
	return length > 0;
}

/* Waits for monitors.xml to differ from saved_xml, then keeps the new text. */
static int
monitors_xml_changed(struct harness *h)
{
	static char now[sizeof saved_xml];

	for (int i = 0; i < 300; i++) {
		if (read_monitors_xml(h, now, sizeof now) &&
		    strcmp(now, saved_xml) != 0) {
			memcpy(saved_xml, now, sizeof now);
			return 1;
		}
		harness_roundtrip(h);
		usleep(10000);
	}
	return 0;
}

static int
primaries(struct client *c)
{
	int n = 0;

	for (int i = 0; i < c->n_heads; i++)
		n += !c->heads[i].finished && c->heads[i].primary;
	return n;
}

static void
expect_error(struct client *c, const char *what)
{
	wl_display_roundtrip(c->display);
	CHECK(wl_display_get_error(c->display) != 0, "%s was accepted", what);
}

int
main(int argc, char **argv)
{
	setenv("POLO_TEST_MANAGED_OUTPUTS", "1", 1);
	struct harness *h = harness_start(argc, argv);

	harness_add_output(h, "eDP-1", "BOE", 0x0a34, 0, "Panel", true, 344,
			   215, modes, 1);
	harness_add_output(h, "DP-1", "QQA", 0x0101, 11, "Twin", false, 600,
			   340, modes, 1);
	harness_add_output(h, "DP-2", "QQA", 0x0101, 22, "Twin", false, 600,
			   340, modes, 1);

	harness_connect_output(h, "headless0", false);

	struct client *c = connect_client(h);

	CHECK(c->has_capabilities && c->capabilities == 7,
	      "capabilities %u, not wallpaper, per output and spanned",
	      c->capabilities);
	CHECK(c->n_heads == 3, "%d heads", c->n_heads);
	struct head *panel = head_named(c, "eDP-1");
	struct head *left = head_named(c, "DP-1");
	struct head *right = head_named(c, "DP-2");

	CHECK(left->dones > 0 && right->dones > 0, "no done on the heads");
	CHECK(strcmp(left->id, "PNP_QQA_-Twin-s0000000b") == 0,
	      "DP-1's id is %s", left->id);
	CHECK(strcmp(right->id, "PNP_QQA_-Twin-s00000016") == 0,
	      "DP-2's id is %s", right->id);
	CHECK(strcmp(left->vendor, "QQA") == 0 && left->product == 0x0101 &&
		      left->serial == 11,
	      "DP-1's identity is %s 0x%x %u", left->vendor, left->product,
	      left->serial);
	CHECK(panel->internal && !left->internal, "internal flags");
	CHECK(strcmp(panel->display_name, "Laptop") == 0,
	      "the panel is named %s", panel->display_name);
	CHECK(strncmp(left->display_name, "PNP(QQA) 27", 11) == 0,
	      "DP-1 is named %s", left->display_name);

	CHECK(apply(h, c, NULL, 0, NULL, NULL) == SUCCEEDED, "all on failed");
	CHECK(primaries(c) == 1, "%d primary heads", primaries(c));

	CHECK(monitors_xml_changed(h), "an ordinary layout was not saved");
	CHECK(apply(h, c, "DP-2", 1, NULL, NULL) == SUCCEEDED,
	      "a temporary layout with DP-2 primary failed");
	CHECK(right->primary && primaries(c) == 1,
	      "DP-2 did not become primary");
	CHECK(!monitors_xml_changed(h),
	      "a temporary layout changed monitors.xml");
	CHECK(apply(h, c, "DP-2", 0, NULL, NULL) == SUCCEEDED,
	      "keeping the layout failed");
	CHECK(monitors_xml_changed(h), "keeping the layout did not save it");

	CHECK(apply(h, c, "DP-2", 1, "DP-1", "DP-2") == SUCCEEDED,
	      "mirroring failed");
	CHECK(left->mirror_of[0] &&
		      strcmp(left->mirror_of, right->mirror_of) == 0,
	      "mirror_of: DP-1 '%s', DP-2 '%s'", left->mirror_of,
	      right->mirror_of);
	CHECK(strcmp(left->mirror_of, left->id) == 0 ||
		      strcmp(left->mirror_of, right->id) == 0,
	      "mirror_of names neither twin: %s", left->mirror_of);
	CHECK(!panel->mirror_of[0], "the panel mirrors %s", panel->mirror_of);
	CHECK(apply(h, c, "DP-2", 1, NULL, NULL) == SUCCEEDED,
	      "unmirroring failed");
	CHECK(!left->mirror_of[0] && !right->mirror_of[0], "still mirrored");

	disconnect_client(c);
	c = connect_client(h);
	int outs_with_ids = 0;

	for (int i = 0; i < c->n_outs; i++) {
		struct out *o = &c->outs[i];

		if (!o->dones)
			continue;
		outs_with_ids++;
		CHECK(o->id[0], "a wl_output without an id");
		CHECK(o->width > 0 && o->height > 0, "%s has no work area",
		      o->id);
		CHECK(o->owner == MATE_OUTPUT_V1_BACKGROUND_OWNER_COMPOSITOR,
		      "%s: background owner %d, not the compositor", o->id,
		      o->owner);
	}
	CHECK(outs_with_ids == 3, "%d wl_outputs answered", outs_with_ids);

	harness_remove_output(h, "DP-1");
	settle(h, c);
	CHECK(head_named(c, "DP-2"), "lost DP-2");
	int finished = 0;

	for (int i = 0; i < c->n_heads; i++)
		finished += c->heads[i].finished;
	CHECK(finished == 1, "%d heads finished after unplugging DP-1",
	      finished);
	CHECK(strcmp(head_named(c, "DP-2")->id, "PNP_QQA_-Twin") == 0,
	      "the twin left alone keeps a suffix: %s",
	      head_named(c, "DP-2")->id);
	disconnect_client(c);

	c = connect_client(h);
	struct zwlr_output_configuration_v1 *config =
		zwlr_output_manager_v1_create_configuration(c->wlr, c->serial);
	struct mate_output_configuration_v1 *mate =
		mate_output_manager_v1_get_configuration(c->mate, config);
	struct mate_output_configuration_v1 *second =
		mate_output_manager_v1_get_configuration(c->mate, config);

	expect_error(c, "a second mate configuration");
	wl_proxy_destroy((struct wl_proxy *)second);
	wl_proxy_destroy((struct wl_proxy *)mate);
	wl_proxy_destroy((struct wl_proxy *)config);
	disconnect_client(c);

	c = connect_client(h);
	config = zwlr_output_manager_v1_create_configuration(c->wlr, c->serial);
	mate = mate_output_manager_v1_get_configuration(c->mate, config);
	mate_output_configuration_v1_set_temporary(mate);
	mate_output_configuration_v1_set_temporary(mate);
	expect_error(c, "set_temporary twice");
	wl_proxy_destroy((struct wl_proxy *)mate);
	wl_proxy_destroy((struct wl_proxy *)config);
	disconnect_client(c);

	c = connect_client(h);
	config = zwlr_output_manager_v1_create_configuration(c->wlr, c->serial);
	struct zwlr_output_configuration_v1 *other =
		zwlr_output_manager_v1_create_configuration(c->wlr, c->serial);
	struct zwlr_output_configuration_head_v1 *foreign =
		zwlr_output_configuration_v1_enable_head(other,
							 c->heads[0].wlr);

	mate = mate_output_manager_v1_get_configuration(c->mate, config);
	mate_output_configuration_v1_set_primary(mate, foreign);
	expect_error(c, "a primary from another configuration");
	wl_proxy_destroy((struct wl_proxy *)mate);
	wl_proxy_destroy((struct wl_proxy *)foreign);
	wl_proxy_destroy((struct wl_proxy *)other);
	wl_proxy_destroy((struct wl_proxy *)config);
	disconnect_client(c);

	c = connect_client(h);
	config = zwlr_output_manager_v1_create_configuration(c->wlr, c->serial);
	zwlr_output_configuration_v1_test(config);
	mate = mate_output_manager_v1_get_configuration(c->mate, config);
	expect_error(c, "a mate configuration for a tested configuration");
	wl_proxy_destroy((struct wl_proxy *)mate);
	wl_proxy_destroy((struct wl_proxy *)config);
	disconnect_client(c);

	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
