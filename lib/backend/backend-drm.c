/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <linux/input-event-codes.h>
#include <libweston/backend-drm.h>
#include "polo-backend-private.h"

void
polo_backend_drm_configure_device(struct weston_compositor *compositor,
				  struct libinput_device *device)
{
	polo_backend_server_input_device_added(
		weston_compositor_get_user_data(compositor), device);
}

static void
device_removed(struct weston_compositor *compositor,
	       struct libinput_device *device)
{
	polo_backend_server_input_device_removed(
		weston_compositor_get_user_data(compositor), device);
}

static void
emergency_exit(struct weston_keyboard *keyboard, const struct timespec *time,
	       uint32_t key, void *data)
{
	weston_compositor_exit(keyboard->seat->compositor);
}

void
polo_backend_drm_install_bindings(PoloBackendServer *server,
				  gboolean debug_kill)
{
	/* The DRM backend installs Ctrl+Alt+F1..F12 through libseat itself */
	if (debug_kill)
		weston_compositor_add_key_binding(
			server->compositor, KEY_BACKSPACE,
			MODIFIER_CTRL | MODIFIER_ALT, emergency_exit, server);
}

struct weston_backend *
polo_backend_drm_load(PoloBackendServer *server,
		      const PoloBackendConfig *config)
{
	struct weston_drm_backend_config drm = { 0 };
	drm.base.struct_version = WESTON_DRM_BACKEND_CONFIG_VERSION;
	drm.base.struct_size = sizeof(drm);
	drm.renderer = config->renderer == POLO_BACKEND_RENDERER_PIXMAN
		? WESTON_RENDERER_PIXMAN
		: WESTON_RENDERER_GL;
	drm.seat_id = (char *)(config->seat ? config->seat : "seat0");
	drm.use_pixman_shadow = true;
	drm.configure_device = polo_backend_drm_configure_device;
	drm.device_removed = device_removed;
	return weston_compositor_load_backend(server->compositor,
					      WESTON_BACKEND_DRM, &drm.base);
}
