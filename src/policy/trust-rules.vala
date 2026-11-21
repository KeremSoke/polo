/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What makes a client that Polo did not start trustworthy: its executable is
 * on a list and is a root-owned file nobody else can write.
 */

namespace Polo.Policy.TrustRules {
    private const uint32 GROUP_OR_OTHER_WRITABLE = 0x12;

    public static bool root_owned_file (string? path) {
        if (path == null || !GLib.Path.is_absolute (path)) {
            return false;
        }
        try {
            var info = GLib.File.new_for_path (path).query_info (
                "standard::type,unix::uid,unix::mode", GLib.FileQueryInfoFlags.NONE);
            return info.get_file_type () == GLib.FileType.REGULAR &&
                   info.get_attribute_uint32 ("unix::uid") == 0 &&
                   (info.get_attribute_uint32 ("unix::mode") & GROUP_OR_OTHER_WRITABLE) == 0;
        } catch (GLib.Error e) {
            return false;
        }
    }

    public static bool executable_trusted (string? path, string[] allowed) {
        return path != null && path in allowed && root_owned_file (path);
    }
}
