project('gst-plugin-qvrate', 'c',
  version : '1.0',
  meson_version : '>= 0.47',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')

gstqeavb_sources = [
  'qeavb/gstqeavb.c',
  'qeavb/gstqeavbcommon.c',
  'qeavb/gstqeavbpcmsrc.c',
  'qeavb/gstqeavbtssrc.c',
]

gstqeavb_c_args = ['-DHAVE_CONFIG_H']
gstqeavb_link_args = [ ]

gstqeavb_link_args += ['-leavbfe']

configinc = include_directories('.')
core_conf = configuration_data()
core_conf.set_quoted('PACKAGE', 'gst-plugin-qeavb')
core_conf.set_quoted('PACKAGE_NAME', 'gst-plugin-qeavb')
core_conf.set_quoted('PACKAGE_VERSION', '1.0')
configure_file(output : 'config.h', configuration : core_conf)

gst_dep = dependency('gstreamer-1.0',
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0',
  fallback : ['gstreamer', 'gst_base_dep'])
glib_dep = dependency('glib-2.0', version: '>= 2.44', required: false)

gstqeavb = library('gstqeavb',
  gstqeavb_sources,
  c_args : [gstqeavb_c_args, '-Wall'],
  link_args : [gstqeavb_link_args, '-ldl'],
  include_directories : [configinc],
  dependencies : [glib_dep, gst_dep, gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
