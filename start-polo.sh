#!/usr/bin/env bash
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Starts the Polo MATE session from a tty.
# Ctrl+Alt+Backspace quits Polo at any time.
#
# Logs go to ~/polo-logs/<date>/: polo.log (Polo) and apps.log (what the session's
# programs print). ~/polo-logs/latest points at the newest; the last 10 runs are kept.
#
#   POLO_BUILD=DIR   build directory to run (default: build, next to this script)
#   POLO_NO_BUILD=1  run the build directory as it is, without compiling first
#   POLO_BUS=user    use the login's D-Bus and systemd --user instead of a private bus
#   POLO_IM=fcitx5   start fcitx5 for input methods
#   POLO_DEBUG=1     also log Polo's debug messages (screens, effects, windows);
#                    or a comma-separated list of scopes, such as polo,drm-backend

set -uo pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${POLO_BUILD:-$SOURCE_DIR/build}"
LOG_ROOT="$HOME/polo-logs"
KEEP_RUNS=10

fail() {
    printf 'start-polo: %s\n' "$*" >&2
    exit 1
}

if [[ $EUID -eq 0 ]]; then
    fail "run it as your own user, not as root"
fi
if [[ -n "${WAYLAND_DISPLAY:-}" || -n "${DISPLAY:-}" ]]; then
    fail "a graphical session is already running here; switch to a text console (Ctrl+Alt+F3) and log in there"
fi
if ! tty -s; then
    fail "run it from a text console"
fi
if [[ -z "${XDG_RUNTIME_DIR:-}" ]]; then
    fail "XDG_RUNTIME_DIR is not set; log in on the console instead of using su or sudo"
fi

if [[ -d "$BUILD_DIR" ]]; then
    if [[ "${POLO_NO_BUILD:-0}" != 1 ]]; then
        printf 'Building %s...\n' "$BUILD_DIR"
        meson compile -C "$BUILD_DIR" || fail "the build failed"
    fi
    polo=(meson devenv -C "$BUILD_DIR" polo)
elif command -v polo >/dev/null 2>&1; then
    polo=(polo)
else
    fail "no build directory at $BUILD_DIR and no installed polo; build Polo first (see README.md)"
fi

run_dir="$LOG_ROOT/$(date +%Y-%m-%d_%H-%M-%S)"
mkdir -p "$run_dir" || fail "cannot create $run_dir"
ln -sfn "$run_dir" "$LOG_ROOT/latest"
ls -1d "$LOG_ROOT"/????-??-??_??-??-?? 2>/dev/null | head -n -"$KEEP_RUNS" | xargs -r rm -rf

clients=()
if [[ "${POLO_IM:-}" == fcitx5 ]]; then
    if command -v fcitx5 >/dev/null 2>&1; then
        export GTK_IM_MODULE=wayland QT_IM_MODULE=wayland
        clients+=(--exec fcitx5)
    else
        printf 'start-polo: fcitx5 is not installed; starting without it\n' >&2
    fi
fi

debug=()
if [[ -n "${POLO_DEBUG:-}" && "${POLO_DEBUG}" != 0 ]]; then
    scopes="$POLO_DEBUG"
    [[ "$scopes" == 1 ]] && scopes=polo,polo-policy,polo-effects
    debug=(--debug-scopes="$scopes")
fi

launcher=(dbus-run-session --)
if [[ "${POLO_BUS:-private}" == user ]]; then
    launcher=()
fi
command -v dbus-run-session >/dev/null 2>&1 || [[ ${#launcher[@]} -eq 0 ]] ||
    fail "dbus-run-session is not installed"

{
    printf 'Polo started %s\n' "$(date)"
    printf 'Source: %s (%s)\n' "$SOURCE_DIR" "$(git -C "$SOURCE_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    printf 'Console: %s, user %s\n\n' "$(tty)" "$USER"
} >"$run_dir/apps.log"

printf 'Starting Polo. Logs: %s\n' "$run_dir"
started=$(date +%s)
cd "$SOURCE_DIR"
"${launcher[@]}" "${polo[@]}" \
    --session \
    --backend=drm \
    --debug-kill \
    --log="$run_dir/polo.log" \
    ${debug[@]+"${debug[@]}"} \
    ${clients[@]+"${clients[@]}"} \
    </dev/null >>"$run_dir/apps.log" 2>&1
status=$?

printf '\nPolo exited with status %d after %d seconds.\n' "$status" "$(($(date +%s) - started))"
printf 'Logs: %s\n' "$run_dir"
if [[ $status -ne 0 ]]; then
    printf 'If it crashed, "coredumpctl list polo" shows the crash and "coredumpctl info" its stack.\n'
fi
exit "$status"
