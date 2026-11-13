/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A desktop soak on headless Polo with the GL renderer and the real
 * polo-shell-ui, on two outputs, in a seeded random order, for a given time:
 * windows opened, closed, focused, maximised, made fullscreen, dragged with
 * wobbly windows and minimised with Magic Lamp; workspace switches through the
 * cube; Alt+Tab both ways, the window menu, expo, shell UI dialogs, samples
 * and OSDs; the magnifier toggled, zoomed and moved between outputs; an
 * output plugged and unplugged; the clipboard handed over between sources and
 * libweston's keeper while a data-control client watches; client processes
 * that are killed while they map; and, with SOAK_ORCA, a stand-in screen
 * reader coming and going on the keyboard monitor. Polo is stopped and
 * started again every session (a logout and login), every other one but the
 * last at scale 2.
 *
 *   soak POLO POLO-SHELL-UI SECONDS [SESSION-SECONDS [SEED]]
 *
 * SOAK_SECONDS, SOAK_SESSION_SECONDS and SOAK_SEED override the arguments
 * (tools/soak.sh runs the meson test with them). SOAK_IDLE_SECONDS measures
 * an idle desktop that long before the workload and again after it.
 * SOAK_SCALE is Polo's scale option for the scaled logins (--scale=2 by
 * default; --scales=1,2 mixes them).
 * SOAK_ORCA is fake-orca.py, for a private session bus. SOAK_LOG_DIR keeps
 * Polo's logs. For chasing a leak, SOAK_ACTIONS keeps only some actions (a
 * comma-separated list of act ()'s cases), SOAK_FIXED_TITLES gives every
 * window the same title, SOAK_PLAIN leaves the effects off and
 * SOAK_SLOWDOWN stretches every pause (for a shell UI under valgrind).
 *
 * Fails on a crash or protocol error (the harness); on the shell UI going
 * away; on keyboard focus missing at a checkpoint; on the magnifier still
 * moving the pointer after it is turned off; on Polo's RSS at the end of a
 * session more than 5% above its RSS after warming up, or its descriptors,
 * threads or the shell UI's Wayland objects growing from checkpoint to
 * checkpoint; on a Polo that leaves a child process or its socket behind when
 * it stops; and on a shell UI that outlives a killed Polo. Prints a line per
 * checkpoint and a summary with the idle measurements.
 */

#include <dirent.h>
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

#include "ext-data-control-v1-client-protocol.h"
#include "harness.h"

#define MAX_WINDOWS 10
#define MAX_CHECKS 128
#define RSS_GROWTH 0.05
#define N_ACTIONS 21
#define TEXT "text/plain;charset=utf-8"

static const char *const ACTION_NAMES[N_ACTIONS] = {
	"open",      "open",      "close",    "workspace",  "alt-tab",
	"minimise",  "wobble",    "maximise", "fullscreen", "window-menu",
	"samples",   "dialog",    "osd",      "expo",       "magnifier",
	"hotplug",   "clipboard", "crash",    "reader",     "focus",
	"interrupt",
};

struct usage {
	double rss, pss, cpu;
	long switches;
	int threads, fds;
};

struct idle {
	bool measured;
	double seconds, polo_cpu, ui_cpu;
	long polo_switches, ui_switches;
	unsigned frames, warnings;
	struct usage polo, ui;
	uint32_t objects, ui_objects;
	long ui_switches_late;
	double trimmed_rss;
};

struct login {
	double early, late, peak, ui_early, ui_late, ui_pss_late;
	int fds_early, fds_late, threads_early, threads_late;
	uint32_t ui_objects_early, ui_objects_late;
	uint32_t ui_outputs_early, ui_outputs_late, plugs_between;
	unsigned frames, warnings;
	double cpu, ui_cpu;
};

static struct harness_window *windows[MAX_WINDOWS];
static int n_windows;
static unsigned long actions, counts[N_ACTIONS];
static char log_dir[256], log_path[300];
static bool own_log_dir;
static struct harness_output_state outs[3];
static int n_outs;
static bool plugged;
static unsigned plugs;
static unsigned reader_starts, reader_watching, crash_clients;

/* ---- Measuring ---------------------------------------------------------- */

static double
now_s(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };
	double slowdown =
		getenv("SOAK_SLOWDOWN") ? atof(getenv("SOAK_SLOWDOWN")) : 1;

	for (double end = now_s() + ms * slowdown / 1000.0; now_s() < end;) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

/* Without talking to Polo, so that waiting does not wake it. */
static void
sleep_s(double seconds)
{
	struct timespec ts = { (time_t)seconds,
			       (long)((seconds - (time_t)seconds) * 1e9) };

	while (nanosleep(&ts, &ts) != 0)
		;
}

static struct usage
usage_of(int pid)
{
	struct usage u = { 0 };
	char path[64], line[512];
	FILE *file;
	DIR *dir;
	long kb, voluntary = 0, involuntary = 0;

	if (pid <= 0)
		return u;
	snprintf(path, sizeof path, "/proc/%d/status", pid);
	if ((file = fopen(path, "r"))) {
		while (fgets(line, sizeof line, file)) {
			if (sscanf(line, "VmRSS: %ld", &kb) == 1)
				u.rss = kb / 1024.0;
			sscanf(line, "Threads: %d", &u.threads);
			sscanf(line, "voluntary_ctxt_switches: %ld",
			       &voluntary);
			sscanf(line, "nonvoluntary_ctxt_switches: %ld",
			       &involuntary);
		}
		fclose(file);
	}
	u.switches = voluntary + involuntary;
	snprintf(path, sizeof path, "/proc/%d/smaps_rollup", pid);
	if ((file = fopen(path, "r"))) {
		while (fgets(line, sizeof line, file))
			if (sscanf(line, "Pss: %ld", &kb) == 1)
				u.pss = kb / 1024.0;
		fclose(file);
	}
	snprintf(path, sizeof path, "/proc/%d/stat", pid);
	if ((file = fopen(path, "r"))) {
		unsigned long user = 0, system = 0;
		char *close_paren;

		if (fgets(line, sizeof line, file) &&
		    (close_paren = strrchr(line, ')')) &&
		    sscanf(close_paren + 2,
			   "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu "
			   "%lu",
			   &user, &system) == 2)
			u.cpu = (double)(user + system) / sysconf(_SC_CLK_TCK);
		fclose(file);
	}
	snprintf(path, sizeof path, "/proc/%d/fd", pid);
	if ((dir = opendir(path))) {
		struct dirent *entry;

		while ((entry = readdir(dir)))
			u.fds += entry->d_name[0] != '.';
		closedir(dir);
	}
	return u;
}

