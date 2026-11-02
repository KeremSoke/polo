/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The one logging entry point for Vala code. Messages go to
 * libpolo-backend's log, which shares its context with libweston's scopes;
 * GLib's own logging (debug (), message (), warning () and other libraries'
 * domains) is routed into the "polo" scope.
 */

namespace Polo {
    /* Polo's log scopes. Names match lib/backend/log.c. */
    public enum LogDomain {
        POLO,
        POLICY,
        INPUT,
        XWM,
        SESSION,
        EFFECTS,
        PROTOCOL;

        public unowned string scope_name () {
            switch (this) {
                case POLICY: return "polo-policy";
                case INPUT: return "polo-input";
                case XWM: return "polo-xwm";
                case SESSION: return "polo-session";
                case EFFECTS: return "polo-effects";
                case PROTOCOL: return "polo-protocol";
                default: return "polo";
            }
        }

        public static LogDomain[] all () {
            return { POLO, POLICY, INPUT, XWM, SESSION, EFFECTS, PROTOCOL };
        }
    }

    namespace Logging {
        private bool active = false;
        private bool writer_installed = false;

        public void init (string? file, string? debug_scopes) throws GLib.Error {
            Backend.Log.init (file, debug_scopes);
            active = true;
            /* GLib allows exactly one writer per process. */
            if (!writer_installed) {
                GLib.Log.set_writer_func (write_glib);
                writer_installed = true;
            }
        }

        public void shutdown () {
            active = false;
            Backend.Log.shutdown ();
        }

        public unowned LogScope scope (LogDomain domain) {
            return Backend.Log.scope (domain.scope_name ());
        }

        private string field_string (GLib.LogField field) {
            unowned string text = (string) field.value;
            return field.length < 0 ? text : text.substring (0, (long) field.length);
        }

        private GLib.LogWriterOutput write_glib (GLib.LogLevelFlags level,
                                                 GLib.LogField[] fields) {
            if (!active) {
                return GLib.Log.writer_default (level, fields);
            }

            string? domain = null;
            string message = "";
            foreach (unowned GLib.LogField field in fields) {
                if (field.key == "MESSAGE") {
                    message = field_string (field);
                } else if (field.key == "GLIB_DOMAIN") {
                    domain = field_string (field);
                }
            }

            if (domain != null && domain != "Polo") {
                message = "%s: %s".printf (domain, message);
            }

            unowned LogScope polo = scope (LogDomain.POLO);
            if ((level & (GLib.LogLevelFlags.LEVEL_ERROR
                          | GLib.LogLevelFlags.LEVEL_CRITICAL
                          | GLib.LogLevelFlags.LEVEL_WARNING)) != 0) {
                polo.warning ("%s", message);
            } else if ((level & GLib.LogLevelFlags.LEVEL_DEBUG) != 0) {
                polo.debug ("%s", message);
            } else {
                polo.info ("%s", message);
            }

            return GLib.LogWriterOutput.HANDLED;
        }
    }
}
