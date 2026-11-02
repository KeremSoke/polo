/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "harness.h"

static void
chord(struct harness *h)
{
	polo_test_v1_key(h->test, KEY_LEFTCTRL, 1);
	polo_test_v1_key(h->test, KEY_LEFTALT, 1);
	polo_test_v1_key(h->test, KEY_BACKSPACE, 1);
	wl_display_flush(h->display);
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	chord(h);
	harness_roundtrip(h); // Disabled by default: compositor stays alive.
	CHECK(harness_stop(h) == 0, "default shutdown");
	setenv("POLO_TEST_KILL", "1", 1);
	h = harness_start(argc, argv);
	chord(h);
	siginfo_t info = { 0 };
	for (int i = 0; i < 100 && info.si_pid == 0; i++) {
		usleep(10000);
		CHECK(waitid(P_PID, h->pid, &info,
			     WEXITED | WNOHANG | WNOWAIT) == 0,
		      "waitid");
	}
	CHECK(info.si_pid == h->pid && info.si_code == CLD_EXITED &&
		      info.si_status == 0,
	      "debug kill did not cleanly terminate compositor");
	return harness_stop(h);
}
