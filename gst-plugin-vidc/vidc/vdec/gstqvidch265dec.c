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
#include "gstqvidch265dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qvidc_vdec_debug);
#define GST_CAT_DEFAULT gst_qvidc_vdec_debug

static gboolean gst_qvidc_h265_dec_open (GstQvidcVdec * decoder);
static gboolean gst_qvidc_h265_dec_set_format (GstQvidcVdec * decoder,
    GstVideoCodecState * state);

/* class initialization */
G_DEFINE_TYPE (GstQvidcH265Dec, gst_qvidc_h265_dec, GST_TYPE_QVIDC_VDEC);

static GstStaticPadTemplate gst_qvidc_h265_dec_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (H265_CAPS));

static void
gst_qvidc_h265_dec_class_init (GstQvidcH265DecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstQvidcVdecClass *qvidcvdec_class = GST_QVIDC_VDEC_CLASS (klass);

  qvidcvdec_class->open = gst_qvidc_h265_dec_open;
  qvidcvdec_class->set_format = gst_qvidc_h265_dec_set_format;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qvidc_h265_dec_sink_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "VIDC video H.265 decoder", "Decoder/Video",
      "Video H.265 Decoder based on HW codec", "QTI");
}

static void
gst_qvidc_h265_dec_init (GstQvidcH265Dec * self)
{
}

static gboolean
gst_qvidc_h265_dec_open (GstQvidcVdec * decoder)
{
  GstQvidcVdec *base_dec = decoder;
  base_dec->check_10bit = TRUE;

  return TRUE;
}

static gboolean
gst_qvidc_h265_dec_set_format (GstQvidcVdec * decoder,
    GstVideoCodecState * state)
{
  GstQvidcH265Dec *dec = GST_QVIDC_H265_DEC (decoder);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (dec, "H265 dec set format");

  ret = dec_set_vidc_pixel_format (decoder, state);

  return ret;
}
