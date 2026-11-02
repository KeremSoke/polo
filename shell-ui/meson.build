shell_ui_xml = meson.project_source_root() / 'protocol' / 'polo-shell-ui-v1.xml'
shell_ui_protocol = [
  custom_target('polo-shell-ui-v1-client-protocol.h',
    input: shell_ui_xml,
    output: 'polo-shell-ui-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  ),
  custom_target('polo-shell-ui-v1-client-protocol.c',
    input: shell_ui_xml,
    output: 'polo-shell-ui-v1-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  ),
]

gtk_dep = dependency('gtk+-3.0', version: '>= 3.24')
shell_ui_client_lib = static_library('polo-shell-ui-client',
  'protocol-client.c', shell_ui_protocol,
  dependencies: [gtk_dep, dependency('gtk+-wayland-3.0'), dependency('wayland-client')],
)

shell_ui_resources = import('gnome').compile_resources('shell-ui-resources', 'shell-ui.gresource.xml',
  c_name: 'polo_shell_ui_resources',
)

shell_ui_exe = executable('polo-shell-ui',
  'main.vala', 'alt-tab.vala', 'window-menu.vala', 'dialogs.vala', shell_ui_resources,
  vala_args: vala_args + ['--pkg', 'polo-shell-ui-client'],
  c_args: vala_c_args,
  include_directories: include_directories('.'),
  link_with: shell_ui_client_lib,
  dependencies: [glib_dep, gobject_dep, gio_dep, gio_unix_dep, gtk_dep, dependency('gtk-layer-shell-0'),
                 dependency('mate-desktop-2.0'),
                 dependency('wayland-client')],
  install: true,
  install_dir: get_option('libexecdir'),
)
