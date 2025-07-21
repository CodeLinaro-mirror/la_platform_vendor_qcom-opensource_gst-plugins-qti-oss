// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "gstqvidcutils.h"

GST_DEBUG_CATEGORY_EXTERN (qvidcutils_debug);
#define GST_CAT_DEFAULT qvidcutils_debug

GST_DEFINE_MINI_OBJECT_TYPE (GstVIDCComp, gst_vidc_comp);

static void gst_vidc_comp_free (GstVIDCComp * gst_vidc_comp);

GstVIDCComp *
gst_vidc_comp_create (void *comp)
{
  GstVIDCComp *gst_vidc_comp = NULL;

  gst_vidc_comp = g_new0 (GstVIDCComp, 1);
  if (gst_vidc_comp) {
    gst_vidc_comp->comp = comp;
    gst_mini_object_init (GST_MINI_OBJECT_CAST (gst_vidc_comp), 0,
        gst_vidc_comp_get_type (), NULL, NULL,
        (GstMiniObjectFreeFunction) gst_vidc_comp_free);

    GST_DEBUG ("gst vidc comp created");
  }

  return gst_vidc_comp;
}

static void
gst_vidc_comp_free (GstVIDCComp * gst_vidc_comp)
{
  if (gst_vidc_comp->comp) {
    vidc_delete (gst_vidc_comp->comp);
    GST_DEBUG ("vidc comp(%p) freed", gst_vidc_comp->comp);

    gst_vidc_comp->comp = NULL;
  }

  g_free (gst_vidc_comp);

  GST_DEBUG ("gst vidc comp freed");
}

GstVIDCComp *
gst_vidc_comp_ref (GstVIDCComp * comp)
{
  g_return_val_if_fail (comp, NULL);

  gint old_refcount = 0;

  old_refcount = g_atomic_int_get (&GST_MINI_OBJECT_CAST (comp)->refcount);

  GST_LOG ("gst vidc comp ref:%d->%d", old_refcount, old_refcount + 1);

  return (GstVIDCComp *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (comp));
}

void
gst_vidc_comp_unref (GstVIDCComp * comp)
{
  g_return_if_fail (comp);

  gint old_refcount = 0;

  old_refcount = g_atomic_int_get (&GST_MINI_OBJECT_CAST (comp)->refcount);

  GST_LOG ("gst vidc comp unref:%d->%d", old_refcount, old_refcount - 1);

  gst_mini_object_unref (GST_MINI_OBJECT_CAST (comp));
}

ConfigParams
make_framerate_param (gfloat framerate, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.isInput = is_input;
  param.config_name = CONFIG_FUNCTION_KEY_FRAMERATE;
  param.framerate = framerate;

  return param;
}

void gst_vidc_buffer_get_custom_meta(GstBuffer *buffer, const gchar *name,
    gint *meta_fd, guint *metasize)
{
  if (buffer && name) {
    GstCustomMeta *meta = gst_buffer_get_custom_meta (buffer, name);
    if (meta) {
      GstMemory *meta_mem = NULL;
      GstStructure *structure =
          gst_custom_meta_get_structure ((GstCustomMeta *) meta);
      if (structure &&
          gst_structure_get (structure, "meta-mem", GST_TYPE_MEMORY, &meta_mem, NULL)) {
        if (gst_is_dmabuf_memory (meta_mem)) {
          *meta_fd = gst_dmabuf_memory_get_fd (meta_mem);
        } else {
          *meta_fd = gst_fd_memory_get_fd (meta_mem);
        }

        gsize meta_offset = 0;
        gsize meta_maxsize = 0;
        gst_memory_get_sizes (meta_mem, &meta_offset, &meta_maxsize);
        *metasize = meta_maxsize - meta_offset;
        GST_DEBUG ("custom meta-mem %p, fd %d, metasize %d, maxsize %d, offset %d",
            meta_mem, *meta_fd, *metasize, meta_maxsize, meta_offset);

        gst_memory_unref (meta_mem);
      } else {
        GST_ERROR ("failed to get custom meta-mem");
      }
    }
  } else {
    GST_ERROR ("Invalid custom meta buffer %p, name %p", buffer, name);
  }
}
