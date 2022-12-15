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

numa_dep = cc.find_library('numa', required: false)

cxl_tool = executable('cxl',
  cxl_src,
  include_directories : root_inc,
  dependencies : [
    cxl_dep,
    util_dep,
    uuid,
    kmod,
    json,
    versiondep,
    numa_dep,
  ],
  install : true,
  install_dir : rootbindir,
)

install_headers('libcxl.h', subdir : 'cxl')
