/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Which clients may see privileged globals. The C side only asks; this
 * file decides. A privileged global is denied unless the client is one Polo
 * started as trusted, descends from a helper Polo started, or runs a
 * root-owned executable on the trusted list; a sandboxed client never sees
 * one.
 */

namespace Polo {
    public class Trust : GLib.Object {
        /* Interfaces only trusted clients should see. */
        private const string[] PRIVILEGED = {
            "zwlr_layer_shell_v1",
            "zwlr_foreign_toplevel_manager_v1",
            "xfce_foreign_toplevel_manager_private_v1",
            "xfce_output_manager_private_v1",
            "ext_foreign_toplevel_list_v1",
            "ext_workspace_manager_v1",
            "ext_session_lock_manager_v1",
            "ext_idle_notifier_v1",
            "zwlr_output_manager_v1",
            "zwlr_data_control_manager_v1",
            "ext_data_control_manager_v1",
            "zwlr_gamma_control_manager_v1",
            "ext_image_copy_capture_manager_v1",
            "ext_output_image_capture_source_manager_v1",
            "ext_foreign_toplevel_image_capture_source_manager_v1",
            "zwp_input_method_manager_v2",
            "zwp_virtual_keyboard_manager_v1",
            "polo_shell_ui_v1",
            "wp_security_context_manager_v1",
            "polo_test_v1",
            "weston_capture_v1",
            "weston_debug_v1",
        };

        public const string[] DEFAULT_EXECUTABLES = {
            "/usr/libexec/xdg-desktop-portal-wlr",
            "/usr/lib/xdg-desktop-portal-wlr",
            "/usr/bin/xdg-desktop-portal-wlr"
        };

        public bool deny_by_default { get; set; default = true; }

        public string[] trusted_executables { get; set; }

        private int[] helpers = {};

        /* The only client that may see polo_shell_ui_v1. */
        public Backend.Client? shell_ui_client { get; set; default = null; }

        private GLib.HashTable<string, bool> privileged;
        private GLib.HashTable<string, bool> logged;

        construct {
            trusted_executables = DEFAULT_EXECUTABLES;
            privileged = new GLib.HashTable<string, bool> (GLib.str_hash, GLib.str_equal);
            foreach (unowned string name in PRIVILEGED) {
                privileged.insert (name, true);
            }
            logged = new GLib.HashTable<string, bool> (GLib.str_hash, GLib.str_equal);
        }

        public bool is_privileged (string interface_name) {
            return privileged.contains (interface_name);
        }

        public void add_helper (int pid) {
            helpers += pid;
        }

        public void remove_helper (int pid) {
            int[] kept = {};
            foreach (int helper in helpers) {
                if (helper != pid) {
                    kept += helper;
                }
            }
            helpers = kept;
        }

        private bool is_vouched_for (Backend.Client client) {
            int pid = client.get_pid ();
            if (Policy.TrustRules.executable_trusted (client.get_executable (), trusted_executables)) {
                return true;
            }
            return pid > 1 && Policy.TrustRules.has_ancestor (pid, helpers) &&
                   Policy.TrustRules.root_owned_file (client.get_executable ());
        }

        public bool filter (Backend.Client client, string interface_name) {
            if (interface_name == "polo_shell_ui_v1") {
                return client == shell_ui_client;
            }
            if (interface_name == "zwp_xwayland_keyboard_grab_manager_v1") {
                return client.get_xserver ();
            }
            if (client.get_sandbox_engine () != null) {
                return !is_privileged (interface_name);
            }
            if (CommandLine.test_mode && (interface_name == "polo_test_v1" || interface_name == "weston_capture_v1")) {
                return true;
            }
            if (!is_privileged (interface_name) || client.get_trusted () || client.get_pid () == Posix.getpid () ||
                is_vouched_for (client)) {
                return true;
            }
            if (interface_name == "ext_session_lock_manager_v1" && !CommandLine.test_mode) {
                var locker_key = "%d/%s".printf (client.get_pid (), interface_name);
                if (!logged.contains (locker_key)) {
                    logged.insert (locker_key, true);
                    Logging.scope (LogDomain.PROTOCOL).info (
                        "denied %s to untrusted client pid %d (%s): only trusted lockers may lock",
                        interface_name, client.get_pid (), client.get_executable () ?? "?");
                }
                return false;
            }

            var key = "%d/%s".printf (client.get_pid (), interface_name);
            if (!logged.contains (key)) {
                logged.insert (key, true);
                Logging.scope (LogDomain.PROTOCOL).info (
                    "%s privileged %s to untrusted client pid %d (%s)",
                    deny_by_default ? "denied" : "granted (default-allow)",
                    interface_name, client.get_pid (), client.get_executable () ?? "?");
            }
            return !deny_by_default;
        }
    }
}
