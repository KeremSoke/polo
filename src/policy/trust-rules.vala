/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What makes a client that Polo did not start trustworthy: its executable is
 * on a list and is a root-owned file nobody else can write, or it descends
 * from a helper process Polo started itself.
 */

namespace Polo.Policy.TrustRules {
    private const uint32 GROUP_OR_OTHER_WRITABLE = 0x12;
    private const int MAX_DEPTH = 32;

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

    private static int parent_of (int pid) {
        string text;
        try {
            GLib.FileUtils.get_contents ("/proc/%d/stat".printf (pid), out text);
        } catch (GLib.FileError e) {
            return 0;
        }
        int close = text.last_index_of_char (')');
        if (close < 0 || close + 2 >= text.length) {
            return 0;
        }
        var fields = text.substring (close + 2).split (" ");
        return fields.length > 1 ? int.parse (fields[1]) : 0;
    }

    public static bool has_ancestor (int pid, int[] ancestors) {
        if (ancestors.length == 0) {
            return false;
        }
        for (int depth = 0; depth < MAX_DEPTH && pid > 1; depth++) {
            pid = parent_of (pid);
            if (pid <= 1) {
                return false;
            }
            if (pid in ancestors) {
                return true;
            }
        }
        return false;
    }
}
