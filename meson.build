project('polo', ['vala', 'c'],
  version: '1.0.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 1.4.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'buildtype=debugoptimized',
  ],
)

valac = meson.get_compiler('vala')
if valac.version().version_compare('< 0.56')
  error('Polo needs valac >= 0.56 (found @0@)'.format(valac.version()))
endif

cc = meson.get_compiler('c')
strict = get_option('strict')

# Polo bundles a private, patched libweston. Everything it
# installs goes under $libdir/polo so it never meets the distribution's
# libweston or /usr/bin/weston.
polo_private_libdir = get_option('libdir') / 'polo'
polo_private_rpath = get_option('prefix') / polo_private_libdir

weston_proj = subproject('weston',
  default_options: [
    'libweston-only=true',
    'private-libdir=' + polo_private_libdir,
    'backend-drm=true',
    'backend-headless=true',
    'backend-wayland=true',
    'backend-pipewire=false',
    'backend-rdp=false',
    'backend-vnc=false',
    'backend-x11=false',
    'backend-default=drm',
    'renderer-gl=true',
    'renderer-vulkan=false',
    'xwayland=false',            # Polo vendors and owns the XWM
    'systemd=false',
    'deprecated-remoting=false',
    'deprecated-pipewire=false',
    'shell-desktop=false',
    'shell-ivi=false',
    'shell-kiosk=false',
    'shell-lua=false',
    'color-management-lcms=true',
    'image-jpeg=false',
    'image-webp=false',
    'tools=[]',
    'demo-clients=false',
    'simple-clients=[]',
    'tests=false',
    'test-junit-xml=false',
    'doc=false',
    'perfetto=false',
    'werror=false',
  ],
)
# -isystem: libweston's headers are not -Wpedantic clean under C11.
libweston_dep = weston_proj.get_variable('dep_libweston_public').as_system()

glib_dep = dependency('glib-2.0', version: '>= 2.80')
gobject_dep = dependency('gobject-2.0', version: '>= 2.80')
gio_dep = dependency('gio-2.0', version: '>= 2.80')
gio_unix_dep = dependency('gio-unix-2.0', version: '>= 2.80')

vapi_dir = meson.current_source_dir() / 'vapi'
add_project_arguments('--vapidir', vapi_dir, language: 'vala')
# '#line' directives so gdb, ASan and backtraces point at .vala lines.
add_project_arguments('--debug', language: 'vala')
add_project_arguments('-DG_LOG_DOMAIN="Polo"', '-D_GNU_SOURCE', language: 'c')
# Wayland and libweston callbacks rarely use every parameter (Weston does
# the same).
add_project_arguments('-Wno-unused-parameter', language: 'c')

# A VAPI that disagrees with its C header must not build.
# valac's output is otherwise compiled with warnings off; these specific
# mismatches are always errors, for every Vala target.
vala_c_args = [
  '-w',
  '-Werror=incompatible-pointer-types',
  '-Werror=discarded-qualifiers',
  '-Werror=implicit-function-declaration',
  '-Werror=int-conversion',
  '-Werror=return-type',
]
vala_args = []
if strict
  add_project_arguments('-Werror', language: 'c')
  vala_args += '--fatal-warnings'
endif

conf = configuration_data()
conf.set_quoted('POLO_VERSION', meson.project_version())
conf.set_quoted('POLO_LIBEXECDIR', get_option('prefix') / get_option('libexecdir'))
conf.set_quoted('POLO_PKGDATADIR', get_option('prefix') / get_option('datadir') / 'polo')

portal_wlr = get_option('xdg-desktop-portal-wlr')
if portal_wlr == ''
  found = find_program('xdg-desktop-portal-wlr', required: false,
    dirs: ['/usr/libexec', '/usr/lib/xdg-desktop-portal-wlr', '/usr/lib', '/usr/local/libexec',
           get_option('prefix') / get_option('libexecdir')])
  portal_wlr = found.found() ? found.full_path() : ''
endif
conf.set_quoted('POLO_PORTAL_WLR', portal_wlr)
runtime = {'xdg-desktop-portal-wlr (screen sharing)': portal_wlr != ''}
foreach name, purpose : {'grim': 'screenshot keys', 'Xwayland': 'X11 applications',
                         'wl-copy': 'screenshots to the clipboard', 'slurp': 'area screenshots'}
  runtime += {'@0@ (@1@)'.format(name, purpose): find_program(name, required: false).found()}
endforeach
if portal_wlr == ''
  warning('xdg-desktop-portal-wlr not found: screen sharing will not work until it is installed')
endif
if not find_program('grim', required: false).found()
  warning('grim not found: the screenshot keys will not work until it is installed')
endif
if get_option('xwayland') and not find_program('Xwayland', required: false).found()
  warning('Xwayland not found: X11 applications will not run until it is installed')
endif
config_h = configure_file(output: 'polo-config.h', configuration: conf)
config_inc = include_directories('.')

subdir('lib')
subdir('data')
subdir('src')
if get_option('shell-ui')
  subdir('shell-ui')
endif

# libweston dlopens its backends and renderers from the install directory
# compiled into it. Uninstalled runs (meson devenv, tests) point it at the
# build tree instead.
weston_build = meson.project_build_root() / 'subprojects' / 'weston' / 'libweston'
weston_module_map = ';'.join([
  'drm-backend.so=' + weston_build / 'backend-drm' / 'drm-backend.so',
  'headless-backend.so=' + weston_build / 'backend-headless' / 'headless-backend.so',
  'wayland-backend.so=' + weston_build / 'backend-wayland' / 'wayland-backend.so',
  'gl-renderer.so=' + weston_build / 'renderer-gl' / 'gl-renderer.so',
  'color-lcms.so=' + weston_build / 'color-lcms' / 'color-lcms.so',
])
devenv = environment()
devenv.set('WESTON_MODULE_MAP', weston_module_map)
devenv.prepend('PATH', meson.project_build_root() / 'src')
devenv.prepend('PATH', meson.project_build_root() / 'data')
devenv.set('POLO_DATA_DIR', meson.project_source_root() / 'data')
devenv.set('POLO_SHARE_CHOOSER', meson.project_build_root() / 'src' / 'polo-share-chooser')
if get_option('shell-ui')
  devenv.set('POLO_SHELL_UI', meson.project_build_root() / 'shell-ui' / 'polo-shell-ui')
endif
meson.add_devenv(devenv)
meson.add_dist_script('tools/dist-patch-subprojects.py')

if get_option('tests')
  subdir('tests')
endif

summary({
  'libweston': '@0@ (private, @1@)'.format(
    weston_proj.get_variable('version_weston'), polo_private_libdir),
  'xwayland': get_option('xwayland'),
  'shell-ui': get_option('shell-ui'),
  'test-protocol': get_option('test-protocol'),
  'strict': strict,
}, section: 'Polo')
summary(runtime, section: 'Runtime', bool_yn: true)
