/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Runs Polo headless with the clients POLO_TEST_EXEC starts, and steps
 * through display changes read from stdin, one per line:
 *
 *   add NAME VENDOR PRODUCT SERIAL MODEL INTERNAL WxH[,WxH...]
 *   connect NAME 0|1 [NAME 0|1 ...]
 *   remove NAME
 *   apply [temporary] [primary=NAME] NAME:off|NAME:X,Y[,SCALE[,WxH]] ...
 *   setting SCHEMA KEY VALUE
 *   wait SECONDS
 *   spawn SCRIPT
 *   restart
 *   state LABEL
 *
 * "state" prints every output as Polo, wlr-output-management and
 * mate-output see it, and saves a screenshot of the whole desktop as
 * LABEL.png in MULTIMON_SHOTS:
 *
 *   state LABEL
 *   output NAME on X Y W H scale S primary P area AX AY AW AH id ID
 *          owner O mirror M
 *   end
 *
 * "spawn" runs a shell script as a client of this Polo, in its own process
 * group; "restart" and the end of the input stop every spawned script
 * first.
 *
 * Errors are printed as "error ..." lines and do not stop the run.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"
#include "mate-output-v1-client-protocol.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"

#define MAX_HEADS 16
#define MAX_MODES 32

struct mode {
	struct zwlr_output_mode_v1 *mode;
	int width, height, refresh;
};

struct head {
	struct zwlr_output_head_v1 *head;
	struct mate_output_head_v1 *mate;
	char name[64], id[128], mirror[128];
	struct mode modes[MAX_MODES];
	int n_modes;
	struct zwlr_output_mode_v1 *current;
	int enabled, x, y, primary;
	wl_fixed_t scale;
	bool gone;
};

struct out {
	struct wl_output *wl;
	struct mate_output_v1 *mate;
	uint32_t global;
	char name[64], id[128];
	int owner;
	bool gone;
};

struct client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct zwlr_output_manager_v1 *manager;
	struct mate_output_manager_v1 *mate;
	struct head heads[MAX_HEADS];
	int n_heads;
	struct out outs[MAX_HEADS];
	int n_outs;
	uint32_t serial;
};

enum { PENDING, SUCCEEDED, FAILED, CANCELLED };

static struct harness *h;
static struct client *c;
static pid_t spawned[16];
static int n_spawned;

static struct head *
head_of(struct zwlr_output_head_v1 *head)
{
	for (int i = 0; i < c->n_heads; i++) {
		if (c->heads[i].head == head)
			return &c->heads[i];
	}
	return NULL;
}

static struct mode *
mode_of(struct zwlr_output_mode_v1 *mode)
{
	for (int i = 0; i < c->n_heads; i++) {
		for (int j = 0; j < c->heads[i].n_modes; j++) {
			if (c->heads[i].modes[j].mode == mode)
				return &c->heads[i].modes[j];
		}
	}
	return NULL;
}

static void
mode_size(void *data, struct zwlr_output_mode_v1 *mode, int32_t w, int32_t ht)
{
	struct mode *m = mode_of(mode);

	if (m) {
		m->width = w;
		m->height = ht;
	}
}

static void
mode_refresh(void *data, struct zwlr_output_mode_v1 *mode, int32_t refresh)
{
	struct mode *m = mode_of(mode);

	if (m)
		m->refresh = refresh;
}

static void
mode_nothing(void *data, struct zwlr_output_mode_v1 *mode)
{
}

static void
mode_finished(void *data, struct zwlr_output_mode_v1 *mode)
{
	struct mode *m = mode_of(mode);

	if (m)
		m->mode = NULL;
	zwlr_output_mode_v1_destroy(mode);
}

static const struct zwlr_output_mode_v1_listener mode_listener = {
	mode_size,
	mode_refresh,
	mode_nothing,
	mode_finished,
};

static void
head_name(void *data, struct zwlr_output_head_v1 *head, const char *name)
{
	snprintf(head_of(head)->name, 64, "%s", name);
}

static void
head_string(void *data, struct zwlr_output_head_v1 *head, const char *s)
{
}

static void
head_size(void *data, struct zwlr_output_head_v1 *head, int32_t w, int32_t ht)
{
}

