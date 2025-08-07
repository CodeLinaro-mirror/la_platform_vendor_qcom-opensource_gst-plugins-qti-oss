codec2wrapper_sources = [
  'C2ComponentAdapter.cpp',
  'C2ComponentInterfaceAdapter.cpp',
  'C2ComponentStoreAdapter.cpp',
  'C2WrapperUtils.cpp',
  'codec2wrapper.cpp',
]

codec2wrapper_inc = include_directories('include')
codec2_vndk_dep = cxx.find_library('codec2_vndk')
thread_dep = dependency('threads')
codec2wrapper_dep_others = [glib_dep, gstvideo_dep, gst_dep, codec2_vndk_dep, dl_dep, thread_dep]
if use_agl_c2service
  codec2_client_dep = [cxx.find_library('agl-c2client')]
  codec2wrapper_dep_others += codec2_client_dep
endif

codec2wrapper_args = ['-D_AGL_LINUX_' , '-DG_LOG_DOMAIN="codec2wrapper"' , '-std=c++20']
codec2wrapper_args += color_fmt_args + agl_c2service_args
use_dmaheap = get_option('usedmaheap')
if use_dmaheap
  codec2wrapper_args += ['-DUSE_DMAHEAP']
endif

qprange_op = get_option('qprange_option')
if qprange_op == 'op0'
  codec2wrapper_args += ['-DGST_QPRANGE_OPTION=0']
elif qprange_op == 'op1'
  codec2wrapper_args += ['-DGST_QPRANGE_OPTION=1']
endif

report_frame_qp_op = get_option('report_frame_qp_option')
if report_frame_qp_op == 'op0'
  codec2wrapper_args += ['-DGST_REPORT_FRAME_QP_OPTION=0']
elif report_frame_qp_op == 'op1'
  codec2wrapper_args += ['-DGST_REPORT_FRAME_QP_OPTION=1']
endif

codec2wrapper = library('codec2wrapper',
  codec2wrapper_sources,
  cpp_args : codec2wrapper_args,
  include_directories: [codec2wrapper_inc],
  dependencies : codec2wrapper_dep_others,
  install : true,
)

codec2wrapper_dep = declare_dependency(link_with : codec2wrapper, include_directories : codec2wrapper_inc)
