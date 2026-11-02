/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco keybindings.c handle_rename_workspace () and its callback, with
 * polo-shell-ui's entry dialog: one at a time, for the workspace active when
 * it opened, and an empty name changes nothing.
 */

namespace Polo.Shell {
    public class RenameWorkspace : GLib.Object {
        private const int RESPONSE_OK = -5;
        /* Above the "not responding" dialogs' ids. */
        private const uint FIRST_DIALOG = 0x10000;

        private Policy.Model model;
        private Desktop desktop;
        private Backend.ShellUi shell_ui;
        private Protocols.WorkspacePreference write_preference;
        private uint next_id = FIRST_DIALOG;
        private uint dialog = 0;
        private int workspace = -1;

        public RenameWorkspace (Policy.Model model, Desktop desktop, Backend.ShellUi shell_ui,
                                owned Protocols.WorkspacePreference write_preference) {
            this.model = model;
            this.desktop = desktop;
            this.shell_ui = shell_ui;
            this.write_preference = (owned) write_preference;
            desktop.bindings.register ("rename-workspace", (binding, seat, event) => handle_rename_workspace ());
            shell_ui.dialog_response.connect (on_response);
            shell_ui.unbound.connect (on_unbound);
        }

        public void shutdown () {
            shell_ui.dialog_response.disconnect (on_response);
            shell_ui.unbound.disconnect (on_unbound);
        }

        private void handle_rename_workspace () {
            if (dialog != 0) {
                return;
            }
            if (!shell_ui.is_bound ()) {
                Logging.scope (LogDomain.POLICY).info ("no polo-shell-ui to rename the workspace");
                return;
            }
            dialog = next_id++;
            workspace = model.current_workspace;
            var output = model.output_at (model.pointer_x, model.pointer_y);
            shell_ui.show_dialog (dialog, Backend.DialogKind.RENAME_WORKSPACE,
                                  output != null ? desktop.backend_output (output) : null,
                                  model.workspace_name (workspace));
        }

        private void on_response (uint id, int response, string? text) {
            if (id != dialog) {
                return;
            }
            dialog = 0;
            if (response == RESPONSE_OK && text != null && text != "" &&
                text != model.workspace_name (workspace)) {
                write_preference ("org.mate.Marco.workspace-names", "name-%d".printf (workspace + 1),
                                  new GLib.Variant.string (text));
            }
        }

        private void on_unbound () {
            dialog = 0;
        }
    }
}
