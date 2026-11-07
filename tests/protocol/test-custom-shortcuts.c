/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Custom shortcuts (mate-settings-daemon's keybindings plugin): an entry
 * under /org/mate/desktop/keybindings/ runs its action with a window
 * focused, and the key never reaches the window; editing the binding moves
 * the shortcut; "disabled" binds nothing; a second entry on a taken key is
 * skipped; an action that cannot run shows the plugin's error.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/input.h>

#include "harness.h"

#define SCHEMA "org.mate.control-center.keybinding:/org/mate/desktop/keybindings/"

static uint32_t
tap(struct harness *h, uint32_t key)
{
	static const uint32_t mods[] = { KEY_LEFTCTRL, KEY_LEFTSHIFT };
	uint32_t seen;
	int i;

	for (i = 0; i < 2; i++)
		harness_key(h, mods[i], true);
	h->last_key = 0;
	harness_key(h, key, true);
	harness_key(h, key, false);
	harness_roundtrip(h);
	seen = h->last_key;
	for (i = 1; i >= 0; i--)
		harness_key(h, mods[i], false);
	harness_roundtrip(h);
	return seen;
}

static void
make_recorder(const char *directory, const char *name)
{
	char path[512];
	FILE *file;

	snprintf(path, sizeof path, "%s/%s", directory, name);
	file = fopen(path, "w");
	CHECK(file != NULL, "fopen %s: %s", path, strerror(errno));
	fputs("#!/bin/sh\nline=$(basename \"$0\")\n"
	      "for arg in \"$@\"; do line=\"$line:$(printf '%s' \"$arg\" | "
	      "tr '\\n' ' ')\"; done\n"
	      "printf '%s\\n' \"$line\" >> \"$POLO_ACTION_LOG\"\n",
	      file);
	CHECK(fclose(file) == 0, "fclose %s: %s", path, strerror(errno));
	CHECK(chmod(path, 0700) == 0, "chmod %s: %s", path, strerror(errno));
}

static int
line_count(const char *path)
{
	char buffer[1024];
	FILE *file = fopen(path, "r");
	int lines = 0;

	if (!file)
		return 0;
	while (fgets(buffer, sizeof buffer, file))
		lines++;
	fclose(file);
	return lines;
}

static void
wait_for_lines(const char *path, int wanted)
{
	for (int i = 0; i < 100 && line_count(path) < wanted; i++)
		usleep(10000);
	CHECK(line_count(path) == wanted, "wanted %d command records, got %d",
	      wanted, line_count(path));
}

static void
set_entry(struct harness *h, const char *entry, const char *action,
	  const char *binding)
{
	char schema[256], value[256];

	snprintf(schema, sizeof schema, SCHEMA "%s/", entry);
	snprintf(value, sizeof value, "'%s'", action);
	harness_set_setting(h, schema, "action", value);
	snprintf(value, sizeof value, "'%s'", binding);
	harness_set_setting(h, schema, "binding", value);
	/* The entries are reloaded from Polo's main loop, a turn after the
	 * requests are answered. */
	for (int i = 0; i < 10; i++) {
		harness_roundtrip(h);
		usleep(20000);
	}
}

static bool
logged(const char *path, const char *line)
{
	char contents[4096];
	FILE *log = fopen(path, "r");
	size_t length;

	if (!log)
		return false;
	length = fread(contents, 1, sizeof contents - 1, log);
	fclose(log);
	contents[length] = '\0';
	return strstr(contents, line) != NULL;
}

int
main(int argc, char **argv)
{
	char directory[] = "/tmp/polo-custom-shortcuts-XXXXXX";
	char log_path[512], new_path[2048], path[512];
	const char *old_path;

	CHECK(mkdtemp(directory) != NULL, "mkdtemp: %s", strerror(errno));
	snprintf(log_path, sizeof log_path, "%s/actions", directory);
	make_recorder(directory, "polo-action-recorder");
	make_recorder(directory, "zenity");
	old_path = getenv("PATH");
	snprintf(new_path, sizeof new_path, "%s:%s", directory,
		 old_path ? old_path : "/usr/bin");
	setenv("PATH", new_path, 1);
	setenv("POLO_ACTION_LOG", log_path, 1);

	struct harness *h = harness_start(argc, argv);
	struct harness_window *window =
		harness_window_create(h, 400, 300, "custom shortcuts");

	CHECK(h->keyboard_focus == window->surface, "window not focused");

	set_entry(h, "custom0", "polo-action-recorder custom zero",
		  "<Control><Shift>k");
	CHECK(tap(h, KEY_K) == 0, "a custom shortcut reached the window");
	wait_for_lines(log_path, 1);
	CHECK(logged(log_path, "polo-action-recorder:custom:zero\n"),
	      "the action did not run with its arguments");

	set_entry(h, "custom0", "polo-action-recorder custom zero",
		  "<Control><Shift>j");
	CHECK(tap(h, KEY_K) == KEY_K, "the old key still fires");
	CHECK(tap(h, KEY_J) == 0, "the new key reached the window");
	wait_for_lines(log_path, 2);

	set_entry(h, "custom1", "polo-action-recorder custom one", "disabled");
	CHECK(tap(h, KEY_L) == KEY_L, "a disabled shortcut took its key");

	set_entry(h, "custom2", "polo-action-recorder custom two",
		  "<Control><Shift>j");
	CHECK(tap(h, KEY_J) == 0, "the shared key reached the window");
	wait_for_lines(log_path, 3);
	CHECK(!logged(log_path, "custom:two"),
	      "a second shortcut on a taken key ran");

	set_entry(h, "custom3", "polo-no-such-command-for-tests",
		  "<Control><Shift>m");
	CHECK(tap(h, KEY_M) == 0, "the failing shortcut reached the window");
	wait_for_lines(log_path, 4);
	CHECK(logged(log_path, "zenity:--error"),
	      "a failing action showed no error");

	harness_window_destroy(window);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");

	unlink(log_path);
	snprintf(path, sizeof path, "%s/polo-action-recorder", directory);
	unlink(path);
	snprintf(path, sizeof path, "%s/zenity", directory);
	unlink(path);
	rmdir(directory);
	return 0;
}
