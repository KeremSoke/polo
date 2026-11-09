#!/usr/bin/env bash
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Collects everything needed to look into a Polo crash, hang or display
# problem into one archive on the Desktop (or in $HOME): system and GPU,
# the source and which Weston patches are applied, the screens as the
# kernel sees them, kernel messages of this boot and the previous one,
# backtraces of the latest Polo crashes, the latest Polo runs' logs, and
# Polo's settings. Only text; nothing is changed. Works for anyone testing
# Polo, from a source checkout or an installed Polo; README.txt in the
# archive lists what is in it, so it can be checked before sending.
#
#   tools/polo-report.sh          after a crash, or after rebooting from a freeze
#   tools/polo-report.sh --hang   while Polo is frozen (from SSH): makes Polo dump
#                                 its state and quit, then collects as above
#
# start-polo.sh runs it by itself when Polo crashes, while the build still
# matches the crash: a backtrace of an older build points at the wrong lines.
#
# Run it as your own user. It may ask for your password once (sudo) to read
# the kernel's display state; leave it empty and press Enter to skip that.

set -uo pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_ROOT="$HOME/polo-logs"
DUMPS=5
RUNS=6

if [[ $EUID -eq 0 ]]; then
    printf 'polo-report: run it as your own user, not as root\n' >&2
    exit 1
fi

stamp=$(date +%Y-%m-%d_%H-%M-%S)
dest=$(xdg-user-dir DESKTOP 2>/dev/null)
[[ -n "$dest" && -d "$dest" ]] || dest="$HOME"
out="$(mktemp -d)/polo-report-$stamp"
mkdir -p "$out/crashes" "$out/runs"

debuginfod_url() {
    local id like
    id=$(. /etc/os-release 2>/dev/null && printf '%s' "${ID:-}")
    like=$(. /etc/os-release 2>/dev/null && printf '%s' "${ID_LIKE:-}")
    case " $id $like " in
        *" ubuntu "*) printf 'https://debuginfod.ubuntu.com' ;;
        *" debian "*) printf 'https://debuginfod.debian.net' ;;
        *" fedora "*) printf 'https://debuginfod.fedoraproject.org' ;;
        *" arch "*) printf 'https://debuginfod.archlinux.org' ;;
        *" opensuse"*|*" suse "*) printf 'https://debuginfod.opensuse.org' ;;
        *) printf '' ;;
    esac
}

step() {
    printf '  %s\n' "$*"
}

run() {
    printf '$ %s\n' "$*"
    "$@" 2>&1
    printf '\n'
}

wait_for_dump() {
    for _ in $(seq "$2"); do
        coredumpctl list --no-legend --no-pager --since="@$1" polo >/dev/null 2>&1 && return 0
        sleep 1
    done
    return 1
}

case "${1:-}" in
    --hang)
        pid=$(pgrep -x polo | head -n 1)
        if [[ -z "$pid" ]]; then
            printf 'polo-report: Polo is not running\n' >&2
        else
            step "making Polo $pid dump its state (it quits)"
            hung_since=$(date +%s)
            kill -ABRT "$pid"
            wait_for_dump "$hung_since" 60
            printf 'Polo %s was frozen; aborted at %s\n' "$pid" "$(date)" >"$out/hang.txt"
        fi
        ;;
    --after)
        step "waiting for the crash to be saved"
        wait_for_dump "${2:-0}" 20
        ;;
esac

printf 'What happened? One line, or Enter to skip: '
notes=""
{ read -r notes </dev/tty; } 2>/dev/null || true
printf '%s\n' "$notes" >"$out/notes.txt"

step "system"
{
    run date
    run uname -a
    run cat /etc/os-release
    run cat /proc/cmdline
    run lspci -nnk -d ::0300
    run lspci -nnk -d ::0302
    run lspci -nnk -d ::0380
    if command -v dpkg-query >/dev/null 2>&1; then
        run dpkg-query -W -f='${Package} ${Version}\n' \
            libgl1-mesa-dri mesa-vulkan-drivers libgbm1 libdrm2 libwayland-server0 \
            libinput10 libglib2.0-0t64 libgtk-3-0t64 xwayland mate-panel atril gdb
    elif command -v rpm >/dev/null 2>&1; then
        run rpm -q mesa-dri-drivers mesa-libgbm libdrm libwayland-server libinput \
            glib2 gtk3 xorg-x11-server-Xwayland mate-panel atril gdb
    elif command -v pacman >/dev/null 2>&1; then
        run pacman -Q mesa libdrm wayland libinput glib2 gtk3 xorg-xwayland mate-panel atril gdb
    fi
    run loginctl session-status
    run free -h
} >"$out/system.txt"

