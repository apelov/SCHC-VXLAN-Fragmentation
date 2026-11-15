linux_inc = include_directories('.')
schcvx_daemon_lib = static_library('schcvx_daemon_portable', ['config.c', 'daemon.c'], dependencies: schcvx_core_dep)
schcvx_daemon_dep = declare_dependency(link_with: schcvx_daemon_lib, include_directories: linux_inc, dependencies: schcvx_core_dep)
# Linux-specific I/O is added separately and must be gated by host_machine.system().
