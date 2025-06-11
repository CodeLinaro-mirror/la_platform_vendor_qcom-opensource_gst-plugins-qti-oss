vidcwrapper_sources = [
  'planeinfo.cpp',
  'threadclass.cpp',
  'vidcfactory.cpp',
  'baseclient.cpp',
  'gstclient.cpp',
  'vidcwrapper.cpp',
]

vidcwrapper_inc = include_directories('include')
thread_dep = dependency('threads')
planedef_dep = cxx.find_library('planedef')
vidcclient_dep = cxx.find_library('vidc_client')
mmosal_dep = cxx.find_library('mmosal')
vidcwrapper_dep_others = [glib_dep, gstvideo_dep, gst_dep, dl_dep, thread_dep, planedef_dep, mmosal_dep, vidcclient_dep]

vidcwrapper_args = ['-D_AGL_LINUX_' , '-DG_LOG_DOMAIN="vidcwrapper"' , '-std=c++17', '-D_VIDC_LRH_LINUX_', '-D_VIDC_LRH_STUBS_']
use_dmaheap = get_option('usedmaheap')
if use_dmaheap
  vidcwrapper_args += ['-DUSE_DMAHEAP']
endif

vidcwrapper = library('vidcwrapper',
  vidcwrapper_sources,
  cpp_args : vidcwrapper_args,
  include_directories: [vidcwrapper_inc],
  dependencies : vidcwrapper_dep_others,
  install : true,
)

vidcwrapper_dep = declare_dependency(link_with : vidcwrapper, include_directories : vidcwrapper_inc)
