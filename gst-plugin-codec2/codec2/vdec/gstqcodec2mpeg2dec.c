/*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include "gstqcodec2mpeg2dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qcodec2_vdec_debug);
#define GST_CAT_DEFAULT gst_qcodec2_vdec_debug

static void gst_qcodec2_mpeg2_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_qcodec2_mpeg2_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_qcodec2_mpeg2_dec_set_format (GstQcodec2Vdec * decoder,
    GstVideoCodecState * state);

typedef struct
{
  // Sequence Extension
  gboolean has_seq_ext;
  guint8 profile_id;      // 4 = Main, 5 = Simple
  guint8 level_id;
  guint8 chroma_format;
  gboolean progressive;
  guint8 h_size_ext;
  guint8 v_size_ext;
} GstQcodec2Mpeg2StreamInfo;

enum
{
  PROP_0,
  PROP_DEINTERLACE,
  PROP_SET_GSTBUF_INTERLACE_FLAG,
};

/* class initialization */
G_DEFINE_TYPE (GstQcodec2MPEG2Dec, gst_qcodec2_mpeg2_dec,
    GST_TYPE_QCODEC2_VDEC);

static gboolean
gst_qcodec2_mpeg2_parse_codec_data (GstBuffer * codec_data,
    GstQcodec2Mpeg2StreamInfo * info)
{
  if (!codec_data || !info)
    return FALSE;

  GstMapInfo map;
  if (!gst_buffer_map (codec_data, &map, GST_MAP_READ))
    return FALSE;

  const guint8 *d = map.data;
  gsize size = map.size;
  gsize i = 0;
  guint8 sc = 0;

  if (size < 6) {
    gst_buffer_unmap (codec_data, &map);
    return FALSE;
  }

  memset (info, 0, sizeof (*info));

  while (i + 4 <= size) {
    // Scan start code 00 00 01 xx
    if (d[i] != 0x00 || d[i + 1] != 0x00 || d[i + 2] != 0x01) {
      i++;
      continue;
    }
    sc = d[i + 3];
    // Skip start code
    i += 4;

    if (sc == 0xb5 && i + 6 <= size) {
      // Sequence extension
      guint8 ext_id = (d[i] >> 4) & 0xF;
      if (ext_id != 0x1) {
        // Not a sequence extension.
        i++;
        continue;
      }

      guint8 pal = ((d[i] & 0xF) << 4) | ((d[i + 1] >> 4) & 0xF);
      info->profile_id    = (pal >> 4) & 0x7;      // bits[6:4]
      info->level_id      = pal & 0xF;      // bits[3:0]
      info->progressive   = (d[i + 1] >> 3) & 0x1;
      info->chroma_format = (d[i + 1] >> 1) & 0x3;
      info->h_size_ext    = d[i + 1] & 0x1;
      info->v_size_ext    = (d[i + 2] >> 6) & 0x3;
      info->has_seq_ext = TRUE;
      i += 6;
    }
  }

  gst_buffer_unmap (codec_data, &map);
  return TRUE;
}

static GstStaticPadTemplate gst_qcodec2_mpeg2_dec_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (MPEG2_CAPS));

