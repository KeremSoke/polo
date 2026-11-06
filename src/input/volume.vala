/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The default sink and source, as libmatemixer's PulseAudio backend gives
 * them to mate-settings-daemon's media keys: a stream control whose volume
 * is the loudest channel, scaled as a whole so the balance stays, between
 * PA_VOLUME_MUTED and PA_VOLUME_NORM, or PA_VOLUME_UI_MAX when
 * overamplifiable. The state is cached from subscription events, so a key
 * press acts at once; PipeWire answers through pipewire-pulse.
 */

namespace Polo.Input {
    public class Volume : GLib.Object {
        public class Control {
            public string name;
            public PulseAudio.CVolume volume;
            public bool muted;

            public uint32 level {
                get { return (uint32) volume.max (); }
            }
        }

        public static uint32 normal_volume () {
            return (uint32) PulseAudio.Volume.NORM;
        }

        public static uint32 max_volume () {
            return (uint32) PulseAudio.Volume.sw_from_dB (11.0);
        }

        private PulseAudio.GLibMainLoop loop;
        private PulseAudio.Context? context = null;
        private uint reconnect_id = 0;
        private string? sink_name = null;
        private string? source_name = null;
        public Control? sink { get; private set; default = null; }
        public Control? source { get; private set; default = null; }

        /* In test mode only */
        public Volume () {
            loop = new PulseAudio.GLibMainLoop (GLib.MainContext.default ());
            if (!CommandLine.test_mode || GLib.Environment.get_variable ("POLO_TEST_PULSE_SERVER") != null) {
                connect_context ();
            }
        }

        public void shutdown () {
            if (reconnect_id != 0) {
                GLib.Source.remove (reconnect_id);
                reconnect_id = 0;
            }
            if (context != null) {
                context.set_state_callback (null);
                context.set_subscribe_callback (null);
                context.disconnect ();
                context = null;
            }
        }

        private void connect_context () {
            var props = new PulseAudio.Proplist ();
            props.sets (PulseAudio.Proplist.PROP_APPLICATION_NAME, "Polo");
            props.sets (PulseAudio.Proplist.PROP_APPLICATION_ID, "org.mate.polo");
            context = new PulseAudio.Context (loop.get_api (), "Polo", props);
            context.set_state_callback (on_state);
            string? server = CommandLine.test_mode ? GLib.Environment.get_variable ("POLO_TEST_PULSE_SERVER") : null;
            if (context.connect (server, PulseAudio.Context.Flags.NOFAIL, null) < 0) {
                Logging.scope (LogDomain.INPUT).info ("no PulseAudio server: %s",
                                                      PulseAudio.strerror (context.errno ()));
            }
        }

        private void on_state (PulseAudio.Context c) {
            switch (c.get_state ()) {
                case PulseAudio.Context.State.READY:
                    c.set_subscribe_callback (on_event);
                    c.subscribe (PulseAudio.Context.SubscriptionMask.SERVER |
                                 PulseAudio.Context.SubscriptionMask.SINK |
                                 PulseAudio.Context.SubscriptionMask.SOURCE);
                    c.get_server_info (on_server_info);
                    break;
                case PulseAudio.Context.State.FAILED:
                case PulseAudio.Context.State.TERMINATED:
                    sink = null;
                    source = null;
                    if (reconnect_id == 0 && context == c) {
                        reconnect_id = GLib.Timeout.add_seconds (5, () => {
                            reconnect_id = 0;
                            connect_context ();
                            return GLib.Source.REMOVE;
                        });
                    }
                    break;
                default:
                    break;
            }
        }

        private void on_event (PulseAudio.Context c, PulseAudio.Context.SubscriptionEventType type, uint32 index) {
            var facility = type & PulseAudio.Context.SubscriptionEventType.FACILITY_MASK;
            if (facility == PulseAudio.Context.SubscriptionEventType.SERVER) {
                c.get_server_info (on_server_info);
            } else if (facility == PulseAudio.Context.SubscriptionEventType.SINK && sink_name != null) {
                c.get_sink_info_by_name (sink_name, on_sink_info);
            } else if (facility == PulseAudio.Context.SubscriptionEventType.SOURCE && source_name != null) {
                c.get_source_info_by_name (source_name, on_source_info);
            }
        }

        private void on_server_info (PulseAudio.Context c, PulseAudio.ServerInfo? info) {
            if (info == null) {
                return;
            }
            sink_name = info.default_sink_name;
            source_name = info.default_source_name;
            if (sink_name != null) {
                c.get_sink_info_by_name (sink_name, on_sink_info);
            } else {
                sink = null;
            }
            if (source_name != null) {
                c.get_source_info_by_name (source_name, on_source_info);
            } else {
                source = null;
            }
        }

        private void on_sink_info (PulseAudio.Context c, PulseAudio.SinkInfo? info, int eol) {
            if (info == null || info.name != sink_name) {
                return;
            }
            sink = new Control () { name = info.name, volume = info.volume, muted = info.mute != 0 };
        }

        private void on_source_info (PulseAudio.Context c, PulseAudio.SourceInfo? info, int eol) {
            if (info == null || info.name != source_name) {
                return;
            }
            source = new Control () { name = info.name, volume = info.volume, muted = info.mute != 0 };
        }

        public bool change_sink (uint32 level, bool muted) {
            if (context == null || sink == null) {
                return false;
            }
            if (muted != sink.muted) {
                context.set_sink_mute_by_name (sink.name, muted);
                sink.muted = muted;
            }
            if (level != sink.level) {
                sink.volume.scale ((PulseAudio.Volume) level);
                context.set_sink_volume_by_name (sink.name, sink.volume);
            }
            return true;
        }

        public bool change_source_mute (bool muted) {
            if (context == null || source == null) {
                return false;
            }
            if (muted != source.muted) {
                context.set_source_mute_by_name (source.name, muted);
                source.muted = muted;
            }
            return true;
        }
    }
}
