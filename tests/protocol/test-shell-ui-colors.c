/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Runs polo-shell-ui from argv[2]. It sends the GTK colour table at bind and
 * answers lookup_color with the GTK theme's @define-color values.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"

static const char *
request_with_prefix(struct harness *h, const char *prefix)
{
	for (int i = 0; i < h->n_shell_ui_requests; i++)
		if (strncmp(h->shell_ui_requests[i], prefix, strlen(prefix)) ==
		    0)
			return h->shell_ui_requests[i];
	return NULL;
}

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec ts = { 0, ms * 1000000L };

	harness_roundtrip(h);
	nanosleep(&ts, NULL);
	harness_roundtrip(h);
}

static const char *
wait_request(struct harness *h, const char *prefix)
{
	for (int i = 0; i < 200 && !request_with_prefix(h, prefix); i++)
		pause_ms(h, 50);
	CHECK(request_with_prefix(h, prefix), "no \"%s\"", prefix);
	return request_with_prefix(h, prefix);
}

int
main(int argc, char **argv)
{
	struct harness *h;
	const char *answer;
	bool menta;
	double first, last, r, g, b, a;

	CHECK(argc >= 3, "usage: %s POLO SHELL_UI", argv[0]);
	setenv("POLO_SHELL_UI", argv[2], 1);
	menta = access("/usr/share/themes/Menta/metacity-1", F_OK) == 0;
	if (menta)
		setenv("POLO_TEST_PREFS", "general/theme='Menta'", 1);
	h = harness_start(argc, argv);

	if (menta) {
		answer = wait_request(h, "custom_color wm_");
		CHECK(strstr(answer, " none") == NULL,
		      "Menta's GTK colour is unknown: %s", answer);
	}

	answer = wait_request(h, "color_table ");
	CHECK(sscanf(answer, "color_table %lf %lf", &first, &last) == 2, "%s",
	      answer);
	CHECK(first >= 0 && first <= 1 && last > 0 && last <= 1, "%s", answer);

	polo_test_v1_shell_ui_lookup_color(h->test, "theme_bg_color");
	answer = wait_request(h, "custom_color theme_bg_color ");
	CHECK(sscanf(answer, "custom_color theme_bg_color %lf %lf %lf %lf", &r,
		     &g, &b, &a) == 4,
	      "%s", answer);
	CHECK(a > 0 && r >= 0 && r <= 1, "%s", answer);

	polo_test_v1_shell_ui_lookup_color(h->test, "polo_no_such_colour");
	answer = wait_request(h, "custom_color polo_no_such_colour ");
	CHECK(strcmp(answer, "custom_color polo_no_such_colour none") == 0,
	      "%s", answer);

	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
