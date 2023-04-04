project('gst-plugin-qvrate', 'c',
  version : '1.0',
  meson_version : '>= 0.47',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')

gstqvrate_sources = [
  'gstqvrate.c',
  'gstqvratepool.c',
  'gstqvratevpp.c',
  'gstqvratedmabuf.c',
]

gstqvrate_c_args = ['-DHAVE_CONFIG_H']
gstqvrate_link_args = [ ]

gstqvrate_link_args += ['-lgbm']
gstqvrate_link_args += ['-L.', '-lhyp_vpp_fe']

configinc = include_directories('.')
core_conf = configuration_data()
core_conf.set_quoted('PACKAGE', 'gst-plugin-qvrate')
core_conf.set_quoted('PACKAGE_NAME', 'gst-plugin-qvrate')
core_conf.set_quoted('PACKAGE_VERSION', '1.0')
configure_file(output : 'config.h', configuration : core_conf)

gst_dep = dependency('gstreamer-1.0',
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0',
  fallback : ['gstreamer', 'gst_base_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0',
  fallback : ['gst-plugins-base', 'video_dep'])
gstallocators_dep = dependency('gstreamer-allocators-1.0',
  fallback : ['gst-plugins-base', 'allocators_dep'])
glib_dep = dependency('glib-2.0', version: '>= 2.44', required: false)

gstqvrate = library('gstqvrate',
  gstqvrate_sources,
  c_args : [gstqvrate_c_args, '-Wall'],
  link_args : [gstqvrate_link_args, '-ldl'],
  include_directories : [configinc],
  dependencies : [glib_dep, gst_dep, gstbase_dep, gstvideo_dep, gstallocators_dep],
  install : true,
  install_dir : plugins_install_dir,
)
