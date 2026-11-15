test_sources = ['test_crc32c.c']
foreach source : test_sources
  name = source.substring(5).split('.')[0]
  exe = executable('test_' + name, source, dependencies: schcvx_core_dep)
  test(name, exe)
endforeach
