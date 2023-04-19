// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/codecparsers/gstav1parser.h>
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
  GstQcodec2AV1Dec *self = GST_QCODEC2_AV1_DEC (decoder);
  /* start C2 component later since checking AV1 10bit format */
  base_dec->delay_start = TRUE;
  self->check_av1_bitdepth = TRUE;

  return TRUE;
}

static gboolean
gst_qcodec2_av1_dec_set_format (GstQcodec2Vdec * decoder,
    GstVideoCodecState * state)
{
  GstQcodec2Vdec *base_dec = decoder;
  GstQcodec2AV1Dec *dec = GST_QCODEC2_AV1_DEC (decoder);
  GstStructure *s = NULL;
  GPtrArray *config = NULL;
  GstVideoFormat output_format = GST_VIDEO_FORMAT_NV12;
  ConfigParams pixelformat;
  guint bit_depth_luma, bit_depth_chroma;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (dec, "AV-1 dec set format");

  /* check AV-1 bitdepth: bit-depth-luma in caps, it's supported since GST 1.20;
   * or it's added in caps explicitly by upstream element in secure mode.
   */
  if (dec->check_av1_bitdepth) {
    GST_DEBUG_OBJECT (dec, "check whether field bit-depth-luma in caps");
    s = gst_caps_get_structure (state->caps, 0);
    if (s && gst_structure_get_uint (s, "bit-depth-luma", &bit_depth_luma) &&
        gst_structure_get_uint (s, "bit-depth-chroma", &bit_depth_chroma)) {
      if (bit_depth_luma == 10 && bit_depth_chroma == 10) {
        if (base_dec->is_ubwc) {
          output_format = GST_VIDEO_FORMAT_NV12_10LE32;
        } else {
          output_format = GST_VIDEO_FORMAT_P010_10LE;
        }
      } else if (bit_depth_luma == 12 && bit_depth_chroma == 12) {
        GST_ERROR_OBJECT (dec, "bitdepth is 12, not supported yet");
        return FALSE;
      }

      config = g_ptr_array_new ();
      if (config) {
        pixelformat =
            make_pixel_format_param (gst_to_c2_pixelformat (base_dec,
                output_format), FALSE);
        GST_LOG_OBJECT (dec, "set c2 output format: %d (%s) for AV-1",
            pixelformat.pixelFormat.fmt,
            gst_video_format_to_string (output_format));
        g_ptr_array_add (config, &pixelformat);
        if (!c2componentInterface_config (base_dec->comp_intf,
                config, BLOCK_MODE_MAY_BLOCK)) {
          GST_ERROR_OBJECT (dec, "Failed to set config");
          ret = FALSE;
        }
        g_ptr_array_free (config, TRUE);
      }

      base_dec->output_format = output_format;
      /* disable checking and delay_start since bit-depth-chroma parsed */
      dec->check_av1_bitdepth = FALSE;
      base_dec->delay_start = FALSE;
    }
  }

  return ret;
}