static void
head_mode(void *data, struct zwlr_output_head_v1 *head,
	  struct zwlr_output_mode_v1 *mode)
{
	struct head *hd = head_of(head);

	if (hd->n_modes < MAX_MODES)
		hd->modes[hd->n_modes++].mode = mode;
	zwlr_output_mode_v1_add_listener(mode, &mode_listener, NULL);
}

static void
head_enabled(void *data, struct zwlr_output_head_v1 *head, int32_t enabled)
{
	head_of(head)->enabled = enabled;
	if (!enabled)
		head_of(head)->current = NULL;
}

static void
head_current_mode(void *data, struct zwlr_output_head_v1 *head,
		  struct zwlr_output_mode_v1 *mode)
{
	head_of(head)->current = mode;
}

static void
head_position(void *data, struct zwlr_output_head_v1 *head, int32_t x,
	      int32_t y)
{
	head_of(head)->x = x;
	head_of(head)->y = y;
}

static void
head_transform(void *data, struct zwlr_output_head_v1 *head, int32_t t)
{
}

static void
head_scale(void *data, struct zwlr_output_head_v1 *head, wl_fixed_t scale)
{
	head_of(head)->scale = scale;
}

static void
head_finished(void *data, struct zwlr_output_head_v1 *head)
{
	struct head *hd = head_of(head);

	hd->gone = true;
	if (hd->mate)
		mate_output_head_v1_destroy(hd->mate);
	hd->mate = NULL;
	zwlr_output_head_v1_release(head);
	hd->head = NULL;
}

static void
head_adaptive_sync(void *data, struct zwlr_output_head_v1 *head, uint32_t s)
{
}

