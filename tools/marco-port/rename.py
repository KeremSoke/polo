#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply mechanical Marco-to-Polo renames for behavior-preserving C ports.

Ported files keep Marco's structure; only names change. Running this tool
on the Marco original and diffing the result against Polo's file shows
exactly what was edited by hand:

    rename.py marco/src/core/boxes.c | diff -u - lib/marco/boxes.c

Review every affected port when changing a mapping.
"""

import re
import sys

# More-specific patterns must come first.
RENAMES = [
    # Struct tags
    (r"\b_MetaRectangle\b", "_PoloRect"),
    (r"\b_MetaStrut\b", "_PoloStrut"),
    (r"\b_MetaEdge\b", "_PoloEdge"),
    # Types
    (r"\bMetaRectangle\b", "PoloRect"),
    (r"\bMetaStrut\b", "PoloStrut"),
    (r"\bMetaEdgeType\b", "PoloEdgeType"),
    (r"\bMetaEdge\b", "PoloEdge"),
    (r"\bMetaSide\b", "PoloSide"),
    (r"\bMetaDirection\b", "PoloDirection"),
    (r"\bFixedDirections\b", "PoloFixedDirections"),
    # Enum values
    (r"\bMETA_EDGE_", "POLO_EDGE_"),
    (r"\bMETA_SIDE_", "POLO_SIDE_"),
    (r"\bMETA_DIRECTION_", "POLO_DIRECTION_"),
    (r"\bFIXED_DIRECTION_", "POLO_FIXED_DIRECTION_"),
    # X11 gravity constants
    (r"\bForgetGravity\b", "POLO_GRAVITY_FORGET"),
    (r"\bNorthWestGravity\b", "POLO_GRAVITY_NORTH_WEST"),
    (r"\bNorthEastGravity\b", "POLO_GRAVITY_NORTH_EAST"),
    (r"\bSouthWestGravity\b", "POLO_GRAVITY_SOUTH_WEST"),
    (r"\bSouthEastGravity\b", "POLO_GRAVITY_SOUTH_EAST"),
    (r"\bNorthGravity\b", "POLO_GRAVITY_NORTH"),
    (r"\bSouthGravity\b", "POLO_GRAVITY_SOUTH"),
    (r"\bWestGravity\b", "POLO_GRAVITY_WEST"),
    (r"\bEastGravity\b", "POLO_GRAVITY_EAST"),
    (r"\bCenterGravity\b", "POLO_GRAVITY_CENTER"),
    (r"\bStaticGravity\b", "POLO_GRAVITY_STATIC"),
    # Macros
    (r"\bBOX_(LEFT|RIGHT|TOP|BOTTOM)\b", r"POLO_BOX_\1"),
    (r"\bRECT_LENGTH\b", "POLO_RECT_LENGTH"),
    (r"\bEDGE_LENGTH\b", "POLO_EDGE_LENGTH"),
    # Functions
    (r"\bmeta_rectangle_", "polo_marco_rect_"),
    (r"\bmeta_rect\b", "polo_marco_rect"),
    (r"\bmeta_warning\b", "g_warning"),
]

INCLUDE_RENAMES = [
    (r'#include "boxes.h"\n', '#include "polo-marco.h"\n'),
    (r'#include "util.h"\n', ""),
    (r"#include <X11/Xutil.h>[^\n]*\n", ""),
    (r'#include "common.h"\n', "/* Marco's common.h enums are in polo-marco.h. */\n"),
    # Header guards
    (r"\bMETA_(\w+)_H\b", r"POLO_MARCO_\1_H"),
]


def rename(text):
    for pattern, repl in INCLUDE_RENAMES + RENAMES:
        text = re.sub(pattern, repl, text)
    return text


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    with open(sys.argv[1], encoding="utf-8") as f:
        sys.stdout.write(rename(f.read()))


if __name__ == "__main__":
    main()
