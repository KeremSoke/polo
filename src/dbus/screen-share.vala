/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Screen sharing through xdg-desktop-portal-wlr: Polo starts it with a
 * config whose chooser is polo-share-chooser, which brings its sources
 * here (org.mate.Polo.ScreenShare1) to be picked in the Alt-Tab popup.
 */

namespace Polo.Dbus {
    public class ScreenShare : GLib.Object {
        public const string INTERFACE = "org.mate.Polo.ScreenShare1";
        private const string RESOURCE = "/org/mate/polo/dbus/org.mate.Polo.ScreenShare1.xml";
        private const string CHOOSER = "polo-share-chooser";
        private const int MAX_SOURCES = 256;
        private const int MAX_SOURCE_LINE = 1024;
        private const string[] PORTAL_PATHS = {
            "/usr/libexec/xdg-desktop-portal-wlr",
            "/usr/lib/xdg-desktop-portal-wlr",
            "/usr/lib/xdg-desktop-portal-wlr/xdg-desktop-portal-wlr",
            "/usr/local/libexec/xdg-desktop-portal-wlr",
        };

        private GLib.DBusConnection connection;
        private Shell.ShareChooser chooser;
        private Registration? exported = null;

        public ScreenShare (GLib.DBusConnection connection, Shell.ShareChooser chooser) {
            this.connection = connection;
            this.chooser = chooser;
        }

        public void export () throws GLib.Error {
            exported = new Registration (connection, Outputs.PATH, Registration.load (RESOURCE, INTERFACE),
                                         on_method);
        }

        public void unexport () {
            if (exported != null) {
                exported.unregister ();
                exported = null;
            }
        }

        private void on_method (string sender, string method, GLib.Variant parameters,
                                owned GLib.DBusMethodInvocation invocation) {
            if (method != "Choose") {
                invocation.return_gerror (new GLib.DBusError.UNKNOWN_METHOD ("no method %s", method));
                return;
            }
            string[] sources = {};
            foreach (unowned string line in parameters.get_child_value (0).get_strv ()) {
                if (sources.length >= MAX_SOURCES) {
                    break;
                }
                if (line.length <= MAX_SOURCE_LINE) {
                    sources += line;
                }
            }
            chooser.choose (sources, (line) => {
                invocation.return_value (new GLib.Variant ("(s)", line ?? ""));
            });
        }

        public static string chooser_path () {
            unowned string? own = GLib.Environment.get_variable ("POLO_SHARE_CHOOSER");
            if (own != null && own != "") {
                return own;
            }
            return GLib.Path.build_filename (Config.LIBEXECDIR, CHOOSER);
        }

        public static string? portal_path () {
            if (Config.PORTAL_WLR != "" && GLib.FileUtils.test (Config.PORTAL_WLR, GLib.FileTest.IS_EXECUTABLE)) {
                return Config.PORTAL_WLR;
            }
            foreach (unowned string path in PORTAL_PATHS) {
                if (GLib.FileUtils.test (path, GLib.FileTest.IS_EXECUTABLE)) {
                    return path;
                }
            }
            return null;
        }

        public static string portal_config (string chooser) {
            return "[screencast]\nchooser_type=dmenu\nchooser_cmd=%s\n".printf (GLib.Shell.quote (chooser));
        }

        public static void start_portal (Spawn spawn) {
            unowned LogScope log = Logging.scope (LogDomain.POLO);
            var portal = portal_path ();
            if (portal == null) {
                log.info ("xdg-desktop-portal-wlr is not installed: no screen sharing");
                return;
            }
            string dir = GLib.Path.build_filename (GLib.Environment.get_user_runtime_dir (), "polo");
            string config = GLib.Path.build_filename (dir, "xdg-desktop-portal-wlr.conf");
            try {
                GLib.DirUtils.create_with_parents (dir, 0700);
                GLib.FileUtils.set_contents (config, portal_config (chooser_path ()));
                unowned Backend.Client client;
                spawn.launch_trusted ({ portal, "--replace", "--config", config }, out client);
            } catch (GLib.Error e) {
                log.warning ("cannot start %s: %s", portal, e.message);
            }
        }
    }
}
