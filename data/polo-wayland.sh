#!/bin/sh
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later

if [ -z "$DBUS_SESSION_BUS_ADDRESS" ] && [ ! -S "${XDG_RUNTIME_DIR:-/nonexistent}/bus" ]; then
    exec dbus-run-session -- polo --session "$@"
fi
exec polo --session "$@"
