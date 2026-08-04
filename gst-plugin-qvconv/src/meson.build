qvconv_sources = [
  'gstqvconv.cpp',
  'c2d_converter.cpp',
  'gstqvconvbufferpool.cpp',
  'gstqvconvbufmeta.cpp',
]

gstqvconv = library('gstqvconv',
  sources : qvconv_sources,
  cpp_args : [gst_plugin_qvconv_args, '-Wall'],
  link_args : gst_plugin_qvconv_link_args,
  include_directories : [qvconv_inc],
  dependencies : [gst_dep, dl_dep, pthread_dep, gstbase_dep, gstvideo_dep,  gstallocators_dep, glib_dep],
  install : true,
  install_dir : plugins_install_dir,
)

gstqvconv_dep = declare_dependency(link_with : gstqvconv, include_directories : qvconv_inc)
