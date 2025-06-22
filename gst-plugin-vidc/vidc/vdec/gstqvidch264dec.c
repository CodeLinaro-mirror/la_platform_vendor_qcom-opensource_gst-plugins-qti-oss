/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*
*     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstqvidch264dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qvidc_vdec_debug);
#define GST_CAT_DEFAULT gst_qvidc_vdec_debug

static void gst_qvidc_h264_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_qvidc_h264_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_qvidc_h264_dec_set_format (GstQvidcVdec * decoder,
    GstVideoCodecState * state);

enum
{
  PROP_0,
  PROP_SET_GSTBUF_INTERLACE_FLAG,
};

/* class initialization */
G_DEFINE_TYPE (GstQvidcH264Dec, gst_qvidc_h264_dec, GST_TYPE_QVIDC_VDEC);

static GstStaticPadTemplate gst_qvidc_h264_dec_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (H264_CAPS));

static void
gst_qvidc_h264_dec_class_init (GstQvidcH264DecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstQvidcVdecClass *videodec_class = GST_QVIDC_VDEC_CLASS (klass);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qvidc_h264_dec_sink_template));

  gobject_class->set_property = gst_qvidc_h264_dec_set_property;
  gobject_class->get_property = gst_qvidc_h264_dec_get_property;

  videodec_class->set_format = gst_qvidc_h264_dec_set_format;

  g_object_class_install_property (gobject_class,
      PROP_SET_GSTBUF_INTERLACE_FLAG,
      g_param_spec_boolean ("set-gstbuf-interlace-flag",
          "set gstbuf interlace flag",
          "set interlace flag on output gstbuf, "
          "if deinterlace disabled and stream is interlace",
          DEFAULT_SET_GSTBUF_INTERLACE_FLAG,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "VIDC video H.264 decoder", "Decoder/Video",
      "Video H.264 Decoder based on HW codec", "QTI");
}

static void
gst_qvidc_h264_dec_init (GstQvidcH264Dec * self)
{
}

static void
gst_qvidc_h264_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQvidcVdec *base_dec = GST_QVIDC_VDEC (object);

  switch (prop_id) {
    case PROP_SET_GSTBUF_INTERLACE_FLAG:
      base_dec->set_gstbuf_interlace_flag = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qvidc_h264_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQvidcVdec *base_dec = GST_QVIDC_VDEC (object);

  switch (prop_id) {
    case PROP_SET_GSTBUF_INTERLACE_FLAG:
      g_value_set_boolean (value, base_dec->set_gstbuf_interlace_flag);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_qvidc_h264_dec_set_format (GstQvidcVdec * decoder,
    GstVideoCodecState * state)
{
  GstQvidcH264Dec *dec = GST_QVIDC_H264_DEC (decoder);
  GstQvidcVdec *base_dec = GST_QVIDC_VDEC (decoder);
  gboolean result = TRUE;
  ConfigParams pixel_format;
  GPtrArray *config = NULL;

  config = g_ptr_array_new ();

  if (config) {
    pixel_format =
        make_pixel_format_param (gst_to_vidc_pixelformat (base_dec,
            base_dec->output_format), FALSE);
    GST_LOG_OBJECT (dec, "set vidc output format: %d for H264",
        pixel_format.pixelFormat.fmt);
    g_ptr_array_add (config, &pixel_format);

    if (!vidc_config (decoder->comp, config, BLOCK_MODE_MAY_BLOCK)) {
      result = FALSE;
      GST_ERROR_OBJECT (dec, "Failed to set config");
      goto out;
    }
  }

out:
  if (config)
    g_ptr_array_free (config, TRUE);

  return result;
}
