test_sources = ['test_crc32c.c', 'test_wire.c', 'test_dtag.c', 'test_retire.c', 'test_rx_ordered.c', 'test_rx_tolerant.c']
foreach source : test_sources
  name = source.substring(5).split('.')[0]
  exe = executable('test_' + name, source, dependencies: schcvx_core_dep)
  test(name, exe)
endforeach

vector_header = custom_target('wire_vectors',
  input: '../../draft/revised/wire-vectors.json',
  output: 'wire_vectors.h',
  command: [find_program('python3'), files('../tools/vectors_to_c.py'), '@INPUT@', '@OUTPUT@'])
vector_exe = executable('test_vectors', 'test_vectors.c', vector_header,
  dependencies: schcvx_core_dep)
test('vectors', vector_exe)

tx_exe = executable('test_tx', 'test_tx.c', vector_header, dependencies: schcvx_core_dep)
test('tx', tx_exe)

cc = meson.get_compiler('c')
fuzzer_available = cc.has_argument('-fsanitize=fuzzer') and cc.links('''
#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) { (void)data; (void)size; return 0; }
''', args: ['-fsanitize=fuzzer'], name: 'libFuzzer runtime')
if fuzzer_available
  fuzz_core = static_library('schcvx_fuzz_core', core_sources, include_directories: core_inc,
    c_args: ['-fsanitize=fuzzer-no-link'])
  fuzz_dep = declare_dependency(link_with: fuzz_core, include_directories: core_inc)
endif
fuzz_targets = [['classify', 'fuzz/classify.c', []],
                ['ordered', 'fuzz/receiver.c', []],
                ['tolerant', 'fuzz/receiver.c', ['-DFUZZ_TOLERANT']]]
foreach target : fuzz_targets
  smoke = executable('fuzz_smoke_' + target[0], [target[1], 'fuzz/driver.c'],
    c_args: target[2], dependencies: schcvx_core_dep)
  test('fuzz-smoke', smoke, args: ['1000'], timeout: 60)
  if fuzzer_available
    executable('fuzz_' + target[0], target[1], c_args: target[2] + ['-fsanitize=fuzzer'],
      link_args: ['-fsanitize=fuzzer'], dependencies: fuzz_dep)
  endif
endforeach

config_exe = executable('test_config', 'test_config.c', dependencies: schcvx_daemon_dep)
test('config', config_exe, args: [meson.project_source_root() / 'linux/schcvx.conf.example'])
daemon_offline_exe = executable('test_daemon_offline', 'test_daemon_offline.c', dependencies: schcvx_daemon_dep)
test('daemon-offline', daemon_offline_exe, args: [meson.project_source_root() / 'linux/schcvx.conf.example'])
