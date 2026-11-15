test_sources = ['test_crc32c.c', 'test_wire.c', 'test_dtag.c']
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
