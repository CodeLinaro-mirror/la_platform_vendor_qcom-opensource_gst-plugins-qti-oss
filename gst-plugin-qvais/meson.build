project('gst-plugin-qvais', 'c',
  version : '1.0',
  meson_version : '>= 0.47',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')

gstqvais_sources = [
  'gstqvais.c',
  'gstqvaispool.c',
  'gstqvaisvpp.c',
  'gstqvaisdmabuf.c',
]

gstqvais_c_args = ['-DHAVE_CONFIG_H']
gstqvais_link_args = [ ]
code_coverage_args = [ ]
code_coverage_link_args = [ ]

gstqvais_link_args += ['-lgbm']
gstqvais_link_args += ['-L.', '-lhyp_vpp_fe']

configinc = include_directories('.')
core_conf = configuration_data()
core_conf.set_quoted('PACKAGE', 'gst-plugin-qvais')
core_conf.set_quoted('PACKAGE_NAME', 'gst-plugin-qvais')
core_conf.set_quoted('PACKAGE_VERSION', '1.0')
configure_file(output : 'config.h', configuration : core_conf)

code_coverage = get_option('code-coverage')
if code_coverage
  code_coverage_args = ['-O0', '--coverage', '-fprofile-arcs', '-ftest-coverage']
  code_coverage_link_args = ['--coverage', '-lgcov', '-fprofile-arcs', '-ftest-coverage']
endif

gst_dep = dependency('gstreamer-1.0',
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0',
  fallback : ['gstreamer', 'gst_base_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0',
  fallback : ['gst-plugins-base', 'video_dep'])
gstallocators_dep = dependency('gstreamer-allocators-1.0',
  fallback : ['gst-plugins-base', 'allocators_dep'])
glib_dep = dependency('glib-2.0', version: '>= 2.44', required: false)

gstqvais = library('gstqvais',
  gstqvais_sources,
  c_args : [gstqvais_c_args, '-Wall', code_coverage_args],
  link_args : [gstqvais_link_args, '-ldl', code_coverage_link_args],
  include_directories : [configinc],
  dependencies : [glib_dep, gst_dep, gstbase_dep, gstvideo_dep, gstallocators_dep],
  install : true,
  install_dir : plugins_install_dir,
)
