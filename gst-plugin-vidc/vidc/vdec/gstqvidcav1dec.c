// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/codecparsers/gstav1parser.h>
#include "gstqvidcav1dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qvidc_vdec_debug);
#define GST_CAT_DEFAULT gst_qvidc_vdec_debug

static gboolean gst_qvidc_av1_dec_open (GstQvidcVdec * decoder);
static gboolean gst_qvidc_av1_dec_set_format (GstQvidcVdec * decoder,
    GstVideoCodecState * state);

/* class initialization */
G_DEFINE_TYPE (GstQvidcAV1Dec, gst_qvidc_av1_dec, GST_TYPE_QVIDC_VDEC);

static GstStaticPadTemplate gst_qvidc_av1_dec_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (AV1_CAPS));

static void
gst_qvidc_av1_dec_class_init (GstQvidcAV1DecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstQvidcVdecClass *qvidcvdec_class = GST_QVIDC_VDEC_CLASS (klass);

  qvidcvdec_class->open = gst_qvidc_av1_dec_open;
  qvidcvdec_class->set_format = gst_qvidc_av1_dec_set_format;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qvidc_av1_dec_sink_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "VIDC video AV-1 decoder", "Decoder/Video",
      "Video AV-1 Decoder based on HW codec", "QTI");
}

static void
gst_qvidc_av1_dec_init (GstQvidcAV1Dec * self)
{
}

static gboolean
gst_qvidc_av1_dec_open (GstQvidcVdec * decoder)
{
  GstQvidcVdec *base_dec = decoder;
  base_dec->check_10bit = TRUE;

  return TRUE;
}

static gboolean
gst_qvidc_av1_dec_set_format (GstQvidcVdec * decoder,
    GstVideoCodecState * state)
{
  GstQvidcAV1Dec *dec = GST_QVIDC_AV1_DEC (decoder);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (dec, "AV-1 dec set format");

  ret = dec_set_vidc_pixel_format (decoder, state);

  return ret;
}
