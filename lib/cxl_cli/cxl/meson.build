cxl_src = [
  'cxl.c',
  'list.c',
  'port.c',
  'bus.c',
  'region.c',
  'memdev.c',
  'smdk.c',
  'smdk_lat_matrix.c',
  'json.c',
  'filter.c',
]

if get_option('systemd').enabled()
  install_data('cxl-monitor.service', install_dir : systemdunitdir)
endif

numa_dep = cc.find_library('numa', required: false)
deps = [
  cxl_dep,
  util_dep,
  uuid,
  kmod,
  json,
  versiondep,
  numa_dep,
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

cxl_tool = executable('cxl',
  cxl_src,
  include_directories : root_inc,
  dependencies : deps,
  install : true,
  install_dir : rootbindir,
)

install_headers('libcxl.h', subdir : 'cxl')
