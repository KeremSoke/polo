/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * VAPI coverage test for polo-bg.vapi: uses every symbol.
 */

int main () {
    var bg = new Polo.Bg.Background ();
    bg.set_color (Polo.Bg.ColorType.SOLID, "#ff0000", "#0000ff");
    bg.set_placement (Polo.Bg.Placement.SPANNED);
    bg.set_filename (null);
    var data = new uint8[4 * 4 * 2];
    bg.draw_output (data, 4, 2, 16, 1, 0, 0, 4, 2);
    /* Opaque red, ARGB32 in native byte order. */
    assert (((uint32[]) data)[0] == 0xffff0000);
    assert (bg.get_timeout () == uint.MAX);
    foreach (var type in new Polo.Bg.ColorType[] { Polo.Bg.ColorType.H_GRADIENT,
                                                   Polo.Bg.ColorType.V_GRADIENT }) {
        bg.set_color (type, "#000000", "#ffffff");
        bg.draw_output (data, 4, 2, 16, 1, 0, 0, 4, 2);
    }
    foreach (var placement in new Polo.Bg.Placement[] {
        Polo.Bg.Placement.TILED, Polo.Bg.Placement.ZOOMED, Polo.Bg.Placement.CENTERED,
        Polo.Bg.Placement.SCALED, Polo.Bg.Placement.FILL_SCREEN }) {
        bg.set_placement (placement);
    }
    return 0;
}
