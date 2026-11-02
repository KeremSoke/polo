/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's delete.c: when a window does not answer the ping after a close
 * request, ask whether to wait or force the client to quit. polo-shell-ui
 * shows Marco's zenity question, transient for the window; without it
 * Polo runs zenity, as Marco does. Anything but Wait (zenity's exit
 * status 1: Force Quit, Escape, closing the dialog) kills the client with
 * SIGKILL.
 */

namespace Polo.Shell {
    public class NotResponding : GLib.Object {
        [CCode (cname = "kill", cheader_filename = "signal.h")]
        private static extern int kill (int pid, int sig);

        [CCode (cname = "getpid", cheader_filename = "unistd.h")]
        private static extern int getpid ();

        private class Dialog {
            public uint id;
            public GLib.Subprocess? process;
            public ulong unmanaged_id;
        }

        private const int SIGKILL = 9;
        private const int RESPONSE_NONE = -1;
        private const int RESPONSE_OK = -5;

        private Policy.Model model;
        private Spawn spawn;
        private Desktop desktop;
        private Backend.ShellUi shell_ui;
        private uint next_id = 1;
        private GLib.HashTable<Window, Dialog> dialogs =
            new GLib.HashTable<Window, Dialog> (GLib.direct_hash, GLib.direct_equal);

        public NotResponding (Policy.Model model, Spawn spawn, Desktop desktop, Backend.ShellUi shell_ui) {
            this.model = model;
            this.spawn = spawn;
            this.desktop = desktop;
            this.shell_ui = shell_ui;
            model.not_responding.connect (on_not_responding);
            shell_ui.dialog_response.connect (on_dialog_response);
            shell_ui.unbound.connect (on_unbound);
            desktop.set_foreign_parent (dialog_parent);
        }

        public void shutdown () {
            model.not_responding.disconnect (on_not_responding);
            shell_ui.dialog_response.disconnect (on_dialog_response);
            shell_ui.unbound.disconnect (on_unbound);
            desktop.set_foreign_parent (null);
            foreach (var window in dialogs.get_keys ()) {
                var process = forget (window).process;
                if (process != null) {
                    process.force_exit ();
                }
            }
        }

        [CCode (cname = "g_dgettext", cheader_filename = "glib.h")]
        private static extern unowned string dgettext (string domain, string msgid);

        /* Marco's own wording, in Marco's translations. */
        public static unowned string marco (string msgid) {
            return dgettext ("marco", msgid);
        }

        private static int pid_of (Window window) {
            var xdg = window.adapter as XdgWindow;
            return xdg != null ? xdg.toplevel.get_pid () : 0;
        }

        private void on_not_responding (Window window) {
            var running = dialogs.lookup (window);
            if (running != null) {
                present (window, running);
                return;
            }
            var entry = new Dialog ();
            if (shell_ui.is_bound ()) {
                entry.id = next_id++;
                var output = model.output_for_window (window);
                shell_ui.show_dialog (entry.id, Backend.DialogKind.NOT_RESPONDING,
                                      output != null ? desktop.backend_output (output) : null,
                                      window.title ?? "");
            } else {
                entry.process = run_zenity (window);
                if (entry.process == null) {
                    return;
                }
            }
            Logging.scope (LogDomain.POLICY).info ("%s is not responding", window.describe ());
            entry.unmanaged_id = window.unmanaged.connect (() => {
                var gone = forget (window);
                if (gone.process != null) {
                    gone.process.force_exit ();
                } else {
                    shell_ui.hide_dialog (gone.id);
                }
            });
            dialogs.insert (window, entry);
            if (entry.process != null) {
                wait_for.begin (window, entry.process);
            }
        }

        private GLib.Subprocess? run_zenity (Window window) {
            var title = GLib.Markup.escape_text (window.title ?? "");
            var tmp = marco ("<tt>%s</tt> is not responding.").printf (title);
            var content = "<big><b>%s</b></big>\n\n<i>%s</i>".printf (
                tmp, marco ("You may choose to wait a short while for it to " +
                            "continue or force the application to quit entirely."));
            string[] argv = {
                "zenity", "--question", "--title", marco ("Marco"), "--text", content,
                "--ok-label", marco ("_Wait"), "--cancel-label", marco ("_Force Quit")
            };
            try {
                return spawn.launch (argv);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLICY).warning ("%s is not responding and the dialog cannot start: %s",
                                                          window.describe (), e.message);
                return null;
            }
        }

        /* dialog_exited (): exit status 1 is Force Quit. */
        private async void wait_for (Window window, GLib.Subprocess dialog) {
            try {
                yield dialog.wait_async ();
            } catch (GLib.Error e) {
            }
            var entry = dialogs.lookup (window);
            if (entry == null || entry.process != dialog) {
                return;
            }
            forget (window);
            if (dialog.get_if_exited () && dialog.get_exit_status () == 1) {
                force_quit (window);
            }
        }

        private void on_dialog_response (uint id, int response, string? text) {
            var window = window_of (id);
            if (window == null) {
                return;
            }
            forget (window);
            if (response != RESPONSE_OK && response != RESPONSE_NONE) {
                force_quit (window);
            }
        }

        private void on_unbound () {
            foreach (var window in dialogs.get_keys ()) {
                if (dialogs[window].process == null) {
                    forget (window);
                }
            }
        }

        private void force_quit (Window window) {
            int pid = pid_of (window);
            Logging.scope (LogDomain.POLICY).info ("force quit %s (pid %d)", window.describe (), pid);
            if (pid > 0 && pid != getpid ()) {
                kill (pid, SIGKILL);
            }
#if POLO_HAVE_XWAYLAND
            var x_window = window.adapter as X11Window;
            if (x_window != null) {
                x_window.xwindow.kill_client ();
            }
#endif
        }

        private Window? window_of (uint id) {
            foreach (var window in dialogs.get_keys ()) {
                if (dialogs[window].id == id && dialogs[window].process == null) {
                    return window;
                }
            }
            return null;
        }

        /* zenity's WINDOWID: the dialog is transient for the hung window. */
        private Window? dialog_parent (Backend.Toplevel toplevel) {
            uint id = shell_ui.get_dialog (toplevel);
            return id != 0 ? window_of (id) : null;
        }

        /* Activate the affected window's dialog. */
        private void present (Window window, Dialog entry) {
            foreach (var w in model.get_windows ()) {
                var xdg = w.adapter as XdgWindow;
                if (xdg == null || w.transient_for != window) {
                    continue;
                }
                bool ours = entry.process != null ? xdg.toplevel.get_pid () == process_id (entry.process)
                                                  : shell_ui.get_dialog (xdg.toplevel) == entry.id;
                if (ours) {
                    model.activate (w, Policy.ActivationSource.PAGER, 0);
                    return;
                }
            }
            if (entry.process != null) {
                int pid = process_id (entry.process);
                foreach (var w in model.get_windows ()) {
                    if (pid_of (w) == pid) {
                        model.activate (w, Policy.ActivationSource.PAGER, 0);
                        return;
                    }
                }
            }
        }

        private static int process_id (GLib.Subprocess process) {
            unowned string? id = process.get_identifier ();
            return id != null ? int.parse (id) : 0;
        }

        private Dialog forget (Window window) {
            var entry = dialogs.lookup (window);
            dialogs.remove (window);
            GLib.SignalHandler.disconnect (window, entry.unmanaged_id);
            return entry;
        }
    }
}
