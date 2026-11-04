python = find_program('python3')

subdir('c')
subdir('vala')
subdir('policy')

# VAPI coverage: always --fatal-warnings, and the
# generated C must agree with the headers (vala_c_args).
vapi_tests = {
  'backend': [polo_backend_dep, ['--pkg', 'polo-backend', '--pkg', 'posix']],
  'marco': [polo_marco_dep, ['--pkg', 'polo-marco']],
  'fx': [polo_fx_dep, ['--pkg', 'polo-fx']],
  'bg': [polo_bg_dep, ['--pkg', 'polo-bg']],
}
if get_option('xwayland')
  vapi_tests += {'xwm': [polo_xwm_dep, ['--pkg', 'polo-xwm', '--pkg', 'polo-backend', '--pkg', 'posix', '--pkg', 'gio-2.0']]}
  x_test_client = executable('x-test-client', 'xwm/x-test-client.c', dependencies: dependency('xcb'))
  x_send = executable('x-send', 'xwm/x-send.c', dependencies: dependency('xcb'))
  x_clip = executable('x-clip', 'xwm/x-clip.c', dependencies: dependency('xcb'))
  x_dnd = executable('x-dnd', 'xwm/x-dnd.c', dependencies: dependency('xcb'))
endif
vapi_test_env = environment()
vapi_test_env.set('WESTON_MODULE_MAP', weston_module_map)
foreach name, spec : vapi_tests
  exe = executable('vapi-' + name,
    'vapi' / name + '.vala',
    vala_args: ['--fatal-warnings'] + spec[1],
    c_args: vala_c_args,
    dependencies: [glib_dep, gobject_dep, gio_dep, spec[0]],
  )
  test('vapi-' + name, exe, suite: 'vapi', env: vapi_test_env,
    args: name == 'xwm' ? [x_test_client.full_path()] : [],
    depends: name == 'xwm' ? [x_test_client] : [],
  )
endforeach

test('session', python,
  args: [files('session/test-session.py'), polo_exe, get_option('systemd') ? 'systemd' : 'no-systemd'],
  env: vapi_test_env,
  suite: 'session',
  timeout: 90,
  is_parallel: false,
)

test('sm-abi', python,
  args: [files('session/test-sm-abi.py'), polo_exe, meson.project_source_root() / 'data' / 'dbus' / 'fixtures'],
  env: vapi_test_env,
  suite: 'session',
  timeout: 60,
  is_parallel: false,
  should_fail: true,
)

test('sm-manager', python,
  args: [files('session/test-sm-manager.py'), polo_exe],
  env: vapi_test_env,
  suite: 'session',
  timeout: 60,
  is_parallel: false,
)

autostart_dump = executable('autostart-dump',
  'session/autostart-dump.vala', meson.project_source_root() / 'src/session/autostart.vala',
  vala_args: vala_args,
  c_args: vala_c_args,
  dependencies: [glib_dep, gobject_dep, gio_dep],
)
test('autostart', python,
  args: [files('session/test-autostart.py'), autostart_dump],
  suite: 'session',
  timeout: 60,
)

test('binary', python,
  args: [files('check-binary.py'), polo_exe, meson.project_build_root()],
  suite: 'unit-vala',
)
test('logging', python,
  args: [files('check-logging.py'), polo_exe],
  suite: 'unit-vala',
)

if get_option('test-protocol')
  subdir('protocol')
endif

if get_option('xwayland') and get_option('test-protocol')
  map_client = executable('map-client', 'session/map-client.c', dependencies: harness_dep)
  phases_env = environment()
  phases_env.set('WESTON_MODULE_MAP', weston_module_map)
  phases_env.set('GSETTINGS_SCHEMA_DIR', meson.project_build_root() / 'data')
  test('phases', python,
    args: [files('session/test-phases.py'), polo_exe, map_client, x_test_client, get_option('shell-ui') ? shell_ui_exe.full_path() : ''],
    env: phases_env,
    depends: get_option('shell-ui') ? [polo_schemas, shell_ui_exe] : [polo_schemas],
    suite: 'session',
    timeout: 120,
    is_parallel: false,
  )
endif

# Boundary enforcement and its self-tests.
check_boundary = files('../tools/check-boundary.py')
test('boundary', python,
  args: [check_boundary, '--root', meson.project_source_root()],
  suite: 'vapi',
)
test('boundary-selftest-clean', python,
  args: [check_boundary, '--root', meson.current_source_dir() / 'boundary' / 'clean'],
  suite: 'vapi',
)
foreach case : [['rule1-namespace', '1'], ['rule1-package', '1'], ['rule2', '2'],
                ['rule3', '3'], ['rule4', '4']]
  test('boundary-selftest-' + case[0], python,
    args: [check_boundary, '--root', meson.current_source_dir() / 'boundary' / case[0],
           '--expect', case[1]],
    suite: 'vapi',
  )
endforeach

# Linters run when installed (always in CI).
clang_format = find_program('clang-format', required: false)
if clang_format.found()
  c_sources = run_command(python, '-c',
    'import pathlib,sys; print("\\n".join(str(p) for p in sorted(pathlib.Path(sys.argv[1]).rglob("*.[ch]"))))',
    meson.project_source_root() / 'lib', check: true).stdout().strip().split('\n')
  test('clang-format', clang_format,
    args: ['--dry-run', '--Werror'] + c_sources,
    suite: 'lint',
  )
endif

vala_lint = find_program('io.elementary.vala-lint', required: false)
if vala_lint.found()
  test('vala-lint', vala_lint,
    args: ['--config', meson.project_source_root() / 'vala-lint.conf',
           meson.project_source_root() / 'src', meson.project_source_root() / 'tests' / 'vapi'],
    suite: 'lint',
  )
endif
