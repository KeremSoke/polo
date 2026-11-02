xwm_deps = [glib_dep, gobject_dep, gio_dep, libweston_dep, dependency('wayland-server')]
foreach name : ['xcb', 'xcb-composite', 'xcb-render', 'xcb-shape', 'xcb-xfixes', 'xcursor']
  xwm_deps += dependency(name)
endforeach

xwayland_shell_xml = dependency('wayland-protocols').get_variable(pkgconfig: 'pkgdatadir') / 'staging' / 'xwayland-shell' / 'xwayland-shell-v1.xml'
xwm_protocol = [
  custom_target('xwayland-shell-v1-server-protocol.h',
    input: xwayland_shell_xml,
    output: 'xwayland-shell-v1-server-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  ),
  custom_target('xwayland-shell-v1-protocol.c',
    input: xwayland_shell_xml,
    output: 'xwayland-shell-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  ),
]

# Upstream's code: its warnings stay upstream's.
xwm_vendored = static_library('polo-xwm-vendored',
  'weston/launcher.c',
  'weston/window-manager.c',
  'weston/selection.c',
  'weston/dnd.c',
  'weston/shared/hash.c',
  'weston/shared/xcb-xwayland.c',
  xwm_protocol,
  include_directories: include_directories('.', 'weston', 'weston/shared', '../backend'),
  dependencies: xwm_deps,
  c_args: ['-w', '-Wno-error'],
)

xwm_lib = static_library('polo-xwm',
  'xwm-object.c',
  'props.c',
  include_directories: include_directories('.', 'weston', '../backend'),
  dependencies: xwm_deps,
  link_whole: xwm_vendored,
)

polo_xwm_dep = declare_dependency(
  link_with: xwm_lib,
  include_directories: include_directories('.'),
  dependencies: [glib_dep, gobject_dep, polo_backend_dep, cc.find_library('xcb'), cc.find_library('xcb-composite'), cc.find_library('xcb-render'), cc.find_library('xcb-shape'), cc.find_library('xcb-xfixes')],
)
