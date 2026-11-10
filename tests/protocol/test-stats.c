/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * polo --stats (here POLO_STATS_INTERVAL_MS): with frames, input and an open
 * animation going, the log gets a summary per output and one for input and
 * effects, with numbers that make sense for a headless output at 60 Hz.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

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
line_with(const char *text, const char *start)
{
	const char *at = strstr(text, start);

	return at;
}

int
main(int argc, char **argv)
{
	struct timespec tick = { 0, 16 * 1000000L };
	struct harness *h;
	struct harness_window *a, *b;
	const char *text, *output_line, *input_line;
	unsigned frames = 0;
	double p50 = -1;

	snprintf(log_path, sizeof log_path, "/tmp/polo-stats-XXXXXX");
	close(mkstemp(log_path));
	setenv("POLO_TEST_LOG", log_path, 1);
	setenv("POLO_STATS_INTERVAL_MS", "400", 1);
	h = harness_start(argc, argv);
	harness_set_setting(h, "org.mate.polo.effects", "open-animation",
			    "'zoom'");
	a = harness_window_create(h, 300, 200, "a");

	for (int i = 0; i < 60; i++) {
		harness_key(h, KEY_A, i % 2 == 0);
		harness_move_pointer(h, 100 + i, 100 + i);
		harness_wait_frame(h, a->surface);
		nanosleep(&tick, NULL);
	}
	b = harness_window_create(h, 300, 200, "b");
	for (int i = 0; i < 40; i++) {
		harness_wait_frame(h, b->surface);
		nanosleep(&tick, NULL);
	}
	/* Idle, then single frames: each restarts the repaint loop. */
	for (int i = 0; i < 3; i++) {
		usleep(700 * 1000);
		harness_wait_frame(h, a->surface);
	}
	usleep(600 * 1000);
	harness_roundtrip(h);

	text = read_log();
	CHECK(strstr(text, "stats: a summary every 400 ms"),
	      "stats did not start");
	if (getenv("POLO_STATS_KEEP"))
		fprintf(stderr, "LOG %s\n", log_path);
	output_line = line_with(text, "stats headless0:");
	CHECK(output_line, "no summary for the output");
	CHECK(sscanf(output_line, "stats headless0: %u frames", &frames) == 1 &&
		      frames > 0,
	      "the output summary counts no frames: %.200s", output_line);
	const char *repaint =
		strstr(output_line, "repaint to presentation p50 ");

	CHECK(repaint &&
		      sscanf(repaint, "repaint to presentation p50 %lf",
			     &p50) == 1 &&
		      p50 >= 0 && p50 < 50,
	      "repaint to presentation is missing or absurd: %.300s",
	      output_line);
	CHECK(strstr(output_line, "damage "), "no damage share: %.300s",
	      output_line);
	for (const char *at = strstr(text, "repaint to presentation p50 "); at;
	     at = strstr(at + 1, "repaint to presentation p50 ")) {
		double p50, p95, max;

		CHECK(sscanf(at,
			     "repaint to presentation p50 %lf p95 %lf max %lf",
			     &p50, &p95, &max) == 3,
		      "unreadable: %.120s", at);
		CHECK(max < 100,
		      "a frame after idle took %.1f ms to present: the restart "
		      "of the repaint loop was taken for its presentation",
		      max);
	}
	CHECK(strstr(output_line, "top view on a plane "),
	      "no plane count: %.300s", output_line);
	input_line = line_with(text, "stats input and effects");
	CHECK(input_line && strstr(input_line, "input to handling p50"),
	      "no input timing");
	CHECK(strstr(text, "input to frame p50"), "no input to frame timing");
	CHECK(strstr(text, "effect simulation p50"),
	      "the open animation was not timed");
	CHECK(strstr(text, "most commits: test-stats "),
	      "the committing program is not named");

	harness_window_destroy(b);
	harness_window_destroy(a);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	if (!getenv("POLO_STATS_KEEP"))
		unlink(log_path);
	return 0;
}
