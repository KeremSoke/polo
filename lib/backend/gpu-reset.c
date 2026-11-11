/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GPU resets the kernel reports: a DRM device's "change" uevent carrying
 * WEDGED=<recovery>. "none" means the kernel brought the GPU back, and
 * every screen is repainted; anything else names what it would take
 * (rebind, bus-reset, ...), which userspace cannot do, so it is logged.
 */

#include <libudev.h>

#include <glib-unix.h>
#include <libweston/libweston.h>

#include "polo-backend-private.h"

void
polo_backend_gpu_wedged(PoloBackendServer *server, const char *device,
			const char *recovery)
{
	PoloLogScope *log = polo_backend_log_scope("polo");

	if (g_strcmp0(recovery, "none") == 0) {
		polo_log_scope_warning(log,
				       "the GPU (%s) was reset and recovered; "
				       "repainting every screen",
				       device);
		weston_compositor_damage_all(server->compositor);
		return;
	}
	polo_log_scope_warning(log,
			       "the GPU (%s) is wedged; the kernel asks for "
			       "recovery by %s, so the screens may stay "
			       "frozen until that is done",
			       device, recovery);
}

static gboolean
udev_ready(gint fd, GIOCondition condition, gpointer data)
{
	PoloBackendServer *server = data;
	struct udev_device *device =
		udev_monitor_receive_device(server->gpu_monitor);
	const char *recovery;

	if (!device)
		return G_SOURCE_CONTINUE;
	recovery = udev_device_get_property_value(device, "WEDGED");
	if (recovery &&
	    g_strcmp0(udev_device_get_action(device), "change") == 0)
		polo_backend_gpu_wedged(server, udev_device_get_sysname(device),
					recovery);
	udev_device_unref(device);
	return G_SOURCE_CONTINUE;
}

void
polo_backend_gpu_reset_init(PoloBackendServer *server)
{
	server->gpu_udev = udev_new();
	if (!server->gpu_udev)
		return;
	server->gpu_monitor =
		udev_monitor_new_from_netlink(server->gpu_udev, "udev");
	if (!server->gpu_monitor ||
	    udev_monitor_filter_add_match_subsystem_devtype(server->gpu_monitor,
							    "drm", NULL) < 0 ||
	    udev_monitor_enable_receiving(server->gpu_monitor) < 0) {
		polo_log_scope_warning(polo_backend_log_scope("polo"),
				       "cannot watch for GPU resets");
		polo_backend_gpu_reset_fini(server);
		return;
	}
	server->gpu_source =
		g_unix_fd_add(udev_monitor_get_fd(server->gpu_monitor), G_IO_IN,
			      udev_ready, server);
}

void
polo_backend_gpu_reset_fini(PoloBackendServer *server)
{
	if (server->gpu_source)
		g_source_remove(server->gpu_source);
	server->gpu_source = 0;
	g_clear_pointer(&server->gpu_monitor, udev_monitor_unref);
	g_clear_pointer(&server->gpu_udev, udev_unref);
}
