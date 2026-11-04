/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Dbus {
    [Flags]
    public enum InhibitorFlags {
        LOGOUT = 1,
        SWITCH_USER = 2,
        SUSPEND = 4,
        IDLE = 8,
        AUTOMOUNT = 16,
    }

    public class Inhibitor : GLib.Object {
        private static uint serial = 1;

        public string path { get; private set; }
        public string app_id { get; construct; }
        public uint toplevel_xid { get; construct; }
        public string reason { get; construct; }
        public uint flags { get; construct; }
        public string bus_name { get; construct; }
        public uint cookie { get; construct; }

        public Inhibitor (string app_id, uint toplevel_xid, string reason, uint flags, string bus_name,
                          uint cookie) {
            Object (app_id: app_id, toplevel_xid: toplevel_xid, reason: reason, flags: flags,
                    bus_name: bus_name, cookie: cookie);
            path = "%s/Inhibitor%u".printf (Manager.PATH, serial++);
        }
    }
}
