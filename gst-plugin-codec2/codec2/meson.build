gstqcodec2_sources = [
  'plugin.c',
  'common/gstqcodec2bufferpool.c',
  'common/gstqcodec2utils.c',
  'vdec/gstqcodec2h264dec.c',
  'vdec/gstqcodec2h265dec.c',
  'vdec/gstqcodec2vp9dec.c',
  'vdec/gstqcodec2vdec.c',
  'venc/gstqcodec2h264enc.c',
  'venc/gstqcodec2h265enc.c',
  'venc/gstqcodec2venc.c',
]

# venc depends on libxml-2.0
xml2_dep = dependency('libxml-2.0', version : '>= 2.8')

# compile codec2 wrapper as another lib
subdir('wrapper')

if get_option('av1-dec').enabled()
  gstqcodec2_sources += ['vdec/gstqcodec2av1dec.c']
  gst_plugin_codec2_args += ['-DGST_SUPPORT_AV1_DEC']
endif

if get_option('mpeg2-dec').enabled()
  gstqcodec2_sources += ['vdec/gstqcodec2mpeg2dec.c']
  gst_plugin_codec2_args += ['-DGST_SUPPORT_MPEG2_DEC']
endif

if get_option('set_dec_input_framerate')
  gst_plugin_codec2_args += ['-DSET_DEC_INPUT_FRAMERATE']
endif

gstqcodec2 = library('gstqcodec2',
  gstqcodec2_sources,
  c_args : gst_plugin_codec2_args,
  include_directories: [configinc, 'vdec', 'venc', 'common', 'wrapper/include'],
  dependencies : [codec2wrapper_dep, gst_dep, gstvideo_dep, gstallocators_dep, gstbase_dep, gstgl_dep,
                  dl_dep, gstcodecparsers_dep, xml2_dep],
  install : true,
  install_dir : plugins_install_dir,
)