static const struct zwlr_output_head_v1_listener head_listener = {
	.name = head_name,
	.description = head_string,
	.physical_size = head_size,
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

static struct head *
mate_head_of(struct mate_output_head_v1 *m)
{
	for (int i = 0; i < c->n_heads; i++) {
		if (c->heads[i].mate == m)
			return &c->heads[i];
	}
	return NULL;
}

static void
mh_id(void *data, struct mate_output_head_v1 *m, const char *id)
{
	snprintf(mate_head_of(m)->id, 128, "%s", id);
}

static void
mh_identity(void *data, struct mate_output_head_v1 *m, const char *vendor,
	    uint32_t product, uint32_t serial, const char *serial_string)
{
}

static void
mh_edid(void *data, struct mate_output_head_v1 *m, int32_t fd, uint32_t size)
{
	close(fd);
}

static void
mh_string(void *data, struct mate_output_head_v1 *m, const char *s)
{
}

static void
mh_uint(void *data, struct mate_output_head_v1 *m, uint32_t v)
{
}

static void
mh_primary(void *data, struct mate_output_head_v1 *m, uint32_t primary)
{
	mate_head_of(m)->primary = primary;
}

static void
mh_mirror(void *data, struct mate_output_head_v1 *m, const char *id)
{
	snprintf(mate_head_of(m)->mirror, 128, "%s", id);
}

static void
mh_done(void *data, struct mate_output_head_v1 *m)
{
}

static void
mh_finished(void *data, struct mate_output_head_v1 *m)
{
}

static const struct mate_output_head_v1_listener mate_head_listener = {
	mh_id,      mh_identity, mh_edid, mh_string,   mh_uint,
	mh_primary, mh_mirror,   mh_done, mh_finished,
};

static void
manager_head(void *data, struct zwlr_output_manager_v1 *manager,
	     struct zwlr_output_head_v1 *head)
{
	struct head *hd;

	if (c->n_heads == MAX_HEADS) {
		for (int i = 0; i < c->n_heads; i++) {
			if (c->heads[i].gone) {
				memmove(&c->heads[i], &c->heads[i + 1],
					(c->n_heads - i - 1) *
						sizeof c->heads[0]);
				c->n_heads--;
				break;
			}
		}
	}
	hd = &c->heads[c->n_heads++];
	memset(hd, 0, sizeof *hd);
	hd->head = head;
	zwlr_output_head_v1_add_listener(head, &head_listener, NULL);
	if (c->mate) {
		hd->mate = mate_output_manager_v1_get_head(c->mate, head);
		mate_output_head_v1_add_listener(hd->mate, &mate_head_listener,
						 NULL);
	}
}

static void
manager_done(void *data, struct zwlr_output_manager_v1 *manager,
	     uint32_t serial)
{
	c->serial = serial;
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

static struct out *
out_of_mate(struct mate_output_v1 *m)
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
	snprintf(out_of_mate(m)->id, 128, "%s", id);
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
mo_workarea(void *data, struct mate_output_v1 *m, int32_t x, int32_t y,
	    uint32_t width, uint32_t height)
{
}

static void
mo_owner(void *data, struct mate_output_v1 *m, uint32_t owner)
{
	out_of_mate(m)->owner = owner;
}

static void
mo_done(void *data, struct mate_output_v1 *m)
{
}

static const struct mate_output_v1_listener mate_out_listener = {
	mo_id,   mo_identity, mo_edid,     mo_string, mo_uint,
	mo_uint, mo_string,   mo_workarea, mo_owner,  mo_done,
};

static void
wl_out_geometry(void *data, struct wl_output *o, int32_t x, int32_t y,
		int32_t pw, int32_t ph, int32_t subpixel, const char *make,
		const char *model, int32_t transform)
{
}

static void
wl_out_mode(void *data, struct wl_output *o, uint32_t flags, int32_t w,
	    int32_t ht, int32_t refresh)
{
}

static void
wl_out_done(void *data, struct wl_output *o)
{
}

static void
wl_out_scale(void *data, struct wl_output *o, int32_t factor)
{
}

static void
wl_out_name(void *data, struct wl_output *o, const char *name)
{
	snprintf(((struct out *)data)->name, 64, "%s", name);
}

static void
wl_out_description(void *data, struct wl_output *o, const char *d)
{
}

static const struct wl_output_listener wl_out_listener = {
	wl_out_geometry, wl_out_mode, wl_out_done,
	wl_out_scale,    wl_out_name, wl_out_description,
};

static void
add_out(uint32_t name)
{
	struct out *o;

	for (int i = 0; i < c->n_outs; i++) {
		if (c->outs[i].gone) {
			memmove(&c->outs[i], &c->outs[i + 1],
				(c->n_outs - i - 1) * sizeof c->outs[0]);
			c->n_outs--;
			i--;
		}
	}
	if (c->n_outs == MAX_HEADS)
		return;
	o = &c->outs[c->n_outs++];
	memset(o, 0, sizeof *o);
	o->global = name;
	o->owner = -1;
	o->wl = wl_registry_bind(c->registry, name, &wl_output_interface, 4);
	wl_output_add_listener(o->wl, &wl_out_listener, o);
	if (c->mate) {
		o->mate = mate_output_manager_v1_get_output(c->mate, o->wl);
		mate_output_v1_add_listener(o->mate, &mate_out_listener, NULL);
	}
}

static void
mate_capabilities(void *data, struct mate_output_manager_v1 *m, uint32_t caps)
{
}

static void
mate_finished(void *data, struct mate_output_manager_v1 *m)
{
}

static const struct mate_output_manager_v1_listener mate_listener = {
	mate_capabilities,
	mate_finished,
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0 &&
	    version >= 4) {
		c->manager = wl_registry_bind(
			registry, name, &zwlr_output_manager_v1_interface, 4);
	} else if (strcmp(interface, mate_output_manager_v1_interface.name) ==
		   0) {
		c->mate = wl_registry_bind(
			registry, name, &mate_output_manager_v1_interface, 1);
		mate_output_manager_v1_add_listener(c->mate, &mate_listener,
						    NULL);
	} else if (strcmp(interface, wl_output_interface.name) == 0 &&
		   version >= 4) {
		add_out(name);
	}
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	for (int i = 0; i < c->n_outs; i++) {
		struct out *o = &c->outs[i];

		if (o->global != name || o->gone)
			continue;
		o->gone = true;
		if (o->mate)
			mate_output_v1_destroy(o->mate);
		wl_output_release(o->wl);
		o->mate = NULL;
		o->wl = NULL;
	}
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
client_connect(void)
{
	c = calloc(1, sizeof *c);
	c->display = wl_display_connect(h->socket);
	CHECK(c->display, "cannot connect");
	c->registry = wl_display_get_registry(c->display);
	wl_registry_add_listener(c->registry, &registry_listener, NULL);
	wl_display_roundtrip(c->display);
	CHECK(c->manager, "no zwlr_output_manager_v1");
	zwlr_output_manager_v1_add_listener(c->manager, &manager_listener,
					    NULL);
	for (int i = 0; i < c->n_outs; i++) {
		if (!c->outs[i].mate && c->mate) {
			c->outs[i].mate = mate_output_manager_v1_get_output(
				c->mate, c->outs[i].wl);
			mate_output_v1_add_listener(c->outs[i].mate,
						    &mate_out_listener, NULL);
		}
	}
	wl_display_roundtrip(c->display);
	wl_display_roundtrip(c->display);
}

static void
client_free(void)
{
	wl_display_disconnect(c->display);
	free(c);
	c = NULL;
}

static void
settle(int rounds)
{
	for (int i = 0; i < rounds; i++) {
		harness_roundtrip(h);
		wl_display_roundtrip(c->display);
		usleep(20000);
	}
}

static struct head *
head_named(const char *name)
{
	for (int i = 0; i < c->n_heads; i++) {
		if (!c->heads[i].gone && strcmp(c->heads[i].name, name) == 0)
			return &c->heads[i];
	}
	return NULL;
}

static struct zwlr_output_mode_v1 *
pick_mode(struct head *hd, int width, int height)
{
	struct zwlr_output_mode_v1 *best = NULL;
	int best_refresh = -1;

	if (width <= 0) {
		if (hd->current)
			return hd->current;
		for (int i = 0; i < hd->n_modes; i++) {
			if (hd->modes[i].mode)
				return hd->modes[i].mode;
		}
		return NULL;
	}
	for (int i = 0; i < hd->n_modes; i++) {
		struct mode *m = &hd->modes[i];

		if (m->mode && m->width == width && m->height == height &&
		    m->refresh > best_refresh) {
			best = m->mode;
			best_refresh = m->refresh;
		}
	}
	return best;
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

/* apply [temporary] [primary=NAME] NAME:off|NAME:X,Y[,SCALE[,WxH]] ... */
static void
apply(char *args)
{
	struct zwlr_output_configuration_v1 *config;
	struct mate_output_configuration_v1 *mate_config = NULL;
	struct zwlr_output_configuration_head_v1 *chs[MAX_HEADS] = { 0 };
	bool named[MAX_HEADS] = { 0 };
	const char *primary = NULL;
	bool temporary = false;
	int result = PENDING;
	char *save, *tok;

	settle(2);
	config = zwlr_output_manager_v1_create_configuration(c->manager,
							     c->serial);
	zwlr_output_configuration_v1_add_listener(config, &config_listener,
						  &result);
	if (c->mate)
		mate_config = mate_output_manager_v1_get_configuration(c->mate,
								       config);
	for (tok = strtok_r(args, " \t\n", &save); tok;
	     tok = strtok_r(NULL, " \t\n", &save)) {
		char name[64], rest[64];
		struct head *hd;
		int index;

		if (strcmp(tok, "temporary") == 0) {
			temporary = true;
			continue;
		}
		if (strncmp(tok, "primary=", 8) == 0) {
			primary = tok + 8;
			continue;
		}
		if (sscanf(tok, "%63[^:]:%63s", name, rest) != 2 ||
		    !(hd = head_named(name))) {
			printf("error apply: no head for %s\n", tok);
			continue;
		}
		index = hd - c->heads;
		named[index] = true;
		if (strcmp(rest, "off") == 0) {
			zwlr_output_configuration_v1_disable_head(config,
								  hd->head);
			continue;
		}
		int x = 0, y = 0, scale = 1, width = 0, height = 0;
		sscanf(rest, "%d,%d,%d,%dx%d", &x, &y, &scale, &width, &height);
		struct zwlr_output_mode_v1 *mode = pick_mode(hd, width, height);
		if (!mode) {
			printf("error apply: no mode %dx%d on %s\n", width,
			       height, name);
			continue;
		}
		chs[index] = zwlr_output_configuration_v1_enable_head(config,
								      hd->head);
		zwlr_output_configuration_head_v1_set_mode(chs[index], mode);
		zwlr_output_configuration_head_v1_set_position(chs[index], x,
							       y);
		zwlr_output_configuration_head_v1_set_scale(
			chs[index], wl_fixed_from_int(scale));
	}
	for (int i = 0; i < c->n_heads; i++) {
		struct head *hd = &c->heads[i];

		if (hd->gone || named[i])
			continue;
		if (!hd->enabled) {
			zwlr_output_configuration_v1_disable_head(config,
								  hd->head);
			continue;
		}
		chs[i] = zwlr_output_configuration_v1_enable_head(config,
								  hd->head);
		zwlr_output_configuration_head_v1_set_mode(chs[i],
							   pick_mode(hd, 0, 0));
		zwlr_output_configuration_head_v1_set_position(chs[i], hd->x,
							       hd->y);
		zwlr_output_configuration_head_v1_set_scale(chs[i], hd->scale);
	}
	if (mate_config && temporary)
		mate_output_configuration_v1_set_temporary(mate_config);
	if (mate_config && primary) {
		struct head *hd = head_named(primary);

		if (hd && chs[hd - c->heads])
			mate_output_configuration_v1_set_primary(
				mate_config, chs[hd - c->heads]);
		else
			printf("error apply: primary %s is not enabled\n",
			       primary);
	}
	zwlr_output_configuration_v1_apply(config);
	for (int i = 0; i < 300 && result == PENDING; i++)
		settle(1);
	if (result != SUCCEEDED)
		printf("error apply: %s\n",
		       result == FAILED              ? "failed"
			       : result == CANCELLED ? "cancelled"
						     : "no answer");
	if (mate_config)
		mate_output_configuration_v1_destroy(mate_config);
	zwlr_output_configuration_v1_destroy(config);
	for (int i = 0; i < MAX_HEADS; i++) {
		if (chs[i])
			wl_proxy_destroy((struct wl_proxy *)chs[i]);
	}
	settle(2);
}

static int
parse_modes(char *spec, int modes[][3], int max)
{
	int n = 0;
	char *save, *tok;

	for (tok = strtok_r(spec, ",", &save); tok && n < max;
	     tok = strtok_r(NULL, ",", &save)) {
		int refresh = 60000;

		if (sscanf(tok, "%dx%d@%d", &modes[n][0], &modes[n][1],
			   &refresh) >= 2) {
			modes[n][2] = refresh;
			n++;
		}
	}
	return n;
}

static void
shot(const char *label)
{
	const char *dir = getenv("MULTIMON_SHOTS");
	char cmd[1024];

	if (!dir)
		return;
	snprintf(cmd, sizeof cmd,
		 "XDG_RUNTIME_DIR='%s' WAYLAND_DISPLAY='%s' grim '%s/%s.png' "
		 ">/dev/null 2>&1",
		 h->runtime_dir, h->socket, dir, label);
	if (system(cmd) != 0)
		printf("error state: grim failed for %s\n", label);
}

static void
state(const char *label)
{
	settle(3);
	harness_output_state(h, "");
	printf("state %s\n", label);
	for (int i = 0; i < h->n_output_states; i++) {
		const struct harness_output_state *o = &h->output_states[i];
		struct head *hd = head_named(o->name);
		const char *id = hd && hd->id[0] ? hd->id : "-";
		const char *mirror = hd && hd->mirror[0] ? hd->mirror : "-";
		int owner = -1;

		for (int j = 0; j < c->n_outs; j++) {
			if (!c->outs[j].gone &&
			    strcmp(c->outs[j].name, o->name) == 0)
				owner = c->outs[j].owner;
		}
		printf("output %s %s %d %d %d %d scale %d primary %d area %d "
		       "%d %d %d id %s owner %d mirror %s\n",
		       o->name, o->enabled ? "on" : "off", o->x, o->y, o->width,
		       o->height, o->scale, o->primary, o->area_x, o->area_y,
		       o->area_width, o->area_height, id, owner, mirror);
	}
	for (int i = 0; i < h->n_output_states; i++) {
		if (h->output_states[i].enabled) {
			shot(label);
			break;
		}
	}
	printf("end\n");
	fflush(stdout);
}

static void
spawn(const char *script)
{
	pid_t pid;

	if (n_spawned == 16)
		return;
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		setpgid(0, 0);
		setenv("XDG_RUNTIME_DIR", h->runtime_dir, 1);
		setenv("WAYLAND_DISPLAY", h->socket, 1);
		execl("/bin/sh", "sh", script, (char *)NULL);
		_exit(127);
	}
	if (pid > 0) {
		setpgid(pid, pid);
		spawned[n_spawned++] = pid;
	}
}

static void
stop_spawned(void)
{
	for (int i = 0; i < n_spawned; i++)
		kill(-spawned[i], SIGTERM);
	for (int i = 0; i < n_spawned; i++) {
		for (int j = 0; j < 100; j++) {
			if (waitpid(spawned[i], NULL, WNOHANG) != 0)
				break;
			if (j == 50)
				kill(-spawned[i], SIGKILL);
			settle(1);
		}
	}
	n_spawned = 0;
}

static void
start(int argc, char **argv)
{
	h = harness_start(argc, argv);
	client_connect();
}

static void
stop(void)
{
	stop_spawned();
	client_free();
	if (harness_stop(h) != 0)
		printf("error stop: polo did not stop cleanly\n");
	h = NULL;
}

int
main(int argc, char **argv)
{
	char line[2048];

	setenv("POLO_TEST_MANAGED_OUTPUTS", "1", 1);
	start(argc, argv);
	while (fgets(line, sizeof line, stdin)) {
		char cmd[32] = "";
		char *args;

		if (sscanf(line, "%31s", cmd) != 1 || cmd[0] == '#')
			continue;
		args = line + strspn(line, " \t");
		args += strlen(cmd);
		args += strspn(args, " \t");
		args[strcspn(args, "\n")] = '\0';
		if (strcmp(cmd, "add") == 0) {
			char name[64], vendor[8], model[64], spec[512];
			unsigned product, serial;
			int internal, modes[16][3], n;

			if (sscanf(args, "%63s %7s %x %u %63s %d %511s", name,
				   vendor, &product, &serial, model, &internal,
				   spec) != 7 ||
			    (n = parse_modes(spec, modes, 16)) == 0) {
				printf("error add: %s\n", args);
				continue;
			}
			harness_add_output(
				h, name, strcmp(vendor, "-") ? vendor : NULL,
				product, serial,
				strcmp(model, "-") ? model : NULL, internal,
				344, 194, (const int (*)[3])modes, n);
			settle(10);
		} else if (strcmp(cmd, "connect") == 0) {
			char name[64];
			int on, used;
			const char *p = args;

			while (sscanf(p, "%63s %d %n", name, &on, &used) == 2) {
				harness_connect_output(h, name, on);
				p += used;
			}
			settle(10);
		} else if (strcmp(cmd, "remove") == 0) {
			harness_remove_output(h, args);
			settle(10);
		} else if (strcmp(cmd, "apply") == 0) {
			apply(args);
		} else if (strcmp(cmd, "setting") == 0) {
			char schema[256], key[128];
			int used = 0;

			if (sscanf(args, "%255s %127s %n", schema, key,
				   &used) >= 2)
				harness_set_setting(h, schema, key,
						    args + used);
			settle(2);
		} else if (strcmp(cmd, "wait") == 0) {
			double seconds = atof(args);
			struct timespec until;

			clock_gettime(CLOCK_MONOTONIC, &until);
			until.tv_sec += (time_t)seconds;
			until.tv_nsec +=
				(long)((seconds - (time_t)seconds) * 1e9);
			for (;;) {
				struct timespec now;

				settle(1);
				clock_gettime(CLOCK_MONOTONIC, &now);
				if (now.tv_sec > until.tv_sec ||
				    (now.tv_sec == until.tv_sec &&
				     now.tv_nsec >= until.tv_nsec))
					break;
			}
		} else if (strcmp(cmd, "spawn") == 0) {
			spawn(args);
		} else if (strcmp(cmd, "restart") == 0) {
			stop();
			start(argc, argv);
		} else if (strcmp(cmd, "state") == 0) {
			state(args);
		} else {
			printf("error unknown command %s\n", cmd);
		}
		fflush(stdout);
	}
	stop();
	return 0;
}