static void
gst_qcodec2_mpeg2_dec_class_init (GstQcodec2MPEG2DecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstQcodec2VdecClass *videodec_class = GST_QCODEC2_VDEC_CLASS (klass);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qcodec2_mpeg2_dec_sink_template));

  gobject_class->set_property = gst_qcodec2_mpeg2_dec_set_property;
  gobject_class->get_property = gst_qcodec2_mpeg2_dec_get_property;

  videodec_class->set_format = gst_qcodec2_mpeg2_dec_set_format;

  g_object_class_install_property (gobject_class, PROP_DEINTERLACE,
      g_param_spec_boolean ("deinterlace", "enable deinterlace in Codec2",
          "enable deinterlace in Codec2 (TRUE=default, "
          "1: enable deinterlace, 0: disable deinterlace)",
          DEFAULT_DEINTERLACE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_SET_GSTBUF_INTERLACE_FLAG,
      g_param_spec_boolean ("set-gstbuf-interlace-flag", "set gstbuf interlace flag",
          "set interlace flag on output gstbuf, "
          "if deinterlace disabled and stream is interlace",
          DEFAULT_SET_GSTBUF_INTERLACE_FLAG,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Codec2 video MPEG-2 decoder", "Decoder/Video",
      "Video MPEG-2 Decoder based on Codec2.0", "QTI");
}

static void
gst_qcodec2_mpeg2_dec_init (GstQcodec2MPEG2Dec * self)
{
}

static void
gst_qcodec2_mpeg2_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQcodec2Vdec *base_dec = GST_QCODEC2_VDEC (object);

  switch (prop_id) {
    case PROP_DEINTERLACE:
      base_dec->deinterlace = g_value_get_boolean (value);
      break;
    case PROP_SET_GSTBUF_INTERLACE_FLAG:
      base_dec->set_gstbuf_interlace_flag = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qcodec2_mpeg2_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQcodec2Vdec *base_dec = GST_QCODEC2_VDEC (object);

  switch (prop_id) {
    case PROP_DEINTERLACE:
      g_value_set_boolean (value, base_dec->deinterlace);
      break;
    case PROP_SET_GSTBUF_INTERLACE_FLAG:
      g_value_set_boolean (value, base_dec->set_gstbuf_interlace_flag);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_qcodec2_mpeg2_dec_set_format (GstQcodec2Vdec * decoder,
    GstVideoCodecState * state)
{
  GstQcodec2MPEG2Dec *dec = GST_QCODEC2_MPEG2_DEC (decoder);
  GstQcodec2Vdec *base_dec = GST_QCODEC2_VDEC (decoder);
  GstStructure *structure = NULL;
  const gchar *caps_profile = NULL;
  const GValue *codec_data_value = NULL;
  GstBuffer *codec_data = NULL;
  GstQcodec2Mpeg2StreamInfo stream_info;
  gboolean unsupported_profile = FALSE;
  gboolean result = TRUE;
  ConfigParams deinterlace;
  ConfigParams pixel_format;
  GPtrArray *config = NULL;

  structure = gst_caps_get_structure (state->caps, 0);
  if (structure) {
    caps_profile = gst_structure_get_string (structure, "profile");

    if (caps_profile) {
      if (g_strcmp0 (caps_profile, "simple") != 0
          && g_strcmp0 (caps_profile, "main") != 0)
        unsupported_profile = TRUE;
    } else {
      codec_data_value = gst_structure_get_value (structure, "codec_data");
      if (codec_data_value) {
        codec_data = gst_value_get_buffer (codec_data_value);
        if (gst_qcodec2_mpeg2_parse_codec_data (codec_data,
                &stream_info) && stream_info.has_seq_ext) {
          if (stream_info.profile_id != 5 && stream_info.profile_id != 4)
            unsupported_profile = TRUE;
        }
      }
    }
  }

  if (unsupported_profile) {
    SG_ERR_OBJ (dec, "Unsupported MPEG2 profile");
    return FALSE;
  }

  config = g_ptr_array_new ();

  if (config) {
    pixel_format =
        make_pixel_format_param (gst_to_c2_pixelformat (base_dec,
            base_dec->output_format), FALSE);
    GST_LOG_OBJECT (dec, "set c2 output format: %d for MPEG2",
        pixel_format.pixelFormat.fmt);
    g_ptr_array_add (config, &pixel_format);

#ifdef GST_SUPPORT_C2DEC_DEINTERLACE
    deinterlace = make_deinterlace_param (base_dec->deinterlace);
    GST_DEBUG_OBJECT (dec, "set deinterlace param");

    g_ptr_array_add (config, &deinterlace);
#endif

    if (!c2componentInterface_config (decoder->comp_intf,
            config, BLOCK_MODE_MAY_BLOCK)) {
      result = FALSE;
      SG_ERR_OBJ (dec, "Failed to set config");
      goto out;
    }
  }

out:
  if (config)
    g_ptr_array_free (config, TRUE);

  return result;
}
