cxl_src = [
  'cxl.c',
  'list.c',
  'port.c',
  'bus.c',
  'region.c',
  'memdev.c',
  'json.c',
  'filter.c',
  '../daxctl/json.c',
  '../daxctl/filter.c',
]

if get_option('systemd').enabled()
  install_data('cxl-monitor.service', install_dir : systemdunitdir)
endif

deps = [
  cxl_dep,
  daxctl_dep,
  util_dep,
  uuid,
  kmod,
  json,
  versiondep,
]

if get_option('libtracefs').enabled()
  cxl_src += [
    'event_trace.c',
    'monitor.c',
  ]
  deps += [
    traceevent,
    tracefs,
  ]
endif

if get_option('smdkplugin').enabled()
  cxl_src += [
    'smdk/smdk_memdev.c',
    'smdk/smdk_control.c',
    'smdk/smdk_lat_matrix.c',
  ]
  numa_dep = cc.find_library('numa', required: false)
  deps += [
    numa_dep,
  ]
endif

cxl_tool = executable('cxl',
  cxl_src,
  include_directories : root_inc,
  dependencies : deps,
  install : true,
  install_dir : rootbindir,
)

install_headers('libcxl.h', subdir : 'cxl')
