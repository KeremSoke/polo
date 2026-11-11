/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * On a fatal signal: the signal, a backtrace and the last lines of the log,
 * to the log and to stderr (the journal under a session manager), then the
 * signal again with its default action, so systemd-coredump still gets the
 * core. Everything here runs in the signal handler: write(2) and fixed
 * memory only. Sanitizer builds keep their own, better, reports.
 */

#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "polo-backend-private.h"

#if defined(__SANITIZE_ADDRESS__)
#define POLO_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define POLO_SANITIZED 1
#endif
#endif

#ifndef POLO_SANITIZED
#define FRAMES 64
#define ALT_STACK_BYTES (64 * 1024)

static const int fatal_signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };

static struct {
	int fds[2];
	int n_fds;
	volatile sig_atomic_t handling;
} crash;

static void
say(int fd, const char *text)
{
	size_t size = strlen(text);

	while (size > 0) {
		ssize_t n = write(fd, text, size);

		if (n <= 0)
			return;
		text += n;
		size -= (size_t)n;
	}
}

static void
say_number(int fd, uintptr_t value, unsigned base)
{
	char digits[32];
	int at = sizeof digits - 1;

	digits[at] = '\0';
	do {
		digits[--at] = "0123456789abcdef"[value % base];
		value /= base;
	} while (value && at > 0);
	if (base == 16) {
		digits[--at] = 'x';
		digits[--at] = '0';
	}
	say(fd, digits + at);
}

static const char *
signal_name(int sig)
{
	switch (sig) {
	case SIGSEGV:
		return "SIGSEGV";
	case SIGBUS:
		return "SIGBUS";
	case SIGILL:
		return "SIGILL";
	case SIGFPE:
		return "SIGFPE";
	case SIGABRT:
		return "SIGABRT";
	default:
		return "a signal";
	}
}

static void
report(int fd, int sig, siginfo_t *info, void **frames, int n_frames)
{
	say(fd, "polo: crashed with ");
	say(fd, signal_name(sig));
	if (info->si_code > 0) {
		say(fd, " at address ");
		say_number(fd, (uintptr_t)info->si_addr, 16);
	}
	say(fd, " in thread ");
	say_number(fd, (uintptr_t)gettid(), 10);
	say(fd, "; where it happened:\n");
	backtrace_symbols_fd(frames, n_frames, fd);
	say(fd, "polo: the log before the crash:\n");
	polo_backend_log_dump_ring(fd);
	say(fd,
	    "polo: end of the crash report; a core dump follows if "
	    "the system keeps them (coredumpctl list polo)\n");
}

static void
on_fatal_signal(int sig, siginfo_t *info, void *context)
{
	void *frames[FRAMES];
	int n_frames;

	(void)context;
	if (!crash.handling) {
		crash.handling = 1;
		n_frames = backtrace(frames, FRAMES);
		for (int i = 0; i < crash.n_fds; i++)
			report(crash.fds[i], sig, info, frames, n_frames);
	}
	if (info->si_code > 0 && sig != SIGABRT)
		return;
	signal(sig, SIG_DFL);
	raise(sig);
}

#endif

void
polo_backend_crash_handler_install(void)
{
#ifdef POLO_SANITIZED
	polo_log_scope_info(polo_backend_log_scope("polo"),
			    "crash reports are left to the sanitizer");
#else
	struct sigaction action = { 0 };
	stack_t stack = { 0 };
	void *frame;
	int log_fd = polo_backend_log_fd();

	crash.n_fds = 0;
	crash.fds[crash.n_fds++] = log_fd;
	if (log_fd != STDERR_FILENO)
		crash.fds[crash.n_fds++] = STDERR_FILENO;

	backtrace(&frame, 1);

	stack.ss_sp = malloc(ALT_STACK_BYTES);
	stack.ss_size = ALT_STACK_BYTES;
	if (stack.ss_sp)
		sigaltstack(&stack, NULL);

	action.sa_sigaction = on_fatal_signal;
	action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
	sigemptyset(&action.sa_mask);
	for (size_t i = 0; i < G_N_ELEMENTS(fatal_signals); i++)
		sigaction(fatal_signals[i], &action, NULL);
#endif
}

void
polo_backend_crash_for_tests(const char *how)
{
	if (g_strcmp0(how, "abort") == 0) {
		abort();
	} else if (g_strcmp0(how, "overflow") == 0) {
		volatile char deep[64 * 1024];

		deep[0] = 1;
		polo_backend_crash_for_tests(how);
		deep[1] = deep[0];
	} else {
		raise(SIGSEGV);
	}
}
