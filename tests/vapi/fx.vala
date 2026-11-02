/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * VAPI coverage test for polo-fx.vapi: uses every symbol.
 */

int main () {
    assert (Polo.Fx.source () == "picom wobbly branch");
    return 0;
}
