/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * org.freedesktop.a11y.KeyboardMonitor on a private session bus, with
 * fake-orca.py as the screen reader (copied as "orca", or "notorca"):
 * only orca with the screen reader turned on may use it; a watching reader
 * sees keys the window still gets; a grabbed modifier takes itself and every
 * key pressed with it, a lone press is dropped and a second one passes; a
 * grabbed keystroke and a full grab keep keys from the window; and when the
 * reader leaves, its grabs go with it.
 *
 *   test-keyboard-monitor POLO FAKE-ORCA
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"

#define XK_a 0x61
#define XK_h 0x68
#define XK_Insert 0xff63
#define XK_F12 0xffc9

struct reader {
	pid_t pid;
	int commands;
	char log[300];
};

static char dir[] = "/tmp/polo-keyboard-monitor-XXXXXX";
static bool dir_made;
static pid_t bus_pid;
static pid_t reader_pid;

static void
cleanup(void)
{
	char command[128];

	if (reader_pid > 0)
		kill(reader_pid, SIGKILL);
	if (bus_pid > 0)
		kill(bus_pid, SIGTERM);
	if (dir_made) {
		snprintf(command, sizeof command, "rm -rf '%s'", dir);
		if (system(command) != 0)
			fprintf(stderr, "cannot remove %s\n", dir);
	}
}

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static void
start_bus(void)
{
	int fds[2];
	char address[512] = "", descriptor[48];
	ssize_t n = 0;

	CHECK(pipe(fds) == 0, "pipe");
	bus_pid = fork();
	if (bus_pid == 0) {
		int null = open("/dev/null", O_WRONLY);

		dup2(null, STDOUT_FILENO);
		dup2(null, STDERR_FILENO);
		close(fds[0]);
		snprintf(descriptor, sizeof descriptor, "--print-address=%d",
			 fds[1]);
		execlp("dbus-daemon", "dbus-daemon", "--session", "--nofork",
		       "--nopidfile", descriptor, (char *)NULL);
		_exit(127);
	}
	close(fds[1]);
	for (int i = 0; i < 100 && !strchr(address, '\n'); i++) {
		ssize_t got = read(fds[0], address + n, sizeof address - 1 - n);

		if (got > 0)
			n += got;
		address[n] = '\0';
	}
	close(fds[0]);
	CHECK(strchr(address, '\n'), "dbus-daemon gave no address");
	*strchr(address, '\n') = '\0';
	setenv("DBUS_SESSION_BUS_ADDRESS", address, 1);
}

static bool
logged(struct reader *r, const char *line)
{
	FILE *file = fopen(r->log, "r");
	char buf[256];
	bool found = false;

	while (file && !found && fgets(buf, sizeof buf, file)) {
		buf[strcspn(buf, "\n")] = '\0';
		found = strcmp(buf, line) == 0;
	}
	if (file)
		fclose(file);
	return found;
}

static void
wait_logged(struct harness *h, struct reader *r, const char *line)
{
	for (int i = 0; i < 300 && !logged(r, line); i++)
		pause_ms(h, 20);
	CHECK(logged(r, line), "%s never logged \"%s\"", r->log, line);
}

static void
copy_as(const char *from, const char *name, char *path, size_t size)
{
	char command[1024];

	snprintf(path, size, "%s/%s", dir, name);
	snprintf(command, sizeof command, "cp '%s' '%s'", from, path);
	CHECK(system(command) == 0, "cannot copy %s", from);
}

static struct reader
start_reader(const char *script, const char *name)
{
	struct reader r;
	char path[300];
	int fds[2];

	copy_as(script, name, path, sizeof path);
	snprintf(r.log, sizeof r.log, "%s/%s.log", dir, name);
	unlink(r.log);
	close(open(r.log, O_CREAT | O_WRONLY, 0600));
	CHECK(pipe(fds) == 0, "pipe");
	r.pid = fork();
	if (r.pid == 0) {
		int log = open(r.log, O_WRONLY | O_APPEND);

		dup2(log, STDOUT_FILENO);
		dup2(log, STDERR_FILENO);
		dup2(fds[0], STDIN_FILENO);
		close(fds[0]);
		close(fds[1]);
		execlp("python3", "python3", path, r.log, (char *)NULL);
		_exit(127);
	}
	close(fds[0]);
	r.commands = fds[1];
	reader_pid = r.pid;
	return r;
}

