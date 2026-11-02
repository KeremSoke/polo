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
