// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QVAIS_H__
#define __GST_QVAIS_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstqvaisvpp.h"

G_BEGIN_DECLS

typedef struct _GstQvais GstQvais;
typedef struct _GstQvaisClass GstQvaisClass;
typedef struct _GstQvaisMessage GstQvaisMessage;

#define GST_TYPE_QVAIS (gst_qvais_get_type())

#define GST_QVAIS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QVAIS,GstQvais))
#define GST_QVAIS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QVAIS,GstQvais))
#define GST_QVAIS_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_QVAIS,GstQvais))
#define GST_IS_QVAIS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QVAIS))
#define GST_IS_QVAIS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QVAIS))
#define GST_QVAIS_CAST(obj) ((GstQvais *)(obj))

typedef enum
{
  GST_QVAIS_MESSAGE_INPUT_BUF_DONE,
  GST_QVAIS_MESSAGE_OUTPUT_BUF_DONE,
  GST_QVAIS_MESSAGE_FLUSH_DONE,
  GST_QVAIS_MESSAGE_DRAIN_DONE,
  GST_QVAIS_MESSAGE_ERROR
} GstQvaisMessageType;

struct _GstQvaisMessage
{
  GstQvaisMessageType type;

  union
  {
    struct vpp_buffer buf;
    struct vpp_event event;
  } content;
};

struct _GstQvais
{
  /*< private > */
  GstElement element;

  /*< protected > */
  GstPad *sinkpad;
  GstPad *srcpad;

  GstVideoInfo in_info;
  GstVideoInfo out_info;

  /* vpp handle */
  void *vpp_ctx;

  gboolean in_dmabuf;
  gboolean out_dmabuf;

  gboolean in_ubwc;
  gboolean out_ubwc;

  guint32 in_req_cnt;
  guint32 out_req_cnt;
  GstBufferPool *pool;
  struct vpp_callbacks cb;
  GstSegment segment;
  guint32 in_vpp_buf_size;
  guint32 out_vpp_buf_size;
  GThread *msg_thread;
  GQueue messages;              /* Queue of GstQvaisMessages */
  GMutex messages_lock;
  GCond flush_cond;
  GMutex flush_lock;
  gboolean input_flushing;
  gboolean output_flushing;
  gboolean active;
  gboolean passthrough;
  /* negotiated caps */
  GstCaps *sink_caps;
  GstCaps *src_caps;
  gboolean eos;
  GCond drain_cond;
  GMutex drain_lock;
  GstTask *outbuf_task;
  GRecMutex outbuf_lock;
  guint32 classification;
  guint32 scale_ratio;
  guint64 frame_number;
};

struct _GstQvaisClass
{
  GstElementClass element_class;
};

G_END_DECLS
#endif /* __GST_QVAIS_H__ */
