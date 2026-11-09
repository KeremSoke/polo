/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Starting child processes as Polo's clients. Trusted children
 * get a pre-connected socket (WAYLAND_SOCKET), which is how Polo knows which
 * client they are; everyone else connects through WAYLAND_DISPLAY.
 */

namespace Polo {
    [Flags]
    public enum SpawnFlags {
        NONE = 0,
        TRUSTED,
        X11
    }

    public class Spawn : GLib.Object {
        /* The fd number children receive their trusted socket on. */
        private const int CHILD_SOCKET_FD = 3;

        private Backend.Server server;
        private Policy.ActivationTokens? activation_tokens;
        private GLib.GenericSet<GLib.Subprocess> children =
            new GLib.GenericSet<GLib.Subprocess> (GLib.direct_hash, GLib.direct_equal);
        private GLib.MainLoop? ending = null;

        public Trust? trust { get; set; default = null; }

        public Spawn (Backend.Server server, Policy.ActivationTokens? activation_tokens = null) {
            this.server = server;
            this.activation_tokens = activation_tokens;
        }

        public GLib.Subprocess launch (string[] argv, SpawnFlags flags = SpawnFlags.NONE, string? cwd = null,
                                       string[]? env = null)
                throws GLib.Error {
            unowned Backend.Client? client;
            var process = start (argv, SpawnFlags.TRUSTED in flags, out client, cwd, SpawnFlags.X11 in flags, env);
            if (trust != null && argv.length > 0 && GLib.Path.get_basename (argv[0]) == Policy.Screenshot.WRAPPER) {
                vouch_for_helper.begin (process);
            }
            return process;
        }

        private async void vouch_for_helper (GLib.Subprocess process) {
            int pid = int.parse (process.get_identifier () ?? "-1");
            trust.add_helper (pid);
            try {
                yield process.wait_async ();
            } catch (GLib.Error e) {
            }
            trust.remove_helper (pid);
        }

        /* client: the trusted socket's server end, until its destroyed signal. */
        public GLib.Subprocess launch_trusted (string[] argv, out unowned Backend.Client client)
                throws GLib.Error {
            unowned Backend.Client? trusted;
            var process = start (argv, true, out trusted);
            client = trusted;
            return process;
        }

        private GLib.Subprocess start (string[] argv, bool trusted, out unowned Backend.Client? client,
                                       string? cwd = null, bool x11 = false, string[]? env = null)
                throws GLib.Error {
            var launcher = new GLib.SubprocessLauncher (GLib.SubprocessFlags.NONE);
            foreach (unowned string assignment in env ?? new string[0]) {
                var parts = assignment.split ("=", 2);
                if (parts.length == 2) {
                    launcher.setenv (parts[0], parts[1], true);
                }
            }
            if (x11) {
                launcher.setenv ("GDK_BACKEND", "x11", true);
            }
            if (cwd != null) {
                launcher.set_cwd (cwd);
            }
            launcher.setenv ("WAYLAND_DISPLAY", server.get_socket_name (), true);
            launcher.unsetenv ("WAYLAND_SOCKET");
            if (activation_tokens != null) {
                var token = activation_tokens.mint_for_launch (server.get_time_msec ());
                launcher.setenv ("XDG_ACTIVATION_TOKEN", token, true);
                launcher.setenv ("DESKTOP_STARTUP_ID", token, true);
            }

            client = null;
            if (trusted) {
                int fd = server.create_client_socket (out client);
                client.set_trusted (true);
                launcher.take_fd (fd, CHILD_SOCKET_FD);
                launcher.setenv ("WAYLAND_SOCKET", CHILD_SOCKET_FD.to_string (), true);
            }

            var process = launcher.spawnv (argv);
            unowned string? pid = process.get_identifier ();
            if (client != null && pid != null) {
                client.set_process (int.parse (pid));
            }
            Logging.scope (LogDomain.POLO).info ("started %s (pid %s%s)", argv[0], pid ?? "?",
                                                 client != null ? ", trusted" : "");
            children.add (process);
            watch.begin (process, argv[0]);
            return process;
        }

        /* SIGTERM, then SIGKILL for whatever is left when the time is up or loop is quit. */
        public void end_children (GLib.MainLoop loop, uint seconds) {
            if (children.length == 0) {
                return;
            }
            children.foreach ((child) => child.send_signal (GLib.ProcessSignal.TERM));
            ending = loop;
            bool fired = false;
            var timer = GLib.Timeout.add_seconds (seconds, () => {
                fired = true;
                loop.quit ();
                return GLib.Source.REMOVE;
            });
            loop.run ();
            ending = null;
            if (!fired) {
                GLib.Source.remove (timer);
            }
            children.foreach ((child) => child.force_exit ());
        }

        public GLib.Subprocess launch_command (string command_line,
                                               SpawnFlags flags = SpawnFlags.NONE)
                throws GLib.Error {
            string[] argv;
            GLib.Shell.parse_argv (command_line, out argv);
            return launch (argv, flags);
        }

        /* GSubprocess reaps the child through a GLib child watch. */
        private async void watch (GLib.Subprocess process, string name) {
            try {
                yield process.wait_async ();
            } catch (GLib.Error e) {
                return;
            }

            children.remove (process);
            if (ending != null && children.length == 0) {
                ending.quit ();
            }

            unowned LogScope log = Logging.scope (LogDomain.POLO);
            if (process.get_if_exited ()) {
                log.info ("%s exited with status %d", name, process.get_exit_status ());
            } else if (process.get_if_signaled ()) {
                log.info ("%s killed by signal %d", name, process.get_term_sig ());
            }
        }
    }
}
