/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GPU resets as the kernel reports them (polo_test_v1 gpu_wedged stands in
 * for the DRM device's WEDGED uevent): one the kernel recovered from is
 * logged and repaints the whole screen; one it did not is logged with the
 * recovery it asks for, and Polo carries on.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"

static char log_path[64];

static char *
read_log(void)
{
	static char text[1 << 20];
	FILE *file = fopen(log_path, "r");
	size_t n = 0;

	if (file) {
		n = fread(text, 1, sizeof text - 1, file);
		fclose(file);
	}
	text[n] = '\0';
	return text;
}

static const char *
wait_logged(struct harness *h, size_t from, const char *line)
{
	const char *at = NULL;

	for (int i = 0; i < 200 && !at; i++) {
		harness_roundtrip(h);
		at = strstr(read_log() + from, line);
		if (!at)
			usleep(10 * 1000);
	}
	return at;
}

int
main(int argc, char **argv)
{
	struct harness *h;
	struct harness_window *w;
	const char *at;
	size_t from;
	double damage = 0;

	snprintf(log_path, sizeof log_path, "/tmp/polo-gpu-reset-XXXXXX");
	close(mkstemp(log_path));
	setenv("POLO_TEST_LOG", log_path, 1);
	setenv("POLO_STATS_INTERVAL_MS", "300", 1);
	h = harness_start(argc, argv);
	w = harness_window_create(h, 300, 200, "reset");
	harness_wait_frame(h, w->surface);
	usleep(700 * 1000);

	from = strlen(read_log());
	polo_test_v1_gpu_wedged(h->test, "none");
	CHECK(wait_logged(h, from,
			  "the GPU (test) was reset and recovered; repainting "
			  "every screen"),
	      "a recovered reset was not logged");
	at = wait_logged(h, from, "stats headless0:");
	CHECK(at, "no frame after the reset");
	at = strstr(at, "damage ");
	CHECK(at && sscanf(at, "damage %lf%%", &damage) == 1 && damage > 99,
	      "the reset repainted %.0f%% of the screen, not all of it",
	      damage);

	from = strlen(read_log());
	polo_test_v1_gpu_wedged(h->test, "rebind,bus-reset");
	CHECK(wait_logged(h, from,
			  "the GPU (test) is wedged; the kernel asks for "
			  "recovery by rebind,bus-reset"),
	      "an unrecovered reset was not logged");
	CHECK(!strstr(read_log() + from, "repainting every screen"),
	      "an unrecovered reset was taken for a recovered one");
	harness_wait_frame(h, w->surface);

	harness_window_destroy(w);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	unlink(log_path);
	return 0;
}
