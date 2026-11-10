# libFuzzer protocol fuzzers (suite fuzz; only with -Dfuzz=true and clang).
if cc.get_id() != 'clang'
  error('-Dfuzz=true needs clang: CC=clang CXX=clang++')
endif

fuzz_protocols = executable('fuzz-protocols', 'fuzz-protocols.c',
  c_args: ['-fsanitize=fuzzer-no-link'],
  link_args: ['-fsanitize=fuzzer'],
  include_directories: include_directories('../../lib/backend'),
  dependencies: backend_deps,
  link_with: [backend_lib, weston_proj.get_variable('lib_weston')],
)

# Each group is the interfaces one client binds; corpus/<group> holds its seeds.
fuzz_groups = {
  'layer-shell': ['zwlr_layer_shell_v1'],
  'foreign-toplevel': ['zwlr_foreign_toplevel_manager_v1', 'ext_foreign_toplevel_list_v1',
                       'xfce_foreign_toplevel_manager_private_v1'],
  'workspace': ['ext_workspace_manager_v1'],
  'session-lock': ['ext_session_lock_manager_v1'],
  'data-control': ['zwlr_data_control_manager_v1', 'ext_data_control_manager_v1'],
  'image-copy-capture': ['ext_output_image_capture_source_manager_v1',
                         'ext_foreign_toplevel_image_capture_source_manager_v1',
                         'ext_image_copy_capture_manager_v1'],
}

foreach name, interfaces : fuzz_groups
  env = environment()
  env.set('WESTON_MODULE_MAP', weston_module_map)
  env.set('POLO_FUZZ_PROTOCOLS', ','.join(interfaces))
  test('fuzz-' + name, fuzz_protocols,
    args: [meson.current_source_dir() / 'corpus' / name, '-runs=0', '-timeout=30'],
    env: env,
    suite: 'fuzz',
    timeout: 300,
    is_parallel: false,
  )
endforeach
