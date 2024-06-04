// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QVRATE_H__
#define __GST_QVRATE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstqvratevpp.h"

G_BEGIN_DECLS

typedef struct _GstQvrate GstQvrate;
typedef struct _GstQvrateClass GstQvrateClass;
typedef struct _GstQvrateMessage GstQvrateMessage;

#define GST_TYPE_QVRATE (gst_qvrate_get_type())

#define GST_QVRATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QVRATE,GstQvrate))
#define GST_QVRATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QVRATE,GstQvrate))
#define GST_QVRATE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_QVRATE,GstQvrate))
#define GST_IS_QVRATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QVRATE))
#define GST_IS_QVRATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QVRATE))
#define GST_QVRATE_CAST(obj) ((GstQvrate *)(obj))

typedef enum {
  GST_QVRATE_MESSAGE_INPUT_BUF_DONE,
  GST_QVRATE_MESSAGE_OUTPUT_BUF_DONE,
  GST_QVRATE_MESSAGE_FLUSH_DONE,
  GST_QVRATE_MESSAGE_DRAIN_DONE,
  GST_QVRATE_MESSAGE_ERROR
} GstQvrateMessageType;

struct _GstQvrateMessage {
  GstQvrateMessageType type;

  union {
    struct {
      struct vpp_buffer buf;
    } qvrate_vpp_buf;
    struct {
      struct vpp_event event;
    } qvrate_vpp_event;
  } content;
};

struct _GstQvrate {
  /*< private >*/
  GstElement     element;

  /*< protected >*/
  GstPad         *sinkpad;
  GstPad         *srcpad;

  GstVideoInfo in_info;
  GstVideoInfo out_info;

  /* vpp handle */
  void* vpp_ctx;

  gboolean in_dmabuf;
  gboolean out_dmabuf;

  gboolean in_ubwc;
  gboolean out_ubwc;

  guint32 in_req_cnt;
  guint32 out_req_cnt;
  GstBufferPool *pool;
  struct vpp_callbacks cb;
  GstSegment segment;
  guint32 vpp_buf_size;
  GThread *msg_thread;
  GQueue messages; /* Queue of GstQvrateMessages */
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
  guint64 frame_number;
};

struct _GstQvrateClass {
  GstElementClass element_class;
};

G_END_DECLS

#endif /* __GST_QVRATE_H__ */
