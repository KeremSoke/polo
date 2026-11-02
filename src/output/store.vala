/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
namespace Polo.Outputs {
    public class Store : GLib.Object {
        public MonitorsXml? saved;
        public MonitorsXml? imported;
        public string path;
        private bool saving;
        private bool dirty;

        public Store () {
            path = GLib.Path.build_filename (GLib.Environment.get_user_config_dir (),
                                            "mate", "polo", "monitors.xml");
        }

        private async MonitorsXml? read (string filename) {
            try {
                uint8[] contents;
                yield GLib.File.new_for_path (filename).load_contents_async (null, out contents, null);
                return new MonitorsXml ((string) contents);
            } catch (GLib.Error e) {
                if (!(e is GLib.IOError.NOT_FOUND)) {
                    GLib.warning ("Cannot read monitor configuration %s: %s", filename, e.message);
                }
                return null;
            }
        }

        public async void load () {
            saved = yield read (path);
            if (saved == null) {
                imported = yield read (GLib.Path.build_filename (
                    GLib.Environment.get_user_config_dir (), "monitors.xml"));
            }
        }

        public void remember (GLib.GenericArray<Polo.Output> outputs) {
            if (outputs.length == 0) {
                return;
            }
            foreach (var output in outputs) {
                if (output.vendor == "" || output.product == "") {
                    return; // Nested outputs have no durable EDID identity.
                }
            }
            if (saved == null) {
                saved = imported ?? new MonitorsXml.empty ();
            }
            var config = new Configuration ();
            foreach (var output in outputs) {
                var copy = new Polo.Output ();
                copy.name = output.name;
                copy.vendor = output.vendor;
                copy.product = output.product;
                copy.serial = output.serial;
                copy.copy_configuration (output);
                config.outputs.add (copy);
            }
            /* Most recent configuration wins; retain other monitor sets. */
            for (uint i = 0; i < saved.configurations.length;) {
                var old = saved.configurations[i];
                bool same = old.outputs.length == outputs.length;
                var used = new GLib.GenericArray<Polo.Output> ();
                foreach (var output in outputs) {
                    bool found = false;
                    foreach (var candidate in old.outputs) {
                        uint index;
                        if (output.same_monitor (candidate) && !used.find (candidate, out index)) {
                            used.add (candidate);
                            found = true;
                            break;
                        }
                    }
                    same &= found;
                }
                if (same) {
                    saved.configurations.remove_index (i);
                } else {
                    i++;
                }
            }
            saved.configurations.add (config);
            dirty = true;
            if (!saving) {
                flush.begin ();
            }
        }

        private async void flush () {
            saving = true;
            while (dirty) {
                dirty = false;
                try {
                    yield saved.save (path);
                } catch (GLib.Error e) {
                    GLib.warning ("Cannot save monitor configuration: %s", e.message);
                }
            }
            saving = false;
        }
    }
}
