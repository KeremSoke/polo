wayland_scanner = find_program(
  dependency('wayland-scanner', native: true).get_variable(pkgconfig: 'wayland_scanner'))

backend_sources = files(
  'client.c',
  'cursor.c',
  'backend-drm.c',
  'grab.c',
  'input-device.c',
  'keymap.c',
  'log.c',
  'loop-source.c',
  'stats.c',
  'watchdog.c',
  'output.c',
  'pixel-buffer.c',
  'protocols/protocol-util.c',
  'protocols/layer-shell.c',
  'protocols/wlr-foreign-toplevel.c',
  'protocols/ext-foreign-toplevel-list.c',
  'protocols/xfce-private.c',
  'protocols/xdg-activation.c',
  'protocols/xdg-decoration.c',
  'protocols/system-bell.c',
  'flash.c',
  'protocols/gtk-shell.c',
  'protocols/security-context.c',
  'protocols/xdg-toplevel-icon.c',
  'protocols/xdg-foreign.c',
  'protocols/xdg-dialog.c',
  'protocols/pointer-gestures.c',
  'protocols/cursor-shape.c',
  'protocols/shortcuts-inhibit.c',
  'protocols/idle-inhibit.c',
  'protocols/ext-idle-notify.c',
  'protocols/ext-session-lock.c',
  'protocols/wlr-output-management.c',
  'idle.c',
  'protocols/text-input.c',
  'protocols/virtual-keyboard.c',
  'protocols/primary-selection.c',
  'protocols/data-control.c',
  'protocols/wlr-gamma-control.c',
  'protocols/ext-image-copy-capture.c',
  'protocols/kde-server-decoration.c',
  'protocols/xwayland-keyboard-grab.c',
  'protocols/polo-shell-ui.c',
  'protocols/ext-workspace.c',
  'seat.c',
  'server.c',
  'test-protocol.c',
  'toplevel.c',
  'version.c',
  'view.c',
)
backend_deps = [glib_dep, gobject_dep, libweston_dep, dependency('libdisplay-info'), dependency('xcursor'), dependency('wayland-client'), dependency('xkbcommon'), dependency('xkbregistry'), dependency('libinput'), dependency('pixman-1')]
backend_c_args = []
if get_option('debug-kill')
  backend_c_args += '-DPOLO_HAVE_DEBUG_KILL'
endif
if get_option('debug-protocol')
  backend_c_args += '-DPOLO_HAVE_DEBUG_PROTOCOL'
endif
if get_option('fuzz')
  backend_c_args += '-fsanitize=fuzzer-no-link'
endif

