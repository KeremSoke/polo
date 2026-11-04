install_data('polo-wayland.sh',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('polo.desktop',
  install_dir: get_option('datadir') / 'wayland-sessions')
# dconf reads only /etc/dconf/profile, whatever the prefix.
install_data('dconf/profile/polo',
  install_dir: '/etc/dconf/profile')
install_data('dconf/db/polo.d/00-polo',
  install_dir: '/etc/dconf/db/polo.d')
install_data('dconf/db/polo.d/locks/00-polo',
  install_dir: '/etc/dconf/db/polo.d/locks')
meson.add_install_script('dconf/update.sh')
if get_option('systemd')
  systemd_pc = dependency('systemd', required: false)
  systemd_user_dir = get_option('prefix') / 'lib/systemd/user'
  if systemd_pc.found()
    systemd_user_dir = systemd_pc.get_variable(pkgconfig: 'systemduserunitdir',
      pkgconfig_define: ['prefix', get_option('prefix')],
      default_value: systemd_user_dir)
  endif
  install_data('systemd/polo-session.target', install_dir: systemd_user_dir)
endif

gnome = import('gnome')
polo_schemas = gnome.compile_schemas(depend_files: files('org.mate.polo.gschema.xml'))
install_data('org.mate.polo.gschema.xml',
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas')
gnome.post_install(glib_compile_schemas: true)
