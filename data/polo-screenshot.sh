#!/bin/sh
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
#
# polo-screenshot [--geometry='X,Y WxH' | --area]: the screenshot keys in a
# Polo session. Saves a PNG to the Pictures folder, as mate-screenshot does,
# and puts it on the clipboard.

set -u

usage() {
	echo "usage: polo-screenshot [--geometry='X,Y WxH' | --area]"
}

fail() {
	echo "polo-screenshot: $1" >&2
	if command -v notify-send >/dev/null 2>&1; then
		notify-send -i applets-screenshooter "Screenshot failed" "$1"
	fi
	exit 1
}

geometry=
area=
for arg; do
	case $arg in
	--geometry=*) geometry=${arg#--geometry=} ;;
	--area) area=1 ;;
	--window) ;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		usage >&2
		exit 2
		;;
	esac
done

command -v grim >/dev/null 2>&1 || fail "grim is not installed"
if [ -n "$area" ]; then
	command -v slurp >/dev/null 2>&1 || fail "slurp is not installed"
	geometry=$(slurp) || exit 0
fi

dir=$(xdg-user-dir PICTURES 2>/dev/null)
if [ -z "$dir" ] || [ "$dir" = "$HOME" ]; then
	dir=$HOME/Pictures
fi
mkdir -p "$dir" || fail "cannot create $dir"

stamp=$(date '+%Y-%m-%d %H-%M-%S')
file="$dir/Screenshot at $stamp.png"
n=1
while [ -e "$file" ]; do
	n=$((n + 1))
	file="$dir/Screenshot at $stamp-$n.png"
done

if [ -n "$geometry" ]; then
	grim -g "$geometry" "$file" || fail "grim could not capture the screen"
else
	grim "$file" || fail "grim could not capture the screen"
fi

if command -v wl-copy >/dev/null 2>&1; then
	wl-copy --type image/png <"$file"
fi
echo "$file"
