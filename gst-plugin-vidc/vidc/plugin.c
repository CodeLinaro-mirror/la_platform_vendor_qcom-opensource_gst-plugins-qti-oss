// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "-"
#endif

#include "gstqvidcvdec.h"
#include "gstqvidcvenc.h"

GST_DEBUG_CATEGORY (qvidcutils_debug);
GST_DEBUG_CATEGORY (qvidcbufferpool_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_INFO ("qvidc plugin init");

  GST_DEBUG_CATEGORY_INIT (qvidcutils_debug,
      "qvidcutils", 0, "GST Qvidc utils");
  GST_DEBUG_CATEGORY_INIT (qvidcbufferpool_debug,
      "qvidcpool", 0, "GST Qvidc buffer pool");

  gboolean ret = TRUE;

  if (!gst_qvidc_vdec_plugin_init (plugin)) {
    GST_ERROR ("qvidcvdec plugin init error");
    ret = FALSE;
  }
  else if (!gst_qvidc_venc_plugin_init (plugin)) {
    GST_ERROR ("qvidcvenc plugin init error");
    ret = FALSE;
  }

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    qvidc, "GST QTI VIDC Video Decoder & Encoder",
    plugin_init, VERSION "-" G_STRINGIFY(GST_VERSION_MAJOR) "/" G_STRINGIFY(GST_VERSION_MINOR) "/" G_STRINGIFY(GST_VERSION_MICRO), GST_LICENSE_UNKNOWN, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