static long
file_size(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 ? st.st_size : 0;
}

/* Frames Polo's --stats counted, and warnings, in the log from @from on. */
static void
scan_log(long from, unsigned *frames, unsigned *warnings)
{
	FILE *file = fopen(log_path, "r");
	char line[1024];

	*frames = *warnings = 0;
	if (!file)
		return;
	fseek(file, from, SEEK_SET);
	while (fgets(line, sizeof line, file)) {
		const char *stats = strstr(line, ": stats ");
		const char *colon = stats ? strchr(stats + 2, ':') : NULL;
		unsigned n;

		if (colon && !strstr(stats, "input and effects") &&
		    sscanf(colon + 1, " %u frames", &n) == 1)
			*frames += n;
		if (strstr(line, "WARNING") || strstr(line, "CRITICAL") ||
		    strstr(line, "ERROR"))
			(*warnings)++;
	}
	fclose(file);
}

/* An orphan whose new parent does not reap it stays a zombie. */
static bool
exited(int pid)
{
	char path[64], stat[512];
	const char *close_paren;
	FILE *file;
	size_t n;

	if (kill(pid, 0) != 0)
		return true;
	snprintf(path, sizeof path, "/proc/%d/stat", pid);
	if (!(file = fopen(path, "r")))
		return true;
	n = fread(stat, 1, sizeof stat - 1, file);
	fclose(file);
	stat[n] = '\0';
	close_paren = strrchr(stat, ')');
	return close_paren && (close_paren[2] == 'Z' || close_paren[2] == 'X');
}

static int
children_of(int pid)
{
	DIR *proc = opendir("/proc");
	struct dirent *entry;
	int count = 0;

	if (!proc)
		return 0;
	while ((entry = readdir(proc))) {
		char path[300], line[512];
		FILE *file;
		int ppid = 0;

		if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
			continue;
		snprintf(path, sizeof path, "/proc/%s/status", entry->d_name);
		if (!(file = fopen(path, "r")))
			continue;
		while (fgets(line, sizeof line, file))
			if (sscanf(line, "PPid: %d", &ppid) == 1)
				break;
		fclose(file);
		count += ppid == pid && !exited(atoi(entry->d_name));
	}
	closedir(proc);
	return count;
}

/* ---- Small helpers ------------------------------------------------------ */

static void
pointer_to(struct harness *h, int x, int y)
{
	polo_test_v1_move_pointer(h->test, x, y);
	harness_roundtrip(h);
}

static void
button(struct harness *h, bool pressed)
{
	polo_test_v1_button(h->test, BTN_LEFT,
			    pressed ? WL_POINTER_BUTTON_STATE_PRESSED
				    : WL_POINTER_BUTTON_STATE_RELEASED);
	harness_roundtrip(h);
}

static void
key_tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
}

static void
chord(struct harness *h, uint32_t a, uint32_t b, uint32_t key)
{
	harness_key(h, a, true);
	if (b)
		harness_key(h, b, true);
	key_tap(h, key);
	if (b)
		harness_key(h, b, false);
	harness_key(h, a, false);
}

static struct harness_window *
any_window(void)
{
	return n_windows ? windows[rand() % n_windows] : NULL;
}

/* A touchscreen-style position; no repaint need follow, so none is awaited. */
static void
pointer_to_output(struct harness *h, const char *output, double x, double y)
{
	polo_test_v1_move_pointer_output(h->test, output,
					 wl_fixed_from_double(x),
					 wl_fixed_from_double(y));
	harness_roundtrip(h);
}

static void
read_outputs(struct harness *h)
{
	n_outs = 0;
	harness_output_count(h);
	for (int i = 0; i < h->n_output_states && n_outs < 3; i++)
		if (h->output_states[i].enabled)
			outs[n_outs++] = h->output_states[i];
}

/* ---- Clipboard ---------------------------------------------------------- */

static struct wl_registry *registry;
static struct wl_data_device_manager *data_manager;
static struct wl_data_device *data_device;
static struct wl_data_source *clip_sources[2];
static struct ext_data_control_manager_v1 *control_manager;
static struct ext_data_control_device_v1 *control_device;
static struct ext_data_control_offer_v1 *control_offer;
static struct wl_data_offer *data_offer;
static uint32_t clip_serial = 100;

