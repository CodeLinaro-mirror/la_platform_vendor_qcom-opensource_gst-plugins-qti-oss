project('gst-plugin-qcarcamsrc', 'c',
  version : '1.0',
  meson_version : '>= 0.47',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')

gstqcarcamsrc_sources = [
  'gstqcarcamsrc.c',
  'gstqcarcamutils.c',
  'gstqcarcamqcx.c',
  'gstqcarcamdmabuf.c',
]

gstqcarcamsrc_c_args = ['-DHAVE_CONFIG_H']
gstqcarcamsrc_link_args = [ ]
use_umd = get_option('useumd')
if use_umd
  gstqcarcamsrc_link_args += ['-L.', '-lqcx','-lqcxclient', '-lqcxosal', '-lcutils', '-lqcxplatform']
  gstqcarcamsrc_c_args += ['-D_ENABLE_UMD_']
else
  gstqcarcamsrc_link_args += ['-L.', '-lqcxclient', '-lqcxosal']
endif



configinc = include_directories('.')
core_conf = configuration_data()
core_conf.set_quoted('PACKAGE', 'gst-plugin-qcarcamsrc')
core_conf.set_quoted('PACKAGE_NAME', 'gst-plugin-qcarcamsrc')
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

gstqcarcamsrc = library('gstqcarcamsrc',
  gstqcarcamsrc_sources,
  c_args : [gstqcarcamsrc_c_args, '-Wall', '-DGST_USE_MMM_COLOR_FMT'],
  link_args : [gstqcarcamsrc_link_args, '-ldl'],
  include_directories : [configinc],
  dependencies : [glib_dep, gst_dep, gstbase_dep, gstvideo_dep, gstallocators_dep],
  install : true,
  install_dir : plugins_install_dir,
)
