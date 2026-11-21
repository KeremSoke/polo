install_data('polo-wayland.sh',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('xdg/xdg-desktop-portal/mate-portals.conf',
  install_dir: get_option('datadir') / 'polo' / 'xdg' / 'xdg-desktop-portal')
man_conf = configuration_data()
man_conf.set('VERSION', meson.project_version())
foreach page : [['polo', '1'], ['polo-session', '7']]
  man_page = configure_file(
    input: 'man' / '@0@.@1@.in'.format(page[0], page[1]),
    output: '@0@.@1@'.format(page[0], page[1]),
    configuration: man_conf,
  )
  install_man(man_page)
endforeach
install_data('polo.desktop',
  install_dir: get_option('datadir') / 'wayland-sessions')
# dconf reads only /etc/dconf/profile, whatever the prefix.
install_data('dconf/profile/polo',
  install_dir: '/etc/dconf/profile')
install_data('dconf/db/polo.d/00-polo',
  install_dir: '/etc/dconf/db/polo.d')
install_data('dconf/db/polo.d/locks/00-polo',
  install_dir: '/etc/dconf/db/polo.d/locks')
# The compiled database ships too, so a package works without a post-install step.
dconf_prog = find_program('dconf', required: false)
if dconf_prog.found()
  custom_target('dconf-db-polo',
    output: 'polo',
    command: [dconf_prog, 'compile', '@OUTPUT@', meson.current_source_dir() / 'dconf' / 'db' / 'polo.d'],
    depend_files: files('dconf/db/polo.d/00-polo', 'dconf/db/polo.d/locks/00-polo'),
    install: true,
    install_dir: '/etc/dconf/db')
else
  warning('dconf not found: /etc/dconf/db/polo is not built; packages must run "dconf update" after installing')
endif
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