static void
global(void *data, struct wl_registry *r, uint32_t name, const char *interface,
       uint32_t version)
{
	if (strcmp(interface, wl_data_device_manager_interface.name) == 0)
		data_manager = wl_registry_bind(
			r, name, &wl_data_device_manager_interface, 3);
	else if (strcmp(interface,
			ext_data_control_manager_v1_interface.name) == 0)
		control_manager = wl_registry_bind(
			r, name, &ext_data_control_manager_v1_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *r, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	global,
	global_remove,
};

static void
source_target(void *data, struct wl_data_source *s, const char *mime)
{
}

static void
source_send(void *data, struct wl_data_source *s, const char *mime, int32_t fd)
{
	if (write(fd, "soak", 4) < 0)
		perror("write");
	close(fd);
}

static void
source_cancelled(void *data, struct wl_data_source *s)
{
}

static const struct wl_data_source_listener source_listener = {
	.target = source_target,
	.send = source_send,
	.cancelled = source_cancelled,
};

static void
device_data_offer(void *data, struct wl_data_device *d, struct wl_data_offer *o)
{
}

static void
device_enter(void *data, struct wl_data_device *d, uint32_t serial,
	     struct wl_surface *s, wl_fixed_t x, wl_fixed_t y,
	     struct wl_data_offer *o)
{
}

static void
device_leave(void *data, struct wl_data_device *d)
{
}

static void
device_motion(void *data, struct wl_data_device *d, uint32_t t, wl_fixed_t x,
	      wl_fixed_t y)
{
}

static void
device_drop(void *data, struct wl_data_device *d)
{
}

static void
device_selection(void *data, struct wl_data_device *d, struct wl_data_offer *o)
{
	if (data_offer)
		wl_data_offer_destroy(data_offer);
	data_offer = o;
}

static const struct wl_data_device_listener data_device_listener = {
	device_data_offer, device_enter, device_leave,
	device_motion,     device_drop,  device_selection,
};

static void
control_data_offer(void *data, struct ext_data_control_device_v1 *d,
		   struct ext_data_control_offer_v1 *o)
{
}

static void
control_selection(void *data, struct ext_data_control_device_v1 *d,
		  struct ext_data_control_offer_v1 *o)
{
	if (control_offer)
		ext_data_control_offer_v1_destroy(control_offer);
	control_offer = o;
}

static void
control_finished(void *data, struct ext_data_control_device_v1 *d)
{
}

static void
control_primary(void *data, struct ext_data_control_device_v1 *d,
		struct ext_data_control_offer_v1 *o)
{
	if (o)
		ext_data_control_offer_v1_destroy(o);
}

static const struct ext_data_control_device_v1_listener control_listener = {
	control_data_offer,
	control_selection,
	control_finished,
	control_primary,
};

static void
clipboard_start(struct harness *h)
{
	registry = wl_display_get_registry(h->display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(data_manager, "no wl_data_device_manager");
	data_device =
		wl_data_device_manager_get_data_device(data_manager, h->seat);
	wl_data_device_add_listener(data_device, &data_device_listener, NULL);
	if (control_manager) {
		control_device = ext_data_control_manager_v1_get_data_device(
			control_manager, h->seat);
		ext_data_control_device_v1_add_listener(
			control_device, &control_listener, NULL);
	}
	harness_roundtrip(h);
}

static void
clipboard_stop(struct harness *h)
{
	for (int i = 0; i < 2; i++)
		if (clip_sources[i])
			wl_data_source_destroy(clip_sources[i]);
	clip_sources[0] = clip_sources[1] = NULL;
	if (data_offer)
		wl_data_offer_destroy(data_offer);
	data_offer = NULL;
	if (control_offer)
		ext_data_control_offer_v1_destroy(control_offer);
	control_offer = NULL;
	if (control_device)
		ext_data_control_device_v1_destroy(control_device);
	control_device = NULL;
	if (control_manager)
		ext_data_control_manager_v1_destroy(control_manager);
	control_manager = NULL;
	if (data_device)
		wl_data_device_release(data_device);
	data_device = NULL;
	if (data_manager)
		wl_data_device_manager_destroy(data_manager);
	data_manager = NULL;
	if (registry)
		wl_registry_destroy(registry);
	registry = NULL;
	harness_roundtrip(h);
}

/* A new source takes the selection; the old one goes before or after, so
 * libweston's keeper hands its copy over in both orders. */
static void
clipboard(struct harness *h)
{
	struct wl_data_source *source =
		wl_data_device_manager_create_data_source(data_manager);

	wl_data_source_add_listener(source, &source_listener, NULL);
	wl_data_source_offer(source, TEXT);
	if (rand() % 2 && clip_sources[0]) {
		wl_data_source_destroy(clip_sources[0]);
		clip_sources[0] = NULL;
		harness_roundtrip(h);
	}
	wl_data_device_set_selection(data_device, source, clip_serial += 10);
	harness_roundtrip(h);
	if (clip_sources[0])
		wl_data_source_destroy(clip_sources[0]);
	clip_sources[0] = clip_sources[1];
	clip_sources[1] = source;
	if (rand() % 3 == 0) {
		wl_data_source_destroy(clip_sources[1]);
		clip_sources[1] = NULL;
	}
	harness_roundtrip(h);
}

/* ---- A client that is killed ------------------------------------------- */

static struct wl_compositor *child_compositor;
static struct wl_shm *child_shm;
static struct xdg_wm_base *child_wm_base;
static pid_t crash_child;

static void
child_global(void *data, struct wl_registry *r, uint32_t name,
	     const char *interface, uint32_t version)
{
	if (strcmp(interface, wl_compositor_interface.name) == 0)
		child_compositor =
			wl_registry_bind(r, name, &wl_compositor_interface, 4);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		child_shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
		child_wm_base =
			wl_registry_bind(r, name, &xdg_wm_base_interface, 2);
}

static const struct wl_registry_listener child_registry_listener = {
	child_global,
	global_remove,
};

static void
child_ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
	xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener child_wm_base_listener = {
	child_ping,
};

static void
child_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
	xdg_surface_ack_configure(xdg_surface, serial);
	*(bool *)data = true;
}

static const struct xdg_surface_listener child_surface_listener = {
	child_configure,
};

static void
crash_client_run(const char *socket)
{
	struct wl_display *display = wl_display_connect(socket);
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	bool configured = false;

	if (!display)
		_exit(2);
	wl_registry_add_listener(wl_display_get_registry(display),
				 &child_registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!child_compositor || !child_shm || !child_wm_base)
		_exit(3);
	xdg_wm_base_add_listener(child_wm_base, &child_wm_base_listener, NULL);
	surface = wl_compositor_create_surface(child_compositor);
	xdg_surface = xdg_wm_base_get_xdg_surface(child_wm_base, surface);
	xdg_surface_add_listener(xdg_surface, &child_surface_listener,
				 &configured);
	xdg_toplevel_set_title(xdg_surface_get_toplevel(xdg_surface), "killed");
	wl_surface_commit(surface);
	while (!configured && wl_display_dispatch(display) != -1)
		;
	wl_surface_attach(surface, harness_buffer_create(child_shm, 260, 180),
			  0, 0);
	wl_surface_commit(surface);
	while (wl_display_dispatch(display) != -1)
		;
	_exit(0);
}

static void
crash_client(struct harness *h)
{
	if (crash_child > 0) {
		kill(crash_child, rand() % 2 ? SIGKILL : SIGTERM);
		waitpid(crash_child, NULL, 0);
		crash_child = 0;
		pause_ms(h, 50);
		return;
	}
	crash_child = fork();
	if (crash_child == 0)
		crash_client_run(h->socket);
	crash_clients++;
	pause_ms(h, 20 + rand() % 300);
}

/* ---- The stand-in screen reader ---------------------------------------- */

static pid_t reader;
static int reader_commands = -1;
static char reader_path[300], reader_log[320];

static void
reader_toggle(struct harness *h)
{
	int fds[2];

	if (!reader_path[0])
		return;
	if (reader > 0) {
		if (rand() % 2) {
			if (write(reader_commands, "quit\n", 5) < 0)
				perror("write");
		} else {
			kill(reader, SIGKILL);
		}
		close(reader_commands);
		waitpid(reader, NULL, 0);
		reader = 0;
		pause_ms(h, 50);
		return;
	}
	CHECK(pipe(fds) == 0, "pipe");
	reader = fork();
	if (reader == 0) {
		int null = open("/dev/null", O_WRONLY);

		dup2(null, STDOUT_FILENO);
		dup2(null, STDERR_FILENO);
		dup2(fds[0], STDIN_FILENO);
		close(fds[0]);
		close(fds[1]);
		execlp("python3", "python3", reader_path, reader_log,
		       (char *)NULL);
		_exit(127);
	}
	close(fds[0]);
	reader_commands = fds[1];
	reader_starts++;
	pause_ms(h, 300);
}

static void
reader_stop(void)
{
	if (reader > 0) {
		kill(reader, SIGKILL);
		waitpid(reader, NULL, 0);
		close(reader_commands);
		reader = 0;
	}
}

/* ---- Actions ------------------------------------------------------------ */

static void
open_window(struct harness *h)
{
	char title[32];

	if (n_windows == MAX_WINDOWS)
		return;
	if (getenv("SOAK_FIXED_TITLES"))
		snprintf(title, sizeof title, "soak");
	else
		snprintf(title, sizeof title, "soak %lu", actions);
	windows[n_windows++] = harness_window_create(h, 200 + rand() % 300,
						     150 + rand() % 200, title);
}

static void
close_window(struct harness *h)
{
	int i;

	if (n_windows <= 2)
		return;
	i = rand() % n_windows;
	harness_window_close(windows[i]);
	windows[i] = windows[--n_windows];
}

static void
switch_workspace(struct harness *h)
{
	chord(h, KEY_LEFTCTRL, KEY_LEFTALT, rand() % 2 ? KEY_RIGHT : KEY_LEFT);
	pause_ms(h, 250);
}

static void
alt_tab(struct harness *h)
{
	bool back = rand() % 3 == 0;

	harness_key(h, KEY_LEFTALT, true);
	if (back)
		harness_key(h, KEY_LEFTSHIFT, true);
	for (int i = 0, n = 1 + rand() % 3; i < n; i++) {
		key_tap(h, KEY_TAB);
		pause_ms(h, 60);
	}
	if (rand() % 4 == 0)
		key_tap(h, KEY_ESC);
	if (back)
		harness_key(h, KEY_LEFTSHIFT, false);
	harness_key(h, KEY_LEFTALT, false);
	pause_ms(h, 150);
}

static void
minimise(struct harness *h)
{
	struct harness_window *w = any_window();

	if (!w)
		return;
	xdg_toplevel_set_minimized(w->xdg_toplevel);
	pause_ms(h, 350);
}

static void
maximise(struct harness *h)
{
	struct harness_window *w = any_window();

	if (!w)
		return;
	if (w->xdg_maximized)
		xdg_toplevel_unset_maximized(w->xdg_toplevel);
	else
		xdg_toplevel_set_maximized(w->xdg_toplevel);
	pause_ms(h, 100);
}

static void
fullscreen(struct harness *h)
{
	struct harness_window *w = any_window();

	if (!w)
		return;
	xdg_toplevel_set_fullscreen(w->xdg_toplevel, NULL);
	pause_ms(h, 150);
	xdg_toplevel_unset_fullscreen(w->xdg_toplevel);
	pause_ms(h, 100);
}

static void
wobble(struct harness *h)
{
	struct harness_window *w = any_window();
	const struct harness_window_info *info;
	int x, y;

	if (!w)
		return;
	info = harness_window_info(w);
	x = info->x + info->width / 2;
	y = info->y + info->height / 2;
	pointer_to(h, x, y);
	pause_ms(h, 30);
	if (h->pointer_focus != w->surface)
		return;
	harness_key(h, KEY_LEFTALT, true);
	button(h, true);
	for (int i = 1; i <= 12; i++) {
		pointer_to(h, x + i * (rand() % 21 - 10),
			   y + i * (rand() % 21 - 10));
		pause_ms(h, 16);
	}
	button(h, false);
	harness_key(h, KEY_LEFTALT, false);
	pause_ms(h, 300);
}

static void
window_menu(struct harness *h)
{
	chord(h, KEY_LEFTALT, 0, KEY_SPACE);
	pause_ms(h, 120);
	if (rand() % 2)
		key_tap(h, KEY_DOWN);
	key_tap(h, KEY_ESC);
	pause_ms(h, 80);
}

static void
dialog(struct harness *h)
{
	static const char *const payloads[] = {
		"Busy app",
		"Renamed",
		"{'dialog': <'logout'>, 'seconds': <60>}",
		"A message",
		"20",
	};
	uint32_t kind = rand() % 5;

	polo_test_v1_shell_ui_dialog(h->test, 1 + rand() % 3, kind,
				     payloads[kind]);
	h->n_shell_ui_requests = 0;
	pause_ms(h, 40 + rand() % 80);
}

static void
osd(struct harness *h)
{
	key_tap(h, rand() % 2 ? KEY_TOUCHPAD_ON : KEY_TOUCHPAD_OFF);
	pause_ms(h, 40);
}

static void
expo(struct harness *h)
{
	chord(h, KEY_LEFTMETA, 0, KEY_E);
	pause_ms(h, 200 + rand() % 300);
	if (rand() % 2)
		key_tap(h, KEY_ESC);
	else
		chord(h, KEY_LEFTMETA, 0, KEY_E);
	pause_ms(h, 400);
}

static void
magnifier(struct harness *h)
{
	static const char *const factors[] = { "1.0", "1.25", "1.5",  "2.37",
					       "3.0", "7.5",  "16.0", "32.0",
					       "0.5", "0.1" };
	static const char *const tracking[] = { "'none'", "'centered'",
						"'proportional'", "'push'" };

	switch (rand() % 8) {
	case 0:
		chord(h, KEY_LEFTMETA, KEY_LEFTALT, KEY_8);
		break;
	case 1:
		chord(h, KEY_LEFTMETA, KEY_LEFTALT, KEY_EQUAL);
		break;
	case 2:
		chord(h, KEY_LEFTMETA, KEY_LEFTALT, KEY_MINUS);
		break;
	case 3:
		harness_set_setting(
			h, "org.gnome.desktop.a11y.magnifier", "mag-factor",
			factors[rand() % (sizeof factors / sizeof *factors)]);
		break;
	case 4:
		harness_set_setting(
			h, "org.gnome.desktop.a11y.magnifier", "focus-tracking",
			tracking[rand() %
				 (sizeof tracking / sizeof *tracking)]);
		break;
	case 5: {
		const struct harness_output_state *o = &outs[rand() % n_outs];
		int w = o->width * o->scale, ht = o->height * o->scale;

		pointer_to_output(h, o->name, rand() % 2 ? 0 : w - 1,
				  rand() % 3 == 0 ? rand() % ht
						  : (rand() % 2 ? 0 : ht - 1));
		break;
	}
	default: {
		const struct harness_output_state *o = &outs[rand() % n_outs];

		pointer_to_output(h, o->name, rand() % (o->width * o->scale),
				  rand() % (o->height * o->scale));
		break;
	}
	}
	pause_ms(h, 60);
}

static void
hotplug(struct harness *h)
{
	static const int modes[][3] = { { 1280, 720, 60000 } };

	if (plugged)
		harness_remove_output(h, "HDMI-A-1");
	else
		harness_add_output(h, "HDMI-A-1", "DEL", 0x4321, 7, "Soak",
				   false, 527, 296, modes, 1);
	plugs += !plugged;
	plugged = !plugged;
	pause_ms(h, 300);
	read_outputs(h);
}

static void
focus_click(struct harness *h)
{
	struct harness_window *w = any_window();
	const struct harness_window_info *info;

	if (!w)
		return;
	info = harness_window_info(w);
	pointer_to(h, info->x + 10 + rand() % 40, info->y + 10 + rand() % 40);
	button(h, true);
	button(h, false);
	pause_ms(h, 40);
}

/* Something else starts before the last animation or popup has ended. */
static void
interrupt(struct harness *h)
{
	switch (rand() % 3) {
	case 0:
		if (n_windows < MAX_WINDOWS) {
			struct harness_window *w =
				harness_window_open(h, 220, 160, "brief");

			pause_ms(h, 30);
			harness_window_close(w);
		}
		break;
	case 1:
		minimise(h);
		alt_tab(h);
		break;
	default:
		harness_key(h, KEY_LEFTALT, true);
		key_tap(h, KEY_TAB);
		polo_test_v1_shell_ui_samples(h->test);
		close_window(h);
		key_tap(h, KEY_TAB);
		harness_key(h, KEY_LEFTALT, false);
		h->n_shell_ui_requests = 0;
		break;
	}
	pause_ms(h, 100);
}

static int
pick_action(void)
{
	const char *only = getenv("SOAK_ACTIONS");
	int what = rand() % N_ACTIONS;

	if (!only || !*only)
		return what;
	for (;;) {
		char wanted[8];
		const char *at = only;

		snprintf(wanted, sizeof wanted, "%d", what);
		while ((at = strstr(at, wanted))) {
			bool start = at == only || at[-1] == ',';
			bool end = at[strlen(wanted)] == ',' ||
				at[strlen(wanted)] == '\0';

			if (start && end)
				return what;
			at++;
		}
		what = rand() % N_ACTIONS;
	}
}

static void
act(struct harness *h)
{
	int what = pick_action();

	if (getenv("SOAK_TRACE"))
		printf("soak: action %lu: %s\n", actions, ACTION_NAMES[what]);
	switch (what) {
	case 0:
	case 1:
		open_window(h);
		break;
	case 2:
		close_window(h);
		break;
	case 3:
		switch_workspace(h);
		break;
	case 4:
		alt_tab(h);
		break;
	case 5:
		minimise(h);
		break;
	case 6:
		wobble(h);
		break;
	case 7:
		maximise(h);
		break;
	case 8:
		fullscreen(h);
		break;
	case 9:
		window_menu(h);
		break;
	case 10:
		polo_test_v1_shell_ui_samples(h->test);
		h->n_shell_ui_requests = 0;
		pause_ms(h, 40 + rand() % 80);
		break;
	case 11:
		dialog(h);
		break;
	case 12:
		osd(h);
		break;
	case 13:
		expo(h);
		break;
	case 14:
		magnifier(h);
		break;
	case 15:
		hotplug(h);
		break;
	case 16:
		clipboard(h);
		break;
	case 17:
		crash_client(h);
		break;
	case 18:
		reader_toggle(h);
		break;
	case 19:
		focus_click(h);
		break;
	default:
		interrupt(h);
		break;
	}
	counts[what]++;
	actions++;
	harness_roundtrip(h);
}

/* ---- Sessions ----------------------------------------------------------- */

static void
configure(struct harness *h)
{
	harness_set_setting(h, "org.mate.Marco.general", "num-workspaces", "4");
	harness_set_setting(h, "org.gnome.desktop.a11y.magnifier", "mag-factor",
			    "2.0");
	if (reader_path[0])
		harness_set_setting(h, "org.gnome.desktop.a11y.applications",
				    "screen-reader-enabled", "true");
	if (getenv("SOAK_PLAIN"))
		goto shell_ui;
	harness_set_setting(h, "org.mate.polo.effects", "cube", "true");
	harness_set_setting(h, "org.mate.polo.effects", "cube-duration-ms",
			    "200");
	harness_set_setting(h, "org.mate.polo.effects", "magic-lamp", "true");
	harness_set_setting(h, "org.mate.polo.effects",
			    "magic-lamp-duration-ms", "250");
	harness_set_setting(h, "org.mate.polo.effects", "wobbly-windows",
			    "true");
shell_ui:
	if (!getenv("POLO_SHELL_UI")[0])
		return;
	for (int i = 0; i < 100 && h->shell_ui_binds == 0; i++)
		pause_ms(h, 100);
	CHECK(h->shell_ui_binds > 0, "polo-shell-ui did not start");
}

static int
by_value(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return x < y ? -1 : x > y;
}

static double
median(const double *values, int n)
{
	double sorted[MAX_CHECKS];

	memcpy(sorted, values, n * sizeof *values);
	qsort(sorted, n, sizeof *sorted, by_value);
	return n % 2 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
}

/* The same desktop every time: three fresh windows, no shell UI dialog,
 * nothing else on screen, the magnifier off, the extra output unplugged, no
 * killed client left. */
static void
standard_state(struct harness *h, int index)
{
	int x, y;

	if (crash_child > 0)
		crash_client(h);
	while (n_windows)
		harness_window_close(windows[--n_windows]);
	/* A dialog shown under an open one's id replaces it; an end-session
	 * dialog with nothing to say then shows nothing. */
	for (uint32_t id = 1; id <= 3; id++)
		polo_test_v1_shell_ui_dialog(h->test, id, 2, "[0]");
	harness_roundtrip(h);
	h->n_shell_ui_requests = 0;
	harness_set_setting(h, "org.gnome.desktop.a11y.applications",
			    "screen-magnifier-enabled", "false");
	harness_set_setting(h, "org.gnome.desktop.a11y.magnifier", "mag-factor",
			    "2.0");
	if (plugged)
		hotplug(h);
	read_outputs(h);
	pointer_to(h, outs[0].x + 5, outs[0].y + 5);
	for (int i = 0; i < 3; i++) {
		char title[32];

		snprintf(title, sizeof title, "checkpoint %d", i);
		windows[n_windows++] =
			harness_window_create(h, 300, 200, title);
	}
	pause_ms(h, 1500);
	pointer_to_output(h, outs[0].name, 40 * outs[0].scale,
			  30 * outs[0].scale);
	harness_pointer_position(h, &x, &y);
	CHECK(abs(x - (outs[0].x + 40)) <= 1 && abs(y - (outs[0].y + 30)) <= 1,
	      "login %d: with the magnifier off, pixel 40,30 of %s went to "
	      "%d,%d",
	      index, outs[0].name, x, y);
	CHECK(h->keyboard_focus == windows[2]->surface,
	      "login %d: the newest window does not have keyboard focus",
	      index);
}

static void
check_shell_ui(struct harness *h, int shell_ui, int index)
{
	if (!getenv("POLO_SHELL_UI")[0])
		return;
	pause_ms(h, 50);
	CHECK(h->shell_ui_binds == 1 && h->shell_ui_unbinds == 0 &&
		      h->shell_ui_pid == shell_ui && !exited(shell_ui),
	      "login %d: polo-shell-ui went away (binds %d, unbinds %d)", index,
	      h->shell_ui_binds, h->shell_ui_unbinds);
}

static struct idle
idle_period(struct harness *h, double seconds, int pid, int ui)
{
	struct idle r = { .measured = true, .seconds = seconds };
	struct usage polo0, ui0;
	long from;

	struct usage ui_half;

	pause_ms(h, 6000);
	polo_test_v1_trim_heap(h->test);
	harness_roundtrip(h);
	r.trimmed_rss = usage_of(pid).rss;
	from = file_size(log_path);
	polo0 = usage_of(pid);
	ui0 = usage_of(ui);
	sleep_s(seconds / 2);
	ui_half = usage_of(ui);
	sleep_s(seconds - seconds / 2);
	r.ui_switches_late = usage_of(ui).switches - ui_half.switches;
	r.polo = usage_of(pid);
	r.ui = usage_of(ui);
	scan_log(from, &r.frames, &r.warnings);
	r.polo_cpu = r.polo.cpu - polo0.cpu;
	r.ui_cpu = r.ui.cpu - ui0.cpu;
	r.polo_switches = r.polo.switches - polo0.switches;
	r.ui_switches = r.ui.switches - ui0.switches;
	r.objects = harness_resources(h)->objects;
	r.ui_objects = h->resources.shell_ui_objects;
	return r;
}

static void
print_idle(const char *when, const struct idle *r)
{
	printf("soak: idle %s, %.0f s: Polo %.2f s CPU (%.3f%%), %ld context "
	       "switches, RSS %.1f MB (%.1f with its heap trimmed), PSS %.1f "
	       "MB, %d threads, %d fds; shell UI %.2f s CPU, %ld switches (%ld "
	       "in the second half), RSS %.1f MB, PSS %.1f MB, %d threads, %d "
	       "fds; %u frames on all outputs; %u Wayland objects (%u the "
	       "shell "
	       "UI's); %u warnings in the log\n",
	       when, r->seconds, r->polo_cpu, 100 * r->polo_cpu / r->seconds,
	       r->polo_switches, r->polo.rss, r->trimmed_rss, r->polo.pss,
	       r->polo.threads, r->polo.fds, r->ui_cpu, r->ui_switches,
	       r->ui_switches_late, r->ui.rss, r->ui.pss, r->ui.threads,
	       r->ui.fds, r->frames, r->objects, r->ui_objects, r->warnings);
}

static struct idle idle_before, idle_after;

static bool
session(int argc, char **argv, double seconds, int index, bool last,
	double idle_seconds, double soak_start, struct login *out)
{
	double start, every = seconds / 10, next_check;
	double checks[MAX_CHECKS], ui_checks[MAX_CHECKS],
		ui_pss_checks[MAX_CHECKS];
	int fds[MAX_CHECKS], threads[MAX_CHECKS];
	uint32_t ui_objects[MAX_CHECKS], ui_outputs[MAX_CHECKS],
		plugs_at[MAX_CHECKS];
	int n_checks = 0, n, shell_ui = 0;
	struct harness *h;
	char socket_path[512];
	int pid;
	struct usage u0, ui0, u1, ui1;
	long log_from;

	snprintf(log_path, sizeof log_path, "%s/login-%d.log", log_dir, index);
	setenv("POLO_TEST_LOG", log_path, 1);
	/* The last login idles at scale 1 like the first, to compare them. */
	if (index % 2 == 0 && !last)
		setenv("POLO_TEST_SCALE",
		       getenv("SOAK_SCALE") ? getenv("SOAK_SCALE")
					    : "--scale=2",
		       1);
	else
		unsetenv("POLO_TEST_SCALE");
	h = harness_start(argc, argv);
	pid = h->pid;
	if (every < 12)
		every = 12;
	if (every > 60)
		every = 60;
	snprintf(socket_path, sizeof socket_path, "%s/%s", h->runtime_dir,
		 h->socket);
	configure(h);
	shell_ui = h->shell_ui_pid;
	read_outputs(h);
	clipboard_start(h);
	if (index == 1 && idle_seconds > 0) {
		standard_state(h, index);
		idle_before = idle_period(h, idle_seconds, pid, shell_ui);
		print_idle("before the workload", &idle_before);
	}
	for (int i = 0; i < 3; i++)
		open_window(h);
	u0 = usage_of(pid);
	ui0 = usage_of(shell_ui);
	log_from = file_size(log_path);
	start = now_s();
	next_check = start + every;
	out->peak = 0;
	while (now_s() - start < seconds) {
		double rss;

		act(h);
		rss = usage_of(pid).rss;
		if (rss > out->peak)
			out->peak = rss;
		if (now_s() >= next_check && n_checks < MAX_CHECKS) {
			struct usage u, ui;

			standard_state(h, index);
			check_shell_ui(h, shell_ui, index);
			polo_test_v1_trim_heap(h->test);
			harness_roundtrip(h);
			u = usage_of(pid);
			ui = usage_of(shell_ui);
			checks[n_checks] = u.rss;
			ui_checks[n_checks] = ui.rss;
			ui_pss_checks[n_checks] = ui.pss;
			fds[n_checks] = u.fds;
			threads[n_checks] = u.threads;
			harness_resources(h);
			ui_outputs[n_checks] = h->resources.shell_ui_outputs;
			ui_objects[n_checks] = h->resources.shell_ui_objects -
				ui_outputs[n_checks];
			plugs_at[n_checks] = plugs;
			n_checks++;
			printf("soak: %.0f min, login %d: %lu actions, Polo "
			       "RSS "
			       "%.1f MB (peak %.1f), %d fds, %d threads; shell "
			       "UI RSS %.1f MB, PSS %.1f MB, %u Wayland "
			       "objects and %u for outputs; %u objects in "
			       "all, %u clients\n",
			       (now_s() - soak_start) / 60, index, actions,
			       u.rss, out->peak, u.fds, u.threads, ui.rss,
			       ui.pss, ui_objects[n_checks - 1],
			       ui_outputs[n_checks - 1], h->resources.objects,
			       h->resources.clients);
			next_check = now_s() + every;
		}
	}
	u1 = usage_of(pid);
	ui1 = usage_of(shell_ui);
	out->cpu = u1.cpu - u0.cpu;
	out->ui_cpu = ui1.cpu - ui0.cpu;
	scan_log(log_from, &out->frames, &out->warnings);
	check_shell_ui(h, shell_ui, index);
	if (last && idle_seconds > 0) {
		standard_state(h, index);
		check_shell_ui(h, shell_ui, index);
		idle_after = idle_period(h, idle_seconds, pid, shell_ui);
		print_idle("after the workload", &idle_after);
	}
	reader_stop();
	if (crash_child > 0)
		crash_client(h);
	clipboard_stop(h);
	while (n_windows)
		harness_window_close(windows[--n_windows]);
	plugged = false;
	CHECK(harness_stop(h) == 0, "login %d: Polo did not stop cleanly",
	      index);
	for (int i = 0; i < 50 && children_of(pid) > 0; i++)
		sleep_s(0.1);
	CHECK(children_of(pid) == 0,
	      "login %d: Polo left a child process behind", index);
	CHECK(shell_ui == 0 || exited(shell_ui),
	      "login %d: polo-shell-ui outlived a clean logout", index);
	CHECK(access(socket_path, F_OK) != 0,
	      "login %d: Polo left its socket %s behind", index, socket_path);
	if (n_checks < 7) {
		printf("soak: login %d ended after %d checkpoints: too short "
		       "to "
		       "judge memory\n",
		       index, n_checks);
		return false;
	}
	n = (n_checks - 1) / 3;
	if (n < 1)
		n = 1;
	if (n > 8)
		n = 8;
	out->early = median(checks + 1, n);
	out->late = median(checks + n_checks - n, n);
	out->ui_early = median(ui_checks + 1, n);
	out->ui_late = median(ui_checks + n_checks - n, n);
	out->ui_pss_late = median(ui_pss_checks + n_checks - n, n);
	out->fds_early = fds[1];
	out->fds_late = fds[n_checks - 1];
	out->threads_early = threads[1];
	out->threads_late = threads[n_checks - 1];
	out->ui_objects_early = ui_objects[1];
	out->ui_objects_late = ui_objects[n_checks - 1];
	out->ui_outputs_early = ui_outputs[1];
	out->ui_outputs_late = ui_outputs[n_checks - 1];
	out->plugs_between = plugs_at[n_checks - 1] - plugs_at[1];
	printf("soak: login %d ended: Polo RSS %.1f MB early, %.1f late "
	       "(medians of %d checkpoints), peak %.1f; shell UI RSS %.1f -> "
	       "%.1f MB; fds %d -> %d, threads %d -> %d, shell UI objects %u "
	       "-> %u, for outputs %u -> %u (%u plugged); %.1f s Polo CPU, "
	       "%.1f s shell UI CPU, %u frames, %u warnings\n",
	       index, out->early, out->late, n, out->peak, out->ui_early,
	       out->ui_late, out->fds_early, out->fds_late, out->threads_early,
	       out->threads_late, out->ui_objects_early, out->ui_objects_late,
	       out->ui_outputs_early, out->ui_outputs_late, out->plugs_between,
	       out->cpu, out->ui_cpu, out->frames, out->warnings);
	CHECK(out->late <= out->early * (1 + RSS_GROWTH),
	      "login %d: RSS at the checkpoints grew from %.1f MB to %.1f MB "
	      "(over %.0f%%)",
	      index, out->early, out->late, RSS_GROWTH * 100);
	CHECK(out->fds_late <= out->fds_early + 2,
	      "login %d: Polo's descriptors grew from %d to %d", index,
	      out->fds_early, out->fds_late);
	CHECK(out->threads_late <= out->threads_early + 2,
	      "login %d: Polo's threads grew from %d to %d", index,
	      out->threads_early, out->threads_late);
	CHECK(out->ui_objects_late <= out->ui_objects_early + 10,
	      "login %d: the shell UI's Wayland objects grew from %u to %u",
	      index, out->ui_objects_early, out->ui_objects_late);
	/* GTK 3 keeps a wl_output (bound at version 2, which cannot be
	 * released) and a zxdg_output_v1 for every output it has seen. */
	CHECK(out->ui_outputs_late <=
		      out->ui_outputs_early + 2 * out->plugs_between,
	      "login %d: the shell UI's output objects grew from %u to %u "
	      "with %u outputs plugged in",
	      index, out->ui_outputs_early, out->ui_outputs_late,
	      out->plugs_between);
	return true;
}

/* A Polo killed in the middle of shell UI work: the shell UI must go too. */
static void
recovery(int argc, char **argv)
{
	struct harness *h;
	int shell_ui;
	bool gone = false;

	snprintf(log_path, sizeof log_path, "%s/recovery.log", log_dir);
	setenv("POLO_TEST_LOG", log_path, 1);
	unsetenv("POLO_TEST_SCALE");
	h = harness_start(argc, argv);
	configure(h);
	shell_ui = h->shell_ui_pid;
	for (int i = 0; i < 4; i++)
		windows[n_windows++] = harness_window_open(h, 200, 150, "w");
	harness_roundtrip(h);
	for (int i = 0; i < 30; i++) {
		polo_test_v1_shell_ui_samples(h->test);
		harness_key(h, KEY_LEFTALT, true);
		key_tap(h, KEY_TAB);
		harness_key(h, KEY_LEFTALT, false);
	}
	wl_display_flush(h->display);
	kill(h->pid, SIGKILL);
	waitpid(h->pid, NULL, 0);
	h->pid = 0;
	for (int i = 0; i < 100 && !gone; i++) {
		gone = exited(shell_ui);
		sleep_s(0.1);
	}
	if (!gone)
		kill(shell_ui, SIGKILL);
	while (n_windows)
		harness_window_close(windows[--n_windows]);
	harness_stop(h);
	CHECK(gone, "recovery: polo-shell-ui outlived a killed Polo by 10 s");
	printf("soak: recovery: Polo killed during shell UI work, the shell UI "
	       "exited\n");
}

static void
remove_log_dir(void)
{
	char command[400];

	if (!own_log_dir)
		return;
	snprintf(command, sizeof command, "rm -rf '%s'", log_dir);
	if (system(command) != 0)
		fprintf(stderr, "could not remove %s\n", log_dir);
}

int
main(int argc, char **argv)
{
	double seconds, session_seconds, idle_seconds, start = now_s(),
						       worked = 0;
	struct login logins[64] = { 0 };
	unsigned seed;
	int index = 0, n_logins;
	bool judged = false;
	double worst = 0;

	CHECK(argc >= 4,
	      "usage: %s POLO POLO-SHELL-UI SECONDS [SESSION-SECONDS [SEED]]",
	      argv[0]);
	setvbuf(stdout, NULL, _IOLBF, 0);
	seconds =
		atof(getenv("SOAK_SECONDS") ? getenv("SOAK_SECONDS") : argv[3]);
	session_seconds = getenv("SOAK_SESSION_SECONDS")
		? atof(getenv("SOAK_SESSION_SECONDS"))
		: argc > 4 ? atof(argv[4])
			   : 900;
	idle_seconds = getenv("SOAK_IDLE_SECONDS")
		? atof(getenv("SOAK_IDLE_SECONDS"))
		: 0;
	if (session_seconds > seconds)
		session_seconds = seconds;
	seed = getenv("SOAK_SEED")
		? (unsigned)strtoul(getenv("SOAK_SEED"), NULL, 10)
		: argc > 5 ? (unsigned)strtoul(argv[5], NULL, 10)
			   : (unsigned)time(NULL);
	srand(seed);
	n_logins = (int)(seconds / session_seconds + 0.5);
	if (n_logins < 1)
		n_logins = 1;
	if (n_logins > 64)
		n_logins = 64;

	if (getenv("SOAK_LOG_DIR")) {
		snprintf(log_dir, sizeof log_dir, "%s", getenv("SOAK_LOG_DIR"));
		mkdir(log_dir, 0700);
	} else {
		snprintf(log_dir, sizeof log_dir, "%s/polo-soak-XXXXXX",
			 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
		CHECK(mkdtemp(log_dir), "cannot make a log directory");
		own_log_dir = true;
		atexit(remove_log_dir);
	}
	if (getenv("SOAK_ORCA") && getenv("DBUS_SESSION_BUS_ADDRESS")) {
		char command[700];

		snprintf(reader_path, sizeof reader_path, "%s/orca", log_dir);
		snprintf(reader_log, sizeof reader_log, "%s/orca.log", log_dir);
		snprintf(command, sizeof command, "cp '%s' '%s'",
			 getenv("SOAK_ORCA"), reader_path);
		CHECK(system(command) == 0, "cannot copy %s",
		      getenv("SOAK_ORCA"));
		setenv("POLO_TEST_A11Y", "1", 1);
	}
	printf("soak: %d logins of %.0f s of work, %.0f s idle before and "
	       "after, seed %u, logs in %s\n",
	       n_logins, session_seconds, idle_seconds, seed, log_dir);
	setenv("POLO_SHELL_UI", argv[2], 1);
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 0);
	setenv("POLO_TEST_TWO_OUTPUTS", "1", 1);
	setenv("POLO_STATS_INTERVAL_MS", "5000", 0);
	while (index < n_logins) {
		double before = now_s();

		index++;
		if (session(argc, argv, session_seconds, index,
			    index == n_logins, idle_seconds, start,
			    &logins[index - 1])) {
			double growth = (logins[index - 1].late -
					 logins[index - 1].early) /
				logins[index - 1].early;

			if (!judged || growth > worst)
				worst = growth;
			judged = true;
		}
		worked += now_s() - before;
	}
	if (getenv("POLO_SHELL_UI")[0])
		recovery(argc, argv);

	printf("soak: actions:");
	for (int i = 0; i < N_ACTIONS; i++)
		if (i != 1)
			printf(" %s %lu", ACTION_NAMES[i],
			       counts[i] + (i == 0 ? counts[1] : 0));
	printf("; %u killed clients, %u screen reader starts\n", crash_clients,
	       reader_starts);
	if (reader_path[0]) {
		FILE *file = fopen(reader_log, "r");
		char line[256];

		while (file && fgets(line, sizeof line, file))
			reader_watching += strncmp(line, "watch ok", 8) == 0;
		if (file)
			fclose(file);
		printf("soak: the screen reader was let watch %u of %u times\n",
		       reader_watching, reader_starts);
		CHECK(reader_starts == 0 || reader_watching > 0,
		      "the screen reader was never let watch the keyboard");
	}
	if (idle_before.measured && idle_after.measured) {
		print_idle("before", &idle_before);
		print_idle("after", &idle_after);
	}
	printf("soak: done: %.1f min in all, %d logins, %lu actions, %s, seed "
	       "%u\n",
	       (now_s() - start) / 60, index, actions,
	       judged ? "memory judged" : "logins too short to judge memory",
	       seed);
	if (judged)
		printf("soak: worst Polo RSS growth %.1f%%\n", worst * 100);
	(void)worked;
	return 0;
}
