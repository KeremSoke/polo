# C unit tests for the C libraries (suite unit-c).

# Marco's own boxes test, ported verbatim. It includes randomized
# runs and aborts on the first failed assertion.
testboxes = executable('testboxes', 'testboxes.c',
  dependencies: [polo_marco_dep, cc.find_library('m')],
)
test('testboxes', testboxes, suite: 'unit-c')

# libpolo-bg: MateBG drawing semantics.
test_bg = executable('test-bg', 'test-bg.c', dependencies: [polo_bg_dep])
test('bg', test_bg, suite: 'unit-c', protocol: 'tap', args: ['--tap'])

test_theme = executable('test-theme', 'test-theme.c', dependencies: [polo_marco_dep])
test('theme', test_theme, suite: 'unit-c', protocol: 'tap', args: ['--tap'], timeout: 120)

test_wobbly = executable('test-wobbly', 'wobbly.c',
  dependencies: [polo_fx_dep, cc.find_library('m')],
)
test('wobbly', test_wobbly, suite: 'unit-c')

test_magic_lamp = executable('test-magic-lamp', 'magic-lamp.c',
  dependencies: [polo_fx_dep, cc.find_library('m')],
)
test('magic-lamp', test_magic_lamp, suite: 'unit-c', protocol: 'tap', args: ['--tap'])

test_blur = executable('test-blur', 'blur.c', dependencies: [polo_fx_dep])
test('blur', test_blur, suite: 'unit-c', protocol: 'tap', args: ['--tap'])

test_cube = executable('test-cube', 'cube.c',
  dependencies: [polo_fx_dep, cc.find_library('m')],
)
test('cube', test_cube, suite: 'unit-c', protocol: 'tap', args: ['--tap'])

test_squeezimize = executable('test-squeezimize', 'squeezimize.c',
  dependencies: [polo_fx_dep, cc.find_library('m')],
)
test('squeezimize', test_squeezimize, suite: 'unit-c', protocol: 'tap', args: ['--tap'])
