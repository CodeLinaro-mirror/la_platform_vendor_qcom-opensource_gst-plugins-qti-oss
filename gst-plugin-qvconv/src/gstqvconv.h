// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef _GST_QVCONV_H_
#define _GST_QVCONV_H_

#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include "c2d_converter.h"
#include <gst/allocators/gstdmabuf.h>

G_BEGIN_DECLS

#define GST_TYPE_QVCONV   (gst_qvconv_get_type())
#define GST_QVCONV(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QVCONV,GstQvconv))
#define GST_QVCONV_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QVCONV,GstQvconvClass))
#define GST_IS_QVCONV(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QVCONV))
#define GST_IS_QVCONV_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QVCONV))

typedef enum {
  METHOD_NONE,
  METHOD_FLIP_H,
  METHOD_FLIP_V
} Gst_Qvconv_Method;

#define QVCONV_DUMP_C2D_BUFFER

#ifdef QVCONV_DUMP_C2D_BUFFER
typedef enum {
  DUMP_OPTION_NONE   = 0,
  DUMP_OPTION_INPUT  = 1,
  DUMP_OPTION_OUTPUT = 2,
  DUMP_OPTION_BOTH   = 3,
} GstQvconvDumpOption;
#endif /* QVCONV_DUMP_C2D_BUFFER */

typedef enum {
  QVCONV_CACHE_GPU_ADDR_NONE = 0,
  QVCONV_CACHE_GPU_ADDR_INT  = 1,
  QVCONV_CACHE_GPU_ADDR_EXT  = 2,
  QVCONV_CACHE_GPU_ADDR_BOTH = 3,
} GstQvconvCacheGpuAddr;

typedef struct _GstQvconv GstQvconv;
typedef struct _GstQvconvClass GstQvconvClass;
typedef struct _GstQvconvPrivate GstQvconvPrivate;
typedef struct _GstQvconvCrop GstQvconvCrop;

struct _GstQvconvCrop
{
   guint x;
   guint y;
   guint width;
   guint height;
};

struct _GstQvconv
{
  GstVideoFilter base_qvconv;
  C2dConverter *c2d_hndl;

  /* video info */
  GstVideoInfo src_info;
  GstVideoInfo dst_info;

  /*< private >*/
  GstQvconvPrivate *priv;
};

struct _GstQvconvClass
{
  GstVideoFilterClass parent_class;
};

struct _GstQvconvPrivate
{
  gint method;
  gboolean active;

  C2DBuffer input_buffer;
  /* own buffer pool for output buffer */
  GstBufferPool *pool;
  GMutex lock;
  GstQvconvCrop crop;

  gboolean ignore_downstream_pool;
  gboolean outubwc;
  gboolean input_nondma;
  int quality_indicator;

#ifdef QVCONV_DUMP_C2D_BUFFER
  /* option to dump C2D input/output buffer
   * 0: dump disabled as default
   * 1: dump input buffer
   * 2: dump output buffer
   * 3: dump input & output buffer
   */
  guint dump_option;
  guint dump_start;      /* start frame # to dump */
  guint dump_frames;     /* # of frames to dump */
  const gchar *dump_dir; /* directory to dump into */
  int   dump_fd_src;     /* fd to dump input buffers */
  int   dump_fd_dst;     /* fd to dump output buffers */
  guint frame_seqno;
  guint dumped_frames;
  gboolean dump_error;
#endif /* QVCONV_DUMP_C2D_BUFFER */

  guint cache_gpu_addr;
  gboolean input_buf_internal;
  gboolean output_buf_internal;

  guint execute_idx;  //just for log, it's a flag to indicate log missing
  guint idx_in_one_cycle;

  gboolean do_deinterlace;

  gboolean do_inputcopy;
};

GType gst_qvconv_get_type (void);

G_END_DECLS

#endif