step "source and Weston patches"
{
    run git -C "$SOURCE_DIR" log --oneline -8
    run git -C "$SOURCE_DIR" status --short
    printf 'Weston patches in subprojects/weston:\n'
    if [[ -d "$SOURCE_DIR/subprojects/weston" ]]; then
        while read -r patch; do
            file="$SOURCE_DIR/subprojects/packagefiles/$patch"
            if (cd "$SOURCE_DIR/subprojects/weston" &&
                patch -p1 -R --dry-run -s -f <"$file" >/dev/null 2>&1); then
                printf '  applied      %s\n' "$patch"
            elif (cd "$SOURCE_DIR/subprojects/weston" &&
                  patch -p1 --dry-run -s -f <"$file" >/dev/null 2>&1); then
                printf '  NOT APPLIED  %s\n' "$patch"
            else
                printf '  unclear      %s\n' "$patch"
            fi
        done < <(sed -n 's|^ *\(weston/[^,]*\.patch\),\{0,1\}$|\1|p' "$SOURCE_DIR/subprojects/weston.wrap")
    else
        printf '  no subprojects/weston\n'
    fi
    printf '\n'
    for build in "$SOURCE_DIR"/build*; do
        [[ -f "$build/meson-info/intro-buildoptions.json" ]] || continue
        printf '%s: ' "$build"
        python3 -I -c 'import json,sys
o={x["name"]:x["value"] for x in json.load(open(sys.argv[1]))}
print(" ".join("%s=%s" % (k,o.get(k)) for k in ("buildtype","debug","b_ndebug","b_sanitize")))' \
            "$build/meson-info/intro-buildoptions.json" 2>&1
    done
} >"$out/source.txt"