if get_option('test-protocol')
  polo_test_xml = meson.project_source_root() / 'protocol' / 'polo-test-v1.xml'
  backend_sources += custom_target('polo-test-v1-server-protocol.h',
    input: polo_test_xml,
    output: 'polo-test-v1-server-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )
  backend_sources += custom_target('polo-test-v1-protocol.c',
    input: polo_test_xml,
    output: 'polo-test-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
  backend_c_args += '-DPOLO_HAVE_TEST_PROTOCOL'
endif
backend_deps += weston_proj.get_variable('dep_libweston_private_h').as_system()

# zwlr_layer_shell_v1 v5 shim. Its get_popup request references
# xdg_popup, so the xdg-shell protocol interface tables must be linked too
# (libweston's own copy is hidden inside the shared library).
wlr_layer_xml = meson.project_source_root() / 'protocol' / 'wlr-layer-shell-unstable-v1.xml'
backend_sources += custom_target('wlr-layer-shell-unstable-v1-server-protocol.h',
  input: wlr_layer_xml,
  output: 'wlr-layer-shell-unstable-v1-server-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)
backend_sources += custom_target('wlr-layer-shell-unstable-v1-protocol.c',
  input: wlr_layer_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
# zwlr_foreign_toplevel_manager_v1 v3 shim.
wlr_ftl_xml = meson.project_source_root() / 'protocol' / 'wlr-foreign-toplevel-management-unstable-v1.xml'
backend_sources += custom_target('wlr-foreign-toplevel-management-unstable-v1-server-protocol.h',
  input: wlr_ftl_xml,
  output: 'wlr-foreign-toplevel-management-unstable-v1-server-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)
backend_sources += custom_target('wlr-foreign-toplevel-management-unstable-v1-protocol.c',
  input: wlr_ftl_xml,
  output: 'wlr-foreign-toplevel-management-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
# ext_foreign_toplevel_list_v1 shim.
ext_ftl_xml = meson.project_source_root() / 'protocol' / 'ext-foreign-toplevel-list-v1.xml'
backend_sources += custom_target('ext-foreign-toplevel-list-v1-server-protocol.h',
  input: ext_ftl_xml,
  output: 'ext-foreign-toplevel-list-v1-server-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)
backend_sources += custom_target('ext-foreign-toplevel-list-v1-protocol.c',
  input: ext_ftl_xml,
  output: 'ext-foreign-toplevel-list-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
wl_protocols_dir = dependency('wayland-protocols').get_variable(pkgconfig: 'pkgdatadir')
foreach name, xml : {
  'xfce-foreign-toplevel-management-private-v1': meson.project_source_root() / 'protocol' / 'xfce-foreign-toplevel-management-private-v1.xml',
  'xfce-output-private-v1': meson.project_source_root() / 'protocol' / 'xfce-output-private-v1.xml',
  'ext-workspace-v1': wl_protocols_dir / 'staging' / 'ext-workspace' / 'ext-workspace-v1.xml',
  'xdg-decoration-unstable-v1': wl_protocols_dir / 'unstable' / 'xdg-decoration' / 'xdg-decoration-unstable-v1.xml',
  'primary-selection-unstable-v1': wl_protocols_dir / 'unstable' / 'primary-selection' / 'primary-selection-unstable-v1.xml',
  'ext-data-control-v1': wl_protocols_dir / 'staging' / 'ext-data-control' / 'ext-data-control-v1.xml',
  'wlr-data-control-unstable-v1': meson.project_source_root() / 'protocol' / 'wlr-data-control-unstable-v1.xml',
  'wlr-gamma-control-unstable-v1': meson.project_source_root() / 'protocol' / 'wlr-gamma-control-unstable-v1.xml',
  'ext-image-capture-source-v1': wl_protocols_dir / 'staging' / 'ext-image-capture-source' / 'ext-image-capture-source-v1.xml',
  'ext-image-copy-capture-v1': wl_protocols_dir / 'staging' / 'ext-image-copy-capture' / 'ext-image-copy-capture-v1.xml',
  'text-input-unstable-v3': wl_protocols_dir / 'unstable' / 'text-input' / 'text-input-unstable-v3.xml',
  'input-method-unstable-v2': meson.project_source_root() / 'protocol' / 'input-method-unstable-v2.xml',
  'virtual-keyboard-unstable-v1': meson.project_source_root() / 'protocol' / 'virtual-keyboard-unstable-v1.xml',
  'idle-inhibit-unstable-v1': wl_protocols_dir / 'unstable' / 'idle-inhibit' / 'idle-inhibit-unstable-v1.xml',
  'ext-idle-notify-v1': wl_protocols_dir / 'staging' / 'ext-idle-notify' / 'ext-idle-notify-v1.xml',
  'ext-session-lock-v1': wl_protocols_dir / 'staging' / 'ext-session-lock' / 'ext-session-lock-v1.xml',
  'keyboard-shortcuts-inhibit-unstable-v1': wl_protocols_dir / 'unstable' / 'keyboard-shortcuts-inhibit' / 'keyboard-shortcuts-inhibit-unstable-v1.xml',
  'cursor-shape-v1': wl_protocols_dir / 'staging' / 'cursor-shape' / 'cursor-shape-v1.xml',
  'tablet-v2': wl_protocols_dir / 'stable' / 'tablet' / 'tablet-v2.xml',
  'pointer-gestures-unstable-v1': wl_protocols_dir / 'unstable' / 'pointer-gestures' / 'pointer-gestures-unstable-v1.xml',
  'xdg-foreign-unstable-v2': wl_protocols_dir / 'unstable' / 'xdg-foreign' / 'xdg-foreign-unstable-v2.xml',
  'xdg-dialog-v1': wl_protocols_dir / 'staging' / 'xdg-dialog' / 'xdg-dialog-v1.xml',
  'xdg-toplevel-icon-v1': wl_protocols_dir / 'staging' / 'xdg-toplevel-icon' / 'xdg-toplevel-icon-v1.xml',
  'security-context-v1': wl_protocols_dir / 'staging' / 'security-context' / 'security-context-v1.xml',
  'xdg-system-bell-v1': wl_protocols_dir / 'staging' / 'xdg-system-bell' / 'xdg-system-bell-v1.xml',
  'gtk-shell': meson.project_source_root() / 'protocol' / 'gtk-shell.xml',
  'server-decoration': meson.project_source_root() / 'protocol' / 'server-decoration.xml',
  'xdg-activation-v1': wl_protocols_dir / 'staging' / 'xdg-activation' / 'xdg-activation-v1.xml',
  'xwayland-keyboard-grab-unstable-v1': wl_protocols_dir / 'unstable' / 'xwayland-keyboard-grab' / 'xwayland-keyboard-grab-unstable-v1.xml',
  'polo-shell-ui-v1': meson.project_source_root() / 'protocol' / 'polo-shell-ui-v1.xml',
  'wlr-output-management-unstable-v1': meson.project_source_root() / 'protocol' / 'wlr-output-management-unstable-v1.xml',
}
  backend_sources += custom_target(name + '-server-protocol.h',
    input: xml,
    output: name + '-server-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )
  backend_sources += custom_target(name + '-protocol.c',
    input: xml,
    output: name + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach
xdg_shell_xml = meson.project_source_root() / 'protocol' / 'xdg-shell.xml'
backend_sources += custom_target('xdg-shell-protocol.c',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

backend_lib = static_library('polo-backend',
  backend_sources,
  c_args: backend_c_args,
  dependencies: backend_deps,
)

# What Vala targets see: the Polo header and a link line, never
# libweston's include path or its pkg-config dependencies. Meson turns every
# pkg-config dependency of a Vala target into "--pkg NAME", so libweston is
# linked as a library target instead of through dep_libweston_public.
polo_backend_dep = declare_dependency(
  link_with: [backend_lib, weston_proj.get_variable('lib_weston')],
  include_directories: include_directories('.'),
  dependencies: [glib_dep, gobject_dep, cc.find_library('Xcursor'), cc.find_library('wayland-client'), cc.find_library('xkbcommon'), cc.find_library('xkbregistry')],
)
