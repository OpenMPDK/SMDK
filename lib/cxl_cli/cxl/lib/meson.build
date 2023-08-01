libcxl_version = '@0@.@1@.@2@'.format(
  LIBCXL_CURRENT - LIBCXL_AGE,
  LIBCXL_REVISION,
  LIBCXL_AGE)

mapfile = files('libcxl.sym')
vflag = '-Wl,--version-script,@0@/@1@'.format(project_source_root, mapfile[0])

libcxl_src = [
  '../../util/sysfs.c',
  '../../util/log.c',
  '../../util/log.h',
  'libcxl.c',
]

if get_option('smdkplugin').enabled()
  libcxl_src += [
    'smdk/smdk_libcxl.c',
  ]
endif

cxl = library('cxl',
  libcxl_src,
  include_directories : root_inc,
  dependencies : [
    uuid,
    kmod,
    libudev,
    daxctl_dep,
  ],
  version : libcxl_version,
  install : true,
  install_dir : rootlibdir,
  link_args : vflag,
  link_depends : mapfile,
)
cxl_dep = declare_dependency(link_with : cxl)

custom_target(
  'libcxl.pc',
  command : pkgconfig_script + [ '@INPUT@' ],
  input : 'libcxl.pc.in',
  output : 'libcxl.pc',
  capture : true,
  install : true,
  install_dir : pkgconfiglibdir,
)
