# Vala unit tests (suite unit-vala).
marco_prefs_test = executable('test-marco-prefs', 'marco-prefs.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep, gio_dep],
)
test('marco-prefs', marco_prefs_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

outputs_test = executable('test-outputs',
  'outputs.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep, polo_output_dep],
)
test('outputs', outputs_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'],
  env: ['POLO_MONITOR_FIXTURE=' + meson.current_source_dir() / 'fixtures/monitors-x-session.xml'])

work_areas_test = executable('test-work-areas', 'work-areas.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('work-areas', work_areas_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

marco_prefs_parse_test = executable('test-marco-prefs-parse', 'marco-prefs-parse.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('marco-prefs-parse', marco_prefs_parse_test, suite: 'unit-vala', protocol: 'tap',
  args: ['--tap'])

window_features_test = executable('test-window-features', 'window-features.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('window-features', window_features_test, suite: 'unit-vala', protocol: 'tap',
  args: ['--tap'])

constraints_fuzz_test = executable('test-constraints-fuzz', 'constraints-fuzz.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('constraints-fuzz', constraints_fuzz_test, suite: 'unit-vala', protocol: 'tap',
  args: ['--tap'])

stack_test = executable('test-stack', 'stack.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('stack', stack_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

tab_list_test = executable('test-tab-list', 'tab-list.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('tab-list', tab_list_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

workspaces_test = executable('test-workspaces', 'workspaces.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('workspaces', workspaces_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

show_desktop_test = executable('test-show-desktop', 'show-desktop.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('show-desktop', show_desktop_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

workspace_layout_test = executable('test-workspace-layout', 'workspace-layout.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('workspace-layout', workspace_layout_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

key_actions_test = executable('test-key-actions', 'key-actions.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('key-actions', key_actions_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

screen_share_test = executable('test-screen-share', 'screen-share.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('screen-share', screen_share_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

activation_test = executable('test-activation', 'activation.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('activation', activation_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

unmanage_test = executable('test-unmanage', 'unmanage.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('unmanage', unmanage_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

ping_test = executable('test-ping', 'ping.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('ping', ping_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'], timeout: 30)

edge_resistance_test = executable('test-edge-resistance', 'edge-resistance.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('edge-resistance-port', edge_resistance_test, suite: 'unit-vala', protocol: 'tap',
  args: ['--tap'])

xkb_settings_test = executable('test-xkb-settings',
  'xkb-config.vala', meson.project_source_root() / 'src' / 'input' / 'xkb-settings.vala',
  vala_args: vala_args + ['--pkg', 'polo-backend'],
  c_args: vala_c_args,
  dependencies: [glib_dep, gobject_dep, polo_backend_dep],
)
test('xkb-settings', xkb_settings_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

device_settings_test = executable('test-device-settings',
  'pointer-settings.vala', meson.project_source_root() / 'src' / 'input' / 'device-settings.vala',
  vala_args: vala_args + ['--pkg', 'polo-backend'],
  c_args: vala_c_args,
  dependencies: [glib_dep, gobject_dep, gio_dep, polo_backend_dep],
)
test('device-settings', device_settings_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'],
  env: ['GSETTINGS_BACKEND=memory'])

tiling_test = executable('test-tiling', 'tiling.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('tiling', tiling_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

x11_props_test = executable('test-x11-props', 'x11-props.vala',
  c_args: vala_c_args,
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  dependencies: [polo_policy_dep],
)
test('x11-props', x11_props_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

decor_frame_test = executable('test-decor-frame',
  'decor-frame.vala',
  meson.project_source_root() / 'src/decor/decorations.vala',
  meson.project_source_root() / 'src/decor/frame.vala',
  meson.project_source_root() / 'src/decor/window-icon.vala',
  meson.project_source_root() / 'src/log.vala',
  vala_args: vala_args + ['--pkg', 'polo-backend', '--pkg', 'polo-marco', '--pkg', 'posix'],
  c_args: vala_c_args,
  dependencies: [polo_backend_dep, polo_marco_dep, gio_dep],
)
test('decor-frame', decor_frame_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'],
  env: ['WESTON_MODULE_MAP=' + weston_module_map, 'GSETTINGS_BACKEND=memory'])

interface_mirror_test = executable('test-interface-mirror',
  'gtk-mirror.vala', meson.project_source_root() / 'src/settings/interface-mirror.vala',
  vala_args: vala_args,
  c_args: vala_c_args,
  dependencies: [glib_dep, gobject_dep, gio_dep],
)
test('interface-mirror', interface_mirror_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'],
  env: ['GSETTINGS_BACKEND=memory', 'GSETTINGS_SCHEMA_DIR=' + meson.project_build_root() / 'data'],
  depends: polo_schemas)

media_keys_schemas = custom_target('media-keys-schemas',
  input: 'fixtures/media-keys/org.mate.SettingsDaemon.plugins.media-keys.gschema.xml',
  output: 'gschemas.compiled',
  build_by_default: true,
  command: [find_program('glib-compile-schemas'), '--targetdir', meson.current_build_dir(),
            meson.current_source_dir() / 'fixtures' / 'media-keys'],
)
media_keys_test = executable('test-media-keys',
  'acme-keys.vala',
  meson.project_source_root() / 'src/input/media-keys.vala',
  meson.project_source_root() / 'src/input/accelerator.vala',
  meson.project_source_root() / 'src/input/mpris.vala',
  meson.project_source_root() / 'src/log.vala',
  vala_args: vala_args + ['--pkg', 'polo-backend', '--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_backend_dep, polo_marco_dep, gio_dep],
)
test('media-keys', media_keys_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'],
  env: ['GSETTINGS_BACKEND=memory', 'POLO_MEDIA_KEYS_SCHEMAS=' + meson.current_build_dir(),
        'WESTON_MODULE_MAP=' + weston_module_map],
  depends: media_keys_schemas)

lamp_target_test = executable('test-lamp-target', 'lamp-target.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep],
)
test('lamp-target', lamp_target_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])

expo_test = executable('test-expo', 'expo.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep, cc.find_library('m')],
)
test('expo', expo_test, suite: 'unit-vala', protocol: 'tap', args: ['--tap'])
