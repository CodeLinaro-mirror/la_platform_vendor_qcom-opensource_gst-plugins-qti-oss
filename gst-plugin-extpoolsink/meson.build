project('gst-plugin-extpoolsink', 'c',
  version : '1.0',
  meson_version : '>= 0.47',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')

gstextpoolsink_sources = [
  'gstextpoolsink.c',
  'gstextpool.c',
  'extdmabuf.c',
]

use_mmmcolorfmt = get_option('mmmcolorfmt')
if use_mmmcolorfmt
  color_fmt_args = ['-DGST_USE_MMM_COLOR_FMT']
else
  color_fmt_args = []
endif


use_umd = get_option('useumd')
if use_umd
  use_umd_args = ['-D_ENABLE_UMD_']
else
  use_umd_args = []
endif

gstextpoolsink_c_args = ['-DHAVE_CONFIG_H'] + color_fmt_args + use_umd_args
gstextpoolsink_link_args = [ ]

# comment out below 5 lines for testing on Ubuntu
gstextpoolsink_c_args += ['-DQTI_PLATFORM']
gstextpoolsink_c_args += ['-DUSE_GBM']
#gstextpoolsink_link_args += ['-lgbm']

if get_option('ASAN')
gstextpoolsink_c_args += ['-fsanitize=address']
endif

configinc = include_directories('.')
core_conf = configuration_data()
core_conf.set_quoted('PACKAGE', 'gst-plugin-extpoolsink')
core_conf.set_quoted('PACKAGE_NAME', 'gst-plugin-extpoolsink')
core_conf.set_quoted('PACKAGE_VERSION', '1.0')
configure_file(output : 'config.h', configuration : core_conf)

#gbm_dep = dependency('gbm')
#gpudi_dep = dependency('gpudi')

gst_dep = dependency('gstreamer-1.0',
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0',
  fallback : ['gstreamer', 'gst_base_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0',
  fallback : ['gst-plugins-base', 'video_dep'])
gstallocators_dep = dependency('gstreamer-allocators-1.0',
  fallback : ['gst-plugins-base', 'allocators_dep'])
glib_dep = dependency('glib-2.0', version: '>= 2.44', required: false)

gstextpoolsink = library('gstextpoolsink',
  gstextpoolsink_sources,
  c_args : [gstextpoolsink_c_args, '-Wall'],
  link_args : [gstextpoolsink_link_args, '-ldl'],
  include_directories : [configinc],
  dependencies : [glib_dep, gst_dep, gstbase_dep, gstvideo_dep, gstallocators_dep],
  install : true,
  install_dir : plugins_install_dir,
)
