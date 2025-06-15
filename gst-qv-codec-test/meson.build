project('gst-qv-codec-test', 'c',
  version : '1.0',
  meson_version : '>= 0.47',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

# Dependencies
gst_dep = dependency('gstreamer-1.0',
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0',
  fallback : ['gstreamer', 'gst_base_dep'])
glib_dep = dependency('glib-2.0', version: '>= 2.44', required: false)

# Source files
test_seek_sources = files('test_seek.c')

# Executable
test_seek = executable('test_seek',
  test_seek_sources,
  dependencies: [gst_dep, gstbase_dep, glib_dep],
  install: true,
  install_dir: get_option('bindir')
)
