marco_lib = static_library('polo-marco',
  'boxes.c',
  'constraints.c',
  'edge-resistance.c',
  'hints.c',
  'marco.c',
  'place.c',
  'prefs.c',
  'stack-constraints.c',
  dependencies: [glib_dep, dependency('pango'), cc.find_library('m')],
)

polo_marco_dep = declare_dependency(
  link_with: marco_lib,
  include_directories: include_directories('.'),
  dependencies: [glib_dep, dependency('pango'), cc.find_library('m')],
)
