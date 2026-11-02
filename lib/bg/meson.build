# libpolo-bg: MATE desktop backgrounds, ported from mate-desktop's
# mate-bg.c. Pure: glib, gdk-pixbuf and pango only (tools/check-boundary.py
# rule 3).
gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0')
pango_dep = dependency('pango')

bg_lib = static_library('polo-bg',
  'bg.c',
  dependencies: [glib_dep, gdk_pixbuf_dep, pango_dep, cc.find_library('m')],
)

polo_bg_dep = declare_dependency(
  link_with: bg_lib,
  include_directories: include_directories('.'),
  dependencies: [glib_dep, gdk_pixbuf_dep, pango_dep, cc.find_library('m')],
)
