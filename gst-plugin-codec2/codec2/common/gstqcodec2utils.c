// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "gstqcodec2utils.h"
#include "codec2wrapper.h"

GST_DEBUG_CATEGORY_EXTERN (qcodec2utils_debug);
#define GST_CAT_DEFAULT qcodec2utils_debug

GST_DEFINE_MINI_OBJECT_TYPE (GstC2Comp, gst_c2_comp);

static void gst_c2_comp_free (GstC2Comp * gst_c2_comp);

GstC2Comp *
gst_c2_comp_create (void *comp)
{
  GstC2Comp *gst_c2_comp = NULL;

  gst_c2_comp = g_new0 (GstC2Comp, 1);
  if (gst_c2_comp) {
    gst_c2_comp->comp = comp;
    gst_mini_object_init (GST_MINI_OBJECT_CAST (gst_c2_comp), 0,
        gst_c2_comp_get_type (), NULL, NULL,
        (GstMiniObjectFreeFunction) gst_c2_comp_free);

    GST_DEBUG ("gst c2 comp created");
  }

  return gst_c2_comp;
}

static void
gst_c2_comp_free (GstC2Comp * gst_c2_comp)
{
  if (gst_c2_comp->comp) {
    c2component_delete (gst_c2_comp->comp);
    GST_DEBUG ("c2 comp(%p) freed", gst_c2_comp->comp);

    gst_c2_comp->comp = NULL;
  }

  g_free (gst_c2_comp);

  GST_DEBUG ("gst c2 comp freed");
}

void *
get_c2_comp (GstC2Comp * gst_c2_comp)
{
  void *c2_comp = NULL;

  if (gst_c2_comp) {
    c2_comp = gst_c2_comp->comp;
  }

  return c2_comp;
}

GstC2Comp *
gst_c2_comp_ref (GstC2Comp * comp)
{
  g_return_val_if_fail (comp, NULL);

  gint old_refcount = 0;

  old_refcount = g_atomic_int_get (&GST_MINI_OBJECT_CAST (comp)->refcount);

  GST_LOG ("gst c2 comp ref:%d->%d", old_refcount, old_refcount + 1);

  return (GstC2Comp *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (comp));
}

void
gst_c2_comp_unref (GstC2Comp * comp)
{
  g_return_if_fail (comp);

  gint old_refcount = 0;

  old_refcount = g_atomic_int_get (&GST_MINI_OBJECT_CAST (comp)->refcount);

  GST_LOG ("gst c2 comp unref:%d->%d", old_refcount, old_refcount - 1);

  gst_mini_object_unref (GST_MINI_OBJECT_CAST (comp));
}
