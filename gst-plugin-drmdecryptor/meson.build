project('gst-plugin-drmdecryptor', 'cpp',
  version : '1.0',
  meson_version : '>= 0.47',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')

gstdrmdecryptor_sources = [
  'drmdecryptor.cc',
  'drmdecryptor-engine.cc',
  'gstmempool.cc'
]

gstdrmdecryptor_cpp_args = ['-DHAVE_CONFIG_H']
gstdrmdecryptor_link_args = [ ]

configinc = include_directories('.')
core_conf = configuration_data()
core_conf.set_quoted('PACKAGE', '-')
core_conf.set_quoted('PACKAGE_VERSION', '1.0')
core_conf.set_quoted('PACKAGE_LICENSE', 'unknown')
core_conf.set_quoted('PACKAGE_SUMMARY', 'gst-plugin-drmdecryptor')
core_conf.set_quoted('PACKAGE_ORIGIN', '-')
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
cutils_dep = dependency('libcutils',
  fallback : ['libcutils', 'cutils_dep'])

gstdrmdecryptor_dep = [glib_dep, gst_dep, gstbase_dep, gstvideo_dep, gstallocators_dep, cutils_dep]

use_dmaheap = get_option('usedmaheap')
if use_dmaheap
  gstdrmdecryptor_cpp_args += ['-DHAVE_LINUX_DMA_HEAP_H']
endif

if get_option('widevine').enabled()
  gstdrmdecryptor_cpp_args += ['-DENABLE_WIDEVINE']
endif

if get_option('drm-optimization')
  gstdrmdecryptor_cpp_args += ['-DENABLE_DRM_OPTIMIZATION']
endif

if get_option('ASAN')
  gstdrmdecryptor_cpp_args += ['-fsanitize=address']
endif

gstdrmdecryptor = library('gstqtidrmdecryptor',
  gstdrmdecryptor_sources,
  cpp_args : [gstdrmdecryptor_cpp_args, '-Wall'],
  link_args : [gstdrmdecryptor_link_args, '-ldl'],
  include_directories : [configinc],
  dependencies : gstdrmdecryptor_dep,
  install : true,
  install_dir : plugins_install_dir,
)
