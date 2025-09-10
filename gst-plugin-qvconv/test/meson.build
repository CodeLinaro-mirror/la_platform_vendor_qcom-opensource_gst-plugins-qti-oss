qvconv_sources = [
  '../src/c2d_converter.cpp',
  'c2dtest.cpp',
]

executable('mm-c2d-test',
  sources : [qvconv_sources],
  cpp_args : [gst_plugin_qvconv_noship_args, '-Wall', '-DMM_C2D_UNIT_TEST'],
  link_args : gst_plugin_qvconv_noship_link_args,
  include_directories : ['../src'],
  dependencies : [gstqvconv_dep, glib_dep, pthread_dep, gst_dep, gstbase_dep, gstvideo_dep, dl_dep, gstallocators_dep],
  install : true,
)