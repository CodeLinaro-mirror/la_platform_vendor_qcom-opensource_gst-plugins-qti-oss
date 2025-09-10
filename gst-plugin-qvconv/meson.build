project('gst-plugin-qvconv-noship',
 'c',
  version : '1.0.0',
  meson_version : '>= 0.47',
  default_options : [ 'warning_level=1',
                    'buildtype=debugoptimized' ])

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')

have_cxx = add_languages('cpp', required : false)
cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')
host_system = host_machine.system()

use_mmmcolorfmt = get_option('mmmcolorfmt')
if use_mmmcolorfmt
  color_fmt_args = ['-DQVCONV_USE_MMM_COLOR_FMT']
else
  color_fmt_args = []
endif

use_umd = get_option('useumd')
if use_umd
  use_umd_args = ['-D_ENABLE_UMD_']
else
  use_umd_args = []
endif

if get_option('ASAN')
  asan_args = ['-fsanitize=address']
else
  asan_args = []
endif

gst_plugin_qvconv_noship_args = ['-DHAVE_CONFIG_H'] + color_fmt_args + use_umd_args + asan_args

gst_plugin_qvconv_noship_link_args = [ ]
gst_plugin_qvconv_noship_link_args += ['-lgbm']

core_conf = configuration_data()
core_conf.set_quoted('PACKAGE', 'gst-plugin-qvconv-noship')
core_conf.set_quoted('PACKAGE_NAME', 'gst-plugin-qvconv-noship')
core_conf.set_quoted('VERSION', '1.0.0')
configure_file(output : 'config.h', configuration : core_conf)

qvconv_inc = include_directories('.')

gst_dep = dependency('gstreamer-1.0',
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0',
  fallback : ['gstreamer', 'gst_base_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0',
  fallback : ['gst-plugins-base', 'video_dep'])
gstallocators_dep = dependency('gstreamer-allocators-1.0',
  fallback : ['gst-plugins-base', 'allocators_dep'])
glib_dep = dependency('glib-2.0', version: '>= 2.36', required: false)
dl_dep = cxx.find_library('dl')
pthread_dep = cxx.find_library('pthread')

subdir('src')
subdir('test')
