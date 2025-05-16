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
#include "gstqvidcvp9dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qvidc_vdec_debug);
#define GST_CAT_DEFAULT gst_qvidc_vdec_debug

static GstFlowReturn gst_qvidc_vp9_dec_handle_frame (GstQvidcVdec * decoder,
    GstVideoCodecFrame * frame);
static gboolean gst_qvidc_vp9_dec_open (GstQvidcVdec * decoder);
static gboolean gst_qvidc_vp9_dec_set_format (GstQvidcVdec * decoder,
    GstVideoCodecState * state);

/* class initialization */
G_DEFINE_TYPE (GstQvidcVP9Dec, gst_qvidc_vp9_dec, GST_TYPE_QVIDC_VDEC);

static GstStaticPadTemplate gst_qvidc_vp9_dec_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VP9_CAPS));

static void
gst_qvidc_vp9_dec_class_init (GstQvidcVP9DecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qvidc_vp9_dec_sink_template));
  GstQvidcVdecClass *qvidcvdec_class = GST_QVIDC_VDEC_CLASS (klass);

  qvidcvdec_class->handle_frame = gst_qvidc_vp9_dec_handle_frame;
  qvidcvdec_class->set_format = gst_qvidc_vp9_dec_set_format;
  qvidcvdec_class->open = gst_qvidc_vp9_dec_open;

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "VIDC video VP9 decoder", "Decoder/Video",
      "Video VP9 Decoder based on HW codec", "QTI");
}

static void
gst_qvidc_vp9_dec_init (GstQvidcVP9Dec * self)
{
}

static gboolean
gst_qvidc_vp9_dec_open (GstQvidcVdec * decoder)
{
  GstQvidcVdec *base_dec = decoder;
  /* start vidc component later since checking VP9 10bit format */
  base_dec->delay_start = TRUE;
  base_dec->check_10bit = TRUE;

  return TRUE;
}

static gboolean
gst_qvidc_vp9_dec_set_format (GstQvidcVdec * decoder,
    GstVideoCodecState * state)
{
  GstQvidcVP9Dec *dec = GST_QVIDC_VP9_DEC (decoder);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (dec, "VP9 dec set format");

  ret = dec_set_vidc_pixel_format (decoder, state);

  return ret;
}

static GstFlowReturn
gst_qvidc_vp9_dec_handle_frame (GstQvidcVdec * decoder,
    GstVideoCodecFrame * frame)
{
  GstQvidcVdec *base_dec = decoder;
  GstQvidcVP9Dec *dec = GST_QVIDC_VP9_DEC (decoder);
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo mapinfo = { 0, };
  GstBuffer *buf = NULL;
  GstVp9Parser *vp9_parser = NULL;
  GstVp9FrameHdr *vp9_hdr = NULL;
  GPtrArray *config = NULL;
  guint8 *frame_data = NULL;
  gsize frame_size = 0;
  GstVideoFormat output_format = GST_VIDEO_FORMAT_NV12;
  ConfigParams pixelformat;

  GST_DEBUG_OBJECT (dec, "VP9 dec handle frame");

  /* check VP9 10bit case 2: no field bit-depth-luma in caps, parse it here in non-secure mode */
  if (base_dec->check_10bit && !base_dec->secure) {
    GST_DEBUG_OBJECT (dec,
        "check VP9 10bit if without field bit-depth-luma in caps");
    vp9_parser = gst_vp9_parser_new ();
    vp9_hdr = g_slice_new0 (GstVp9FrameHdr);
    config = g_ptr_array_new ();
    buf = frame->input_buffer;
    gst_buffer_map (buf, &mapinfo, GST_MAP_READ);
    frame_data = mapinfo.data;
    frame_size = mapinfo.size;
    if (vp9_parser && vp9_hdr && config) {
      gst_vp9_parser_parse_frame_header (vp9_parser, vp9_hdr, frame_data,
          frame_size);
    } else {
      GST_ERROR_OBJECT (dec, "failed to new some structure");
      gst_buffer_unmap (buf, &mapinfo);
      ret = GST_FLOW_ERROR;
      goto done;
    }
    gst_buffer_unmap (buf, &mapinfo);

    if (vp9_parser->bit_depth == GST_VP9_BIT_DEPTH_10) {
      if (base_dec->is_ubwc) {
        output_format = GST_VIDEO_FORMAT_NV12_10LE32;
      } else {
        output_format = GST_VIDEO_FORMAT_P010_10LE;
      }

      base_dec->output_format = output_format;

      GST_LOG_OBJECT (dec,
          "output width: %d, height: %d, format: %d (%s) for VP9",
          base_dec->width, base_dec->height, output_format,
          gst_video_format_to_string (output_format));

      if (config) {
        pixelformat =
            make_pixel_format_param (gst_to_vidc_pixelformat (base_dec,
                output_format), FALSE);
        GST_LOG_OBJECT (dec, "set vidc output format: %d for VP9",
            pixelformat.pixelFormat.fmt);
        g_ptr_array_add (config, &pixelformat);
        if (!vidc_config (base_dec->comp, config, BLOCK_MODE_MAY_BLOCK)) {
          GST_ERROR_OBJECT (dec, "Failed to set config");
          ret = GST_FLOW_ERROR;
          goto done;
        }
      }
    }

    base_dec->check_10bit = FALSE;
  }

  if (base_dec->delay_start) {
    if (!gst_qvidc_vdec_config_pool (base_dec, NULL, BUFFER_PORT_INPUT)) {
      GST_ERROR_OBJECT (dec, "failed to config pool");
      ret = GST_FLOW_ERROR;
      goto done;
    }
    base_dec->delay_start = FALSE;
  }

done:
  if (config)
    g_ptr_array_free (config, TRUE);
  if (vp9_hdr)
    g_slice_free (GstVp9FrameHdr, vp9_hdr);
  if (vp9_parser)
    gst_vp9_parser_free (vp9_parser);

  return ret;
}
