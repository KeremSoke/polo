/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * polo-shell-ui's popups take no keyboard, so a screen reader hears them only
 * as announcements: on a private session bus with its own AT-SPI bus,
 * a11y-listen.py logs what Orca would be told. Alt+Tab announces the
 * selected window's title, and an OSD says what it shows. Skips without
 * at-spi-bus-launcher.
 *
 *   test-shell-ui-a11y POLO POLO-SHELL-UI A11Y-LISTEN
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"

static const char *const LAUNCHERS[] = {
	"/usr/libexec/at-spi-bus-launcher",
	"/usr/lib/at-spi2-core/at-spi-bus-launcher",
	"/usr/lib/at-spi-bus-launcher",
};

static const char *const REGISTRIES[] = {
	"/usr/libexec/at-spi2-registryd",
	"/usr/lib/at-spi2-core/at-spi2-registryd",
	"/usr/lib/at-spi2-registryd",
};

static pid_t children[4];
static char log_path[] = "/tmp/polo-a11y-listen-XXXXXX";
static char runtime[] = "/tmp/polo-a11y-run-XXXXXX";
static bool runtime_made;

static void
cleanup(void)
{
	for (int i = 0; i < 4; i++)
		if (children[i] > 0)
			kill(children[i], SIGTERM);
	for (int i = 0; i < 4; i++)
		if (children[i] > 0)
			waitpid(children[i], NULL, 0);
	unlink(log_path);
	if (runtime_made) {
		char command[128];

		snprintf(command, sizeof command, "rm -rf '%s'", runtime);
		if (system(command) != 0)
			fprintf(stderr, "cannot remove %s\n", runtime);
	}
}

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		if (h)
			harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static pid_t
spawn(const char *const argv[], int out)
{
	pid_t pid = fork();

	if (pid == 0) {
		if (out >= 0) {
			dup2(out, STDOUT_FILENO);
			dup2(out, STDERR_FILENO);
		}
		execv(argv[0], (char *const *)argv);
		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}
	return pid;
}

static void
start_bus(void)
{
	int fds[2];
	char address[512] = "", descriptor[48];
	ssize_t n = 0;
	int null = open("/dev/null", O_WRONLY);

	CHECK(pipe(fds) == 0, "pipe");
	snprintf(descriptor, sizeof descriptor, "--print-address=%d", fds[1]);
	{
		const char *argv[] = { "dbus-daemon", "--session", "--nofork",
				       "--nopidfile", descriptor,  NULL };

		children[0] = spawn(argv, null);
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
heard(const char *line)
{
	FILE *file = fopen(log_path, "r");
	char buf[512];
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
wait_heard(struct harness *h, const char *line)
{
	for (int i = 0; i < 300 && !heard(line); i++)
		pause_ms(h, 20);
	CHECK(heard(line), "the screen reader never heard \"%s\"", line);
}

int
main(int argc, char **argv)
{
	const char *launcher = NULL, *registry = NULL;
	struct harness *h;
	struct harness_window *a, *b;
	int log;

	CHECK(argc >= 4, "usage: %s POLO POLO-SHELL-UI A11Y-LISTEN", argv[0]);
	for (size_t i = 0; i < sizeof LAUNCHERS / sizeof LAUNCHERS[0]; i++)
		if (access(LAUNCHERS[i], X_OK) == 0)
			launcher = LAUNCHERS[i];
	for (size_t i = 0; i < sizeof REGISTRIES / sizeof REGISTRIES[0]; i++)
		if (access(REGISTRIES[i], X_OK) == 0)
			registry = REGISTRIES[i];
	if (!launcher || !registry) {
		printf("skipped: no at-spi-bus-launcher or "
		       "at-spi2-registryd\n");
		return 77;
	}
	atexit(cleanup);
	log = mkstemp(log_path);
	CHECK(log >= 0, "mkstemp: %s", strerror(errno));
	CHECK(mkdtemp(runtime), "mkdtemp: %s", strerror(errno));
	runtime_made = true;
	setenv("XDG_RUNTIME_DIR", runtime, 1);
	setenv("GIO_USE_VFS", "local", 1);
	start_bus();
	{
		const char *argv_launcher[] = { launcher,
						"--launch-immediately", NULL };

		children[1] = spawn(argv_launcher, open("/dev/null", O_WRONLY));
	}
	pause_ms(NULL, 300);
	{
		const char *argv_registry[] = { registry, NULL };

		children[3] = spawn(argv_registry, open("/dev/null", O_WRONLY));
	}
	pause_ms(NULL, 300);
	{
		const char *argv_listen[] = { "python3", argv[3], log_path,
					      NULL };

		children[2] = spawn(argv_listen, log);
	}
	for (int i = 0; i < 300 && !heard("ready"); i++)
		pause_ms(NULL, 20);
	CHECK(heard("ready"), "the AT-SPI listener did not start");

	unsetenv("NO_AT_BRIDGE");
	unsetenv("GTK_A11Y");
	setenv("GNOME_ACCESSIBILITY", "1", 1);
	setenv("POLO_SHELL_UI", argv[2], 1);
	h = harness_start(argc, argv);
	for (int i = 0; i < 100 && h->shell_ui_binds == 0; i++)
		pause_ms(h, 50);
	CHECK(h->shell_ui_binds > 0, "polo-shell-ui did not start");
	pause_ms(h, 1000);

	a = harness_window_create(h, 300, 200, "First window");
	b = harness_window_create(h, 300, 200, "Second window");
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_TAB, true);
	harness_key(h, KEY_TAB, false);
	wait_heard(h, "announce First window");
	harness_key(h, KEY_TAB, true);
	harness_key(h, KEY_TAB, false);
	wait_heard(h, "announce Second window");
	harness_key(h, KEY_LEFTALT, false);
	pause_ms(h, 200);

	harness_key(h, KEY_TOUCHPAD_OFF, true);
	harness_key(h, KEY_TOUCHPAD_OFF, false);
	wait_heard(h, "announce Touchpad disabled");

	harness_window_destroy(b);
	harness_window_destroy(a);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
