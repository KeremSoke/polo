#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a screen reader hears, for test-shell-ui-a11y: every AT-SPI
object:announcement, one "announce TEXT" line in LOG, after a "ready" line.

  a11y-listen.py LOG
"""

import sys

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi  # noqa: E402

log = open(sys.argv[1], "a", buffering=1)


def on_event(event):
    if isinstance(event.any_data, str):
        log.write("announce %s\n" % event.any_data)


Atspi.init()
listener = Atspi.EventListener.new(on_event)
listener.register("object:announcement")
log.write("ready\n")
Atspi.event_main()