step "screens"
{
    for connector in /sys/class/drm/card*-*; do
        [[ -e "$connector/status" ]] || continue
        printf '%s: status=%s enabled=%s dpms=%s edid=%s bytes\n' "${connector##*/}" \
            "$(cat "$connector/status" 2>/dev/null)" "$(cat "$connector/enabled" 2>/dev/null)" \
            "$(cat "$connector/dpms" 2>/dev/null)" "$(wc -c <"$connector/edid" 2>/dev/null)"
        sed 's/^/    /' "$connector/modes" 2>/dev/null | head -n 12
    done
    printf '\n'
    if [[ -t 0 ]] && printf '  The kernel display state needs sudo; leave the password empty to skip.\n' >/dev/tty &&
       sudo -v 2>/dev/null; then
        for state in /sys/kernel/debug/dri/*/state; do
            [[ -e "$state" ]] || continue
            printf '== %s\n' "$state"
            sudo cat "$state" 2>&1
        done
    else
        printf '(kernel display state skipped: no sudo)\n'
    fi
} >"$out/screens.txt"
for xml in "$HOME/.config/mate/polo/monitors.xml" "$HOME/.config/monitors.xml"; do
    [[ -f "$xml" ]] && cp "$xml" "$out/$(basename "$(dirname "$xml")")-monitors.xml"
done

step "kernel messages (this boot and the previous one)"
pattern='drm|amdgpu|i915|xe |nouveau|radeon|gpu|flip|vblank|hang|timeout|reset|ring|fence|segfault|polo|atril|oom|BUG|WARNING|Call Trace'
{
    printf '== this boot, matching lines\n'
    journalctl -k -b 0 --no-pager -o short-monotonic 2>&1 | grep -iE "$pattern" | tail -n 400
    printf '\n== previous boot, last 300 lines\n'
    journalctl -k -b -1 --no-pager -o short-monotonic 2>&1 | tail -n 300
} >"$out/kernel.txt"
{
    printf '== previous boot, lines about the session\n'
    journalctl -b -1 --no-pager 2>&1 |
        grep -iE "$pattern|systemd-coredump|gdm|lightdm|logind|Xwayland|mate|watchdog" | tail -n 300
} >"$out/journal-previous-boot.txt"

step "crashes"
coredumpctl list --no-pager --since=-7d 2>&1 >"$out/crashes/list.txt"
gdb_ok=0
command -v gdb >/dev/null 2>&1 && gdb_ok=1
mapfile -t polo_pids < <(coredumpctl list --no-legend --no-pager --since=-7d polo 2>/dev/null |
                         awk '{for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+$/ && $(i+1) ~ /^[0-9]+$/) { print $i; break }}' |
                         tail -n "$DUMPS")
for pid in "${polo_pids[@]}"; do
    coredumpctl info --no-pager "$pid" >"$out/crashes/polo-$pid-info.txt" 2>&1
    if [[ $gdb_ok -eq 1 ]]; then
        exe=$(sed -n 's/^ *Executable: //p' "$out/crashes/polo-$pid-info.txt" | head -n 1)
        core=$(mktemp)
        if coredumpctl dump --no-pager "$pid" -o "$core" >/dev/null 2>&1 && [[ -n "$exe" && -x "$exe" ]]; then
            DEBUGINFOD_URLS="${DEBUGINFOD_URLS:-$(debuginfod_url)}" \
            timeout 300 gdb -q -batch -nx \
                -iex 'set debuginfod enabled on' -iex 'set pagination off' -ex 'set print frame-arguments all' \
                -ex 'echo \n== crashing thread\n' -ex 'bt full' \
                -ex 'echo \n== all threads\n' -ex 'thread apply all bt' \
                -ex 'echo \n== libraries\n' -ex 'info sharedlibrary' \
                "$exe" "$core" >"$out/crashes/polo-$pid-gdb.txt" 2>&1
            if grep -qE "($exe|libweston-[0-9]+\.so[.0-9]*|drm-backend\.so)( \(deleted\))? doesn't match build-id|Can't open file $exe" \
                "$out/crashes/polo-$pid-gdb.txt"; then
                sed -i '1i POLO WAS REBUILT AFTER THIS CRASH: the backtrace below may point at the wrong lines.\n' \
                    "$out/crashes/polo-$pid-gdb.txt"
            fi
        else
            printf 'cannot dump the core or find %s\n' "$exe" >"$out/crashes/polo-$pid-gdb.txt"
        fi
        rm -f "$core"
    fi
done
[[ $gdb_ok -eq 1 ]] || printf 'gdb is not installed: no source lines (sudo apt install gdb)\n' >"$out/crashes/NO-GDB.txt"
mapfile -t other_pids < <(coredumpctl list --no-legend --no-pager --since=-7d 2>/dev/null | grep -v ' polo *$' |
                          awk '{for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+$/ && $(i+1) ~ /^[0-9]+$/) { print $i; break }}' |
                          tail -n 6)
for pid in "${other_pids[@]}"; do
    coredumpctl info --no-pager "$pid" >"$out/crashes/other-$pid-info.txt" 2>&1
done

step "Polo's latest runs"
if [[ -d "$LOG_ROOT" ]]; then
    ls -1d "$LOG_ROOT"/????-??-??_??-??-?? 2>/dev/null | tail -n "$RUNS" | while read -r dir; do
        cp -r "$dir" "$out/runs/"
    done
fi

step "settings"
{
    for schema in $(gsettings list-schemas 2>/dev/null | grep -E '^org\.mate\.polo' | sort); do
        gsettings list-recursively "$schema" 2>&1
    done
} >"$out/settings.txt"

cat >"$out/README.txt" <<'END'
A Polo problem report. Everything here is plain text; look through it before
sending if you like. It holds:

  notes.txt                  what you typed about the problem
  hang.txt                   only when collected with --hang
  system.txt                 kernel, distribution, graphics card, package versions,
                             memory, your login session (user name, seat, tty)
  source.txt                 Polo's version, local changes, which Weston patches are
                             applied, build options
  screens.txt                connected screens, their modes and the kernel's display
                             state; monitors.xml is the saved screen layout (it names
                             the monitors, with serial numbers)
  kernel.txt                 graphics and crash lines from the kernel log, and the
                             end of the previous boot's kernel log
  journal-previous-boot.txt  session-related lines of the previous boot's log
  crashes/                   the list of crashed programs this week; for Polo, where
                             it crashed (gdb backtraces); for others, coredumpctl info
  runs/                      Polo's own logs of its latest runs (polo.log) and what
                             the session's programs printed (apps.log); these name
                             the windows and programs you had open
  settings.txt               Polo's settings
END

archive="$dest/polo-report-$stamp.tar.gz"
tar -czf "$archive" -C "$(dirname "$out")" "$(basename "$out")"
rm -rf "$(dirname "$out")"
printf '\nSaved %s (%s)\n' "$archive" "$(du -h "$archive" | cut -f1)"
