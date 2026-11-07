fx_lib = static_library('polo-fx',
  'blur.c',
  'cube.c',
  'fx.c',
  'grid.c',
  'magic-lamp.c',
  'squeezimize.c',
  'wobbly.c',
  'wobbly-presets.c',
  dependencies: [glib_dep, cc.find_library('m')],
)

polo_fx_dep = declare_dependency(
  link_with: fx_lib,
  include_directories: include_directories('.'),
  dependencies: [glib_dep],
)
