/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Privileged globals are denied by default. A client Polo did not start as
 * trusted sees none of them and still sees the ordinary ones; a client Polo
 * started with --exec sees them; a program in a helper Polo started is
 * trusted only while its executable is a root-owned file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"

static const char *const PRIVILEGED[] = {
	"zwlr_layer_shell_v1",
	"zwlr_foreign_toplevel_manager_v1",
	"ext_foreign_toplevel_list_v1",
	"ext_workspace_manager_v1",
	"ext_session_lock_manager_v1",
	"ext_idle_notifier_v1",
	"zwlr_output_manager_v1",
	"zwlr_data_control_manager_v1",
	"ext_data_control_manager_v1",
	"zwlr_gamma_control_manager_v1",
	"ext_image_copy_capture_manager_v1",
	"ext_output_image_capture_source_manager_v1",
	"zwp_input_method_manager_v2",
	"zwp_virtual_keyboard_manager_v1",
	"wp_security_context_manager_v1",
};

static const char *const ORDINARY[] = {
	"wl_compositor",
	"xdg_wm_base",
	"wl_seat",
	"wl_output",
};

struct listing {
	char names[96][96];
	int count;
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct listing *l = data;

	if (l->count < 96)
		snprintf(l->names[l->count++], sizeof l->names[0], "%s",
			 interface);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static bool
has(const struct listing *l, const char *name)
{
	for (int i = 0; i < l->count; i++)
		if (strcmp(l->names[i], name) == 0)
			return true;
	return false;
}

static void
list_globals(struct wl_display *display, struct listing *l)
{
	struct wl_registry *registry = wl_display_get_registry(display);

	memset(l, 0, sizeof *l);
	wl_registry_add_listener(registry, &registry_listener, l);
	wl_display_roundtrip(display);
	wl_registry_destroy(registry);
}

static int
report(const char *path)
{
	struct wl_display *display = wl_display_connect(NULL);
	struct listing l;
	FILE *file;

	if (!display)
		return 2;
	list_globals(display, &l);
	file = fopen(path, "w");
	if (!file)
		return 3;
	for (int i = 0; i < l.count; i++)
		fprintf(file, "%s\n", l.names[i]);
	fclose(file);
	wl_display_disconnect(display);
	return 0;
}

static void
read_listing(const char *path, struct listing *l)
{
	char line[96];
	FILE *file = NULL;

	for (int i = 0; i < 500 && !file; i++) {
		file = fopen(path, "r");
		if (!file)
			usleep(20000);
	}
	CHECK(file, "the --exec client wrote no list of globals");
	memset(l, 0, sizeof *l);
	while (fgets(line, sizeof line, file) && l->count < 96) {
		line[strcspn(line, "\n")] = '\0';
		snprintf(l->names[l->count++], sizeof l->names[0], "%s", line);
	}
	fclose(file);
}

int
main(int argc, char **argv)
{
	char path[128], command[512];
	struct listing l;

	if (argc == 3 && strcmp(argv[1], "--report") == 0)
		return report(argv[2]);
	CHECK(argc >= 2, "usage: %s POLO", argv[0]);

	setenv("POLO_TEST_TRUST", "deny", 1);
	snprintf(path, sizeof path, "/tmp/polo-trust-%d.txt", (int)getpid());
	unlink(path);
	snprintf(command, sizeof command, "%s --report %s", argv[0], path);
	setenv("POLO_TEST_EXEC", command, 1);

	struct harness *h = harness_start(argc, argv);

	list_globals(h->display, &l);
	for (size_t i = 0; i < sizeof PRIVILEGED / sizeof PRIVILEGED[0]; i++)
		CHECK(!has(&l, PRIVILEGED[i]), "an untrusted client sees %s",
		      PRIVILEGED[i]);
	for (size_t i = 0; i < sizeof ORDINARY / sizeof ORDINARY[0]; i++)
		CHECK(has(&l, ORDINARY[i]), "an untrusted client lacks %s",
		      ORDINARY[i]);

	read_listing(path, &l);
	for (size_t i = 0; i < sizeof PRIVILEGED / sizeof PRIVILEGED[0]; i++)
		CHECK(has(&l, PRIVILEGED[i]),
		      "the client started with --exec lacks %s", PRIVILEGED[i]);
	for (size_t i = 0; i < sizeof ORDINARY / sizeof ORDINARY[0]; i++)
		CHECK(has(&l, ORDINARY[i]), "the --exec client lacks %s",
		      ORDINARY[i]);

	unlink(path);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
