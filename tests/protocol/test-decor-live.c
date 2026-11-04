/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tests live decoration changes, as Marco's prefs_changed_callback () makes
 * them: a new theme or title font re-lays out framed windows at once, and a
 * theme that does not load leaves the current one in place.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "harness.h"

static void
settle(struct harness *h)
{
	for (int i = 0; i < 8; i++) {
		harness_roundtrip(h);
		usleep(10000);
	}
}

static int
titlebar(struct harness *h, struct harness_window *w)
{
	settle(h);
	harness_window_draw_pending(w);
	settle(h);
	return harness_window_info(w)->y;
}

int
main(int argc, char **argv)
{
	if (access("/usr/share/themes/Atlanta/metacity-1", F_OK) != 0 ||
	    access("/usr/share/themes/Menta/metacity-1", F_OK) != 0) {
		printf("Atlanta or Menta is not installed\n");
		return 77;
	}
	setenv("POLO_TEST_PREFS",
	       "general/theme='Atlanta';general/titlebar-uses-system-font=false;"
	       "general/titlebar-font='Sans Bold 10'",
	       1);
	struct harness *h = harness_start(argc, argv);
	struct harness_window *w =
		harness_window_create_decorated(h, 300, 200, "live");

	xdg_toplevel_set_maximized(w->xdg_toplevel);
	int atlanta = titlebar(h, w);

	CHECK(atlanta > 0, "no titlebar under Atlanta (y %d)", atlanta);

	harness_set_setting(h, "org.mate.Marco.general", "theme", "'Menta'");
	int menta = titlebar(h, w);

	CHECK(menta > 0 && menta != atlanta,
	      "switching to Menta left the titlebar at %d (Atlanta %d)", menta,
	      atlanta);

	harness_set_setting(h, "org.mate.Marco.general", "theme",
			    "'PoloNoSuchTheme'");
	CHECK(titlebar(h, w) == menta,
	      "a theme that does not load replaced Menta");

	harness_set_setting(h, "org.mate.Marco.general", "titlebar-font",
			    "'Sans Bold 24'");
	int big = titlebar(h, w);

	CHECK(big > menta, "a 24 point title font left the titlebar at %d", big);

	harness_set_setting(h, "org.mate.interface", "font-name", "'Sans 10'");
	harness_set_setting(h, "org.mate.Marco.general",
			    "titlebar-uses-system-font", "true");
	int system = titlebar(h, w);

	CHECK(system < big, "the system font did not replace the title font");
	harness_set_setting(h, "org.mate.interface", "font-name", "'Sans 28'");
	CHECK(titlebar(h, w) > system,
	      "a larger system font did not grow the titlebar");

	harness_window_destroy(w);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
