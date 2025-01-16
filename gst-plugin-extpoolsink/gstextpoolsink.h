// Copyright (c) 2022, 2025 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_EXTPOOLSINK_H__
#define __GST_EXTPOOLSINK_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

#ifdef GST_USE_MMM_COLOR_FMT
#include <media/mmm_color_fmt.h>

enum color_fmts {
        COLOR_FMT_NV12 = MMM_COLOR_FMT_NV12,
        COLOR_FMT_NV21 = MMM_COLOR_FMT_NV21,
        COLOR_FMT_NV12_UBWC = MMM_COLOR_FMT_NV12_UBWC,
        COLOR_FMT_NV12_BPP10_UBWC = MMM_COLOR_FMT_NV12_BPP10_UBWC,
        COLOR_FMT_RGBA8888 = MMM_COLOR_FMT_RGBA8888,
        COLOR_FMT_RGBA8888_UBWC = MMM_COLOR_FMT_RGBA8888_UBWC,
        COLOR_FMT_RGBA1010102_UBWC = MMM_COLOR_FMT_RGBA1010102_UBWC,
        COLOR_FMT_RGB565_UBWC = MMM_COLOR_FMT_RGB565_UBWC,
        COLOR_FMT_P010_UBWC = MMM_COLOR_FMT_P010_UBWC,
        COLOR_FMT_P010 = MMM_COLOR_FMT_P010,
        COLOR_FMT_NV12_512 = MMM_COLOR_FMT_NV12_512,
};

#define VENUS_Y_STRIDE MMM_COLOR_FMT_Y_STRIDE
#define VENUS_UV_STRIDE MMM_COLOR_FMT_UV_STRIDE
#define VENUS_Y_SCANLINES MMM_COLOR_FMT_Y_SCANLINES
#define VENUS_UV_SCANLINES MMM_COLOR_FMT_UV_SCANLINES
#define VENUS_Y_META_STRIDE MMM_COLOR_FMT_Y_META_STRIDE
#define VENUS_UV_META_STRIDE MMM_COLOR_FMT_UV_META_STRIDE
#define VENUS_Y_META_SCANLINES MMM_COLOR_FMT_Y_META_SCANLINES
#define VENUS_UV_META_SCANLINES MMM_COLOR_FMT_UV_META_SCANLINES
#define VENUS_BUFFER_SIZE MMM_COLOR_FMT_BUFFER_SIZE
#define VENUS_BUFFER_SIZE_USED MMM_COLOR_FMT_BUFFER_SIZE_USED

#else
#include <vidc/media/msm_media_info.h>
#endif

G_BEGIN_DECLS
#define GST_TYPE_EXTPOOLSINK (gst_ext_pool_sink_get_type())
G_DECLARE_FINAL_TYPE (GstExtPoolSink, gst_ext_pool_sink,
    GST, EXTPOOLSINK, GstVideoFilter)

//#define GST_IS_EXTPOOLSINK_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_EXTPOOLSINK))
//#define GST_EXTPOOLSINK_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_EXTPOOLSINK, GstExtPoolSinkClass))
//#define GST_EXTPOOLSINK_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_EXTPOOLSINK, GstExtPoolSinkClass))

struct _GstExtPoolSink {
  GstVideoFilter parent;

  /* gstbasetransform manages lifecycle of buffer pool totally.
   * use pool's aligned info to set up gpudi. */
  //GstBufferPool *pool; // not need this for it's stored in GstBaseTransform

  /* here are aligned info, info of caps are in GstVideoFilter */
  GstVideoInfo in_info;
  GstVideoInfo out_info;

  GstBuffer *ref_buf_held;

  gboolean active;
  gboolean silent;

  gboolean in_dmabuf;
  gboolean out_dmabuf;

  gboolean in_ubwc;
  gboolean out_ubwc;
};

G_END_DECLS
#endif /* __GST_EXTPOOLSINK_H__ */
