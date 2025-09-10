// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "gst/video/gstvideometa.h"
#include "gstqvconvbufmeta.h"

#include "gst/gstinfo.h"
GST_DEBUG_CATEGORY_EXTERN (gst_qvconv_debug);
#define GST_CAT_DEFAULT gst_qvconv_debug

static gboolean
qvconv_extbuf_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstQvconvExtBufMeta *dmeta, *smeta;

  smeta = (GstQvconvExtBufMeta *) meta;
  if (GST_META_TRANSFORM_IS_COPY (type)) {
    GstMetaTransformCopy *copy = (GstMetaTransformCopy *) data;
    if (!copy->region) {
      dmeta = (GstQvconvExtBufMeta *) gst_buffer_add_meta (dest,
          GST_QVCONV_EXTBUF_META_INFO, NULL);

      if (!dmeta)
        return FALSE;

      GST_DEBUG ("copy qvconv extbuf meta");
      dmeta->fd = smeta->fd;
      dmeta->handle = smeta->handle;
      dmeta->ptr = smeta->ptr;
      dmeta->size = smeta->size;
    }
  }
  return TRUE;
}

static gboolean
gst_qvconv_extbuf_meta_init (GstQvconvExtBufMeta * meta, gpointer params, GstBuffer * buffer)
{
  return TRUE;
}

GType
gst_qvconv_extbuf_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { GST_META_TAG_VIDEO_STR,
      GST_META_TAG_MEMORY_STR, NULL };
  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("QVCONV_EXTBUF_META_API", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_qvconv_extbuf_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *meta =
      gst_meta_register (GST_QVCONV_EXTBUF_META_API_TYPE, "GstQvconvExtBufMeta",
      sizeof (GstQvconvExtBufMeta), (GstMetaInitFunction) gst_qvconv_extbuf_meta_init,
      (GstMetaFreeFunction) NULL, qvconv_extbuf_meta_transform);
    g_once_init_leave (&meta_info, meta);
  }
  return meta_info;
}

GstQvconvExtBufMeta *
gst_buffer_add_qvconv_extbuf_meta (GstBuffer * buffer,
    gint fd, gint handle, gpointer ptr, gint size)
{
  GstQvconvExtBufMeta *meta;

  meta = (GstQvconvExtBufMeta *) gst_buffer_add_meta (buffer,
      GST_QVCONV_EXTBUF_META_INFO, NULL);

  if (!meta)
    return NULL;

  GST_LOG ("adding QvconvExtBufMeta with fd:%d handle:%d ptr:%p size:%d",
      fd, handle, ptr, size);

  meta->fd = fd;
  meta->handle = handle;
  meta->ptr = ptr;
  meta->size = size;

  return meta;
}
