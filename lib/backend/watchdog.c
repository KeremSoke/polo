/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The main loop beats every half second. Late beats are logged; a thread of
 * its own logs the main thread's stack once a beat is a second late, and
 * aborts Polo when the main loop has not moved for too long, so a frozen
 * session leaves a core instead of needing a reboot.
 */

#include <errno.h>
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "polo-backend-private.h"

#define BEAT_MS 500
#define LATE_MS 300
#define TRACE_MS 1000
#define FRAMES 64

static struct {
	gboolean running;
	pthread_t main_thread;
	pthread_t thread;
	guint abort_ms;
	int fd;
	gint64 last_beat_ms;
	gint64 previous_beat_ms;
	int trace_signal;
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

static void
sleep_until(gint64 deadline_ms)
{
	struct timespec ts = { deadline_ms / 1000,
			       (deadline_ms % 1000) * 1000000 };

	while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) ==
	       EINTR)
		;
}

static void *
watch(void *data)
{
	gint64 traced_beat = -1;
	char line[160];

	(void)data;
	for (;;) {
		gint64 beat = __atomic_load_n(&watchdog.last_beat_ms,
					      __ATOMIC_ACQUIRE);
		gint64 now = now_ms();
		gint64 lag = now - beat;
		gint64 next;

		if (lag >= BEAT_MS + TRACE_MS && traced_beat != beat) {
			traced_beat = beat;
			snprintf(line, sizeof line,
				 "polo: watchdog: the main loop has not run "
				 "for %" G_GINT64_FORMAT " ms\n",
				 lag);
			say_at(line);
			pthread_kill(watchdog.main_thread,
				     watchdog.trace_signal);
		}
		if (watchdog.abort_ms && lag >= (gint64)watchdog.abort_ms) {
			if (traced()) {
				sleep_until(now_ms() + 1000);
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

		if (traced_beat != beat)
			next = beat + BEAT_MS + TRACE_MS;
		else if (watchdog.abort_ms)
			next = beat + watchdog.abort_ms;
		else
			next = now + BEAT_MS;
		if (watchdog.abort_ms)
			next = MIN(next, beat + (gint64)watchdog.abort_ms);
		if (next <= now)
			next = now + BEAT_MS;
		sleep_until(next);
		if (now_ms() - next > TRACE_MS)
			sleep_until(now_ms() + 3 * BEAT_MS);
	}
	return NULL;
}

static gboolean
on_beat(gpointer data)
{
	gint64 now = now_ms();
	gint64 gap = now - watchdog.previous_beat_ms;

	(void)data;
	if (gap > BEAT_MS + LATE_MS)
		polo_log_scope_warning(polo_backend_log_scope("polo"),
				       "the main loop was blocked for at least "
				       "%" G_GINT64_FORMAT " ms",
				       gap - BEAT_MS);
	watchdog.previous_beat_ms = now;
	__atomic_store_n(&watchdog.last_beat_ms, now, __ATOMIC_RELEASE);
	return G_SOURCE_CONTINUE;
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
		seconds ? (guint)strtoul(seconds, NULL, 10) * 1000 : 15000;
	watchdog.fd = polo_backend_log_fd();
	watchdog.main_thread = pthread_self();
	watchdog.trace_signal = SIGRTMIN + 4;
	watchdog.previous_beat_ms = now_ms();
	watchdog.last_beat_ms = watchdog.previous_beat_ms;

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
	g_timeout_add_full(G_PRIORITY_HIGH, BEAT_MS, on_beat, NULL, NULL);
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
