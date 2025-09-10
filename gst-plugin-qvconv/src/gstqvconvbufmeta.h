// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QVCONVEXTBUF_META_H__
#define __GST_QVCONVEXTBUF_META_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_QVCONV_EXTBUF_META_API_TYPE (gst_qvconv_extbuf_meta_api_get_type())
#define GST_QVCONV_EXTBUF_META_INFO  (gst_qvconv_extbuf_meta_get_info())
typedef struct _GstQvconvExtBufMeta GstQvconvExtBufMeta;

struct _GstQvconvExtBufMeta {
  GstMeta meta;

  gint fd;
  gint handle;
  gpointer ptr;
  gint size;
};

GType gst_qvconv_extbuf_meta_api_get_type (void);
const GstMetaInfo * gst_qvconv_extbuf_meta_get_info (void);

#define gst_buffer_get_qvconv_extbuf_meta(b)     \
   ((GstQvconvExtBufMeta*)gst_buffer_get_meta((b), GST_QVCONV_EXTBUF_META_API_TYPE))

GstQvconvExtBufMeta * gst_buffer_add_qvconv_extbuf_meta (GstBuffer *buffer, gint fd,
    gint handle, gpointer ptr, gint size);

G_END_DECLS
#endif /* __GST_QVCONVEXTBUF_META_H__*/
