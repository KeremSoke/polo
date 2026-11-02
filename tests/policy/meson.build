# Marco oracle replay (suite "policy").
json_glib_dep = dependency('json-glib-1.0')

replay = executable('replay', 'replay.vala',
  vala_args: vala_args + ['--pkg', 'polo-marco'],
  c_args: vala_c_args,
  dependencies: [polo_policy_dep, json_glib_dep],
)
test('marco-oracle', replay,
  args: ['--fixtures', meson.current_source_dir() / 'fixtures',
         '--passing', meson.current_source_dir() / 'passing.txt'],
  suite: 'policy',
  verbose: true,
)
