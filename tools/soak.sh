#!/bin/sh
# The desktop soak (tests/soak/soak.c) for hours instead of the two minutes of
# its meson test: window churn, workspace switches through the cube, Alt+Tab,
# Magic Lamp, wobbly drags and maximise on headless GL, with a logout and
# login every SESSION minutes.
#
#   tools/soak.sh [HOURS] [BUILD-DIR] [SESSION-MINUTES] [SEED]
#
# HOURS defaults to 8, BUILD-DIR to build, SESSION-MINUTES to 15 and SEED to
# the time. Progress goes to the terminal once a minute; the whole run to
# BUILD-DIR/meson-logs/testlog.txt. It fails on a crash, a protocol error,
# RSS more than 5% above its value after warming up at the end of a login,
# or a child process or socket left behind at logout.
set -eu

hours=${1:-8}
build=${2:-build}
session=${3:-15}
seed=${4:-$(date +%s)}
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"

[ -d "$build" ] || { echo "soak.sh: no build directory $build" >&2; exit 1; }
ninja -C "$build" >/dev/null
echo "soak.sh: $hours h in $build, logins of $session min, seed $seed"
SOAK_SECONDS=$(awk "BEGIN { print int($hours * 3600) }") \
SOAK_SESSION_SECONDS=$((session * 60)) \
SOAK_SEED=$seed \
    meson test -C "$build" soak --timeout-multiplier 0 --verbose
