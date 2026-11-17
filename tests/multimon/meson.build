multimon_driver = executable('multimon-driver', 'driver.c', dependencies: harness_dep,
                             include_directories: include_directories('../protocol'))

multimon_env = environment()
multimon_env.set('WESTON_MODULE_MAP', weston_module_map)
multimon_env.set('MULTIMON_DRIVER', multimon_driver.full_path())
multimon_env.set('MULTIMON_POLO_SCHEMAS', meson.project_build_root() / 'data' + ':' +
                 meson.project_build_root() / 'tests' / 'schemas')
test('multimon', find_program('python3'),
  args: [files('run.py'), polo_exe],
  env: multimon_env,
  depends: multimon_driver,
  suite: 'multimon',
  timeout: 1800,
  is_parallel: false,
)
