/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_idle_notifier_v1 v2: idled after the timeout without input, resumed on
 * input; get_idle_notification honours zwp_idle_inhibitor_v1 and
 * get_input_idle_notification does not.
 */

#include <string.h>
#include <time.h>

#include "ext-idle-notify-v1-client-protocol.h"
#include "harness.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"

#define TIMEOUT_MSEC 300

static struct ext_idle_notifier_v1 *notifier;
static struct zwp_idle_inhibit_manager_v1 *inhibit_manager;

struct watch {
	struct ext_idle_notification_v1 *notification;
	bool idled;
	int idles;
	int resumes;
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface, ext_idle_notifier_v1_interface.name) == 0 &&
	    version >= 2)
		notifier = wl_registry_bind(registry, name,
					    &ext_idle_notifier_v1_interface, 2);
	else if (strcmp(interface,
			zwp_idle_inhibit_manager_v1_interface.name) == 0)
		inhibit_manager = wl_registry_bind(
			registry, name, &zwp_idle_inhibit_manager_v1_interface,
			1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
idled(void *data, struct ext_idle_notification_v1 *notification)
{
	struct watch *watch = data;

	watch->idled = true;
	watch->idles++;
}

static void
resumed(void *data, struct ext_idle_notification_v1 *notification)
{
	struct watch *watch = data;

	watch->idled = false;
	watch->resumes++;
}

static const struct ext_idle_notification_v1_listener notification_listener = {
	idled,
	resumed,
};

static void
pump(struct harness *h, int msec)
{
	struct timespec tick = { 0, 20 * 1000000L };

	for (int i = 0; i < msec / 20; i++) {
		nanosleep(&tick, NULL);
		harness_roundtrip(h);
	}
}

static bool
wait_for(struct harness *h, struct watch *watch, bool wanted, int msec)
{
	struct timespec tick = { 0, 20 * 1000000L };

	for (int i = 0; i < msec / 20 && watch->idled != wanted; i++) {
		nanosleep(&tick, NULL);
		harness_roundtrip(h);
	}
	return watch->idled == wanted;
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);
	struct watch session = { 0 }, input = { 0 };

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(notifier, "no ext_idle_notifier_v1 version 2");
	CHECK(inhibit_manager, "no zwp_idle_inhibit_manager_v1");

	session.notification = ext_idle_notifier_v1_get_idle_notification(
		notifier, TIMEOUT_MSEC, h->seat);
	ext_idle_notification_v1_add_listener(session.notification,
					      &notification_listener, &session);
	input.notification = ext_idle_notifier_v1_get_input_idle_notification(
		notifier, TIMEOUT_MSEC, h->seat);
	ext_idle_notification_v1_add_listener(input.notification,
					      &notification_listener, &input);

	CHECK(wait_for(h, &session, true, 3000), "no idled without input");
	CHECK(wait_for(h, &input, true, 3000), "no input idled without input");

	harness_move_pointer(h, 100, 100);
	CHECK(wait_for(h, &session, false, 2000), "input did not resume");
	CHECK(wait_for(h, &input, false, 2000),
	      "input did not resume input idle");
	CHECK(wait_for(h, &session, true, 3000), "no idled after the input");

	struct harness_window *w = harness_window_create(h, 200, 150, "video");
	struct zwp_idle_inhibitor_v1 *inhibitor =
		zwp_idle_inhibit_manager_v1_create_inhibitor(inhibit_manager,
							     w->surface);

	harness_move_pointer(h, 120, 120);
	CHECK(wait_for(h, &session, false, 2000),
	      "the window's input did not resume");
	CHECK(wait_for(h, &input, true, 3000),
	      "an inhibitor held off input idle, which ignores inhibitors");
	pump(h, 1500);
	CHECK(!session.idled, "idled while an inhibitor was visible");

	zwp_idle_inhibitor_v1_destroy(inhibitor);
	CHECK(wait_for(h, &session, true, 3000),
	      "no idled after the inhibitor went");

	int input_resumes = input.resumes;

	inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(
		inhibit_manager, w->surface);
	CHECK(wait_for(h, &session, false, 2000),
	      "a new inhibitor did not resume an idle notification");
	pump(h, 200);
	CHECK(input.idled && input.resumes == input_resumes,
	      "a new inhibitor resumed input idle");

	zwp_idle_inhibitor_v1_destroy(inhibitor);
	harness_window_destroy(w);
	ext_idle_notification_v1_destroy(session.notification);
	ext_idle_notification_v1_destroy(input.notification);
	ext_idle_notifier_v1_destroy(notifier);
	zwp_idle_inhibit_manager_v1_destroy(inhibit_manager);
	wl_registry_destroy(registry);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