static void
tell(struct reader *r, const char *command)
{
	CHECK(write(r->commands, command, strlen(command)) ==
		      (ssize_t)strlen(command),
	      "cannot tell the reader %s", command);
}

static void
stop_reader(struct reader *r)
{
	tell(r, "quit\n");
	close(r->commands);
	waitpid(r->pid, NULL, 0);
	reader_pid = 0;
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
	pause_ms(h, 30);
}

static bool
window_got(struct harness *h, uint32_t key)
{
	bool got = h->last_key == key;

	h->last_key = 0;
	return got;
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w;
	struct reader r;
	char line[128];

	CHECK(argc >= 3, "usage: %s POLO FAKE-ORCA", argv[0]);
	CHECK(mkdtemp(dir), "mkdtemp: %s", strerror(errno));
	dir_made = true;
	atexit(cleanup);
	start_bus();
	setenv("POLO_TEST_A11Y", "1", 1);
	h = harness_start(argc, argv);
	w = harness_window_create(h, 300, 200, "typing");
	harness_wait_frame(h, w->surface);

	r = start_reader(argv[2], "notorca");
	wait_logged(h, &r, "watch denied");
	stop_reader(&r);

	r = start_reader(argv[2], "orca");
	wait_logged(h, &r, "watch denied");
	stop_reader(&r);

	harness_set_setting(h, "org.gnome.desktop.a11y.applications",
			    "screen-reader-enabled", "true");
	r = start_reader(argv[2], "orca");
	wait_logged(h, &r, "watch ok");

	window_got(h, 0);
	tap(h, KEY_A);
	snprintf(line, sizeof line, "key 0 0 %d %d %d", XK_a, 'a', KEY_A + 8);
	wait_logged(h, &r, line);
	snprintf(line, sizeof line, "key 1 0 %d %d %d", XK_a, 'a', KEY_A + 8);
	wait_logged(h, &r, line);
	CHECK(window_got(h, KEY_A), "watching took the key from the window");

	snprintf(line, sizeof line, "keygrabs %d %d:0\n", XK_Insert, XK_F12);
	tell(&r, line);
	wait_logged(h, &r, "keygrabs ok");

	harness_key(h, KEY_INSERT, true);
	window_got(h, 0);
	tap(h, KEY_H);
	CHECK(!window_got(h, KEY_H),
	      "a key pressed with the reader's "
	      "modifier reached the window");
	harness_key(h, KEY_INSERT, false);
	pause_ms(h, 50);
	snprintf(line, sizeof line, "key 0 0 %d %d %d", XK_h, 'h', KEY_H + 8);
	wait_logged(h, &r, line);

	pause_ms(h, 600);
	tap(h, KEY_INSERT);
	CHECK(!window_got(h, KEY_INSERT),
	      "a lone press of the reader's modifier reached the window");
	tap(h, KEY_INSERT);
	CHECK(window_got(h, KEY_INSERT),
	      "a second press of the reader's modifier did not pass");

	pause_ms(h, 600);
	tap(h, KEY_F12);
	CHECK(!window_got(h, KEY_F12),
	      "a grabbed keystroke reached the window");
	snprintf(line, sizeof line, "key 0 0 %d 0 %d", XK_F12, KEY_F12 + 8);
	wait_logged(h, &r, line);

	tell(&r, "grab\n");
	wait_logged(h, &r, "grab ok");
	tap(h, KEY_B);
	CHECK(!window_got(h, KEY_B), "a full grab let a key reach the window");
	tell(&r, "ungrab\n");
	wait_logged(h, &r, "ungrab ok");
	tap(h, KEY_C);
	CHECK(window_got(h, KEY_C), "after ungrab the window got no key");

	stop_reader(&r);
	pause_ms(h, 300);
	harness_key(h, KEY_INSERT, true);
	window_got(h, 0);
	tap(h, KEY_H);
	CHECK(window_got(h, KEY_H), "the reader's grabs outlived it");
	harness_key(h, KEY_INSERT, false);
	pause_ms(h, 50);
	tap(h, KEY_F12);
	CHECK(window_got(h, KEY_F12),
	      "the reader's keystroke grab outlived it");

	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	kill(bus_pid, SIGTERM);
	waitpid(bus_pid, NULL, 0);
	bus_pid = 0;
	return 0;
}
