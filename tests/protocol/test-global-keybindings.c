/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Global command bindings launch the configured command through Polo's
 * shared child launcher, and panel-run-dialog uses mate-panel's supported
 * command-line interface. A command that cannot run shows Marco's zenity
 * error (error_on_command ()). The binding press and release never reach
 * the focused client.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/input.h>

#include "harness.h"

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
	/* One append per line: launched children run concurrently. */
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
	int i;

	for (i = 0; i < 100 && line_count(path) < wanted; i++)
		usleep(10000);
	CHECK(line_count(path) == wanted, "wanted %d command records, got %d",
	      wanted, line_count(path));
}

int
main(int argc, char **argv)
{
	char directory[] = "/tmp/polo-global-bindings-XXXXXX";
	char log_path[512];
	char new_path[2048];
	char contents[4096];
	const char *old_path;
	struct harness *h;
	struct harness_window *window;
	FILE *log;
	size_t length;
	int status;

	CHECK(mkdtemp(directory) != NULL, "mkdtemp: %s", strerror(errno));
	snprintf(log_path, sizeof log_path, "%s/actions", directory);
	make_recorder(directory, "polo-action-recorder");
	make_recorder(directory, "polo-screenshot");
	make_recorder(directory, "mate-panel");
	make_recorder(directory, "mate-terminal");
	make_recorder(directory, "zenity");
	old_path = getenv("PATH");
	snprintf(new_path, sizeof new_path, "%s:%s", directory,
		 old_path ? old_path : "/usr/bin");
	setenv("PATH", new_path, 1);
	setenv("POLO_ACTION_LOG", log_path, 1);
	setenv("POLO_TEST_PREFS",
	       "global-keybindings/run-command-1='<Control><Shift>1';"
	       "global-keybindings/run-command-2='<Control><Shift>2';"
	       "global-keybindings/run-command-screenshot='<Control><Shift>s';"
	       "global-keybindings/"
	       "run-command-window-screenshot='<Control><Shift>w';"
	       "global-keybindings/run-command-terminal='<Control><Shift>t';"
	       "global-keybindings/panel-run-dialog='<Control><Shift>r';"
	       "keybinding-commands/command-1='polo-action-recorder command-1 "
	       "two';"
	       "keybinding-commands/command-2=''",
	       1);

	h = harness_start(argc, argv);
	window = harness_window_create(h, 400, 300, "bindings");
	CHECK(h->keyboard_focus == window->surface, "window not focused");

	CHECK(tap(h, KEY_1) == 0, "run-command-1 reached the client");
	wait_for_lines(log_path, 1);
	CHECK(tap(h, KEY_2) == 0, "empty run-command-2 reached the client");
	wait_for_lines(log_path, 2);
	CHECK(tap(h, KEY_S) == 0, "screenshot command reached the client");
	wait_for_lines(log_path, 3);
	CHECK(tap(h, KEY_W) == 0,
	      "window screenshot command reached the client");
	wait_for_lines(log_path, 4);
	CHECK(tap(h, KEY_R) == 0, "panel run-dialog reached the client");
	wait_for_lines(log_path, 5);
	CHECK(tap(h, KEY_T) == 0, "terminal command reached the client");
	wait_for_lines(log_path, 6);

	log = fopen(log_path, "r");
	CHECK(log != NULL, "fopen %s: %s", log_path, strerror(errno));
	length = fread(contents, 1, sizeof contents - 1, log);
	CHECK(length > 0, "empty action log");
	contents[length] = '\0';
	fclose(log);
	CHECK(strstr(contents, "polo-action-recorder:command-1:two\n") != NULL,
	      "configured command and arguments were not preserved: %s",
	      contents);
	CHECK(strstr(contents, "\nzenity:--error:--title:") != NULL &&
		      strstr(contents, ":--text:") != NULL &&
		      strstr(contents, "<tt>") == NULL,
	      "a blank command did not show Marco's error alone: %s", contents);
	CHECK(strstr(contents, "polo-screenshot\n") != NULL,
	      "screenshot command was not launched: %s", contents);
	CHECK(strstr(contents, "polo-screenshot:--geometry=") != NULL,
	      "window screenshot command was not launched: %s", contents);
	CHECK(strstr(contents, "mate-panel:--run-dialog\n") != NULL,
	      "panel run dialog was not launched: %s", contents);
	CHECK(strstr(contents, "mate-terminal\n") != NULL,
	      "configured terminal was not launched: %s", contents);

	harness_window_destroy(window);
	status = harness_stop(h);
	CHECK(status == 0, "polo exited with %d on SIGTERM", status);

	unlink(log_path);
	snprintf(contents, sizeof contents, "%s/polo-action-recorder",
		 directory);
	unlink(contents);
	snprintf(contents, sizeof contents, "%s/polo-screenshot", directory);
	unlink(contents);
	snprintf(contents, sizeof contents, "%s/mate-panel", directory);
	unlink(contents);
	snprintf(contents, sizeof contents, "%s/mate-terminal", directory);
	unlink(contents);
	snprintf(contents, sizeof contents, "%s/zenity", directory);
	unlink(contents);
	rmdir(directory);
	printf("ok\n");
	return 0;
}
