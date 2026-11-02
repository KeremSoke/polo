/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bindings for libpolo-fx (lib/fx/polo-fx.h).
 */

[CCode (cheader_filename = "polo-fx.h")]
namespace Polo.Fx {
    [CCode (cname = "polo_fx_source")]
    public unowned string source ();
}
