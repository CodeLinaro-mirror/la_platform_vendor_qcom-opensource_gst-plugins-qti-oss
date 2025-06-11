gstqvidc_sources = [
  'plugin.c',
  'common/gstqvidcallocator.c',
  'common/gstqvidcbufferpool.c',
  'common/gstqvidcutils.c',
  'vdec/gstqvidch264dec.c',
  'vdec/gstqvidch265dec.c',
  'vdec/gstqvidcmpeg2dec.c',
  'vdec/gstqvidcvp9dec.c',
  'vdec/gstqvidcvdec.c',
  'venc/gstqvidch264enc.c',
  'venc/gstqvidch265enc.c',
  'venc/gstqvidcvenc.c',
]

# venc depends on libxml-2.0
xml2_dep = dependency('libxml-2.0', version : '>= 2.8')

# kiumd dependency
kiumd_dep = cxx.find_library('kiumd')

# compile vidc wrapper as another lib
subdir('wrapper')

gst_plugin_vidc_args += ['-D_VIDC_LRH_LINUX_', '-D_VIDC_LRH_STUBS_']
if get_option('av1-dec').enabled()
  gstqvidc_sources += ['vdec/gstqvidcav1dec.c']
  gst_plugin_vidc_args += ['-DGST_SUPPORT_AV1_DEC']
endif

gstqvidc = library('gstqvidc',
  gstqvidc_sources,
  c_args : gst_plugin_vidc_args,
  include_directories: [configinc, 'vdec', 'venc', 'common', 'wrapper/include'],
  dependencies : [vidcwrapper_dep, gst_dep, gstvideo_dep, gstallocators_dep,
                  dl_dep, gstcodecparsers_dep, xml2_dep, kiumd_dep],
  install : true,
  install_dir : plugins_install_dir,
)
