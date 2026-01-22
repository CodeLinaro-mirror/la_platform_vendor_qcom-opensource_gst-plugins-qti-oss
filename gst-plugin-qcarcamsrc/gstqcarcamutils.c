// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqcarcamutils.h"
#include "gstqcarcamdmabuf.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qcarcam_src_debug);
#define GST_CAT_DEFAULT gst_qcarcam_src_debug

/* Below is a simple GstQcarcamMeta implementation. */

#define GST_QCARCAM_META_API_TYPE (gst_qcarcam_meta_api_get_type())
#define GST_QCARCAM_META_INFO     (gst_qcarcam_meta_get_info())

#define gst_buffer_get_qcarcam_meta(b) \
    ((GstQcarcamMeta *)gst_buffer_get_meta((b),GST_QCARCAM_META_API_TYPE))

GType
gst_qcarcam_meta_api_get_type (void);
const GstMetaInfo *
gst_qcarcam_meta_get_info (void);

GType
gst_qcarcam_meta_api_get_type (void)
{
  static GType type = 0;

  if (g_once_init_enter (&type)) {
    static const gchar *tags[] = { NULL };
    GType _type = gst_meta_api_type_register ("GstQcarcamMetaAPI", tags);
    SG_INFO ("type %" G_GSIZE_FORMAT, (gsize) _type);
    g_once_init_leave (&type, _type);
  }

  return type;
}

static gboolean
gst_qcarcam_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstQcarcamMeta *emeta = (GstQcarcamMeta *) meta;

  emeta->desc = NULL;

  return TRUE;
}

const GstMetaInfo *
gst_qcarcam_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *mi = gst_meta_register (GST_QCARCAM_META_API_TYPE,
        "GstQcarcamMeta", sizeof (GstQcarcamMeta),
        gst_qcarcam_meta_init, NULL, NULL);
    SG_INFO_LITE ("meta info %p", mi);
    g_once_init_leave (&meta_info, mi);
  }

  return meta_info;
}

DmaBufDesc *
gst_qcarcam_meta_get_desc (GstBuffer * buffer)
{
  GstQcarcamMeta *meta = gst_buffer_get_qcarcam_meta(buffer);
  return meta ? meta->desc : NULL;
}

GstQcarcamMeta *
_add_qcarcam_meta (GstBuffer * buffer, DmaBufDesc * desc)
{
  GstQcarcamMeta *meta;

  GST_DEBUG ("buffer %p, desc %p", buffer, desc);

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (desc != NULL, NULL);

  meta = (GstQcarcamMeta *) gst_buffer_add_meta (buffer,
      GST_QCARCAM_META_INFO, NULL);
  g_return_val_if_fail (meta != NULL, NULL);

  meta->desc = desc;

  return meta;
}

static G_DEFINE_QUARK (FBufModifierQuark, gst_fbuf_modifier_qdata);

static void
_modifier_free (gpointer modifier)
{
  GST_DEBUG ("modifier %p", modifier);

  if (modifier)
    g_slice_free (guint64, modifier);
}

void
_modifier_attach (GstBuffer * buffer, DmaBufDesc * desc)
{
  GstMiniObject *mobject = GST_MINI_OBJECT_CAST (buffer);
  guint64 *modifier = g_slice_new (guint64);

  if (!modifier) {
    SG_ERR_LITE ("new modifier error");
    return;
  }

  *modifier = qcarcam_dmabuf_get_modifier (desc);
  gst_mini_object_set_qdata (mobject, gst_fbuf_modifier_qdata_quark (),
      modifier, _modifier_free);

  GST_DEBUG ("modifier %p, value 0x%lx, gstbuf %p",
      modifier, *modifier, buffer);
}

static inline gboolean
do_dmabuf_free (GstBuffer * buffer)
{
  GstMemory *mem = gst_buffer_peek_memory (buffer, 0);
  gint mem_fd = gst_dmabuf_memory_get_fd (mem);
  GstQcarcamMeta *meta = gst_buffer_get_qcarcam_meta (buffer);
  DmaBufDesc *desc;
  gint desc_fd;

  GST_DEBUG ("buffer %p, fd %d", buffer, mem_fd);

  g_return_val_if_fail (meta != NULL, FALSE);

  desc = (DmaBufDesc *) meta->desc;
  g_return_val_if_fail (desc != NULL, FALSE);

  desc_fd = qcarcam_dmabuf_get_fd (desc);
  GST_DEBUG ("desc %p, fd %d", desc, desc_fd);

  g_return_val_if_fail (mem_fd == desc_fd, FALSE);

  qcarcam_dmabuf_free (desc);

  return TRUE;
}
