# Headless integration tests. Need -Dtest-protocol=true.
wayland_client_dep = dependency('wayland-client')
wayland_protocols_dir = dependency('wayland-protocols').get_variable(pkgconfig: 'pkgdatadir')

client_protocols = {
  'xdg-shell': wayland_protocols_dir / 'stable' / 'xdg-shell' / 'xdg-shell.xml',
  'polo-test-v1': meson.project_source_root() / 'protocol' / 'polo-test-v1.xml',
  'wlr-layer-shell-unstable-v1': meson.project_source_root() / 'protocol' / 'wlr-layer-shell-unstable-v1.xml',
  'wlr-foreign-toplevel-management-unstable-v1': meson.project_source_root() / 'protocol' / 'wlr-foreign-toplevel-management-unstable-v1.xml',
  'ext-foreign-toplevel-list-v1': meson.project_source_root() / 'protocol' / 'ext-foreign-toplevel-list-v1.xml',
  'xfce-foreign-toplevel-management-private-v1': meson.project_source_root() / 'protocol' / 'xfce-foreign-toplevel-management-private-v1.xml',
  'xfce-output-private-v1': meson.project_source_root() / 'protocol' / 'xfce-output-private-v1.xml',
  'ext-workspace-v1': wayland_protocols_dir / 'staging' / 'ext-workspace' / 'ext-workspace-v1.xml',
  'xdg-output-unstable-v1': wayland_protocols_dir / 'unstable' / 'xdg-output' / 'xdg-output-unstable-v1.xml',
  'primary-selection-unstable-v1': wayland_protocols_dir / 'unstable' / 'primary-selection' / 'primary-selection-unstable-v1.xml',
  'text-input-unstable-v3': wayland_protocols_dir / 'unstable' / 'text-input' / 'text-input-unstable-v3.xml',
  'input-method-unstable-v2': meson.project_source_root() / 'protocol' / 'input-method-unstable-v2.xml',
  'virtual-keyboard-unstable-v1': meson.project_source_root() / 'protocol' / 'virtual-keyboard-unstable-v1.xml',
  'idle-inhibit-unstable-v1': wayland_protocols_dir / 'unstable' / 'idle-inhibit' / 'idle-inhibit-unstable-v1.xml',
  'ext-idle-notify-v1': wayland_protocols_dir / 'staging' / 'ext-idle-notify' / 'ext-idle-notify-v1.xml',
  'ext-session-lock-v1': wayland_protocols_dir / 'staging' / 'ext-session-lock' / 'ext-session-lock-v1.xml',
  'wlr-output-management-unstable-v1': meson.project_source_root() / 'protocol' / 'wlr-output-management-unstable-v1.xml',
  'keyboard-shortcuts-inhibit-unstable-v1': wayland_protocols_dir / 'unstable' / 'keyboard-shortcuts-inhibit' / 'keyboard-shortcuts-inhibit-unstable-v1.xml',
  'cursor-shape-v1': wayland_protocols_dir / 'staging' / 'cursor-shape' / 'cursor-shape-v1.xml',
  'tablet-v2': wayland_protocols_dir / 'stable' / 'tablet' / 'tablet-v2.xml',
  'pointer-gestures-unstable-v1': wayland_protocols_dir / 'unstable' / 'pointer-gestures' / 'pointer-gestures-unstable-v1.xml',
  'xdg-foreign-unstable-v2': wayland_protocols_dir / 'unstable' / 'xdg-foreign' / 'xdg-foreign-unstable-v2.xml',
  'xdg-dialog-v1': wayland_protocols_dir / 'staging' / 'xdg-dialog' / 'xdg-dialog-v1.xml',
  'xdg-toplevel-icon-v1': wayland_protocols_dir / 'staging' / 'xdg-toplevel-icon' / 'xdg-toplevel-icon-v1.xml',
  'security-context-v1': wayland_protocols_dir / 'staging' / 'security-context' / 'security-context-v1.xml',
  'xdg-system-bell-v1': wayland_protocols_dir / 'staging' / 'xdg-system-bell' / 'xdg-system-bell-v1.xml',
  'gtk-shell': meson.project_source_root() / 'protocol' / 'gtk-shell.xml',
  'ext-data-control-v1': wayland_protocols_dir / 'staging' / 'ext-data-control' / 'ext-data-control-v1.xml',
  'wlr-data-control-unstable-v1': meson.project_source_root() / 'protocol' / 'wlr-data-control-unstable-v1.xml',
  'wlr-gamma-control-unstable-v1': meson.project_source_root() / 'protocol' / 'wlr-gamma-control-unstable-v1.xml',
  'linux-explicit-synchronization-unstable-v1': wayland_protocols_dir / 'unstable' / 'linux-explicit-synchronization' / 'linux-explicit-synchronization-unstable-v1.xml',
  'linux-drm-syncobj-v1': wayland_protocols_dir / 'staging' / 'linux-drm-syncobj' / 'linux-drm-syncobj-v1.xml',
  'linux-dmabuf-v1': wayland_protocols_dir / 'stable' / 'linux-dmabuf' / 'linux-dmabuf-v1.xml',
  'xdg-decoration-unstable-v1': wayland_protocols_dir / 'unstable' / 'xdg-decoration' / 'xdg-decoration-unstable-v1.xml',
  'server-decoration': meson.project_source_root() / 'protocol' / 'server-decoration.xml',
  'xdg-activation-v1': wayland_protocols_dir / 'staging' / 'xdg-activation' / 'xdg-activation-v1.xml',
  'polo-shell-ui-v1': meson.project_source_root() / 'protocol' / 'polo-shell-ui-v1.xml',
  'weston-output-capture': meson.project_source_root() / 'protocol' / 'weston-output-capture.xml',
  'ext-image-capture-source-v1': wayland_protocols_dir / 'staging' / 'ext-image-capture-source' / 'ext-image-capture-source-v1.xml',
  'ext-image-copy-capture-v1': wayland_protocols_dir / 'staging' / 'ext-image-copy-capture' / 'ext-image-copy-capture-v1.xml',
}
client_protocol_sources = []
foreach name, xml : client_protocols
  client_protocol_sources += custom_target(name + '-client-protocol.h',
    input: xml,
    output: name + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  client_protocol_sources += custom_target(name + '-protocol.c',
    input: xml,
    output: name + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach

harness_lib = static_library('polo-test-harness',
  'harness.c',
  client_protocol_sources,
  dependencies: wayland_client_dep,
)
harness_dep = declare_dependency(
  link_with: harness_lib,
  sources: client_protocol_sources,
  dependencies: wayland_client_dep,
)

protocol_test_env = environment()
protocol_test_env.set('WESTON_MODULE_MAP', weston_module_map)
# Polo reads MATE settings (scaling, cursor); never the developer's dconf.
protocol_test_env.set('GSETTINGS_BACKEND', 'memory')
# No helper unless a test names one.
protocol_test_env.set('POLO_SHELL_UI', '')

foreach name : ['ssd', 'frame-input', 'shade', 'frame-scale', 'output-replug', 'decor-live', 'pixel-buffer-gl', 'cursor-no-theme', 'windows', 'interactive', 'outputs', 'managed-outputs', 'debug-kill', 'layer-shell', 'xdg-output', 'foreign-toplevel',
              'ext-foreign-toplevel-list', 'popups',
              'xfce-private', 'ext-workspace', 'focus', 'activation', 'keybindings', 'monitor-keybindings', 'edge-resistance', 'global-keybindings',
              'window-keybindings', 'keyboard-move-resize', 'mouse-actions', 'keyboard-config', 'tiling', 'decoration', 'primary-selection', 'text-input', 'idle-inhibit', 'idle-notify', 'session-lock', 'output-management', 'shortcuts-inhibit', 'cursor-shape', 'pointer-gestures', 'xdg-foreign', 'security-context', 'trust', 'system-bell', 'locate-pointer', 'accessx', 'data-control', 'gamma-control', 'image-copy-capture', 'wallpaper', 'custom-shortcuts', 'lamp-target', 'explicit-sync', 'blank-wake']
  test_deps = {'cursor-shape': [dependency('xcursor')], 'wallpaper': [dependency('cairo')]}
  exe = executable('test-' + name, 'test-' + name + '.c',
    dependencies: [harness_dep] + test_deps.get(name, []))
  test(name, exe,
    args: [polo_exe],
    env: protocol_test_env,
    suite: 'protocol',
    timeout: 60,
  )
endforeach

fake_shell_ui = executable('fake-shell-ui', 'fake-shell-ui.c', dependencies: harness_dep)
test('toplevel-icon', executable('test-toplevel-icon', 'test-toplevel-icon.c', dependencies: harness_dep),
  args: [polo_exe, fake_shell_ui],
  env: protocol_test_env,
  suite: 'protocol',
  timeout: 60,
)
drm_syncobj_deps = [dependency('libdrm', required: false), dependency('gbm', required: false)]
if drm_syncobj_deps[0].found() and drm_syncobj_deps[1].found()
  test('drm-syncobj', executable('test-drm-syncobj', 'test-drm-syncobj.c', dependencies: [harness_dep] + drm_syncobj_deps),
    args: [polo_exe],
    env: protocol_test_env,
    suite: 'protocol',
    timeout: 60,
  )
endif
test('shell-ui', executable('test-shell-ui', 'test-shell-ui.c', dependencies: harness_dep),
  args: [polo_exe, fake_shell_ui],
  env: protocol_test_env,
  suite: 'protocol',
  timeout: 60,
)
test('media-keys-osd', executable('test-media-keys', 'test-media-keys.c', dependencies: harness_dep),
  args: [polo_exe, fake_shell_ui],
  env: protocol_test_env,
  suite: 'protocol',
  timeout: 60,
)
test('switcher', executable('test-switcher', 'test-switcher.c', dependencies: harness_dep),
  args: [polo_exe, fake_shell_ui],
  env: protocol_test_env,
  suite: 'protocol',
  timeout: 60,
)
test('workspace-switcher', executable('test-workspace-switcher', 'test-workspace-switcher.c', dependencies: harness_dep),
  args: [polo_exe, fake_shell_ui],
  env: protocol_test_env,
  suite: 'protocol',
  timeout: 60,
)
test('window-menu', executable('test-window-menu', 'test-window-menu.c', dependencies: harness_dep),
  args: [polo_exe, fake_shell_ui],
  env: protocol_test_env,
  suite: 'protocol',
  timeout: 60,
)
test('frame-actions', executable('test-frame-actions', 'test-frame-actions.c', dependencies: harness_dep),
  args: [polo_exe, fake_shell_ui],
  env: protocol_test_env,
  suite: 'protocol',
  timeout: 60,
)
if get_option('shell-ui')
  shell_ui_gtk_env = protocol_test_env
  shell_ui_gtk_env.set('NO_AT_BRIDGE', '1')
  shell_ui_gtk_env.set('GTK_A11Y', 'none')
  shell_ui_gtk_env.set('GTK_MODULES', '')
  shell_ui_gtk_env.set('GTK3_MODULES', '')
  test('shell-ui-gtk', executable('test-shell-ui-gtk', 'test-shell-ui-gtk.c', dependencies: harness_dep),
    args: [polo_exe, shell_ui_exe],
    env: shell_ui_gtk_env,
    suite: 'protocol',
    timeout: 60,
  )
  test('alt-tab-hide', executable('test-alt-tab-hide', 'test-alt-tab-hide.c', dependencies: harness_dep),
    args: [polo_exe, shell_ui_exe],
    env: shell_ui_gtk_env,
    suite: 'protocol',
    timeout: 60,
  )
  test('shell-ui-visible', executable('test-shell-ui-visible', 'test-shell-ui-visible.c', dependencies: harness_dep),
    args: [polo_exe, shell_ui_exe],
    env: shell_ui_gtk_env,
    suite: 'protocol',
    timeout: 60,
  )
  if get_option('xwayland')
    test('xwayland', executable('test-xwayland', 'test-xwayland.c', dependencies: harness_dep),
      args: [polo_exe, x_test_client.full_path(), fake_shell_ui.full_path(), x_send.full_path(), x_clip.full_path(), x_dnd.full_path()],
      depends: [x_test_client, fake_shell_ui, x_send, x_clip, x_dnd],
      env: protocol_test_env,
      suite: 'protocol',
      timeout: 180,
    )
    polo_input = executable('polo-input', '../xwayland/polo-input.c', dependencies: harness_dep)
    test('x-oracle', python,
      args: [files('../xwayland/replay-x.py'), polo_exe, polo_input.full_path(),
             '--fixtures', meson.project_source_root() / 'tests' / 'policy' / 'fixtures',
             '--passing', meson.project_source_root() / 'tests' / 'xwayland' / 'passing.txt'],
      depends: [polo_input],
      env: protocol_test_env,
      suite: 'xwayland',
      timeout: 900,
      verbose: true,
    )
    test('decor-visual', python,
      args: [files('../render/decor/compare.py'), polo_exe, polo_input.full_path(), shell_ui_exe.full_path()],
      depends: [polo_input, shell_ui_exe],
      env: shell_ui_gtk_env,
      suite: 'render',
      timeout: 600,
      verbose: true,
    )
    test('app-matrix', python,
      args: [files('../apps/matrix.py'), polo_exe, polo_input.full_path()],
      depends: [polo_input],
      env: protocol_test_env,
      suite: 'apps',
      timeout: 900,
      is_parallel: false,
    )
    test('x-smoke', python,
      args: [files('../xwayland/smoke.py'), polo_exe, polo_input.full_path()],
      depends: [polo_input],
      env: protocol_test_env,
      suite: 'xwayland',
      timeout: 120,
    )
  endif
  test('rename-workspace', executable('test-rename-workspace', 'test-rename-workspace.c', dependencies: harness_dep),
    args: [polo_exe, shell_ui_exe],
    env: shell_ui_gtk_env,
    suite: 'protocol',
    timeout: 60,
  )
  test('shell-ui-colors', executable('test-shell-ui-colors', 'test-shell-ui-colors.c', dependencies: harness_dep),
    args: [polo_exe, shell_ui_exe],
    env: shell_ui_gtk_env,
    suite: 'protocol',
    timeout: 60,
  )
  test('not-responding', executable('test-not-responding', 'test-not-responding.c', dependencies: harness_dep),
    args: [polo_exe, shell_ui_exe],
    env: shell_ui_gtk_env,
    suite: 'protocol',
    timeout: 60,
  )
endif

# Magic Lamp needs the GL renderer and Polo's own effects schema.
magic_lamp_env = environment()
magic_lamp_env.set('WESTON_MODULE_MAP', weston_module_map)
magic_lamp_env.set('GSETTINGS_BACKEND', 'memory')
magic_lamp_env.set('POLO_SHELL_UI', '')
magic_lamp_env.set('GSETTINGS_SCHEMA_DIR', meson.project_build_root() / 'data')
test('magic-lamp', executable('test-magic-lamp', 'test-magic-lamp.c', dependencies: harness_dep),
  args: [polo_exe],
  env: magic_lamp_env,
  suite: 'protocol',
  timeout: 60,
)
test('magic-lamp-fade', executable('test-magic-lamp-fade', 'test-magic-lamp-fade.c', dependencies: harness_dep),
  args: [polo_exe],
  env: magic_lamp_env,
  suite: 'protocol',
  timeout: 60,
)
test('expo', executable('test-expo', 'test-expo.c', dependencies: harness_dep),
  args: [polo_exe],
  env: magic_lamp_env,
  suite: 'protocol',
  timeout: 60,
)
test('bend-mirror', executable('test-bend-mirror', 'test-bend-mirror.c', dependencies: harness_dep),
  args: [polo_exe],
  env: magic_lamp_env,
  suite: 'protocol',
  timeout: 60,
)
test('cube-draw', executable('test-cube-draw', 'test-cube-draw.c', dependencies: harness_dep),
  args: [polo_exe],
  env: magic_lamp_env,
  suite: 'protocol',
  timeout: 90,
)
test('cube-background', executable('test-cube-background', 'test-cube-background.c',
    dependencies: [harness_dep, dependency('cairo')]),
  args: [polo_exe],
  env: magic_lamp_env,
  suite: 'protocol',
  timeout: 60,
)
test('cube-switch', executable('test-cube-switch', 'test-cube-switch.c', dependencies: harness_dep),
  args: [polo_exe],
  env: magic_lamp_env,
  suite: 'protocol',
  timeout: 90,
)
test('cube-interactive', executable('test-cube-interactive', 'test-cube-interactive.c', dependencies: harness_dep),
  args: [polo_exe],
  env: magic_lamp_env,
  suite: 'protocol',
  timeout: 90,
)
cube_env = environment()
cube_env.set('WESTON_MODULE_MAP', weston_module_map)
cube_env.set('GSETTINGS_BACKEND', 'memory')
cube_env.set('POLO_SHELL_UI', '')
cube_env.set('GSETTINGS_SCHEMA_DIR', meson.project_build_root() / 'data')
cube_env.set('LIBGL_ALWAYS_SOFTWARE', '1')
cube_env.set('GALLIUM_DRIVER', 'llvmpipe')
cube_test = executable('test-cube', 'test-cube.c',
  dependencies: [harness_dep, polo_fx_dep, cc.find_library('m')])
test('cube', cube_test,
  args: [polo_exe],
  env: cube_env,
  suite: 'protocol',
  timeout: 180,
)
test('cube-pixman', cube_test,
  args: [polo_exe, 'pixman'],
  env: cube_env,
  suite: 'protocol',
  timeout: 60,
)
test('cube-animations-off', cube_test,
  args: [polo_exe, 'animations-off'],
  env: cube_env,
  suite: 'protocol',
  timeout: 60,
)
test('cube-outputs', cube_test,
  args: [polo_exe, 'outputs'],
  env: cube_env,
  suite: 'protocol',
  timeout: 90,
)
test('cube-deform', cube_test,
  args: [polo_exe, 'deform'],
  env: cube_env,
  suite: 'protocol',
  timeout: 60,
)
if get_option('xwayland')
  test('cube-layout', executable('test-cube-layout', 'test-cube-layout.c',
      dependencies: [harness_dep, dependency('x11')]),
    args: [polo_exe, fake_shell_ui],
    env: cube_env,
    suite: 'protocol',
    timeout: 60,
  )
endif
window_animations_test = executable('test-window-animations', 'test-window-animations.c',
  dependencies: harness_dep)
test('window-animations', window_animations_test,
  args: [polo_exe],
  env: magic_lamp_env,
  suite: 'protocol',
  timeout: 120,
)
test('window-animations-pixman', window_animations_test,
  args: [polo_exe, 'pixman'],
  env: magic_lamp_env,
  suite: 'protocol',
  timeout: 60,
)
test('window-animations-squeezimize', window_animations_test,
  args: [polo_exe, 'squeezimize'],
  env: magic_lamp_env,
  suite: 'protocol',
  timeout: 60,
)
test('display-switcher', executable('test-display-switcher', 'test-display-switcher.c', dependencies: harness_dep),
  args: [polo_exe, fake_shell_ui],
  env: cube_env,
  suite: 'protocol',
  timeout: 60,
)
test('hotplug', executable('test-hotplug', 'test-hotplug.c', dependencies: harness_dep),
  args: [polo_exe, fake_shell_ui],
  env: cube_env,
  suite: 'protocol',
  timeout: 60,
)
