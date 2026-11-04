# libpolo-xsmp: an XSMP server on libSM/ICE, as mate-session's
# gsm-xsmp-server.c; the policy is src/session/xsmp.vala.
sm_dep = dependency('sm')
ice_dep = dependency('ice')

xsmp_lib = static_library('polo-xsmp',
  'xsmp-server.c',
  dependencies: [glib_dep, gobject_dep, gio_dep, sm_dep, ice_dep],
)

polo_xsmp_dep = declare_dependency(
  link_with: xsmp_lib,
  include_directories: include_directories('.'),
  dependencies: [glib_dep, gobject_dep, gio_dep],
)
