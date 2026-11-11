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
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
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
	int probe[2];
	volatile sig_atomic_t handling;
	volatile sig_atomic_t signal;
	volatile sig_atomic_t thread;
	volatile sig_atomic_t fail_report;
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

/* write(2) reports an unreadable buffer as EFAULT instead of faulting. */
static bool
readable(uintptr_t address)
{
	char byte;

	if (crash.probe[1] < 0 ||
	    write(crash.probe[1], (void *)address, 1) != 1)
		return false;
	if (read(crash.probe[0], &byte, 1) != 1)
		return true;
	return true;
}

static void
interrupted_at(void *context, uintptr_t *pc, uintptr_t *sp)
{
	ucontext_t *uc = context;

	*pc = *sp = 0;
#if defined(__x86_64__)
	*pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
	*sp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
#elif defined(__aarch64__)
	*pc = (uintptr_t)uc->uc_mcontext.pc;
	*sp = (uintptr_t)uc->uc_mcontext.sp;
#else
	(void)uc;
#endif
}

static void
report(int fd, int sig, siginfo_t *info, void **frames, int n_frames,
       uintptr_t bad_pc)
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
	if (crash.fail_report)
		raise(SIGSEGV);
	if (bad_pc) {
		say(fd, "  (a call to the unreadable address ");
		say_number(fd, bad_pc, 16);
		say(fd, "; the next frame made it)\n");
	}
	for (int i = 0; i < n_frames; i++) {
		say(fd, "  ");
		say_number(fd, (uintptr_t)frames[i], 16);
		say(fd, "\n");
	}
	backtrace_symbols_fd(frames, n_frames, fd);
	say(fd, "polo: the log before the crash:\n");
	polo_backend_log_dump_ring(fd);
	say(fd,
	    "polo: end of the crash report; a core dump follows if "
	    "the system keeps them (coredumpctl list polo)\n");
}

/* Naming the frames takes the dynamic loader's lock, which the crashed
 * thread may hold: rather than hang, die of the first signal after a while. */
static void
on_alarm(int sig)
{
	(void)sig;
	signal(crash.signal, SIG_DFL);
	raise(crash.signal);
}

static void
on_fatal_signal(int sig, siginfo_t *info, void *context)
{
	void *frames[FRAMES];
	int n_frames = 0;
	uintptr_t pc, sp, bad_pc = 0;

	if (crash.handling && crash.thread != gettid()) {
		for (;;)
			pause();
	}
	if (crash.handling == 1) {
		crash.handling = 2;
		for (int i = 0; i < crash.n_fds; i++) {
			say(crash.fds[i],
			    "polo: the crash report itself crashed; "
			    "the log before the crash:\n");
			polo_backend_log_dump_ring(crash.fds[i]);
			say(crash.fds[i], "polo: end of the crash report\n");
		}
		signal(sig, SIG_DFL);
		raise(sig);
		return;
	}
	if (!crash.handling) {
		crash.handling = 1;
		crash.signal = sig;
		crash.thread = gettid();
		signal(SIGALRM, on_alarm);
		alarm(3);
		interrupted_at(context, &pc, &sp);
		if (pc && !readable(pc)) {
			/* Unwinding from there faults inside libgcc. */
			bad_pc = pc;
			frames[n_frames++] = (void *)pc;
			if (sp && readable(sp) &&
			    readable(sp + sizeof(void *) - 1))
				frames[n_frames++] = *(void **)sp;
		} else {
			n_frames = backtrace(frames, FRAMES);
		}
		for (int i = 0; i < crash.n_fds; i++)
			report(crash.fds[i], sig, info, frames, n_frames,
			       bad_pc);
	}
	signal(sig, SIG_DFL);
	if (info->si_code > 0 && sig != SIGABRT)
		return;
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
	if (pipe2(crash.probe, O_CLOEXEC | O_NONBLOCK) < 0)
		crash.probe[0] = crash.probe[1] = -1;

	stack.ss_sp = malloc(ALT_STACK_BYTES);
	stack.ss_size = ALT_STACK_BYTES;
	if (stack.ss_sp)
		sigaltstack(&stack, NULL);

	action.sa_sigaction = on_fatal_signal;
	/* Not reset on entry, so that a fault in the report reaches it again.
	 */
	action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
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
	} else if (g_strcmp0(how, "report") == 0) {
#ifndef POLO_SANITIZED
		crash.fail_report = 1;
#endif
		raise(SIGSEGV);
	} else if (g_strcmp0(how, "jump") == 0) {
		void (*volatile nowhere)(void) =
			(void (*)(void))(uintptr_t)0x48;

		nowhere();
	} else if (g_strcmp0(how, "overflow") == 0) {
		volatile char deep[64 * 1024];

		deep[0] = 1;
		polo_backend_crash_for_tests(how);
		deep[1] = deep[0];
	} else {
		raise(SIGSEGV);
	}
}
