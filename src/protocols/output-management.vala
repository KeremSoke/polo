/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Protocols {
    public class OutputManagement : GLib.Object {
        private Backend.Server server;

        public OutputManagement (Backend.Server server, Shell.Desktop desktop) {
            this.server = server;
            server.set_output_config_handler ((requests, test) => desktop.apply_requests (requests, test));
        }

        public void shutdown () {
            server.set_output_config_handler (null);
        }
    }
}
