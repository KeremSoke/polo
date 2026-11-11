#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""The main-loop watchdog, against a headless Polo whose main loop a test
hook blocks.

  test-watchdog.py POLO

A blocked main loop is logged, one blocked for over a second also logs where
the main thread is, and one blocked past POLO_WATCHDOG_SECONDS aborts Polo,
unless the system is short of memory (POLO_WATCHDOG_PRESSURE stands in for
/proc/pressure/memory).
"""

import os
import re
import resource
import signal
import subprocess
import sys
import tempfile
import time

failures = []
shown = []


def check(condition, message, log=None):
    if not condition:
        failures.append(message)
        print("FAIL:", message, file=sys.stderr)
        if log is not None and log not in shown:
            shown.append(log)
            print("Polo's log:\n" + log, file=sys.stderr)


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def start(polo, runtime, log, stalls, seconds, extra=None):
    env = dict(os.environ)
    env.pop("POLO_WATCHDOG_PRESSURE", None)
    env.update(extra or {})
    env.update(XDG_RUNTIME_DIR=runtime, POLO_SHELL_UI="", POLO_XWAYLAND="off",
               GSETTINGS_BACKEND="memory", POLO_TEST_STALL_MS=stalls)
    env.pop("POLO_WATCHDOG_SECONDS", None)
    if seconds is not None:
        env["POLO_WATCHDOG_SECONDS"] = seconds
    return subprocess.Popen(
        [polo, "--backend=headless", "--renderer=pixman", "--test-mode",
         "--socket=polo-watchdog-%d" % os.getpid(), "--width=640", "--height=480"],
        env=env, stdout=log, stderr=subprocess.STDOUT, preexec_fn=no_core)


def text(path):
    with open(path, errors="replace") as f:
        return f.read()


def wait_for(proc, path, count, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if text(path).count("test: main loop unblocked") >= count or proc.poll() is not None:
            return
        time.sleep(0.05)


def frozen_process(polo, runtime):
    path = os.path.join(runtime, "frozen.log")
    with open(path, "w") as log:
        proc = start(polo, runtime, log, "1", "2")
        time.sleep(2)
        proc.send_signal(signal.SIGSTOP)
        time.sleep(5)
        proc.send_signal(signal.SIGCONT)
        time.sleep(2)
        alive = proc.poll() is None
        status = stop(proc)
    return text(path), alive, status


def stop(proc):
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
    try:
        return proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
        return proc.wait()


def run(polo, runtime, name, stalls, seconds, unblocked, timeout, extra=None):
    path = os.path.join(runtime, name + ".log")
    with open(path, "w") as log:
        proc = start(polo, runtime, log, stalls, seconds, extra)
        wait_for(proc, path, unblocked, timeout)
        alive = proc.poll() is None
        status = stop(proc)
    return text(path), alive, status


def blocked_ms(log):
    return [int(m) for m in re.findall(r"main loop was blocked for at least (\d+) ms", log)]


def main():
    if len(sys.argv) < 2:
        print("usage: test-watchdog.py POLO", file=sys.stderr)
        return 2
    polo = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="polo-watchdog-") as runtime:
        os.chmod(runtime, 0o700)

        log, alive, status = run(polo, runtime, "stalls", "1200,2600", "6", 2, 30)
        check(alive, "Polo did not survive stalls shorter than its limit", log)
        check(status == 0, "Polo exited with %s after SIGTERM" % status, log)
        check("watchdog: aborting after 6 s without progress" in log, "the watchdog did not say it was on", log)
        ms = blocked_ms(log)
        check(any(1150 <= v <= 1700 for v in ms) and any(2550 <= v <= 3200 for v in ms),
              "the 1.2 s and 2.6 s blocks were not measured: %s" % ms, log)
        check(log.count("watchdog: the main loop has not run for") == 1,
              "the second-long stall was not reported exactly once", log)
        check(re.search(r"the main loop is here:\n(.*\n)*.*g_usleep", log) is not None,
              "the main thread's stack does not show where it was blocked", log)
        check("aborting\n" not in log, "Polo aborted below its limit", log)

        log, alive, status = run(polo, runtime, "abort", "30000", "2", 1, 15)
        check(status == -signal.SIGABRT, "a stuck main loop ended with %s, not SIGABRT" % status, log)
        check(re.search(r"stuck for \d+ ms; aborting", log) is not None, "the abort was not logged", log)
        check("test: main loop unblocked" not in log, "Polo waited out the whole stall", log)

        log, alive, status = run(polo, runtime, "log-only", "3000", "0", 1, 20)
        check(alive and status == 0, "POLO_WATCHDOG_SECONDS=0 still ended Polo (%s)" % status, log)
        check("watchdog: logging stalls only" in log, "POLO_WATCHDOG_SECONDS=0 did not say it only logs", log)
        check("the main loop is here:" in log, "POLO_WATCHDOG_SECONDS=0 stopped the stack trace", log)

        log, alive, status = run(polo, runtime, "kernel", "k6000", "2", 1, 20)
        check(alive and status == 0, "a main loop waiting in the kernel was aborted: %s" % status, log)
        check("waiting in the kernel" in log, "a main loop in a kernel wait was not reported as such", log)
        check("aborting\n" not in log, "the watchdog aborted a main loop that could not exit", log)
        check("test: main loop unblocked" in log, "the kernel wait did not finish", log)

        pressure = os.path.join(runtime, "pressure")
        with open(pressure, "w") as f:
            f.write("some avg10=80.00 avg60=40.00 avg300=10.00 total=1\n"
                    "full avg10=50.00 avg60=20.00 avg300=5.00 total=1\n")
        log, alive, status = run(polo, runtime, "thrashing", "6000", "2", 1, 20,
                                 {"POLO_WATCHDOG_PRESSURE": pressure})
        check(alive and status == 0, "a main loop stuck while memory thrashed was aborted: %s" % status, log)
        check("short of memory (50% stalled)" in log, "thrashing memory was not reported", log)
        check("aborting\n" not in log, "the watchdog aborted while the system was short of memory", log)

        log, alive, status = frozen_process(polo, runtime)
        check(alive and status == 0,
              "a process frozen as a whole (hibernation) was aborted on waking: %s" % status, log)
        check("aborting\n" not in log, "the watchdog blamed a frozen process on the main loop", log)

        log, alive, status = run(polo, runtime, "off", "1200", None, 1, 15)
        check(alive and status == 0, "Polo without the watchdog ended with %s" % status, log)
        check("watchdog:" not in log and not blocked_ms(log),
              "test mode started the watchdog without POLO_WATCHDOG_SECONDS", log)

    if failures:
        return 1
    print("watchdog: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
