/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
namespace Polo.Outputs {
    public class Configuration : GLib.Object {
        public GLib.GenericArray<Polo.Output> outputs = new GLib.GenericArray<Polo.Output> ();
        public bool clone;
    }

    public class MonitorsXml : GLib.Object {
        public GLib.GenericArray<Configuration> configurations = new GLib.GenericArray<Configuration> ();
        private Configuration? configuration;
        private Polo.Output? output;
        private string field = "";
        private GLib.StringBuilder content = new GLib.StringBuilder ();
        private int depth;
        private bool reflect_x;
        private bool reflect_y;
        private int rotation;

        public MonitorsXml (string xml) throws GLib.MarkupError {
            GLib.MarkupParser parser = { start, end, text, null, null };
            var context = new GLib.MarkupParseContext (parser, 0, this, null);
            context.parse (xml, xml.length);
            context.end_parse ();
            if (depth != 0 || configurations.length == 0) {
                throw new GLib.MarkupError.INVALID_CONTENT ("No monitor configurations");
            }
        }

        public MonitorsXml.empty () {
        }

        private static void start (GLib.MarkupParseContext context, string name,
                                   string[] names, string[] values) throws GLib.MarkupError {
            var self = (MonitorsXml) context.get_user_data ();
            self.depth++;
            self.content.truncate (0);
            if (self.depth == 1) {
                if (name != "monitors") {
                    throw new GLib.MarkupError.INVALID_CONTENT ("Expected monitors");
                }
                bool version = false;
                for (int i = 0; i < names.length; i++) {
                    if (names[i] == "version") {
                        version = values[i] == "1";
                    }
                }
                if (!version) {
                    throw new GLib.MarkupError.INVALID_CONTENT ("Expected MateRR version 1");
                }
            } else if (self.depth == 2 && name == "configuration") {
                self.configuration = new Configuration ();
                self.configurations.add (self.configuration);
            } else if (self.depth == 3 && name == "output" && self.configuration != null) {
                self.output = new Polo.Output ();
                self.output.enabled = false;
                self.rotation = 0;
                self.reflect_x = self.reflect_y = false;
                for (int i = 0; i < names.length; i++) {
                    if (names[i] == "name") {
                        self.output.name = values[i];
                    }
                }
                self.configuration.outputs.add (self.output);
            } else if ((self.depth == 4 && self.output != null) ||
                       (self.depth == 3 && name == "clone")) {
                self.field = name;
            } else {
                throw new GLib.MarkupError.INVALID_CONTENT ("Unexpected element %s", name);
            }
        }

        private static int number (string value, int min, int max) throws GLib.MarkupError {
            int64 n;
            if (!int64.try_parse (value, out n) || n < min || n > max) {
                throw new GLib.MarkupError.INVALID_CONTENT ("Invalid monitor number: %s", value);
            }
            return (int) n;
        }

        private static void end (GLib.MarkupParseContext context, string name) throws GLib.MarkupError {
            var self = (MonitorsXml) context.get_user_data ();
            string value = self.content.str.strip ();
            var o = self.output;
            if (self.depth == 4 && o != null) {
                switch (self.field) {
                    case "vendor": o.vendor = value; break;
                    case "product": o.product = value.down (); break;
                    case "serial": o.serial = value.down (); break;
                    case "width": o.width = number (value, 0, 32768); break;
                    case "height": o.height = number (value, 0, 32768); break;
                    case "rate":
                        double rate;
                        if (!double.try_parse (value, out rate) || !rate.is_finite () || rate < 0 || rate > 1000) {
                            throw new GLib.MarkupError.INVALID_CONTENT ("Invalid refresh rate");
                        }
                        o.rate = (int) Math.round (rate * 1000);
                        break;
                    case "x": o.x = number (value, -32768, 32768); break;
                    case "y": o.y = number (value, -32768, 32768); break;
                    case "scale": o.scale = number (value, 1, 8); break;
                    case "primary": o.primary = value == "yes"; break;
                    case "reflect_x": self.reflect_x = value == "yes"; break;
                    case "reflect_y": self.reflect_y = value == "yes"; break;
                    case "rotation":
                        switch (value) {
                            case "normal": self.rotation = 0; break;
                            case "left": self.rotation = 1; break;
                            case "upside_down": self.rotation = 2; break;
                            case "right": self.rotation = 3; break;
                            default: throw new GLib.MarkupError.INVALID_CONTENT ("Invalid rotation");
                        }
                        break;
                    default: throw new GLib.MarkupError.INVALID_CONTENT ("Unknown output field %s", self.field);
                }
            } else if (self.depth == 3 && name == "clone") {
                self.configuration.clone = value == "yes";
            } else if (self.depth == 3 && name == "output" && o != null) {
                o.enabled = o.width > 0 && o.height > 0;
                int rotation = (self.rotation + (self.reflect_y ? 2 : 0)) % 4;
                o.transform = rotation + ((self.reflect_x != self.reflect_y) ? 4 : 0);
                self.output = null;
            } else if (self.depth == 2) {
                self.configuration = null;
            }
            self.field = "";
            self.depth--;
        }

        private static void text (GLib.MarkupParseContext context, string text, size_t length) throws GLib.MarkupError {
            var self = (MonitorsXml) context.get_user_data ();
            self.content.append_len (text, (ssize_t) length);
        }

        public string serialize () {
            var xml = new GLib.StringBuilder ("<monitors version=\"1\">\n");
            string[] rotations = { "normal", "left", "upside_down", "right" };
            foreach (var config in configurations) {
                xml.append_printf ("  <configuration>\n    <clone>%s</clone>\n", config.clone ? "yes" : "no");
                foreach (var o in config.outputs) {
                    xml.append_printf ("    <output name=\"%s\">\n", GLib.Markup.escape_text (o.name));
                    xml.append_printf ("      <vendor>%s</vendor><product>%s</product><serial>%s</serial>\n",
                        GLib.Markup.escape_text (o.vendor), GLib.Markup.escape_text (o.product),
                        GLib.Markup.escape_text (o.serial));
                    if (o.enabled) {
                        xml.append_printf ("      <width>%d</width><height>%d</height><rate>%s</rate>\n",
                            o.width, o.height, (o.rate / 1000.0).to_string ());
                        xml.append_printf ("      <x>%d</x><y>%d</y><scale>%d</scale>\n", o.x, o.y, o.scale);
                        xml.append_printf ("      <rotation>%s</rotation>", rotations[o.transform % 4]);
                        xml.append_printf ("<reflect_x>%s</reflect_x><reflect_y>no</reflect_y>\n",
                                           o.transform >= 4 ? "yes" : "no");
                        xml.append_printf ("      <primary>%s</primary>\n", o.primary ? "yes" : "no");
                    }
                    xml.append ("    </output>\n");
                }
                xml.append ("  </configuration>\n");
            }
            xml.append ("</monitors>\n");
            return xml.str;
        }

        public async void save (string path) throws GLib.Error {
            var file = GLib.File.new_for_path (path);
            var parent = file.get_parent ();
            /* Directory creation runs off the compositor main thread. */
            new GLib.Thread<void*> ("output-directory", () => {
                GLib.DirUtils.create_with_parents (parent.get_path (), 0700);
                GLib.Idle.add (() => { save.callback (); return GLib.Source.REMOVE; });
                return null;
            });
            yield;
            string xml = serialize ();
            yield file.replace_contents_async (xml.data, null, false,
                                               GLib.FileCreateFlags.PRIVATE, null, null);
        }
    }
}
