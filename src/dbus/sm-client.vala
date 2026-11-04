/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Dbus {
    public enum ClientStatus {
        UNREGISTERED = 0,
        REGISTERED = 1,
        FINISHED = 2,
        FAILED = 3,
    }

    public class Client : GLib.Object {
        private static uint serial = 1;

        public string path { get; private set; }
        public string startup_id { get; construct; }
        public string bus_name { get; construct; }
        public string app_id { get; set; default = ""; }
        public ClientStatus status { get; set; default = ClientStatus.UNREGISTERED; }

        public Client (string startup_id, string bus_name) {
            Object (startup_id: startup_id, bus_name: bus_name);
            path = "%s/Client%u".printf (Manager.PATH, serial++);
        }
    }
}
