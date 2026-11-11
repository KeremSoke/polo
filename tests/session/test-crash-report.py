#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""The crash report, against a headless Polo that a test hook crashes.

  test-crash-report.py POLO

A segfault, an abort and a stack overflow each leave, in the log and on
stderr, the signal, a backtrace and the log before the crash, and Polo still
dies of the signal itself, so systemd-coredump gets its core.
"""

import os
import resource
import signal
import subprocess
import sys
import tempfile

failures = []


def check(condition, message, log=None):
    if not condition:
        failures.append(message)
        print("FAIL:", message, file=sys.stderr)
        if log is not None:
            print("Polo's log:\n" + log, file=sys.stderr)


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def text(path):
    with open(path, errors="replace") as f:
        return f.read()


def crash(polo, runtime, how):
    log_path = os.path.join(runtime, how + ".log")
    env = dict(os.environ)
    env.update(XDG_RUNTIME_DIR=runtime, POLO_SHELL_UI="", POLO_XWAYLAND="off",
               GSETTINGS_BACKEND="memory", POLO_TEST_CRASH=how)
    env.pop("POLO_WATCHDOG_SECONDS", None)
    proc = subprocess.run(
        [polo, "--backend=headless", "--renderer=pixman", "--test-mode",
         "--socket=polo-crash-%d" % os.getpid(), "--width=640", "--height=480",
         "--log=" + log_path],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=60,
        preexec_fn=no_core)
    return text(log_path), proc.stderr.decode(errors="replace"), proc.returncode


def main():
    if len(sys.argv) < 2:
        print("usage: test-crash-report.py POLO", file=sys.stderr)
        return 2
    polo = sys.argv[1]
    cases = [("segv", signal.SIGSEGV, "SIGSEGV"), ("abort", signal.SIGABRT, "SIGABRT"),
             ("overflow", signal.SIGSEGV, "SIGSEGV")]
    with tempfile.TemporaryDirectory(prefix="polo-crash-") as runtime:
        os.chmod(runtime, 0o700)
        for how, sig, name in cases:
            log, err, status = crash(polo, runtime, how)
            if "crash reports are left to the sanitizer" in log:
                print("skipped: a sanitizer build reports crashes itself")
                return 77
            check(status == -sig, "%s: Polo ended with %s, not %s" % (how, status, name), log)
            for where, out in (("the log", log), ("stderr", err)):
                check("polo: crashed with " + name in out,
                      "%s: %s does not say Polo crashed with %s" % (how, where, name), log)
                check("polo: the log before the crash:" in out,
                      "%s: %s has no log before the crash" % (how, where), log)
                report = out[out.find("polo: the log before the crash:"):]
                check("test: crashing with " + how in report,
                      "%s: the log before the crash in %s misses its last line" % (how, where), log)
                check("listening on polo-crash-" in report,
                      "%s: the log before the crash in %s misses an earlier line" % (how, where), log)
                check("end of the crash report" in out,
                      "%s: the report in %s did not finish" % (how, where), log)
            where_from = log[log.find("where it happened:"):log.find("polo: the log before")]
            check(where_from.count("\n") >= 3,
                  "%s: the backtrace has under 3 frames" % how, log)
    if failures:
        return 1
    print("crash report: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
