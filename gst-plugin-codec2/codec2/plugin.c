// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef GST_PACKAGE_ORIGIN
#   define GST_PACKAGE_ORIGIN "-"
#endif

#include "gstqcodec2vdec.h"
#include "gstqcodec2venc.h"

GST_DEBUG_CATEGORY (qcodec2utils_debug);
GST_DEBUG_CATEGORY (qcodec2bufferpool_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_INFO ("qcodec2 plugin init");

  GST_DEBUG_CATEGORY_INIT (qcodec2utils_debug,
      "qcodec2utils", 0, "GST Qcodec2.0 utils");
  GST_DEBUG_CATEGORY_INIT (qcodec2bufferpool_debug,
      "qcodec2pool", 0, "GST Qcodec2.0 buffer pool");

  gboolean ret = FALSE;
  GPtrArray *array = NULL;
  void *comp_store = c2componentStore_create ();
  if (comp_store) {
    array = g_ptr_array_new ();
    if (array) {
      ret = c2componentStore_listComponents (comp_store, array);
    }

    c2componentStore_delete (comp_store);
    comp_store = NULL;
  }

  if (!ret) {
    GST_ERROR ("create componentStore failed");
    goto END;
  }

  if (!gst_qcodec2_vdec_plugin_init (plugin, array)) {
    GST_ERROR ("qcodec2vdec plugin init error");
    ret = FALSE;
    goto END;
  }

  if (!gst_qcodec2_venc_plugin_init (plugin, array)) {
    GST_ERROR ("qcodec2venc plugin init error");
    ret = FALSE;
  }

END:
  if (array) {
    g_ptr_array_free (array, TRUE);
  }

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    qcodec2, "GST QTI Codec2.0 Video Decoder & Encoder",
    plugin_init, VERSION, GST_LICENSE_UNKNOWN, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
