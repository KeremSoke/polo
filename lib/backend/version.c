/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>

#include <libweston/libweston.h>

#include "polo-backend.h"

const char *
polo_backend_libweston_version(void)
{
	static char version[32];
	int major, minor, micro;

	if (version[0] == '\0') {
		weston_version(&major, &minor, &micro);
		snprintf(version, sizeof(version), "%d.%d.%d", major, minor,
			 micro);
	}

	return version;
}
