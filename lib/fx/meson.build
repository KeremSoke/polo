fx_lib = static_library('polo-fx',
  'fx.c',
  dependencies: [glib_dep],
)

polo_fx_dep = declare_dependency(
  link_with: fx_lib,
  include_directories: include_directories('.'),
  dependencies: [glib_dep],
)
