/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The main loop's poll is wrapped: Polo is idle while it waits there and
 * working from its return until the next wait. A stretch of work longer
 * than LATE_MS is logged; a thread of its own logs the main thread's stack
 * once a stretch passes TRACE_MS, and ends Polo when one lasts too long, so
 * a frozen session leaves a core instead of needing a reboot. Waiting costs
 * nothing: the thread sleeps until the loop starts working.
 */

#include <errno.h>
#include <execinfo.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "polo-backend-private.h"

#define LATE_MS 300
#define TRACE_MS 1000
#define KERNEL_CHECK_MS 250
#define FRAMES 64

static struct {
	gboolean running;
	pthread_t main_thread;
	pthread_t thread;
	guint abort_ms;
	int fd;
	GMutex lock;
	GCond wake;
	gboolean working;
	gboolean parked;
	gint64 work_start_ms;
	GPollFunc poll;
	int trace_signal;
	pid_t tid;
} watchdog;

static gint64
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (gint64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void
say(const char *text)
{
	size_t done = 0, length = strlen(text);

	while (done < length) {
		ssize_t n = write(watchdog.fd, text + done, length - done);

		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return;
		done += (size_t)n;
	}
}

static void
say_at(const char *text)
{
	char line[256];
	struct timespec ts;
	struct tm tm;

	clock_gettime(CLOCK_REALTIME, &ts);
	localtime_r(&ts.tv_sec, &tm);
	snprintf(line, sizeof line, "[%02d:%02d:%02d.%03ld] %s", tm.tm_hour,
		 tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000, text);
	say(line);
}

static void
on_trace_signal(int signal_number)
{
	void *frames[FRAMES];
	int saved = errno;
	int n = backtrace(frames, FRAMES);

	(void)signal_number;
	say("polo: watchdog: the main loop is here:\n");
	backtrace_symbols_fd(frames, n, watchdog.fd);
	errno = saved;
}

static gboolean
traced(void)
{
	char *status = NULL;
	const char *line;
	gboolean result = FALSE;

	if (!g_file_get_contents("/proc/self/status", &status, NULL, NULL))
		return FALSE;
	line = strstr(status, "TracerPid:");
	if (line)
		result = atoi(line + strlen("TracerPid:")) != 0;
	g_free(status);
	return result;
}

static gboolean
main_thread_in_kernel(char *wchan, size_t size)
{
	char path[64], *stat = NULL, *close_paren, *contents = NULL;
	gboolean waiting = FALSE;

	snprintf(path, sizeof path, "/proc/self/task/%d/stat", watchdog.tid);
	if (!g_file_get_contents(path, &stat, NULL, NULL))
		return FALSE;
	close_paren = strrchr(stat, ')');
	waiting = close_paren && close_paren[1] == ' ' && close_paren[2] == 'D';
	g_free(stat);

	snprintf(wchan, size, "?");
	snprintf(path, sizeof path, "/proc/self/task/%d/wchan", watchdog.tid);
	if (waiting && g_file_get_contents(path, &contents, NULL, NULL)) {
		snprintf(wchan, size, "%s", contents[0] ? contents : "?");
		g_free(contents);
	}
	return waiting;
}

static int
sleeper(void *data)
{
	nanosleep(data, NULL);
	_exit(0);
}

void
polo_backend_watchdog_test_block_in_kernel(guint msec)
{
	struct timespec ts = { msec / 1000, (msec % 1000) * 1000000L };
	size_t size = 64 * 1024;
	char *stack = g_malloc(size);
	pid_t child = clone(sleeper, stack + size,
			    CLONE_VM | CLONE_VFORK | SIGCHLD, &ts);

	if (child > 0)
		waitpid(child, NULL, 0);
	g_free(stack);
}

static void
wait_until(gint64 deadline_ms)
{
	gint64 wait_us = (deadline_ms - now_ms()) * 1000;

	if (wait_us > 0)
		g_cond_wait_until(&watchdog.wake, &watchdog.lock,
				  g_get_monotonic_time() + wait_us);
}

static void *
watch(void *data)
{
	gint64 traced_start = -1, last_note = G_MININT64 / 2;
	gint64 in_kernel_at = G_MININT64 / 2;
	char line[400], wchan[64];

	(void)data;
	g_mutex_lock(&watchdog.lock);
	for (;;) {
		gint64 start, now, lag, next, asleep_from;

		if (!watchdog.working) {
			watchdog.parked = TRUE;
			g_cond_wait(&watchdog.wake, &watchdog.lock);
			watchdog.parked = FALSE;
			continue;
		}
		start = watchdog.work_start_ms;
		now = now_ms();
		lag = now - start;

		if (lag >= TRACE_MS && traced_start != start) {
			traced_start = start;
			g_mutex_unlock(&watchdog.lock);
			snprintf(line, sizeof line,
				 "polo: watchdog: the main loop has not run "
				 "for %" G_GINT64_FORMAT " ms\n",
				 lag);
			say_at(line);
			pthread_kill(watchdog.main_thread,
				     watchdog.trace_signal);
			g_mutex_lock(&watchdog.lock);
			continue;
		}
		if (watchdog.abort_ms && lag >= (gint64)watchdog.abort_ms) {
			g_mutex_unlock(&watchdog.lock);
			if (traced()) {
				g_mutex_lock(&watchdog.lock);
				wait_until(now_ms() + 1000);
				continue;
			}
			if (main_thread_in_kernel(wchan, sizeof wchan)) {
				in_kernel_at = now_ms();
				if (now - last_note >=
				    (gint64)watchdog.abort_ms) {
					last_note = now;
					snprintf(
						line, sizeof line,
						"polo: watchdog: the main loop "
						"is waiting in the kernel (%s) "
						"for %" G_GINT64_FORMAT
						" ms, probably on the GPU; not "
						"aborting, which could not end "
						"it\n",
						wchan, lag);
					say_at(line);
				}
			}
			if (now_ms() - in_kernel_at <
			    (gint64)watchdog.abort_ms) {
				g_mutex_lock(&watchdog.lock);
				wait_until(now_ms() + KERNEL_CHECK_MS);
				continue;
			}
			snprintf(line, sizeof line,
				 "polo: watchdog: the main loop has been "
				 "stuck for %" G_GINT64_FORMAT
				 " ms; aborting\n",
				 lag);
			say_at(line);
			abort();
		}

		next = traced_start != start ? start + TRACE_MS
					     : start + watchdog.abort_ms;
		if (!watchdog.abort_ms && traced_start == start) {
			watchdog.parked = TRUE;
			g_cond_wait(&watchdog.wake, &watchdog.lock);
			watchdog.parked = FALSE;
			continue;
		}
		asleep_from = now_ms();
		if (next <= asleep_from)
			next = asleep_from + 100;
		wait_until(next);
		if (now_ms() - MAX(next, asleep_from) > TRACE_MS &&
		    watchdog.working && watchdog.work_start_ms == start)
			watchdog.work_start_ms = now_ms();
	}
	return NULL;
}

static gint
watched_poll(GPollFD *fds, guint n_fds, gint timeout)
{
	gint64 now = now_ms(), worked;
	gint result;

	g_mutex_lock(&watchdog.lock);
	worked = now - watchdog.work_start_ms;
	watchdog.working = FALSE;
	g_mutex_unlock(&watchdog.lock);
	if (worked > LATE_MS)
		polo_log_scope_warning(polo_backend_log_scope("polo"),
				       "the main loop was blocked for at least "
				       "%" G_GINT64_FORMAT " ms",
				       worked);

	result = watchdog.poll(fds, n_fds, timeout);

	g_mutex_lock(&watchdog.lock);
	watchdog.working = TRUE;
	watchdog.work_start_ms = now_ms();
	if (watchdog.parked)
		g_cond_signal(&watchdog.wake);
	g_mutex_unlock(&watchdog.lock);
	return result;
}

void
polo_backend_watchdog_start(gboolean by_default)
{
	const char *seconds = g_getenv("POLO_WATCHDOG_SECONDS");
	struct sigaction action = { 0 };
	void *frames[1];
	sigset_t all, old;

	if (watchdog.running || (!seconds && !by_default))
		return;

	watchdog.abort_ms =
		seconds ? (guint)strtoul(seconds, NULL, 10) * 1000 : 0;
	watchdog.fd = polo_backend_log_fd();
	watchdog.main_thread = pthread_self();
	watchdog.tid = (pid_t)syscall(SYS_gettid);
	watchdog.trace_signal = SIGRTMIN + 4;
	g_mutex_init(&watchdog.lock);
	g_cond_init(&watchdog.wake);
	watchdog.working = TRUE;
	watchdog.work_start_ms = now_ms();

	backtrace(frames, 1);
	action.sa_handler = on_trace_signal;
	action.sa_flags = SA_RESTART;
	sigemptyset(&action.sa_mask);
	sigaction(watchdog.trace_signal, &action, NULL);

	sigfillset(&all);
	pthread_sigmask(SIG_SETMASK, &all, &old);
	if (pthread_create(&watchdog.thread, NULL, watch, NULL) != 0) {
		pthread_sigmask(SIG_SETMASK, &old, NULL);
		polo_log_scope_warning(polo_backend_log_scope("polo"),
				       "watchdog: cannot start its thread");
		return;
	}
	pthread_sigmask(SIG_SETMASK, &old, NULL);
	pthread_detach(watchdog.thread);
	watchdog.poll = g_main_context_get_poll_func(NULL);
	g_main_context_set_poll_func(NULL, watched_poll);
	watchdog.running = TRUE;

	if (watchdog.abort_ms)
		polo_log_scope_info(polo_backend_log_scope("polo"),
				    "watchdog: aborting after %u s without "
				    "progress",
				    watchdog.abort_ms / 1000);
	else
		polo_log_scope_info(polo_backend_log_scope("polo"),
				    "watchdog: logging stalls only");
}
