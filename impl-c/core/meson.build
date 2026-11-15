core_inc = include_directories('include')
core_sources = files('src/crc32c.c', 'src/wire.c', 'src/tx.c', 'src/dtag.c', 'src/retire.c', 'src/rx_common.c', 'src/counters.c', 'src/rx_ordered.c', 'src/rx_tolerant.c')
schcvx_core = static_library('schcvx_core', core_sources,
  include_directories: core_inc)
schcvx_core_dep = declare_dependency(link_with: schcvx_core,
  include_directories: core_inc)
