# libpolo-policy: the window model and every policy decision.
# GLib and libpolo-marco only, no backend, so tests/policy replays Marco
# oracle scenarios against it without a compositor (enforced by
# tools/check-boundary.py ).
policy_sources = files(
  'core/window-adapter.vala',
  'core/window.vala',
  'policy/marco-prefs.vala',
  'policy/marco-prefs-tracker.vala',
  'policy/model.vala',
  'policy/activation.vala',
  'policy/constraints.vala',
  'policy/focus.vala',
  'policy/placement.vala',
  'policy/stack.vala',
  'policy/tab-list.vala',
  'policy/x11-props.vala',
  'policy/workspace-layout.vala',
  'policy/work-areas.vala',
  'policy/screenshot.vala',
)
policy_lib = static_library('polo-policy',
  policy_sources,
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  vala_header: 'polo-policy.h',
  vala_vapi: 'polo-policy.vapi',
  c_args: vala_c_args,
  dependencies: [glib_dep, gobject_dep, polo_marco_dep],
)
polo_policy_dep = declare_dependency(
  link_with: policy_lib,
  include_directories: include_directories('.'),
  dependencies: [glib_dep, gobject_dep, polo_marco_dep],
)

output_lib = static_library('polo-output',
  'core/output.vala', 'output/monitors-xml.vala', 'output/layout.vala', 'output/store.vala',
  vala_args: vala_args,
  vala_header: 'polo-output.h',
  vala_vapi: 'polo-output.vapi',
  c_args: vala_c_args,
  dependencies: [glib_dep, gobject_dep, gio_dep, cc.find_library('m')],
)
polo_output_dep = declare_dependency(
  link_with: output_lib,
  include_directories: include_directories('.'),
  dependencies: [glib_dep, gobject_dep, gio_dep, cc.find_library('m')],
)

polo_sources = files(
  'application.vala',
  'session/session.vala',
  'session/env.vala',
  'session/autostart.vala',
  'session/phases.vala',
  'session/end-session.vala',
  'session/logind.vala',
  'session/saved-session.vala',
  'dbus/registration.vala',
  'dbus/sm-client.vala',
  'dbus/sm-inhibitor.vala',
  'dbus/sm-app.vala',
  'dbus/sm-presence.vala',
  'dbus/sm-manager.vala',
  'dbus/outputs.vala',
  'core/spawn.vala',
  'decor/decorations.vala',
  'decor/frame.vala',
  'decor/window-icon.vala',
  'decor/frame-state.vala',
  'decor/shadow.vala',
  'core/trust.vala',
  'effects/engine.vala',
  'effects/locate-pointer.vala',
  'effects/backend-target.vala',
  'effects/wobbly-effect.vala',
  'log.vala',
  'main.vala',
  'protocols/foreign-toplevel.vala',
  'protocols/workspaces.vala',
  'protocols/session-lock.vala',
  'protocols/output-management.vala',
  'input/idle-tracker.vala',
  'input/a11y.vala',
  'input/accelerator.vala',
  'input/keyboard.vala',
  'input/bindings.vala',
  'input/device-settings.vala',
  'input/libinput-device.vala',
  'input/move-resize.vala',
  'input/xkb-settings.vala',
  'input/marco-actions.vala',
  'input/media-keys.vala',
  'input/mpris.vala',
  'input/volume.vala',
  'settings/background.vala',
  'settings/interface-mirror.vala',
  'settings/marco-prefs-reader.vala',
  'shell/desktop.vala',
  'shell/bell.vala',
  'shell/layer-shell.vala',
  'shell/media-key-actions.vala',
  'shell/not-responding.vala',
  'shell/rename-workspace.vala',
  'shell/tile-preview.vala',
  'shell/shell-ui.vala',
  'shell/switcher.vala',
  'shell/frame-input.vala',
  'shell/window-menu.vala',
  'shell/workspace-switcher.vala',
  'shell/xdg-adapter.vala',
)

# The only packages Vala code may use. tools/check-boundary.py
# enforces that every --pkg in src/ comes from this list.
systemd_vala_args = get_option('systemd') ? ['--define=POLO_HAVE_SYSTEMD'] : []
polo_vala_args = vala_args + systemd_vala_args + [
  '--pkg', 'polo-config',
  '--pkg', 'polo-backend',
  '--pkg', 'polo-marco',
  '--pkg', 'polo-fx',
  '--pkg', 'polo-bg',
  '--pkg', 'gio-unix-2.0',
  '--pkg', 'libpulse',
  '--pkg', 'libpulse-mainloop-glib',
  '--pkg', 'libcanberra',
]
polo_deps = [glib_dep, gobject_dep, gio_dep, gio_unix_dep, polo_backend_dep, polo_marco_dep,
             polo_fx_dep, polo_bg_dep, polo_policy_dep, polo_output_dep,
             dependency('libpulse'), dependency('libpulse-mainloop-glib'), dependency('libcanberra')]

if get_option('xwayland')
  polo_sources += files('shell/x11-adapter.vala', 'shell/ewmh.vala')
  polo_vala_args += ['--pkg', 'polo-xwm', '--pkg', 'posix', '--define=POLO_HAVE_XWAYLAND']
  polo_deps += polo_xwm_dep
endif
if have_xsmp
  polo_sources += files('session/xsmp.vala')
  polo_vala_args += ['--pkg', 'polo-xsmp', '--define=POLO_HAVE_XSMP']
  polo_deps += polo_xsmp_dep
endif

# The session manager's interfaces, as mate-session 1.28 defines them
sm_resources = import('gnome').compile_resources('sm-resources',
  meson.project_source_root() / 'data' / 'dbus' / 'fixtures' / 'sm.gresource.xml',
  source_dir: meson.project_source_root() / 'data' / 'dbus' / 'fixtures' / 'source',
)
polo_dbus_resources = import('gnome').compile_resources('polo-dbus-resources',
  meson.project_source_root() / 'data' / 'dbus' / 'polo.gresource.xml',
  source_dir: meson.project_source_root() / 'data' / 'dbus',
  c_name: 'polo_dbus',
)

polo_exe = executable('polo',
  polo_sources, sm_resources, polo_dbus_resources,
  vala_args: polo_vala_args,
  c_args: vala_c_args,
  include_directories: config_inc,
  dependencies: polo_deps,
  install: true,
  install_rpath: polo_private_rpath,
)
