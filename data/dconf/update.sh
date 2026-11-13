#!/bin/sh
# Compiles /etc/dconf/db/polo from polo.d; packages do this in %post.
# Only Polo's database: "dconf update" fails on any broken file in other ones.
if [ -z "$DESTDIR" ] && command -v dconf >/dev/null 2>&1; then
    dconf compile /etc/dconf/db/polo /etc/dconf/db/polo.d ||
        echo "warning: could not compile /etc/dconf/db/polo; run: sudo dconf compile /etc/dconf/db/polo /etc/dconf/db/polo.d" >&2
fi
