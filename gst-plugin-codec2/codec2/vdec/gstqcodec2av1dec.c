// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstqcodec2av1dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qcodec2_vdec_debug);
#define GST_CAT_DEFAULT gst_qcodec2_vdec_debug

static gboolean gst_qcodec2_av1_dec_open (GstQcodec2Vdec * decoder);
static gboolean gst_qcodec2_av1_dec_set_format (GstQcodec2Vdec * decoder,
    GstVideoCodecState * state);

/* class initialization */
G_DEFINE_TYPE (GstQcodec2AV1Dec, gst_qcodec2_av1_dec, GST_TYPE_QCODEC2_VDEC);

static GstStaticPadTemplate gst_qcodec2_av1_dec_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (AV1_CAPS));

static void
gst_qcodec2_av1_dec_class_init (GstQcodec2AV1DecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstQcodec2VdecClass *qcodec2vdec_class = GST_QCODEC2_VDEC_CLASS (klass);

  qcodec2vdec_class->open = gst_qcodec2_av1_dec_open;
  qcodec2vdec_class->set_format = gst_qcodec2_av1_dec_set_format;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qcodec2_av1_dec_sink_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Codec2 video AV-1 decoder", "Decoder/Video",
      "Video AV-1 Decoder based on Codec2.0", "QTI");
}

static void
gst_qcodec2_av1_dec_init (GstQcodec2AV1Dec * self)
{
}

static gboolean
gst_qcodec2_av1_dec_open (GstQcodec2Vdec * decoder)
{
  GstQcodec2Vdec *base_dec = decoder;
  base_dec->check_10bit = TRUE;

  return TRUE;
}

static gboolean
gst_qcodec2_av1_dec_set_format (GstQcodec2Vdec * decoder,
    GstVideoCodecState * state)
{
  GstQcodec2AV1Dec *dec = GST_QCODEC2_AV1_DEC (decoder);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (dec, "AV-1 dec set format");

  ret = dec_set_c2_pixel_format (decoder, state);

  return ret;
}
