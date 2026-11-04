#!/bin/sh
# Compiles /etc/dconf/db/polo from polo.d; packages do this in %post.
if [ -z "$DESTDIR" ] && command -v dconf >/dev/null 2>&1; then
    dconf update
fi
