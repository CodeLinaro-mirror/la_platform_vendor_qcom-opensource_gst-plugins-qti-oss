/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
Changes from Qualcomm Innovation Center, Inc. are provided under the following license:

Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted (subject to the limitations in the
disclaimer below) provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "gstqvidcvenc.h"
#include "gstqvidch264enc.h"
#include "gstqvidch265enc.h"

GST_DEBUG_CATEGORY (gst_qvidc_venc_debug);
#define GST_CAT_DEFAULT gst_qvidc_venc_debug

#define DEFAULT_COLOR_SPACE_CONVERSION            (FALSE)
#define DEFAULT_BITRATE_SAVING_MODE               (0xffffffff)
#define DEFAULT_BLUR_MODE                         (0xffffffff)
#define DEFAULT_INTERVAL_INTRAFRAMES              (0xffffffff)
#define DEFAULT_INLINE_HEADERS                    (FALSE)
#define DEFAULT_USE_EXTERNAL_POOL                 (FALSE)
#define DEFAULT_INIT_QUANT_I_FRAMES               (0xffffffff)
#define DEFAULT_INIT_QUANT_P_FRAMES               (0xffffffff)
#define DEFAULT_INIT_QUANT_B_FRAMES               (0xffffffff)


/* class initialization */
G_DEFINE_TYPE (GstQvidcVenc, gst_qvidc_venc, GST_TYPE_VIDEO_ENCODER);

#define GST_TYPE_VIDC_ENC_MIRROR_TYPE (gst_qvidc_venc_mirror_get_type ())
#define GST_TYPE_VIDC_ENC_RATE_CONTROL (gst_qvidc_venc_rate_control_get_type ())
#define GST_TYPE_VIDC_ENC_COLOR_PRIMARIES (gst_qvidc_venc_color_primaries_get_type())
#define GST_TYPE_VIDC_ENC_MATRIX_COEFFS (gst_qvidc_venc_matrix_coeffs_get_type())
#define GST_TYPE_VIDC_ENC_TRANSFER_CHAR (gst_qvidc_venc_transfer_characteristics_get_type())
#define GST_TYPE_VIDC_ENC_FULL_RANGE (gst_qvidc_venc_full_range_get_type())
#define GST_TYPE_VIDC_ENC_INTRA_REFRESH_MODE (gst_qvidc_venc_intra_refresh_mode_get_type ())
#define GST_TYPE_VIDC_ENC_SLICE_MODE (gst_qvidc_venc_slice_mode_get_type ())
#define GST_TYPE_VIDC_ENC_BLUR_MODE (gst_qvidc_venc_blur_mode_get_type ())
#define GST_TYPE_VIDC_ENC_BITRATE_SAVING_MODE (gst_qvidc_venc_bitrate_saving_mode_get_type ())

#define parent_class gst_qvidc_venc_parent_class
#define NANO_TO_MILLI(x)  ((x) / 1000)
#define EOS_WAITING_TIMEOUT 5
#define ACQUIRE_TIMEOUT 100
#define ROI_ARRAY_SIZE 128
#define DYNAMIC_PROP_BIT(x) ((1) << (x))
#define DYNAMIC_PROP_BITRATE DYNAMIC_PROP_BIT(0)
#define DYNAMIC_PROP_IFRAME DYNAMIC_PROP_BIT(1)
#define DYNAMIC_PROP_FRAMERATE DYNAMIC_PROP_BIT(2)

#define ENCODER_ELEMENT(codec, element, vidc_codec) \
  {"vidc.qti." G_STRINGIFY (codec) ".encoder", \
   "qvidc" G_STRINGIFY (element) "enc", \
   GST_RANK_PRIMARY + 10, \
   gst_qvidc_##element##_enc_get_type, \
   vidc_codec}

static const ElementInfo kENCODER_ELEMENTS[] = {
  ENCODER_ELEMENT (avc, h264, VIDC_CODEC_H264),
  ENCODER_ELEMENT (hevc, h265, VIDC_CODEC_HEVC),
};

enum
{
  /* actions */
  SIGNAL_FORCE_IDR,

  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_RATE_CONTROL,
  PROP_DOWNSCALE_WIDTH,
  PROP_DOWNSCALE_HEIGHT,
  PROP_COLOR_SPACE_PRIMARIES,
  PROP_COLOR_SPACE_MATRIX_COEFFS,
  PROP_COLOR_SPACE_TRANSFER_CHAR,
  PROP_COLOR_SPACE_FULL_RANGE,
  PROP_COLOR_SPACE_CONVERSION,
  PROP_MIRROR,
  PROP_ROTATION,
  PROP_INTRA_REFRESH_MODE,
  PROP_INTRA_REFRESH_MBS,
  PROP_TARGET_BITRATE,
  PROP_SLICE_MODE,
  PROP_SLICE_SIZE,
  PROP_BLUR_MODE,
  PROP_BLUR_WIDTH,
  PROP_BLUR_HEIGHT,
  PROP_ROI,
  PROP_BITRATE_SAVING_MODE,
  PROP_INTERVAL_INTRAFRAMES,
  PROP_INLINE_SPSPPS_HEADERS,
  PROP_MIN_QP_I_FRAMES,
  PROP_MAX_QP_I_FRAMES,
  PROP_MIN_QP_P_FRAMES,
  PROP_MAX_QP_P_FRAMES,
  PROP_MIN_QP_B_FRAMES,
  PROP_MAX_QP_B_FRAMES,
  PROP_INIT_QUANT_I_FRAMES,
  PROP_INIT_QUANT_P_FRAMES,
  PROP_INIT_QUANT_B_FRAMES,
  PROP_REPORT_AVERAGE_FRAME_QP,
  PROP_HIER_P,
  PROP_HIER_B,
  PROP_BITRATE_RATIOS,
  PROP_LTR_COUNT,
  PROP_LTR_MARK,
  PROP_LTR_USE,
  PROP_USE_EXTERNAL_POOL,
};

/* GstVideoEncoder base class method */
static gboolean gst_qvidc_venc_stop (GstVideoEncoder * encoder);
static gboolean gst_qvidc_venc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_qvidc_venc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_qvidc_venc_finish (GstVideoEncoder * encoder);
static gboolean gst_qvidc_venc_open (GstVideoEncoder * encoder);
static gboolean gst_qvidc_venc_close (GstVideoEncoder * encoder);
static gboolean gst_qvidc_venc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean gst_qvidc_venc_decide_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

static void gst_qvidc_venc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_qvidc_venc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_qvidc_venc_finalize (GObject * object);

static gboolean gst_qvidc_venc_create_component (GstVideoEncoder * encoder);
static void handle_video_event (const void *handle, EVENT_TYPE type,
    void *data);

static GstFlowReturn gst_qvidc_venc_encode (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_qvidc_venc_setup_output (GstVideoEncoder * encoder,
    GstVideoCodecState * state);

static void gst_qvidc_venc_build_roi_array (GstVideoEncoder * encoder,
    const GValue * value);
static gboolean handle_dynamic_meta (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static void build_roi_meta (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static void add_roi_to_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame, GstStructure * roimeta);

static gboolean
gst_qvidc_venc_refresh_input_layout_info (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame, BufferDescriptor * bufinfo);

static void gst_qvidc_venc_handle_dynamic_config (GstVideoEncoder * encoder);

static GstStateChangeReturn gst_qvidc_venc_change_state (GstElement * element,
    GstStateChange transition);

static void queue_vidc_bufferDesc (BufferDescriptor * buffer,
    gpointer user_data);

static guint gst_qvidc_venc_signals[LAST_SIGNAL] = { 0 };

static ConfigParams
make_codec_param (const gchar * name)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_CODEC;
  param.codec = 0;

  guint count = 0;
  for (guint i = 0; i < G_N_ELEMENTS (kENCODER_ELEMENTS); i++) {
    GST_ERROR ("element[%d] name %s", i, kENCODER_ELEMENTS[i].codec);
    if (g_strcmp0 (kENCODER_ELEMENTS[i].codec, name) == 0) {
      param.codec = kENCODER_ELEMENTS[i].vidc_codec;
      GST_ERROR ("element[%d] codec 0x%x", i, param.codec);
      break;
    }
  }

  GST_ERROR ("element name %s, codec 0x%x", name, param.codec);

  return param;
}

static ConfigParams
make_ltr_count_param (guint count)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_LTR_COUNT;
  param.isInput = TRUE;
  param.ltr.count = count;

  return param;
}

static ConfigParams
make_ltr_mark_param (guint mark_index)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_LTR_MARK_INDEX;
  param.isInput = TRUE;
  param.ltr.mark_index = mark_index;

  return param;
}

static ConfigParams
make_ltr_use_param (guint use_index)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_LTR_USE_INDEX;
  param.isInput = TRUE;
  param.ltr.use_index = use_index;

  return param;
}

static ConfigParams
make_temporallayer_param (guint32 hierp_layers, guint32 hierb_layers,
    guint32 size, gfloat * ratios)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_TEMPORAL_LAYER;

  param.temporalLayer.layerCount = hierp_layers + hierb_layers;
  param.temporalLayer.bLayerCount = hierb_layers;
  param.temporalLayer.ratioSize = size;

  if (ratios) {
    param.temporalLayer.ratios = ratios;
  }

  return param;
}

static ConfigParams
make_bitrate_param (guint32 bitrate, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_BITRATE;
  param.isInput = is_input;
  param.val.u32 = bitrate;

  return param;
}

static ConfigParams
make_resolution_param (guint32 width, guint32 height, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_RESOLUTION;
  param.isInput = is_input;
  param.resolution.width = width;
  param.resolution.height = height;

  return param;
}

static ConfigParams
make_pixel_format_param (guint32 fmt, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_PIXELFORMAT;
  param.isInput = is_input;
  param.pixelFormat.fmt = fmt;

  return param;
}

static ConfigParams
make_mirror_param (MIRROR_TYPE mirror, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_MIRROR;
  param.isInput = is_input;
  param.mirror.type = mirror;

  return param;
}

static ConfigParams
make_rotation_param (guint32 rotation, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_ROTATION;
  param.isInput = is_input;
  param.val.u32 = rotation;

  return param;
}

static ConfigParams
make_rate_control_param (RC_MODE_TYPE mode)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_RATECONTROL;
  param.rcMode.type = mode;

  return param;
}

static ConfigParams
make_downscale_param (guint32 width, guint32 height)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_DOWNSCALE;
  param.resolution.width = width;
  param.resolution.height = height;

  return param;
}

static ConfigParams
make_slicemode_param (guint32 size, SLICE_MODE mode)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_SLICE_MODE;
  param.sliceMode.slice_size = size;
  param.sliceMode.type = mode;

  return param;
}

static ConfigParams
make_color_space_conv_param (gboolean csc)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_ENC_CSC;
  param.color_space_conversion = csc;

  return param;
}

static ConfigParams
make_color_aspects_param (COLOR_PRIMARIES primaries,
    TRANSFER_CHAR transfer_char, MATRIX matrix, FULL_RANGE full_range)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_COLOR_ASPECTS_INFO;
  param.colorAspects.primaries = primaries;
  param.colorAspects.transfer_char = transfer_char;
  param.colorAspects.matrix = matrix;
  param.colorAspects.full_range = full_range;

  return param;
}

static ConfigParams
make_intra_refresh_param (IR_MODE_TYPE mode, guint32 intra_refresh_mbs)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_INTRAREFRESH;
  param.irMode.type = mode;
  param.irMode.intra_refresh_mbs = intra_refresh_mbs;

  return param;
}

static ConfigParams
make_intra_refresh_type_param (IR_MODE_TYPE mode)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_INTRAREFRESH_TYPE;
  if (mode == IR_RANDOM) {
    param.irMode.type = 0;      // qc2::IntraRefreshMode::INTRA_REFRESH_RANDOM
  } else if (mode == IR_CYCLIC) {
    param.irMode.type = 1;      // qc2::IntraRefreshMode::INTRA_REFRESH_CYCLIC
  }

  return param;
}


static ConfigParams
make_blur_mode_param (BLUR_MODE mode, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_BLUR_MODE;
  param.isInput = is_input;
  param.blur.mode = mode;

  return param;
}

static ConfigParams
make_blur_resolution_param (guint32 width, guint32 height, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_BLUR_RESOLUTION;
  param.isInput = is_input;
  param.resolution.width = width;
  param.resolution.height = height;

  return param;
}

static ConfigParams
make_roi_param (GstQvidcVenc * enc, const int64_t timestamp,
    const char *type, const char *payload, const char *payloadExt)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));
  memset (enc->roi_type, 0, sizeof (char) * ROI_ARRAY_SIZE);
  memset (enc->roi_rect_payload, 0, sizeof (char) * ROI_ARRAY_SIZE);
  memset (enc->roi_rect_payload_ext, 0, sizeof (char) * ROI_ARRAY_SIZE);

  param.config_name = CONFIG_FUNCTION_KEY_ROIREGION;
  param.roiRegion.timestampUs = timestamp;
  param.roiRegion.type = enc->roi_type;
  param.roiRegion.rectPayload = enc->roi_rect_payload;
  param.roiRegion.rectPayloadExt = enc->roi_rect_payload_ext;

  memcpy (param.roiRegion.type, type, strlen (type));
  memcpy (param.roiRegion.rectPayload, payload, strlen (payload));
  memcpy (param.roiRegion.rectPayloadExt, payloadExt, strlen (payloadExt));

  return param;
}

static ConfigParams
make_bitrate_saving_mode (BITRATE_SAVING_MODE mode, gboolean isInput)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_BITRATE_SAVING_MODE;
  param.isInput = isInput;
  param.bitrate_saving_mode.saving_mode = mode;

  return param;
}

ConfigParams
make_profile_level_param (VIDC_PROFILE_T profile, VIDC_LEVEL_T level)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_PROFILE_LEVEL;
  param.profileAndLevel.profile = profile;
  param.profileAndLevel.level = level;

  return param;
}


static ConfigParams
make_dynamic_framerate_param (gfloat framerate)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_DYNAMIC_FRAMERATE;
  param.framerate = framerate;

  return param;
}

static ConfigParams
make_intraframes_period_param (guint32 interval, gfloat framerate)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_INTRAFRAMES_PERIOD;
  param.val.i64 = (gint64) (interval + 1) * 1e6 / framerate;

  return param;
}

static ConfigParams
make_force_idr_param (gboolean force_idr)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_INTRA_VIDEO_FRAME_REQUEST;
  param.force_idr = force_idr;

  return param;
}

static ConfigParams
make_header_mode_param (gboolean header_mode)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_VIDEO_HEADER_MODE;
  param.inline_sps_pps_headers = header_mode;

  return param;
}

static ConfigParams
make_qp_ranges_param (guint32 min_i_qp, guint32 max_i_qp, guint32 min_p_qp,
    guint32 max_p_qp, guint32 min_b_qp, guint32 max_b_qp)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_IPB_QP_RANGE;
  param.qp_ranges.min_i_qp = min_i_qp;
  param.qp_ranges.max_i_qp = max_i_qp;
  param.qp_ranges.min_p_qp = min_p_qp;
  param.qp_ranges.max_p_qp = max_p_qp;
  param.qp_ranges.min_b_qp = min_b_qp;
  param.qp_ranges.max_b_qp = max_b_qp;

  return param;
}

static ConfigParams
make_qp_init_param (guint32 quant_i_frames, guint32 quant_p_frames,
    guint32 quant_b_frames)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_IPB_QP_INIT;
  if (quant_i_frames != DEFAULT_INIT_QUANT_I_FRAMES) {
    param.qp_init.quant_i_frames_enable = TRUE;
    param.qp_init.quant_i_frames = quant_i_frames;
  }
  if (quant_p_frames != DEFAULT_INIT_QUANT_P_FRAMES) {
    param.qp_init.quant_p_frames_enable = TRUE;
    param.qp_init.quant_p_frames = quant_p_frames;
  }
  if (quant_b_frames != DEFAULT_INIT_QUANT_B_FRAMES) {
    param.qp_init.quant_b_frames_enable = TRUE;
    param.qp_init.quant_b_frames = quant_b_frames;
  }

  return param;
}

static ConfigParams
make_report_avg_frame_qp_param (gboolean enable)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_REPORT_AVERAGE_FRAME_QP;
  param.report_average_frame_qp = enable;

  return param;
}

static ConfigParams
make_dynamic_buffer_mode_param (gboolean enable)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_EXTERNAL_BUFFER;
  param.use_external_buf = enable;

  return param;
}

static gchar *
get_vidc_comp_name (GstStructure * structure)
{
  gchar *ret = NULL;

  if (gst_structure_has_name (structure, "video/x-h264")) {
    ret = g_strdup ("vidc.qti.avc.encoder");
  } else if (gst_structure_has_name (structure, "video/x-h265")) {
    ret = g_strdup ("vidc.qti.hevc.encoder");
  } else if (gst_structure_has_name (structure, "video/x-heic")) {
    ret = g_strdup ("vidc.qti.heic.encoder");
  }

  return ret;
}

static guint32
gst_to_vidc_pixelformat (GstVideoEncoder * encoder, GstVideoFormat format)
{
  guint32 result = 0;
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);

  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      if (enc->is_ubwc) {
        result = PIXEL_FORMAT_NV12_UBWC;
      } else if (enc->is_heic)
        result = PIXEL_FORMAT_NV12_512;
      else {
        result = PIXEL_FORMAT_NV12_LINEAR;
      }
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      result = PIXEL_FORMAT_P010;
      break;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      if (enc->is_ubwc) {
        result = PIXEL_FORMAT_TP10_UBWC;
      } else {
        GST_ERROR_OBJECT (enc, "unsupported format Linear NV12_10LE32 yet");
      }
      break;
    default:
      break;
  }

  GST_DEBUG_OBJECT (enc, "to_vidc_pixelformat (%s), vidc format: %d",
      gst_video_format_to_string (format), result);

  return result;
}

static GType
gst_qvidc_venc_mirror_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {MIRROR_NONE, "Mirror None", "none"},
      {MIRROR_VERTICAL, "Mirror Vertical", "vertical"},
      {MIRROR_HORIZONTAL, "Mirror Horizontal", "horizontal"},
      {MIRROR_BOTH, "Mirror Both", "both"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencMirror", values);
  }
  return qtype;
}

static GType
gst_qvidc_venc_slice_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {SLICE_MODE_DISABLE, "Slice Mode Disable", "disable"},
      {SLICE_MODE_MB, "Slice Mode MB", "MB"},
      {SLICE_MODE_BYTES, "Slice Mode Bytes", "bytes"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencSliceMode", values);
  }
  return qtype;
}

static GType
gst_qvidc_venc_blur_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {BLUR_AUTO, "Disable External Blur but Enable Internal Blur. If set "
            "before start, blur is disabled throughout the session.", "auto"},
      {BLUR_MANUAL, "External Dynamic Blur Enable. Must be set before start. "
            "Blur is applied when valid resolution is set.", "manual"},
      {BLUR_DISABLE, "Disable External and Internal Blur.", "disable"},
      {0xffffffff, "Component Default", "default"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencBlurMode", values);
  }
  return qtype;
}

static GType
gst_qvidc_venc_rate_control_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {RC_OFF, "Disable RC", "disable"},
      {RC_CONST, "Constant bitrate, constant framerate, CBR-CFR", "constant"},
      {RC_CBR_VFR,
            "Constant bitrate, variable framerate(skip frame if bit budget not enough)",
          "CBR-VFR"},
      {RC_VBR_CFR, "Variable bitrate, constant framerate", "VBR-CFR"},
      {RC_VBR_VFR,
            "Variable bitrate, variable framerate(skip frame if bit budget not enough)",
          "VBR-VFR"},
      {RC_CQ, "Constant quality", "CQ"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencRateControl", values);
  }
  return qtype;
}

static GType
gst_qvidc_venc_color_primaries_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {COLOR_PRIMARIES_UNSPECIFIED, "primaries are unspecified", "NONE"},
      {COLOR_PRIMARIES_BT709, "Rec.ITU-R BT.709-6 or equivalent", "BT709"},
      {COLOR_PRIMARIES_BT470_M, "Rec.ITU-R BT.470-6 System M or equivalent",
          "BT470_M"},
      {COLOR_PRIMARIES_BT601_625, "Rec.ITU-R BT.601-6 625 or equivalent",
          "BT601_625"},
      {COLOR_PRIMARIES_BT601_525, "Rec.ITU-R BT.601-6 525 or equivalent",
          "BT601_525"},
      {COLOR_PRIMARIES_GENERIC_FILM, "Generic Film", "GENERIC_FILM"},
      {COLOR_PRIMARIES_BT2020, "Rec.ITU-R BT.2020 or equivalent", "BT2020"},
      {COLOR_PRIMARIES_RP431, "SMPTE RP 431-2 or equivalent", "RP431"},
      {COLOR_PRIMARIES_EG432, "SMPTE EG 432-1 or equivalent", "EG432"},
      {COLOR_PRIMARIES_EBU3213, "EBU Tech.3213-E or equivalent", "EBU3213"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencColorPrimaries", values);
  }
  return qtype;
}

static GType
gst_qvidc_venc_matrix_coeffs_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {COLOR_MATRIX_UNSPECIFIED, "Matrix coefficients are unspecified", "NONE"},
      {COLOR_MATRIX_BT709, "Rec.ITU-R BT.709-5 or equivalent", "BT709"},
      {COLOR_MATRIX_FCC47_73_682,
            "FCC Title 47 CFR 73.682 or equivalent (KR=0.30, KB=0.11)",
          "FCC47_73_682"},
      {COLOR_MATRIX_BT601,
            "FCC Title 47 CFR 73.682 or equivalent (KR=0.30, KB=0.11)",
          "BT601"},
      {COLOR_MATRIX_240M, "SMPTE 240M or equivalent", "240M"},
      {COLOR_MATRIX_BT2020, "Rec.ITU-R BT.2020 non-constant luminance",
          "BT2020"},
      {COLOR_MATRIX_BT2020_CONSTANT, "Rec.ITU-R BT.2020 constant luminance",
          "BT2020_CONSTANT"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencMatrixCoeffs", values);
  }
  return qtype;
}

static GType
gst_qvidc_venc_transfer_characteristics_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {COLOR_TRANSFER_UNSPECIFIED, "Transfer is unspecified", "NONE"},
      {COLOR_TRANSFER_LINEAR, "Linear transfer characteristics", "LINEAR"},
      {COLOR_TRANSFER_SRGB, "sRGB or equivalent", "SRGB"},
      {COLOR_TRANSFER_170M, "SMPTE 170M or equivalent (e.g. BT.601/709/2020)",
          "170M"},
      {COLOR_TRANSFER_GAMMA22, "Assumed display gamma 2.2", "GAMMA22"},
      {COLOR_TRANSFER_GAMMA28, "Assumed display gamma 2.8", "GAMMA28"},
      {COLOR_TRANSFER_ST2084, "SMPTE ST 2084 for 10/12/14/16 bit systems",
          "ST2084"},
      {COLOR_TRANSFER_HLG, "ARIB STD-B67 hybrid-log-gamma", "HLG"},
      {COLOR_TRANSFER_240M, "SMPTE 240M or equivalent", "240M"},
      {COLOR_TRANSFER_XVYCC, "IEC 61966-2-4 or equivalent", "XVYCC"},
      {COLOR_TRANSFER_BT1361, "Rec.ITU-R BT.1361 extended gamut", "BT1361"},
      {COLOR_TRANSFER_ST428, "SMPTE ST 428-1 or equivalent", "ST428"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencTransferChar", values);
  }
  return qtype;
}

static GType
gst_qvidc_venc_full_range_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {COLOR_RANGE_UNSPECIFIED, "Range is unspecified", "NONE"},
      {COLOR_RANGE_FULL, "Full range", "FULL"},
      {COLOR_RANGE_LIMITED, "Limited range", "LIMITED"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencFullRange", values);
  }
  return qtype;
}

static GType
gst_qvidc_venc_intra_refresh_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {IR_NONE, "None", "none"},
      {IR_RANDOM, "Random", "random"},
      {IR_CYCLIC, "Cyclic", "cyclic"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencIntraRefreshMode", values);
  }
  return qtype;
}

static GType
gst_qvidc_venc_bitrate_saving_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {BITRATE_SAVING_MODE_DISABLE_ALL, "Bitrate saving mode disable",
          "disable"},
      {BITRATE_SAVING_MODE_ENABLE_8BIT, "8bit bitrate saving Mode enable",
          "8bit"},
      {BITRATE_SAVING_MODE_ENABLE_10BIT, "10bit bitrate saving Mode enable",
          "10bit"},
      {BITRATE_SAVING_MODE_ENABLE_ALL, "All bitrate saving mode enable", "all"},
      {0xffffffff, "Component Default", "default"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencBitrateSavingMode", values);
  }
  return qtype;
}

static gboolean
gst_qvidc_caps_has_feature (const GstCaps * caps, const gchar * partten)
{
  guint count = gst_caps_get_size (caps);
  gboolean ret = FALSE;

  if (count > 0) {
    for (gint i = 0; i < count; i++) {
      GstCapsFeatures *features = gst_caps_get_features (caps, i);
      if (gst_caps_features_is_any (features))
        continue;
      if (gst_caps_features_contains (features, partten))
        ret = TRUE;
    }
  }

  return ret;
}

static void
parse_roi (GstVideoEncoder * encoder, xmlNodePtr pDynProp)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);

  xmlNodePtr cur = pDynProp->xmlChildrenNode;
  gint id = 0;
  gint64 nFrameNum = -1;

  while (cur) {
    if (!xmlStrcmp (cur->name, (const xmlChar *) "FrameNum")) {
      nFrameNum = strtol ((const char *) cur->children->content, NULL, 10);
    } else if (!xmlStrcmp (cur->name, (const xmlChar *) "ROI")) {
      if (nFrameNum < 0 || (nFrameNum == G_MAXINT64 && errno == ERANGE)) {
        GST_ERROR_OBJECT (enc, "FrameNum out of range or invalid");
        break;
      }

      const char *token = (const char *) cur->children->content;
      static const char *pattern = "%d,%d-%d,%d=%d";
      guint top, left, bottom, right, qp;

      guint count = sscanf (token, pattern,
          &top, &left, &bottom, &right, &qp);
      if (count == 5) {
        GST_DEBUG_OBJECT (enc, "ROI: %ld:%d,%d-%d,%d=%d\n",
            nFrameNum, top, left, bottom, right, qp);

        GstStructure *roimeta = gst_structure_new_empty ("roi-meta");
        if (roimeta) {
          gst_structure_set (roimeta, "frame", G_TYPE_UINT64, nFrameNum, NULL);
          if (bottom == 0 || right == 0) {
            /* region roi info must be configured before encoder start
             * use 0,0-0,0=0 dummy meta to trigger ROI config
             */
            gst_structure_set (roimeta, "roi_type", G_TYPE_STRING, "dummy",
                NULL);
          } else {
            gst_structure_set (roimeta, "roi_type", G_TYPE_STRING, "rect",
                NULL);
          }
          gst_structure_set (roimeta, "id", G_TYPE_INT, id, NULL);
          gst_structure_set (roimeta, "top", G_TYPE_UINT, top, NULL);
          gst_structure_set (roimeta, "left", G_TYPE_UINT, left, NULL);
          gst_structure_set (roimeta, "width", G_TYPE_UINT, right - left, NULL);
          gst_structure_set (roimeta, "height", G_TYPE_UINT, bottom - top,
              NULL);
          gst_structure_set (roimeta, "qp", G_TYPE_UINT, qp, NULL);

          if (enc->roi_array == NULL) {
            enc->roi_array = g_array_new (FALSE, TRUE, sizeof (GstStructure *));
          }

          g_array_append_val (enc->roi_array, roimeta);
          id++;
        }
      } else {
        GST_ERROR_OBJECT (enc, "meta pattern mismatched");
      }
    }
    cur = cur->next;
  }
}

static void
gst_qvidc_venc_build_roi_array (GstVideoEncoder * encoder, const GValue * value)
{
  gchar *roi_xml = g_value_dup_string (value);
  if (roi_xml == NULL) {
    return;
  }

  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  GST_INFO_OBJECT (enc, "roi config path %s", roi_xml);

  xmlDocPtr doc;
  xmlNodePtr cur;

  doc = xmlParseFile (roi_xml);
  g_free (roi_xml);

  if (doc == NULL) {
    GST_ERROR_OBJECT (enc, "roi document not parsed failed.");
    return;
  }

  cur = xmlDocGetRootElement (doc);

  if (cur == NULL) {
    GST_ERROR_OBJECT (enc, "empty roi document");
    xmlFreeDoc (doc);
    return;
  }
  // find session root
  cur = cur->xmlChildrenNode;
  while (cur) {
    if (!xmlStrcmp (cur->name, (const xmlChar *) "EncodeSession")) {
      cur = cur->xmlChildrenNode;
      break;
    }
    cur = cur->next;
  }

  // find dynamic property node
  while (cur) {
    if (!xmlStrcmp (cur->name, (const xmlChar *) "DynamicProperty")) {
      parse_roi (encoder, cur);
    }

    cur = cur->next;
  }

  xmlFreeDoc (doc);
}

static gboolean
gst_qvidc_venc_caps_has_feature (const GstCaps * caps, const gchar * partten)
{
  guint count = gst_caps_get_size (caps);
  gboolean ret = FALSE;

  if (count > 0) {
    for (gint i = 0; i < count; i++) {
      GstCapsFeatures *features = gst_caps_get_features (caps, i);
      if (gst_caps_features_is_any (features))
        continue;
      if (gst_caps_features_contains (features, partten)) {
        ret = TRUE;
        break;
      }
    }
  }

  return ret;
}

static gboolean
gst_qvidc_config_pool (GstVideoEncoder * encoder, GstQuery * query,
    BUFFER_PORT_TYPE port)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  guint size = 0, metasize = 0;
  guint min = 0, max = 0;
  GstBufferPoolInitParam param;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  gboolean update = FALSE;
  gboolean use_peer_pool = FALSE;
  GstAllocationParams params = { (GstMemoryFlags) 0 };
  GstCaps *caps = NULL;
  GstVideoInfo info;

  GST_DEBUG_OBJECT (enc, "start config pool port %s",
      port == BUFFER_PORT_INPUT ? "in" : "out");

  if (query) {
    gst_query_parse_allocation (query, &caps, NULL);
    GST_DEBUG_OBJECT (enc, "caps %" GST_PTR_FORMAT, caps);
    if (!caps) {
      GST_WARNING_OBJECT (enc, "failed to get caps");
      goto cleanup;
    } else {
      GST_INFO_OBJECT (enc, "allocation caps: %" GST_PTR_FORMAT, caps);

      if (!gst_video_info_from_caps (&info, caps)) {
        GST_INFO_OBJECT (enc, "failed to get video info");
        goto cleanup;
      }

      if (gst_qvidc_venc_caps_has_feature (caps,
              GST_CAPS_FEATURE_MEMORY_DMABUF)) {
        GST_INFO_OBJECT (enc, "peer has dmabuf feature");
        param.mode = GST_QVIDC_DMABUF_HEAP_MODE;
        enc->use_external_buf = TRUE;
      } else {
        GST_INFO_OBJECT (enc,
            "peer component does not support dmabuf feature: %" GST_PTR_FORMAT,
            caps);
      }
    }

    if (gst_query_get_n_allocation_params (query) > 0) {
      gst_query_parse_nth_allocation_param (query, 0, NULL, &params);
      GST_DEBUG_OBJECT (enc, "peer query has params flag 0x%x", params.flags);
    }

    if (gst_query_get_n_allocation_pools (query) > 0) {
      GST_DEBUG_OBJECT (enc, "peer query has pool");
      update = TRUE;
      guint size_ext = 0;
      guint min_ext = 0, max_ext = 0;
      GstStructure *config_ext;

      gst_query_parse_nth_allocation_pool (query, 0, &pool, &size_ext, &min_ext,
          &max_ext);
      GST_DEBUG_OBJECT (enc,
          "Use buffer pool from peer: pool: %p, size: %u, "
          "min_buffers: %u, max_buffers: %u", pool, size_ext, min_ext, max_ext);
      gst_object_unref (pool);
      pool = NULL;
    } else {
      GST_WARNING_OBJECT (enc, "Failed to parse peer proposed pool");
    }
  } else {
    GST_WARNING_OBJECT (enc, "peer does not propose buffer pool, "
        "reset use_external_buf flag to false");
  }

  if (!vidc_getAllocationCountAndSize (enc->comp, port, &min, &size, &metasize)) {
    GST_ERROR_OBJECT (enc, "get allocation failed");
    return FALSE;
  }

  if (enc->use_external_buf && port == BUFFER_PORT_INPUT) {
    GST_ERROR_OBJECT (enc, "external buffer mode for input");
    return TRUE;
  }

  memset (&param, 0, sizeof (GstBufferPoolInitParam));
  if (port == BUFFER_PORT_INPUT) {
    param.info.size = size;
    param.is_ubwc = enc->is_ubwc;
    param.is_outport = FALSE;
  } else {
    param.info.size = size;
    param.is_outport = TRUE;
  }
  param.mode = GST_QVIDC_DMABUF_HEAP_MODE;

  param.gst_vidc_comp = gst_vidc_comp_ref (enc->gst_vidc_comp);
  param.metasize = metasize;
  max = min;

  pool = gst_qvidc_buffer_pool_new (&param);
  GST_DEBUG_OBJECT (enc, "allocation: size:%u min:%u max:%u pool:%"
      GST_PTR_FORMAT, size, min, max, pool);

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, port == BUFFER_PORT_OUTPUT ?
      enc->output_state->caps : enc->input_state->caps, size, min, max);

  GST_DEBUG_OBJECT (enc, "setting own pool config to %" GST_PTR_FORMAT, config);

  /* configure own pool */
  gst_buffer_pool_set_active (pool, FALSE);
  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (enc, "configure our own buffer pool failed");
    gst_structure_free (config);
    goto cleanup;
  }

  /* For simplicity, simply read back the active configuration, so our base
   * class get the right information */
  config = gst_buffer_pool_get_config (pool);
  if (!gst_buffer_pool_config_get_params (config, NULL, &size, &min, &max)) {
    GST_ERROR_OBJECT (enc, "Can't get buffer pool config param");
    gst_structure_free (config);
    goto cleanup;
  }
  gst_structure_free (config);

  GST_DEBUG_OBJECT (enc, "setting pool with size %d, min: %d, max: %d",
      size, min, max);

  if (query) {
    if (update) {
      if (use_peer_pool) {
        GST_DEBUG_OBJECT (enc,
            "update peer pool %p size %d, min %d, max %d to query %p",
            param.ext_pool, size, min, max, query);
        gst_query_set_nth_allocation_pool (query, 0, param.ext_pool, size, min,
            max);
      } else {
        GST_DEBUG_OBJECT (enc,
            "update buffer pool %p size %d, min %d, max %d to query %p", pool,
            size, min, max, query);
        gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
      }
    } else {
      GST_DEBUG_OBJECT (enc,
          "add buffer pool %p size %d, min %d, max %d to query %p", pool, size,
          min, max, query);
      if (port == BUFFER_PORT_INPUT) {
        gst_query_add_allocation_pool (query, 0, size, min, max);
      } else {
        gst_query_add_allocation_pool (query, pool, size, min, max);
      }
    }
  }

  GST_DEBUG_OBJECT (enc, "activate pool %" GST_PTR_FORMAT, pool);
  gst_buffer_pool_set_active (pool, TRUE);

  for (gint i = 0; i < min; i++) {
    GstBuffer *buffer = NULL;
    GstMemory *memory = NULL;

    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (enc, "quit use buffer loop");
      goto cleanup;
    }

    GstBufferPoolAcquireParamsExt params_ext;
    memset (&params_ext, 0, sizeof (GstBufferPoolAcquireParamsExt));
    params_ext.params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
    ret = gst_buffer_pool_acquire_buffer (pool, &buffer, &params_ext);
    if (ret == GST_FLOW_OK) {
      memory = gst_buffer_peek_memory (buffer, 0);
      if (memory) {
        gint fd;
        if (gst_is_dmabuf_memory (memory)) {
          fd = gst_dmabuf_memory_get_fd (memory);
        } else {
          fd = gst_fd_memory_get_fd (memory);
        }
        GST_DEBUG_OBJECT (enc,
            "Acquired buffer fd: %d in buffer: %p from pool: %p", fd,
            buffer, pool);

        gsize offset = 0;
        gsize maxsize = 0;
        gst_memory_get_sizes (memory, &offset, &maxsize);
        GST_DEBUG_OBJECT (enc, "mem offset %d, maxsize %d", offset, maxsize);

        BufferDescriptor buf;
        memset (&buf, 0, sizeof (BufferDescriptor));
        buf.capacity = maxsize - offset;
        buf.fd = fd;
        buf.size = maxsize - offset;
        buf.port_type = port;

        if (!vidc_alloc (enc->comp, &buf)) {
          GST_ERROR_OBJECT (enc, "setBuffer %d failed pool: %p", buf.fd, pool);
          gst_buffer_unref (buffer);
          ret = GST_FLOW_NOT_NEGOTIATED;
          goto cleanup;
        }
      }

      gst_buffer_pool_release_buffer (pool, buffer);
    } else {
      GST_ERROR_OBJECT (enc, "no buffer found from pool %p", pool);
      ret = GST_FLOW_NOT_NEGOTIATED;
      goto cleanup;
    }
  }

  if (port == BUFFER_PORT_INPUT) {
    if (enc->in_port_pool) {
      gst_object_unref (enc->in_port_pool);
    }
    enc->in_port_pool = pool;
  } else {
    if (enc->out_port_pool) {
      gst_object_unref (enc->out_port_pool);
    }
    enc->out_port_pool = pool;
  }

  return (ret == GST_FLOW_OK ? TRUE : FALSE);

cleanup:
  if (pool) {
    gst_object_unref (pool);
  }

  return FALSE;
}

static gboolean
gst_qvidc_venc_create_component (GstVideoEncoder * encoder)
{
  gboolean ret = FALSE;
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);

  GST_DEBUG_OBJECT (enc, "create_component");

  if (enc->comp_store) {

    ret =
        vidcStore_createComponent (enc->comp_store, enc->comp_name,
        &enc->comp, NULL);
    if (ret == FALSE) {
      GST_DEBUG_OBJECT (enc, "Failed to create component");
    }

    ret =
        vidc_setListener (enc->comp, encoder, handle_video_event,
        BLOCK_MODE_MAY_BLOCK);
    if (ret == FALSE) {
      GST_DEBUG_OBJECT (enc, "Failed to set event handler");
    }
  } else {
    GST_DEBUG_OBJECT (enc, "Component store is Null");
  }

  if (TRUE == ret) {
    if (G_UNLIKELY (enc->gst_vidc_comp)) {
      gst_vidc_comp_unref (enc->gst_vidc_comp);
      GST_DEBUG_OBJECT (enc, "unref previous gst vidc component");
    }

    GST_DEBUG_OBJECT (enc, "create gst vidc comp");
    enc->gst_vidc_comp = gst_vidc_comp_create (enc->comp);
    if (!enc->gst_vidc_comp) {
      ret = FALSE;
      GST_ERROR_OBJECT (enc, "failed to create gst vidc comp");
    }
  }

  if (!ret) {
    if (enc->comp) {
      vidc_delete (enc->comp);
      enc->comp = NULL;
      GST_ERROR_OBJECT (enc, "clean up vidc comp adapter since error happened");
    }
  }

  return ret;
}

static GstFlowReturn
gst_qvidc_venc_setup_output (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *outcaps;

  GST_DEBUG_OBJECT (enc, "setup_output");

  if (enc->output_state) {
    gst_video_codec_state_unref (enc->output_state);
  }

  outcaps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  if (outcaps) {
    GstStructure *structure;
    gchar *comp_name;

    if (gst_caps_is_empty (outcaps)) {
      gst_caps_unref (outcaps);
      GST_ERROR_OBJECT (enc, "Unsupported format in caps: %" GST_PTR_FORMAT,
          outcaps);
      return GST_FLOW_ERROR;
    }

    outcaps = gst_caps_make_writable (outcaps);
    outcaps = gst_caps_fixate (outcaps);
    structure = gst_caps_get_structure (outcaps, 0);

    /* Fill actual width/height into output caps */
    GValue g_width = { 0, };
    GValue g_height = { 0, };
    g_value_init (&g_width, G_TYPE_INT);
    g_value_set_int (&g_width, enc->width);

    g_value_init (&g_height, G_TYPE_INT);
    g_value_set_int (&g_height, enc->height);

    if ((enc->rotation == 90) || (enc->rotation == 270)) {
      gst_caps_set_value (outcaps, "width", &g_height);
      gst_caps_set_value (outcaps, "height", &g_width);
    } else {
      gst_caps_set_value (outcaps, "width", &g_width);
      gst_caps_set_value (outcaps, "height", &g_height);
    }

    GST_INFO_OBJECT (enc, "Fixed output caps: %" GST_PTR_FORMAT, outcaps);

    comp_name = get_vidc_comp_name (structure);
    if (!comp_name) {
      GST_ERROR_OBJECT (enc, "Unsupported format in caps: %" GST_PTR_FORMAT,
          outcaps);
      gst_caps_unref (outcaps);
      return GST_FLOW_ERROR;
    }

    enc->comp_name = comp_name;
    enc->output_state =
        gst_video_encoder_set_output_state (encoder, outcaps, state);
    if (!enc->output_state) {
      GST_ERROR_OBJECT (enc, "set output state error");
      gst_caps_unref (outcaps);
      g_free (comp_name);
      return GST_FLOW_ERROR;
    }
    enc->output_setup = TRUE;

    if ((enc->rotation == 90) || (enc->rotation == 270)) {
      enc->output_state->info.width = enc->height;
      enc->output_state->info.height = enc->width;
    }
  }

  return ret;
}

/* Called when the element stops processing. Close external resources. */
static gboolean
gst_qvidc_venc_stop (GstVideoEncoder * encoder)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);

  GST_DEBUG_OBJECT (enc, "stop");
  enc->input_setup = FALSE;
  enc->output_setup = FALSE;

  /* Stop the component */
  if (enc->comp) {
    vidc_stop (enc->comp, BUFFER_PORT_INPUT);
    vidc_stop (enc->comp, BUFFER_PORT_OUTPUT);
  }

  return TRUE;
}

/* Dispatch any pending remaining data at EOS. Class can refuse to encode new data after. */
static GstFlowReturn
gst_qvidc_venc_finish (GstVideoEncoder * encoder)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  gint64 end_time = 0;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buffer = NULL;
  GstMemory *mem = NULL;
  guint count = 0;

  GST_DEBUG_OBJECT (enc, "finish");
  //TODO: queue EOS buffer

  /* wait for all the pending buffers to return */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  g_mutex_lock (&enc->pending_lock);

  end_time =
      g_get_monotonic_time () + (EOS_WAITING_TIMEOUT * G_TIME_SPAN_SECOND);
  while (!enc->eos_reached) {
    GST_DEBUG_OBJECT (enc, "wait until EOS signal is triggered");

    if (!g_cond_wait_until (&enc->pending_cond, &enc->pending_lock, end_time)) {
      GST_ERROR_OBJECT (enc, "Timed out on wait, exiting!");
      break;
    }
  }

  enc->eos_reached = FALSE;

  g_mutex_unlock (&enc->pending_lock);
  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  return GST_FLOW_OK;
}

static gboolean
caps_has_compression (const GstCaps * caps, const gchar * compression)
{
  GstStructure *structure = NULL;
  const gchar *string = NULL;

  structure = gst_caps_get_structure (caps, 0);
  string = gst_structure_has_field (structure, "compression") ?
      gst_structure_get_string (structure, "compression") : NULL;

  return (g_strcmp0 (string, compression) == 0) ? TRUE : FALSE;
}

/* Called to inform the caps describing input video data that encoder is about to receive.
  Might be called more than once, if changing input parameters require reconfiguration. */
static gboolean
gst_qvidc_venc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  GstQvidcVencClass *enc_class = GST_QVIDC_VENC_GET_CLASS (encoder);
  GstStructure *structure;
  const gchar *mode;
  const gchar *fmt;
  gint retval = 0;
  gint width = 0;
  gint height = 0;
  GstVideoFormat input_format = GST_VIDEO_FORMAT_UNKNOWN;
  GstVideoInterlaceMode interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
  GstVideoMasteringDisplayInfo display_info;
  GstVideoContentLightLevel content_light_level;
  GPtrArray *config = NULL;
  ConfigParams resolution;
  ConfigParams pixelformat;
  ConfigParams mirror;
  ConfigParams rotation;
  ConfigParams rate_control;
  ConfigParams downscale;
  ConfigParams color_space_conversion;
  ConfigParams color_aspects;
  ConfigParams intra_refresh;
  ConfigParams intra_refresh_type;
  ConfigParams bitrate;
  gboolean update_bitrate = FALSE;
  gboolean update_i_interval = FALSE;
  ConfigParams slice_mode;
  ConfigParams blur_info;
  ConfigParams bitrate_saving_mode;
  ConfigParams framerate;
  ConfigParams intraframes_period;
  ConfigParams inline_header;
  ConfigParams qp_ranges;
  ConfigParams qp_init;
  ConfigParams report_frame_qp;
  ConfigParams temporal_layer;
  ConfigParams ltr_count;
  ConfigParams hdr_static_info;
  ConfigParams codectype;
  ConfigParams dyn_buffer;
  gfloat fps = COMMON_FRAMERATE;

  GST_DEBUG_OBJECT (enc, "set_format");

  structure = gst_caps_get_structure (state->caps, 0);
  retval = gst_structure_get_int (structure, "width", &width);
  retval &= gst_structure_get_int (structure, "height", &height);
  if (!retval) {
    goto error_res;
  }

  fmt = gst_structure_get_string (structure, "format");
  if (fmt) {
    input_format = gst_video_format_from_string (fmt);
    if (input_format == GST_VIDEO_FORMAT_UNKNOWN) {
      goto error_format;
    }
  }

  GST_DEBUG_OBJECT (enc, "caps: %" GST_PTR_FORMAT, state->caps);
  enc->is_ubwc = caps_has_compression (state->caps, "ubwc");
  GST_DEBUG_OBJECT (enc, "Fixed color format:%s, UBWC:%d", fmt, enc->is_ubwc);

  if (enc->input_state) {
    gst_video_codec_state_unref (enc->input_state);
  }

  enc->input_state = gst_video_codec_state_ref (state);

  gst_video_info_from_caps (&enc->input_info, state->caps);

  if (enc->input_setup) {
    /* Already setup, check to see if something has changed on input caps... */
    if ((enc->width == width) && (enc->height == height)) {
      goto done;                /* Nothing has changed */
    } else {
      gst_qvidc_venc_stop (encoder);
    }
  }

  if ((mode = gst_structure_get_string (structure, "interlace-mode"))) {
    if (g_str_equal ("progressive", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
    } else if (g_str_equal ("interleaved", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
    } else if (g_str_equal ("mixed", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
    } else if (g_str_equal ("fields", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_FIELDS;
    }
  }

  enc->width = width;
  enc->height = height;
  enc->interlace_mode = interlace_mode;
  enc->input_format = input_format;

  if (GST_FLOW_OK != gst_qvidc_venc_setup_output (encoder, state)) {
    GST_ERROR_OBJECT (enc, "fail to setup output");
    goto error_output;
  }

  if (enc->comp_name && strstr (enc->comp_name, "heic")) {
    enc->is_heic = TRUE;
  }

  /* Create component */
  if (!gst_qvidc_venc_create_component (encoder)) {
    GST_ERROR_OBJECT (enc, "Failed to create component");
  }

  GST_DEBUG_OBJECT (enc,
      "set with: %d, height: %d, format: %x, rc mode: %d",
      enc->width, enc->height, enc->input_format, enc->rcMode);

  if (enc_class->set_format) {
    if (!enc_class->set_format (enc, state)) {
      GST_ERROR_OBJECT (enc, "Subclass failed to set the new format");
      return FALSE;
    }
  }

  config = g_ptr_array_new ();

  codectype = make_codec_param (enc->comp_name);
  g_ptr_array_add (config, &codectype);

  resolution = make_resolution_param (width, height, TRUE);
  g_ptr_array_add (config, &resolution);

  pixelformat =
      make_pixel_format_param (gst_to_vidc_pixelformat (encoder, input_format),
      TRUE);
  g_ptr_array_add (config, &pixelformat);

  if (enc->input_info.fps_n != 0 && enc->input_info.fps_d != 0) {
    fps = (float) enc->input_info.fps_n / enc->input_info.fps_d;
    GST_DEBUG_OBJECT (enc, "got fps %0.2f from caps", fps);
  }
  framerate = make_framerate_param (fps, FALSE);
  g_ptr_array_add (config, &framerate);
  GST_DEBUG_OBJECT (enc, "set framerate %0.2f", fps);

  if (enc->target_bitrate > 0) {
    bitrate = make_bitrate_param (enc->target_bitrate, FALSE);
    g_ptr_array_add (config, &bitrate);
    GST_DEBUG_OBJECT (enc, "set target bitrate:%u", enc->target_bitrate);
    update_bitrate = TRUE;
  }

  dyn_buffer = make_dynamic_buffer_mode_param (enc->use_external_buf);
  g_ptr_array_add (config, &dyn_buffer);

  if (!vidc_config (enc->comp, config, BLOCK_MODE_DONT_BLOCK)) {
    GST_WARNING_OBJECT (enc, "Failed to set config");
    goto error_format;
  } else {
    if (update_bitrate) {
      enc->configured_target_bitrate = enc->target_bitrate;
    }
  }

  g_ptr_array_free (config, TRUE);

  if (!gst_video_encoder_negotiate (encoder)) {
    GST_ERROR_OBJECT (enc, "Failed to negotiate with downstream");
    goto error_output;
  }

  GST_DEBUG_OBJECT (enc, "vidc component started");

done:
  enc->input_setup = TRUE;
  return TRUE;

  /* Errors */
error_format:
  {
    if (config) {
      g_ptr_array_free (config, TRUE);
    }

    GST_ERROR_OBJECT (enc, "Unsupported format in caps: %" GST_PTR_FORMAT,
        state->caps);
    return FALSE;
  }
error_res:
  {
    GST_ERROR_OBJECT (enc, "Unable to get width/height value");
    return FALSE;
  }
error_output:
  {
    GST_ERROR_OBJECT (enc, "Unable to set output state");
    return FALSE;
  }
error_config:
  {
    GST_ERROR_OBJECT (enc, "Unable to configure the component");
    return FALSE;
  }
}

/* Called when the element changes to GST_STATE_READY */
static gboolean
gst_qvidc_venc_open (GstVideoEncoder * encoder)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (enc, "open");

  enc->comp = NULL;
  enc->comp_intf = NULL;
  enc->input_setup = FALSE;
  enc->output_setup = FALSE;
  enc->eos_reached = FALSE;
  enc->input_state = NULL;
  enc->output_state = NULL;
  enc->in_port_pool = NULL;
  enc->out_port_pool = NULL;
  enc->width = 0;
  enc->height = 0;
  enc->frame_index = 0;
  enc->num_output_done = 0;
  enc->gst_vidc_comp = NULL;

  /* Create component store */
  enc->comp_store = vidcStore_create ();

  return ret;
}

/* Called when the element changes to GST_STATE_NULL */
static gboolean
gst_qvidc_venc_close (GstVideoEncoder * encoder)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);

  GST_DEBUG_OBJECT (enc, "qvidc_venc_close");

  if (enc->in_port_pool) {
    GST_DEBUG_OBJECT (enc, "in pool ref cnt:%d",
        GST_OBJECT_REFCOUNT (enc->in_port_pool));
    gst_object_unref (enc->in_port_pool);
    enc->in_port_pool = NULL;
  }

  if (enc->out_port_pool) {
    GST_DEBUG_OBJECT (enc, "out pool ref cnt:%d",
        GST_OBJECT_REFCOUNT (enc->out_port_pool));
    gst_object_unref (enc->out_port_pool);
    enc->out_port_pool = NULL;
  }

  if (enc->gst_vidc_comp) {
    gst_vidc_comp_unref (enc->gst_vidc_comp);
    enc->gst_vidc_comp = NULL;
  }

  if (enc->comp_store) {
    vidcStore_delete (enc->comp_store);
    enc->comp_store = NULL;
  }

  if (enc->input_state) {
    gst_video_codec_state_unref (enc->input_state);
    enc->input_state = NULL;
  }

  if (enc->output_state) {
    gst_video_codec_state_unref (enc->output_state);
    enc->output_state = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_qvidc_venc_force_idr (GstQvidcVenc * encoder)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GST_DEBUG_OBJECT (encoder, "gst_qvidc_venc_force_idr");

  GPtrArray *config = g_ptr_array_new ();
  if (config) {
    ConfigParams force_idr = make_force_idr_param (TRUE);
    g_ptr_array_add (config, &force_idr);

    if (!vidc_config (encoder->comp_intf, config, BLOCK_MODE_MAY_BLOCK)) {
      GST_WARNING_OBJECT (encoder, "Failed to set force-IDR config");
      ret = GST_FLOW_ERROR;
    }
    g_ptr_array_free (config, TRUE);
  }

  return ret;
}

static void
gst_qvidc_venc_handle_dynamic_config (GstVideoEncoder * encoder)
{
  GPtrArray *config = NULL;
  ConfigParams bitrate;
  ConfigParams intraframes_period;
  ConfigParams framerate;
  gfloat fps = COMMON_FRAMERATE;
  guint32 update_prop_mask = 0;

  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);

  if (enc->output_state->info.fps_n != 0 && enc->output_state->info.fps_d != 0) {
    // retrieve last fps first if exists
    fps = (float) enc->output_state->info.fps_n / enc->output_state->info.fps_d;
  }

  if ((enc->target_bitrate > 0) &&
      (enc->target_bitrate != enc->configured_target_bitrate)) {
    bitrate = make_bitrate_param (enc->target_bitrate, FALSE);
    GST_DEBUG_OBJECT (enc, "Dynamically configure target bitrate to %u from %u",
        enc->target_bitrate, enc->configured_target_bitrate);
    update_prop_mask |= DYNAMIC_PROP_BITRATE;
  }

  if (enc->input_state->info.fps_n != 0 && enc->input_state->info.fps_d != 0) {
    fps = (float) enc->input_state->info.fps_n / enc->input_state->info.fps_d;

    if (enc->output_state->info.fps_n != enc->input_state->info.fps_n
        || enc->output_state->info.fps_d != enc->input_state->info.fps_d) {
      if (enc->interval_intraframes != enc->configured_interval_intraframes) {
        // need to reset framerate while I-interval changing combined
        GST_DEBUG_OBJECT (enc, "reset fps as i-interval changing combined");
        framerate = make_framerate_param (fps, FALSE);
      } else {
        framerate = make_dynamic_framerate_param (fps);
      }
      GST_DEBUG_OBJECT (enc,
          "Dynamically config target framerate to %0.2f from %0.2f", fps,
          (float) enc->output_state->info.fps_n /
          enc->output_state->info.fps_d);
      update_prop_mask |= DYNAMIC_PROP_FRAMERATE;
    }
  }

  if (enc->interval_intraframes != enc->configured_interval_intraframes) {
    intraframes_period =
        make_intraframes_period_param (enc->interval_intraframes, fps);
    GST_DEBUG_OBJECT (enc,
        "Dynamically configure interval intraframes: %u, framerate: %f, "
        "intraframes period: %" G_GINT64_FORMAT, enc->interval_intraframes, fps,
        intraframes_period.val.i64);
    update_prop_mask |= DYNAMIC_PROP_IFRAME;
  }

  if (update_prop_mask) {
    config = g_ptr_array_new ();

    if (config) {
      if (update_prop_mask & DYNAMIC_PROP_BITRATE) {
        g_ptr_array_add (config, &bitrate);
      }

      if (update_prop_mask & DYNAMIC_PROP_FRAMERATE) {
        g_ptr_array_add (config, &framerate);
      }

      if (update_prop_mask & DYNAMIC_PROP_IFRAME) {
        g_ptr_array_add (config, &intraframes_period);
      }

      if (!vidc_config (enc->comp_intf, config, BLOCK_MODE_MAY_BLOCK)) {
        GST_WARNING_OBJECT (enc,
            "Failed to set encoder config for prop_mask 0x%x",
            update_prop_mask);
      } else {
        if (update_prop_mask & DYNAMIC_PROP_BITRATE) {
          enc->configured_target_bitrate = enc->target_bitrate;
        }

        if (update_prop_mask & DYNAMIC_PROP_FRAMERATE) {
          enc->output_state->info.fps_n = enc->input_state->info.fps_n;
          enc->output_state->info.fps_d = enc->input_state->info.fps_d;
        }

        if (update_prop_mask & DYNAMIC_PROP_IFRAME) {
          enc->configured_interval_intraframes = enc->interval_intraframes;
        }
      }
      g_ptr_array_free (config, TRUE);
    }
  }
}

/* Called whenever a input frame from the upstream is sent to encoder */
static GstFlowReturn
gst_qvidc_venc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (enc, "handle_frame");

  g_return_val_if_fail (frame != NULL, GST_FLOW_ERROR);

  if (!enc->input_setup) {
    goto done;
  }

  if (!enc->output_setup) {
    ret = GST_FLOW_ERROR;
    goto done;
  }

  if (!enc->in_port_pool) {
    if (!gst_qvidc_config_pool (encoder, NULL, BUFFER_PORT_INPUT)) {
      GST_ERROR_OBJECT (enc, "failed to config pool in");
      ret = GST_FLOW_ERROR;
      goto done;
    }
  }

  GST_DEBUG ("Frame number : %d, pts: %" GST_TIME_FORMAT,
      frame->system_frame_number, GST_TIME_ARGS (frame->pts));

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    GST_INFO_OBJECT (enc, "Forcing key frame");
    if (GST_FLOW_OK != gst_qvidc_venc_force_idr (enc)) {
      GST_ERROR_OBJECT (enc, "Failed to force key frame");
    }
  }
  //TODO: add dynamic config support
  // gst_qvidc_venc_handle_dynamic_config (encoder);

  /* Encode frame */
  ret = gst_qvidc_venc_encode (encoder, frame);

done:
  gst_video_codec_frame_unref (frame);

  return ret;
}

static gboolean
gst_qvidc_venc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (enc, "enter query %" GST_PTR_FORMAT, query);

  ret = gst_qvidc_config_pool (encoder, query, BUFFER_PORT_INPUT);
  if (!ret) {
    goto cleanup;
  }

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);

cleanup:
  if (enc->in_port_pool)
    gst_object_unref (enc->in_port_pool);

  return ret;
}

static gboolean
gst_qvidc_venc_decide_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  GstCaps *outcaps;
  gboolean ret = FALSE;

  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);

  GST_DEBUG_OBJECT (enc, "decide allocation");

  gst_query_parse_allocation (query, &outcaps, NULL);

  GST_DEBUG_OBJECT (enc, "allocation caps: %" GST_PTR_FORMAT, outcaps);
  GST_DEBUG_OBJECT (enc, "allocation params: %" GST_PTR_FORMAT, query);

  ret = gst_qvidc_config_pool (encoder, query, BUFFER_PORT_OUTPUT);

  return ret;
}

static GstBuffer *
fill_output_buffer (GstQvidcVenc * enc, GstVideoInfo * vinfo,
    BufferDescriptor * desc)
{
  GstBuffer *out_buf = NULL;
  GstVideoCodecState *state;
  GstFlowReturn ret = GST_FLOW_OK;
  guint output_size = desc->size;
  GstBufferPoolAcquireParamsExt param_ext;

  memset (&param_ext, 0, sizeof (GstBufferPoolAcquireParamsExt));

  param_ext.fd = desc->fd;
  param_ext.meta_fd = desc->meta_fd;
  param_ext.index = desc->index;
  param_ext.size = desc->size;
  param_ext.vidc_buf = desc->vidcBuffer;
  param_ext.params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
  ret = gst_buffer_pool_acquire_buffer (enc->out_port_pool, &out_buf,
      (GstBufferPoolAcquireParams *) & param_ext);

  if (ret == GST_FLOW_OK && out_buf) {
    GST_ERROR_OBJECT (enc,
        "acquired output gst buffer, desc size %d, out size %d", desc->size,
        gst_buffer_get_size (out_buf));

    GST_BUFFER_PTS (out_buf) = gst_util_uint64_scale (desc->timestamp,
        GST_SECOND, TICKS_PER_SECOND);

    if (vinfo->fps_n > 0) {
      GST_BUFFER_DURATION (out_buf) = gst_util_uint64_scale (GST_SECOND,
          vinfo->fps_d, vinfo->fps_n);
    }

    GST_LOG_OBJECT (enc,
        "gstbuf:%p, PTS:%lu, duration:%lu, fps_d:%d, fps_n:%d, size %d",
        out_buf, GST_BUFFER_PTS (out_buf), GST_BUFFER_DURATION (out_buf),
        vinfo->fps_d, vinfo->fps_n, desc->size);

  } else {
    GST_ERROR_OBJECT (enc, "Fail to acquire output gst buffer");
    if (out_buf) {
      gst_buffer_unref (out_buf);
      out_buf = NULL;
    }
  }

  return out_buf;
}

/* Push encoded frame to downstream element */
static GstFlowReturn
push_frame_downstream (GstVideoEncoder * encoder, BufferDescriptor * desc)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstVideoCodecFrame *frame = NULL;
  GstBuffer *outbuf = NULL;
  GstVideoCodecState *state = NULL;

  GST_LOG_OBJECT (enc, "push frame downstream");

  state = gst_video_encoder_get_output_state (encoder);
  if (NULL == state) {
    GST_ERROR_OBJECT (enc, "video codec state is NULL, unexpected!");
    goto out;
  }

  frame = gst_video_encoder_get_frame (encoder, desc->index);
  if (frame == NULL) {
    GST_ERROR_OBJECT (enc, "failed to get frame by index: %lu", desc->index);
    goto out;
  }

  outbuf = fill_output_buffer (enc, &state->info, desc);
  frame->output_buffer = outbuf;
  if (NULL == outbuf) {
    GST_ERROR_OBJECT (enc, "failed to create outbuf");
    if (desc->flag & FLAG_TYPE_INCOMPLETE) {
      ret = gst_video_encoder_finish_subframe (encoder, frame);
    } else {
      ret = gst_video_encoder_finish_frame (encoder, frame);
    }
    goto out;
  }

  if (desc->flag & FLAG_TYPE_INCOMPLETE) {
    GST_ERROR_OBJECT (enc, "gst_video_encoder_finish_subframe");
    ret = gst_video_encoder_finish_subframe (encoder, frame);
  } else {
    GST_ERROR_OBJECT (enc, "gst_video_encoder_finish_frame");
    ret = gst_video_encoder_finish_frame (encoder, frame);
  }

  if (ret == GST_FLOW_FLUSHING) {
    GST_WARNING_OBJECT (enc, "downstream is flushing");
  } else if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (enc, "failed to finish frame, outbuf: %p", outbuf);
  }

  if (state)
    gst_video_codec_state_unref (state);

  return ret;

out:
  queue_vidc_bufferDesc (desc, encoder);

  if (state)
    gst_video_codec_state_unref (state);

  return ret;
}

static void
queue_vidc_bufferDesc (BufferDescriptor * buffer, gpointer user_data)
{
  GstVideoEncoder *encoder = (GstVideoEncoder *) user_data;
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  GST_DEBUG_OBJECT (enc,
      "buffer=%p, mem fd %d, capacity %d, size %d, port %d, index %d",
      buffer, buffer->fd, buffer->capacity, buffer->size, buffer->port_type,
      buffer->index);

  buffer->size = 0;

  if (!vidc_queue (enc->comp, buffer)) {
    GST_ERROR_OBJECT (enc, "queueBuffer %d failed", buffer->fd);
  }
}

/* Handle event from VIDC */
static void
handle_video_event (const void *handle, EVENT_TYPE type, void *data)
{
  GstVideoEncoder *encoder = (GstVideoEncoder *) handle;
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (enc, "handle_video_event");

  switch (type) {
    case EVENT_INPUTS_DONE:{
      BufferDescriptor *in_buf = (BufferDescriptor *) data;
      GstBufferPool *pool = enc->in_port_pool;
      GST_DEBUG_OBJECT (enc,
          "EVENT_INPUTS_DONE buffer fd %d, index %d to pool %p", in_buf->fd,
          in_buf->index, pool);

      if (enc->use_external_buf) {
        GST_DEBUG_OBJECT (enc,
          "external buffer, release by upstream");
      } else {
        gint64 key = ((gint64) in_buf->fd << 32) | ((gint64) in_buf->meta_fd);
        GstBuffer *gst_buf = gst_qvidc_buffer_pool_find_buffer (pool, key);
        if (gst_buf) {
          gst_buffer_pool_release_buffer (pool, gst_buf);
        }
      }
      GST_DEBUG_OBJECT (enc,
          "EVENT_INPUTS_DONE pending_lock buffer fd %d, index %d to pool %p",
          in_buf->fd, in_buf->index, pool);
      g_mutex_lock (&enc->pending_lock);
      g_cond_signal (&enc->pending_cond);
      g_mutex_unlock (&enc->pending_lock);
      GST_DEBUG_OBJECT (enc,
          "EVENT_INPUTS_DONE pending_lock done buffer fd %d, index %d to pool %p",
          in_buf->fd, in_buf->index, pool);
    }
      break;

    case EVENT_OUTPUTS_DONE:{
      BufferDescriptor *outBuffer = (BufferDescriptor *) data;

      GST_LOG_OBJECT (enc, "Event output done, va: %p, offsets: %"
          G_GSIZE_FORMAT " %" G_GSIZE_FORMAT ", index: %lu, fd: %u,"
          "filled len: %u, buffer size: %u, timestamp: %lu, flag: %x",
          outBuffer->data, outBuffer->offset[0], outBuffer->offset[1],
          outBuffer->index, outBuffer->fd, outBuffer->size, outBuffer->capacity,
          outBuffer->timestamp, outBuffer->flag);

      if (outBuffer->fd > 0 || outBuffer->size > 0) {
        ret = push_frame_downstream (encoder, outBuffer);
        if (ret != GST_FLOW_FLUSHING && ret != GST_FLOW_OK) {
          GST_ERROR_OBJECT (enc, "Failed to push frame downstream");
        }

        enc->num_output_done++;
        GST_LOG_OBJECT (enc, "output done, count: %lu", enc->num_output_done);
      } else if (outBuffer->flag & FLAG_TYPE_END_OF_STREAM) {
        GST_INFO_OBJECT (enc, "Encoder reached EOS");
        g_mutex_lock (&enc->pending_lock);
        enc->eos_reached = TRUE;
        g_cond_signal (&enc->pending_cond);
        g_mutex_unlock (&enc->pending_lock);
      } else {
        GST_ERROR_OBJECT (enc, "Invalid output buffer");
      }
      break;
    }
    case EVENT_ERROR:{
      GST_ERROR_OBJECT (enc, "EVENT_ERROR(%d)", *(gint32 *) data);
      GST_ELEMENT_ERROR (enc, STREAM, ENCODE, ("Encoder posts an error"),
          (NULL));
      break;
    }
    case EVENT_RECONFIG:{
      GST_DEBUG_OBJECT (enc, "Ignore event:reconfig:%d on enc", type);
      break;
    }
    default:{
      GST_ERROR_OBJECT (enc, "Invalid Event(%d)", type);
    }
  }
}

static void
_free_roi_struct (GstQvidcVenc * enc)
{
  if (enc->roi_type) {
    g_free (enc->roi_type);
    enc->roi_type = NULL;
  }
  if (enc->roi_rect_payload) {
    g_free (enc->roi_rect_payload);
    enc->roi_rect_payload = NULL;
  }
  if (enc->roi_rect_payload_ext) {
    g_free (enc->roi_rect_payload_ext);
    enc->roi_rect_payload_ext = NULL;
  }
}

static gboolean
_allocate_roi_struct (GstQvidcVenc * enc)
{
  gboolean ret = TRUE;

  /* allocate these structures only once */
  if (!enc->roi_type)
    enc->roi_type = g_malloc (ROI_ARRAY_SIZE * sizeof (char));
  if (!enc->roi_rect_payload)
    enc->roi_rect_payload = g_malloc (ROI_ARRAY_SIZE * sizeof (char));
  if (!enc->roi_rect_payload_ext)
    enc->roi_rect_payload_ext = g_malloc (ROI_ARRAY_SIZE * sizeof (char));

  if (!enc->roi_type || !enc->roi_rect_payload || !enc->roi_rect_payload_ext) {
    _free_roi_struct (enc);
    GST_ERROR_OBJECT (enc, "Failed to allocate ROI structure");
    ret = FALSE;
  }

  return ret;
}

static gboolean
handle_dynamic_meta (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  GstMeta *meta = NULL;
  gpointer state = NULL;
  gboolean result = TRUE;
  GPtrArray *config = NULL;

  gchar roi_config_param[ROI_ARRAY_SIZE];
  gchar roi_type[ROI_ARRAY_SIZE];
  gint config_param_len = 0;
  memset (roi_config_param, 0, sizeof (roi_config_param));
  memset (roi_type, 0, sizeof (roi_type));

  while ((meta =
          gst_buffer_iterate_meta_filtered (frame->input_buffer, &state,
              GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE))) {

    GstVideoRegionOfInterestMeta *roi = (GstVideoRegionOfInterestMeta *) meta;
    GstStructure *roimeta =
        gst_video_region_of_interest_meta_get_param (roi, "roi-meta");
    if (roimeta) {
      guint right = roi->x + roi->w;
      guint bottom = roi->y + roi->h;
      guint qp;
      gst_structure_get_uint (roimeta, "qp", &qp);

      char rect_qp[ROI_ARRAY_SIZE];
      gint rect_qp_len =
          g_snprintf (rect_qp, sizeof (rect_qp), "%d,%d-%d,%d=%d;",
          roi->y, roi->x, bottom, right, qp);
      if (config_param_len + rect_qp_len < sizeof (roi_config_param)) {
        config_param_len =
            g_strlcat (roi_config_param, rect_qp, sizeof (roi_config_param));
      } else {
        GST_WARNING_OBJECT (enc, "failed to append roi for frame[%lu:%d]=%s, "
            "will ignore subsequent roi parameters", enc->frame_index, roi->id,
            rect_qp);
        g_warn_if_fail (FALSE && "failed to append roi");
        break;
      }

      g_strlcpy (roi_type, g_quark_to_string (roi->roi_type),
          sizeof (roi_type));
    }
  }

  if (config_param_len > 0) {
    config = g_ptr_array_new ();
    if (config) {
      ConfigParams roiRegion;
      if (_allocate_roi_struct (enc) == FALSE) {
        result = FALSE;
        goto out;
      }
      roiRegion = make_roi_param (enc, NANO_TO_MILLI (frame->pts),
          roi_type, roi_config_param, roi_config_param);

      GST_INFO_OBJECT (enc, "frame[%lu]: roi_type %s, %s",
          enc->frame_index, roi_type, roi_config_param);
      g_ptr_array_add (config, &roiRegion);

      if (!vidc_config (enc->comp_intf, config, BLOCK_MODE_MAY_BLOCK)) {
        GST_WARNING_OBJECT (enc, "Failed to set encoder config for ROI");
      }
    }
  }

out:
  if (config)
    g_ptr_array_free (config, TRUE);
  return result;
}

static void
handle_ltr (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  gint ltr_mark_array_size = gst_value_array_get_size (&enc->ltr_mark);
  gint ltr_use_array_size = gst_value_array_get_size (&enc->ltr_use);

  if (ltr_mark_array_size) {
    gint i;
    for (i = 0; i < ltr_mark_array_size; i++) {
      const GValue *mark_frame_idx =
          gst_value_array_get_value (&enc->ltr_mark, i);
      guint32 ltr_mark_frame, ltr_mark_idx;
      ltr_mark_frame =
          g_value_get_int (gst_value_array_get_value (mark_frame_idx, 0));
      if (enc->frame_index == ltr_mark_frame) {
        ltr_mark_idx =
            g_value_get_int (gst_value_array_get_value (mark_frame_idx, 1));
        GST_DEBUG_OBJECT (enc, "ltr-mark %d:%d", ltr_mark_frame, ltr_mark_idx);

        GPtrArray *config = NULL;
        config = g_ptr_array_new ();
        if (config) {
          ConfigParams ltr_mark;
          ltr_mark = make_ltr_mark_param (ltr_mark_idx);
          g_ptr_array_add (config, &ltr_mark);

          if (!vidc_config (enc->comp_intf, config, BLOCK_MODE_MAY_BLOCK)) {
            GST_WARNING_OBJECT (enc, "Failed to set ltr-mark encoder config");
          }

          g_ptr_array_free (config, TRUE);
        }

        break;
      }
    }
  }

  if (ltr_use_array_size) {
    gint i;
    for (i = 0; i < ltr_use_array_size; i++) {
      const GValue *use_frame_idx =
          gst_value_array_get_value (&enc->ltr_use, i);
      guint32 ltr_use_frame, ltr_use_idx;
      ltr_use_frame =
          g_value_get_int (gst_value_array_get_value (use_frame_idx, 0));
      if (enc->frame_index == ltr_use_frame) {
        ltr_use_idx =
            g_value_get_int (gst_value_array_get_value (use_frame_idx, 1));
        GST_DEBUG_OBJECT (enc, "ltr-use %d:%d", ltr_use_frame, ltr_use_idx);

        GPtrArray *config = NULL;
        config = g_ptr_array_new ();
        if (config) {
          ConfigParams ltr_use;
          ltr_use = make_ltr_use_param (ltr_use_idx);
          g_ptr_array_add (config, &ltr_use);

          if (!vidc_config (enc->comp_intf, config, BLOCK_MODE_MAY_BLOCK)) {
            GST_WARNING_OBJECT (enc, "Failed to set ltr-use encoder config");
          }

          g_ptr_array_free (config, TRUE);
        }

        break;
      }
    }
  }
}

static void
add_roi_to_frame (GstVideoEncoder * encoder, GstVideoCodecFrame * frame,
    GstStructure * roimeta)
{
  if (encoder == NULL || frame == NULL || roimeta == NULL) {
    return;
  }

  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);

  guint x, y, w, h, qp;
  gst_structure_get_uint (roimeta, "left", &x);
  gst_structure_get_uint (roimeta, "top", &y);
  gst_structure_get_uint (roimeta, "width", &w);
  gst_structure_get_uint (roimeta, "height", &h);
  gst_structure_get_uint (roimeta, "qp", &qp);

  gint id;
  gst_structure_get_int (roimeta, "id", &id);

  GstVideoRegionOfInterestMeta *meta =
      gst_buffer_add_video_region_of_interest_meta (frame->input_buffer,
      gst_structure_get_string (roimeta, "roi_type"), x, y, w, h);
  if (meta) {
    meta->id = id;

    gst_video_region_of_interest_meta_add_param (meta,
        gst_structure_copy (roimeta));

    GST_DEBUG_OBJECT (enc,
        "frame[%lu] add VideoRegionOfInterestMeta[%d] %d-%d-%d-%d=%d",
        enc->frame_index, id, y, x, x + w, y + h, qp);
  }
}

static void
build_roi_meta (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  if (encoder == NULL || frame == NULL) {
    return;
  }

  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);

  GArray *array = enc->roi_array;
  if (array) {
    guint64 index = enc->frame_index;
    for (guint i = 0; i < array->len; i++) {
      GstStructure *roimeta = g_array_index (array, GstStructure *, i);
      if (roimeta) {
        guint64 frame_num;
        gst_structure_get_uint64 (roimeta, "frame", &frame_num);
        if (index == frame_num) {
          add_roi_to_frame (encoder, frame, roimeta);
        }
      }
    }
  }
}

static gboolean
gst_qvidc_venc_refresh_input_layout_info (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame, BufferDescriptor * bufinfo)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);

  bufinfo->stride[0] = GST_VIDEO_INFO_COMP_STRIDE (&enc->input_info, 0);
  bufinfo->stride[1] = GST_VIDEO_INFO_COMP_STRIDE (&enc->input_info, 1);
  bufinfo->offset[0] = GST_VIDEO_INFO_COMP_OFFSET (&enc->input_info, 0);
  bufinfo->offset[1] = GST_VIDEO_INFO_COMP_OFFSET (&enc->input_info, 1);

  GST_DEBUG_OBJECT (enc, "layout info width %u, height %u, "
      "stride0 %d, stride1 %d, "
      "offset0 %" G_GSIZE_FORMAT ", offset1 %" G_GSIZE_FORMAT,
      bufinfo->width, bufinfo->height, bufinfo->stride[0],
      bufinfo->stride[1], bufinfo->offset[0], bufinfo->offset[1]);

  const GstVideoMeta *meta = gst_buffer_get_video_meta (frame->input_buffer);
  if (meta) {
    g_return_val_if_fail (meta->format == bufinfo->format, FALSE);
    g_return_val_if_fail (meta->n_planes == 2, FALSE);
    g_return_val_if_fail (meta->stride[0] > 0, FALSE);
    g_return_val_if_fail (meta->stride[0] == meta->stride[1], FALSE);

    GST_INFO_OBJECT (enc, "GstVideoMeta format %d, width %u, height %u, "
        "stride0 %d, stride1 %d, "
        "offset0 %" G_GSIZE_FORMAT ", offset1 %" G_GSIZE_FORMAT,
        meta->format, meta->width, meta->height, meta->stride[0],
        meta->stride[1], meta->offset[0], meta->offset[1]);

    bufinfo->width = meta->width;
    bufinfo->height = meta->height;
    bufinfo->stride[0] = meta->stride[0];
    bufinfo->stride[1] = meta->stride[1];
    bufinfo->offset[0] = meta->offset[0];
    bufinfo->offset[1] = meta->offset[1];
  }

  return TRUE;
}

/* Push frame to VIDC */
static GstFlowReturn
gst_qvidc_venc_encode (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (encoder);
  BufferDescriptor inBuf;
  GstBuffer *buf = NULL;
  GstMemory *mem;
  GstMapInfo mapinfo = { 0, };
  gboolean mem_mapped = FALSE;
  gboolean status = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (enc, "enter");

  memset (&inBuf, 0, sizeof (BufferDescriptor));

  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  buf = frame->input_buffer;
  mem = gst_buffer_get_memory (buf, 0);

  if (gst_is_dmabuf_memory (mem)) {
    GST_DEBUG_OBJECT (enc, "dmabuf_memory");
    inBuf.fd = gst_dmabuf_memory_get_fd (mem);
    inBuf.size = gst_memory_get_sizes (mem, NULL, NULL);
    inBuf.capacity = inBuf.size;
    GST_DEBUG_OBJECT (enc, "input vidc buffer fd:%d, size %u",
        inBuf.fd, inBuf.size);
  } else if (gst_is_fd_memory (mem)) {
    GST_DEBUG_OBJECT (enc, "fd emory %d", gst_fd_memory_get_fd (mem));
    inBuf.fd = gst_fd_memory_get_fd (mem);
    inBuf.size = gst_memory_get_sizes (mem, NULL, NULL);
    inBuf.capacity = inBuf.size;
  } else {
    GST_DEBUG_OBJECT (enc, "non-dmabuf_memory/fd_memory");
    gst_buffer_map (buf, &mapinfo, GST_MAP_READ);
    mem_mapped = TRUE;
    inBuf.fd = -1;
    inBuf.data = mapinfo.data;
    inBuf.size = mapinfo.size;
  }

  inBuf.timestamp = NANO_TO_MILLI (frame->pts);
  inBuf.index = frame->system_frame_number;
  inBuf.width = enc->width;
  inBuf.height = enc->height;
  inBuf.format = enc->input_format;
  inBuf.ubwc_flag = enc->is_ubwc;
  inBuf.heic_flag = enc->is_heic;
  inBuf.port_type = BUFFER_PORT_INPUT;

  gst_memory_unref (mem);

  g_warn_if_fail (gst_qvidc_venc_refresh_input_layout_info (encoder, frame,
          &inBuf) && "invalid input layout info");

  GST_DEBUG_OBJECT (enc,
      "input buffer: fd: %d, va:%p, size: %d, timestamp: %lu, index: %ld, "
      "stride %u, width %u, height %u",
      inBuf.fd, inBuf.data, inBuf.size, inBuf.timestamp, inBuf.index,
      inBuf.stride[0], inBuf.width, inBuf.height);

  GstBuffer *inter_buf = NULL;
  GstMemory *inter_mem = NULL;

  if (!mem_mapped) {
    GST_DEBUG_OBJECT (enc, "external buffer, no mapped, zero-copy");

    /* external mode, only support metadata buffer from vidc dec now
     * TODO: support generic metadata from upstream
     */
    gint meta_fd = -1;
    guint metasize = 0;
    gst_vidc_buffer_get_custom_meta (buf, "GstQVIDCDMeta", &meta_fd, &metasize);

    inBuf.meta_fd = meta_fd;
    inBuf.metasize = metasize;

    if (!vidc_queue (enc->comp, &inBuf)) {
      ret = GST_FLOW_ERROR;
      goto out;
    }
  } else {
    GST_DEBUG_OBJECT (enc, "acquire_inter_buffer");
    do {
      GstBufferPoolAcquireParamsExt params_ext;
      memset (&params_ext, 0, sizeof (GstBufferPoolAcquireParamsExt));
      params_ext.params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
      ret =
          gst_buffer_pool_acquire_buffer (enc->in_port_pool, &inter_buf,
              &params_ext);
      if (ret == GST_FLOW_OK) {
        break;
      } else {
        GST_DEBUG_OBJECT (enc, "acquire_inter_buffer failed");
      }

      GST_DEBUG_OBJECT (enc, "try wait and acquire again");
      guint64 end_time =
          g_get_monotonic_time () + (ACQUIRE_TIMEOUT * G_TIME_SPAN_MILLISECOND);
      g_mutex_lock (&(enc->pending_lock));
      if (!g_cond_wait_until (&enc->pending_cond, &enc->pending_lock, end_time)) {
        GST_ERROR_OBJECT (enc, "Timed out on wait");
      }
      g_mutex_unlock (&(enc->pending_lock));
      GST_DEBUG_OBJECT (enc, "acquire_inter_buffer pending_lock done");
    } while (enc->input_setup);

    if (ret != GST_FLOW_OK || inter_buf == NULL) {
      GST_ERROR_OBJECT (enc, "Failed to acquire_buffer downstream");
      ret = GST_FLOW_NOT_NEGOTIATED;
      goto out;
    } else {
      GST_DEBUG_OBJECT (enc, "acquire_inter_buffer done");
      inter_mem = gst_buffer_peek_memory (inter_buf, 0);
      if (inter_mem) {
        gint fd;
        if (gst_is_dmabuf_memory (inter_mem)) {
          fd = gst_dmabuf_memory_get_fd (inter_mem);
        } else {
          fd = gst_fd_memory_get_fd (inter_mem);
        }
        GST_DEBUG_OBJECT (enc,
            "Acquired internal buffer fd: %d in buffer: %p mem %p from pool: %p",
            fd, inter_buf, inter_mem, enc->in_port_pool);
        gsize offset = 0;
        gsize maxsize = 0;
        gst_memory_get_sizes (inter_mem, &offset, &maxsize);
        GST_DEBUG_OBJECT (enc, "mem offset %d, maxsize %d", offset, maxsize);

        BufferDescriptor vidcbuf;
        memset (&vidcbuf, 0, sizeof (BufferDescriptor));
        vidcbuf.fd = fd;
        vidcbuf.port_type = BUFFER_PORT_INPUT;

        GstMapInfo info;
        gst_memory_map (inter_mem, &info, GST_MAP_WRITE);
        GST_DEBUG_OBJECT (enc, "mem data %p, size %d, maxsize %d, ubwc_flag %d",
            info.data, info.size, info.maxsize, inBuf.ubwc_flag);
        if (inBuf.ubwc_flag) {
          memcpy (info.data, inBuf.data, inBuf.size);
        } else {
          if (!writePlane (enc->comp, info.data, &inBuf)) {
            ret = GST_FLOW_ERROR;
            gst_memory_unmap (inter_mem, &info);
            gst_buffer_unref (inter_buf);
            goto out;
          }
        }

        gint meta_fd = -1;
        guint metasize = 0;
        gst_vidc_buffer_get_custom_meta (inter_buf, "GstQVIDCEMeta", &meta_fd, &metasize);

        vidcbuf.data = info.data;
        vidcbuf.capacity = info.maxsize;
        vidcbuf.size = inBuf.size;
        vidcbuf.index = enc->frame_index;
        vidcbuf.timestamp = inBuf.timestamp;
        vidcbuf.meta_fd = meta_fd;
        vidcbuf.metasize = metasize;
        gst_memory_unmap (inter_mem, &info);

        if (!vidc_queue (enc->comp, &vidcbuf)) {
          ret = GST_FLOW_ERROR;
          gst_buffer_unref (inter_buf);
          goto out;
        }
      }
    }
  }
  enc->frame_index += 1;

  GST_DEBUG_OBJECT (enc, "queue input out frame_index %d", enc->frame_index);
out:
  /* unmap the gstbuffer if it's mapped */
  if (mem_mapped) {
    gst_buffer_unmap (buf, &mapinfo);
  }
  GST_DEBUG_OBJECT (enc, "stream lock ret %d", ret);
  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  GST_DEBUG_OBJECT (enc, "ret %d", ret);
  return ret;
}

static void
gst_qvidc_venc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{

  GstQvidcVenc *enc = GST_QVIDC_VENC (object);

  GST_DEBUG_OBJECT (enc, "qvidc_venc_set_property");

  switch (prop_id) {
    case PROP_MIRROR:
      enc->mirror = g_value_get_enum (value);
      break;
    case PROP_ROTATION:
      enc->rotation = g_value_get_uint (value);
      break;
    case PROP_BLUR_MODE:
      enc->blur_mode = g_value_get_enum (value);
      break;
    case PROP_BLUR_WIDTH:
      enc->blur_width = g_value_get_uint (value);
      break;
    case PROP_BLUR_HEIGHT:
      enc->blur_height = g_value_get_uint (value);
      break;
    case PROP_RATE_CONTROL:
      enc->rcMode = g_value_get_enum (value);
      break;
    case PROP_DOWNSCALE_WIDTH:
      enc->downscale_width = g_value_get_uint (value);
      break;
    case PROP_DOWNSCALE_HEIGHT:
      enc->downscale_height = g_value_get_uint (value);
      break;
    case PROP_COLOR_SPACE_PRIMARIES:
      enc->primaries = g_value_get_enum (value);
      break;
    case PROP_COLOR_SPACE_MATRIX_COEFFS:
      enc->matrix = g_value_get_enum (value);
      break;
    case PROP_COLOR_SPACE_TRANSFER_CHAR:
      enc->transfer_char = g_value_get_enum (value);
      break;
    case PROP_COLOR_SPACE_FULL_RANGE:
      enc->full_range = g_value_get_enum (value);
      break;
    case PROP_COLOR_SPACE_CONVERSION:
      enc->color_space_conversion = g_value_get_boolean (value);
      break;
    case PROP_INTRA_REFRESH_MODE:
      enc->intra_refresh_mode = g_value_get_enum (value);
      break;
    case PROP_INTRA_REFRESH_MBS:
      enc->intra_refresh_mbs = g_value_get_uint (value);
      break;
    case PROP_TARGET_BITRATE:
      enc->target_bitrate = g_value_get_uint (value);
      break;
    case PROP_SLICE_SIZE:
      enc->slice_size = g_value_get_uint (value);
      break;
    case PROP_SLICE_MODE:
      enc->slice_mode = g_value_get_enum (value);
      break;
    case PROP_ROI:
      if (enc->roi_array) {
        for (guint i = 0; i < enc->roi_array->len; i++) {
          GstStructure *roimeta =
              g_array_index (enc->roi_array, GstStructure *, i);
          if (roimeta) {
            gst_clear_structure (&roimeta);
          }
        }

        g_array_free (enc->roi_array, TRUE);
        enc->roi_array = NULL;
      }

      gst_qvidc_venc_build_roi_array ((GstVideoEncoder *) object, value);
      break;
    case PROP_BITRATE_SAVING_MODE:
      enc->bitrate_saving_mode = g_value_get_enum (value);
      break;
    case PROP_INTERVAL_INTRAFRAMES:
      enc->interval_intraframes = g_value_get_uint (value);
      break;
    case PROP_INLINE_SPSPPS_HEADERS:
      enc->inline_sps_pps_headers = g_value_get_boolean (value);
      break;
    case PROP_MIN_QP_I_FRAMES:
      enc->min_qp_i_frames = g_value_get_uint (value);
      break;
    case PROP_MAX_QP_I_FRAMES:
      enc->max_qp_i_frames = g_value_get_uint (value);
      break;
    case PROP_MIN_QP_P_FRAMES:
      enc->min_qp_p_frames = g_value_get_uint (value);
      break;
    case PROP_MAX_QP_P_FRAMES:
      enc->max_qp_p_frames = g_value_get_uint (value);
      break;
    case PROP_MIN_QP_B_FRAMES:
      enc->min_qp_b_frames = g_value_get_uint (value);
      break;
    case PROP_MAX_QP_B_FRAMES:
      enc->max_qp_b_frames = g_value_get_uint (value);
      break;
    case PROP_INIT_QUANT_I_FRAMES:
      enc->quant_i_frames = g_value_get_uint (value);
      break;
    case PROP_INIT_QUANT_P_FRAMES:
      enc->quant_p_frames = g_value_get_uint (value);
      break;
    case PROP_INIT_QUANT_B_FRAMES:
      enc->quant_b_frames = g_value_get_uint (value);
      break;
    case PROP_REPORT_AVERAGE_FRAME_QP:
      enc->report_average_frame_qp = g_value_get_boolean (value);
      break;
    case PROP_HIER_P:
      enc->hierp_layers = g_value_get_uint (value);
      break;
    case PROP_HIER_B:
      enc->hierb_layers = g_value_get_uint (value);
      break;
    case PROP_BITRATE_RATIOS:
      if (enc->bitrate_ratios) {
        g_free (enc->bitrate_ratios);
        enc->bitrate_ratios = NULL;
      }

      enc->ratio_size = gst_value_array_get_size (value);
      enc->bitrate_ratios = g_new (gfloat, enc->ratio_size);
      if (enc->bitrate_ratios) {
        for (gint i = 0; i < enc->ratio_size; i++) {
          const GValue *ratio = gst_value_array_get_value (value, i);
          enc->bitrate_ratios[i] = g_value_get_float (ratio);
        }
      }
      break;
    case PROP_LTR_COUNT:
      enc->ltr_count = g_value_get_uint (value);
      break;
    case PROP_LTR_MARK:
      if (gst_value_array_get_size (value)) {
        g_value_copy (value, &enc->ltr_mark);
      }
      break;
    case PROP_LTR_USE:
      if (gst_value_array_get_size (value)) {
        g_value_copy (value, &enc->ltr_use);
      }
      break;
    case PROP_USE_EXTERNAL_POOL:
      enc->use_external_buf = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qvidc_venc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{

  GstQvidcVenc *enc = GST_QVIDC_VENC (object);

  GST_DEBUG_OBJECT (enc, "qvidc_venc_get_property");

  switch (prop_id) {
    case PROP_MIRROR:
      g_value_set_enum (value, enc->mirror);
      break;
    case PROP_ROTATION:
      g_value_set_uint (value, enc->rotation);
      break;
    case PROP_BLUR_MODE:
      g_value_set_enum (value, enc->blur_mode);
      break;
    case PROP_BLUR_WIDTH:
      g_value_set_uint (value, enc->blur_width);
      break;
    case PROP_BLUR_HEIGHT:
      g_value_set_uint (value, enc->blur_height);
      break;
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, enc->rcMode);
      break;
    case PROP_DOWNSCALE_WIDTH:
      g_value_set_uint (value, enc->downscale_width);
      break;
    case PROP_DOWNSCALE_HEIGHT:
      g_value_set_uint (value, enc->downscale_height);
      break;
    case PROP_COLOR_SPACE_PRIMARIES:
      g_value_set_enum (value, enc->primaries);
      break;
    case PROP_COLOR_SPACE_MATRIX_COEFFS:
      g_value_set_enum (value, enc->matrix);
      break;
    case PROP_COLOR_SPACE_TRANSFER_CHAR:
      g_value_set_enum (value, enc->transfer_char);
      break;
    case PROP_COLOR_SPACE_FULL_RANGE:
      g_value_set_enum (value, enc->full_range);
      break;
    case PROP_COLOR_SPACE_CONVERSION:
      g_value_set_boolean (value, enc->color_space_conversion);
      break;
    case PROP_INTRA_REFRESH_MODE:
      g_value_set_enum (value, enc->intra_refresh_mode);
      break;
    case PROP_INTRA_REFRESH_MBS:
      g_value_set_uint (value, enc->intra_refresh_mbs);
      break;
    case PROP_TARGET_BITRATE:
      g_value_set_uint (value, enc->target_bitrate);
      break;
    case PROP_SLICE_SIZE:
      g_value_set_uint (value, enc->slice_size);
      break;
    case PROP_SLICE_MODE:
      g_value_set_enum (value, enc->slice_mode);
      break;
    case PROP_BITRATE_SAVING_MODE:
      g_value_set_enum (value, enc->bitrate_saving_mode);
      break;
    case PROP_INTERVAL_INTRAFRAMES:
      g_value_set_uint (value, enc->interval_intraframes);
      break;
    case PROP_INLINE_SPSPPS_HEADERS:
      g_value_set_boolean (value, enc->inline_sps_pps_headers);
      break;
    case PROP_MIN_QP_I_FRAMES:
      g_value_set_uint (value, enc->min_qp_i_frames);
      break;
    case PROP_MAX_QP_I_FRAMES:
      g_value_set_uint (value, enc->max_qp_i_frames);
      break;
    case PROP_MIN_QP_P_FRAMES:
      g_value_set_uint (value, enc->min_qp_p_frames);
      break;
    case PROP_MAX_QP_P_FRAMES:
      g_value_set_uint (value, enc->max_qp_p_frames);
      break;
    case PROP_MIN_QP_B_FRAMES:
      g_value_set_uint (value, enc->min_qp_b_frames);
      break;
    case PROP_MAX_QP_B_FRAMES:
      g_value_set_uint (value, enc->max_qp_b_frames);
      break;
    case PROP_INIT_QUANT_I_FRAMES:
      g_value_set_uint (value, enc->quant_i_frames);
      break;
    case PROP_INIT_QUANT_P_FRAMES:
      g_value_set_uint (value, enc->quant_p_frames);
      break;
    case PROP_INIT_QUANT_B_FRAMES:
      g_value_set_uint (value, enc->quant_b_frames);
      break;
    case PROP_REPORT_AVERAGE_FRAME_QP:
      g_value_set_boolean (value, enc->report_average_frame_qp);
      break;
    case PROP_LTR_COUNT:
      g_value_set_uint (value, enc->ltr_count);
      break;
    case PROP_LTR_MARK:
      if (gst_value_array_get_size (&enc->ltr_mark)) {
        g_value_copy (&enc->ltr_mark, value);
      }
      break;
    case PROP_LTR_USE:
      if (gst_value_array_get_size (&enc->ltr_use)) {
        g_value_copy (&enc->ltr_use, value);
      }
      break;
    case PROP_USE_EXTERNAL_POOL:
      g_value_set_boolean (value, enc->use_external_buf);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Called during object destruction process */
static void
gst_qvidc_venc_finalize (GObject * object)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (object);

  GST_DEBUG_OBJECT (enc, "finalize");

  g_mutex_clear (&enc->pending_lock);
  g_cond_clear (&enc->pending_cond);

  g_free (enc->comp_name);

  if (enc->comp_name) {
    enc->comp_name = NULL;
  }

  if (enc->bitrate_ratios) {
    g_free (enc->bitrate_ratios);
    enc->bitrate_ratios = NULL;
  }

  g_value_unset (&enc->ltr_mark);

  g_value_unset (&enc->ltr_use);

  if (enc->roi_array) {
    for (guint i = 0; i < enc->roi_array->len; i++) {
      GstStructure *roimeta = g_array_index (enc->roi_array, GstStructure *, i);
      if (roimeta) {
        gst_clear_structure (&roimeta);
      }
    }

    g_array_free (enc->roi_array, TRUE);
    enc->roi_array = NULL;
  }

  _free_roi_struct (enc);

  /* Lastly chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_qvidc_venc_change_state (GstElement * element, GstStateChange transition)
{
  GstQvidcVenc *enc = GST_QVIDC_VENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_LOG_OBJECT (enc, "encoder state change from PAUSED to READY");
      break;
    default:
      break;
  }
  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static void
gst_qvidc_venc_class_init (GstQvidcVencClass * klass)
{
  GstVideoEncoderClass *video_encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  /* Set GObject class property */
  gobject_class->set_property = gst_qvidc_venc_set_property;
  gobject_class->get_property = gst_qvidc_venc_get_property;
  gobject_class->finalize = gst_qvidc_venc_finalize;

  /* Add property to this class */
  g_object_class_install_property (gobject_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control", "Rate Control",
          "Bitrate control method",
          GST_TYPE_VIDC_ENC_RATE_CONTROL,
          RC_OFF,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MIRROR,
      g_param_spec_enum ("mirror", "Mirror Type",
          "Specify the mirror type",
          GST_TYPE_VIDC_ENC_MIRROR_TYPE,
          MIRROR_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_ROTATION,
      g_param_spec_uint ("rotation", "Rotation",
          "Specify the angle of clockwise rotation. [0|90|180|270]",
          0, 270, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_BLUR_MODE,
      g_param_spec_enum ("blur-mode", "Blur Mode",
          "Specify the blur mode",
          GST_TYPE_VIDC_ENC_BLUR_MODE,
          DEFAULT_BLUR_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_BLUR_WIDTH,
      g_param_spec_uint ("blur-width", "Blur Width",
          "Specify the blur filter width.",
          0, UINT_MAX, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_BLUR_HEIGHT,
      g_param_spec_uint ("blur-height", "Blur Height",
          "Specify the blur filter height.",
          0, UINT_MAX, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DOWNSCALE_WIDTH,
      g_param_spec_uint ("downscale-width", "Downscale width",
          "Specify the downscale width", 0, UINT_MAX, 0, G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_DOWNSCALE_HEIGHT, g_param_spec_uint ("downscale-height",
          "Downscale height", "Specify the downscale height", 0, UINT_MAX, 0,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_COLOR_SPACE_PRIMARIES,
      g_param_spec_enum ("color-primaries", "Input colour primaries",
          "Chromaticity coordinates of the source primaries",
          GST_TYPE_VIDC_ENC_COLOR_PRIMARIES,
          COLOR_PRIMARIES_UNSPECIFIED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_COLOR_SPACE_MATRIX_COEFFS, g_param_spec_enum ("matrix-coeffs",
          "Input matrix coefficients",
          "Matrix coefficients used in deriving luma and chroma signals from RGB primaries",
          GST_TYPE_VIDC_ENC_MATRIX_COEFFS, COLOR_MATRIX_UNSPECIFIED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_COLOR_SPACE_TRANSFER_CHAR, g_param_spec_enum ("transfer-char",
          "Input transfer characteristics",
          "The opto-electronic transfer characteristics to use.",
          GST_TYPE_VIDC_ENC_TRANSFER_CHAR, COLOR_TRANSFER_UNSPECIFIED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_COLOR_SPACE_FULL_RANGE,
      g_param_spec_enum ("full-range", "Full range flag",
          "Black level and range of the luma and chroma signals.",
          GST_TYPE_VIDC_ENC_FULL_RANGE,
          COLOR_RANGE_UNSPECIFIED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_COLOR_SPACE_CONVERSION,
      g_param_spec_boolean ("color-space-conversion", "Color space conversion",
          "If enabled, should be in color space conversion mode",
          DEFAULT_COLOR_SPACE_CONVERSION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_INTRA_REFRESH_MODE,
      g_param_spec_enum ("intra-refresh-mode", "Intra refresh mode",
          "Intra refresh mode, support random and cyclic mode. Allow IR only for CBR(_CFR/VFR) RC modes",
          GST_TYPE_VIDC_ENC_INTRA_REFRESH_MODE,
          IR_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_INTRA_REFRESH_MBS,
      g_param_spec_uint ("intra-refresh-mbs", "Intra refresh mbs/period",
          "Period of intra refresh, support random and cyclic mode.",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TARGET_BITRATE,
      g_param_spec_uint ("target-bitrate", "Target bitrate",
          "Target bitrate in bits per second (0 means not explicitly set bitrate)",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  g_object_class_install_property (gobject_class, PROP_SLICE_MODE,
      g_param_spec_enum ("slice-mode", "slice mode",
          "Slice mode, support MB and BYTES mode",
          GST_TYPE_VIDC_ENC_SLICE_MODE,
          SLICE_MODE_DISABLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  //4096 is from vidc internal MIN_ENC_SLICE_BYTE_SIZE/MIN_SLICE_BYTE_SIZE
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SLICE_SIZE,
      g_param_spec_uint ("slice-size", "Slice size",
          "Slice size, just set when slice mode setting to MB or Bytes (unit is bit and min is 4096 when mode is Bytes)",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ROI,
      g_param_spec_string ("roi", "ROI config",
          "roi xml config file path", NULL,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_BITRATE_SAVING_MODE,
      g_param_spec_enum ("bps-saving-mode", "Bps saving mode",
          "Bitrate saving mode (0xffffffff=component default)",
          GST_TYPE_VIDC_ENC_BITRATE_SAVING_MODE,
          DEFAULT_BITRATE_SAVING_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_INTERVAL_INTRAFRAMES,
      g_param_spec_uint ("interval-intraframes",
          "Interval of coding Intra frames",
          "Interval of coding Intra frames (0xffffffff=component default)",
          0, G_MAXUINT,
          DEFAULT_INTERVAL_INTRAFRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  g_object_class_install_property (gobject_class, PROP_INLINE_SPSPPS_HEADERS,
      g_param_spec_boolean ("inline-header",
          "Inline SPS/PPS headers before IDR",
          "Inline SPS/PPS header before IDR",
          DEFAULT_INLINE_HEADERS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MIN_QP_I_FRAMES,
      g_param_spec_uint ("min-quant-i-frames", "Min quant I frames",
          "Minimum quantization parameter allowed for I-frames, 0 means no limit",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MAX_QP_I_FRAMES,
      g_param_spec_uint ("max-quant-i-frames", "Max quant I frames",
          "Maximum quantization parameter allowed for I-frames, 0 means no limit",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MIN_QP_P_FRAMES,
      g_param_spec_uint ("min-quant-p-frames", "Min quant P frames",
          "Minimum quantization parameter allowed for P-frames, 0 means no limit",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MAX_QP_P_FRAMES,
      g_param_spec_uint ("max-quant-p-frames", "Max quant P frames",
          "Maximum quantization parameter allowed for P-frames, 0 means no limit",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MIN_QP_B_FRAMES,
      g_param_spec_uint ("min-quant-b-frames", "Min quant B frames",
          "Minimum quantization parameter allowed for B-frames, 0 means no limit",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MAX_QP_B_FRAMES,
      g_param_spec_uint ("max-quant-b-frames", "Max quant B frames",
          "Maximum quantization parameter allowed for B-frames, 0 means no limit",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_INIT_QUANT_I_FRAMES,
      g_param_spec_uint ("init-quant-i-frames", "I-Frame Quantization",
          "Initial quantization parameter for I-frames (0xffffffff=component default), "
          "work for RC-off and RC-on modes",
          0, G_MAXUINT, DEFAULT_INIT_QUANT_I_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_INIT_QUANT_P_FRAMES,
      g_param_spec_uint ("init-quant-p-frames", "P-Frame Quantization",
          "Initial quantization parameter for P-frames (0xffffffff=component default), "
          "work for RC-off and RC-on modes",
          0, G_MAXUINT, DEFAULT_INIT_QUANT_P_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_INIT_QUANT_B_FRAMES,
      g_param_spec_uint ("init-quant-b-frames", "B-Frame Quantization",
          "Initial quantization parameter for B-frames (0xffffffff=component default), "
          "work for RC-off and RC-on modes",
          0, G_MAXUINT, DEFAULT_INIT_QUANT_B_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_REPORT_AVERAGE_FRAME_QP,
      g_param_spec_boolean ("report-frame-qp", "Report Frame QP",
          "Return average frame QP for each output frame and attach it to gstbuffer",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HIER_P,
      g_param_spec_uint ("hier-p", "Hier-P",
          "total number of P layers",
          0, G_MAXUINT, 0,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HIER_B,
      g_param_spec_uint ("hier-b", "Hier-B",
          "total number of B layers",
          0, G_MAXUINT, 0,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BITRATE_RATIOS,
      gst_param_spec_array ("bitrate-ratios", "Bitrate ratios",
          "Bitrate ratio array for each layer",
          g_param_spec_float ("bitrate-ratio", "Bitrate ratio",
              "Bitrate budgets for each layer and the layers below, "
              "given as a ratio of the total, stream bitrate",
              0.0, 1.0, 0.0,
              G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LTR_COUNT,
      g_param_spec_uint ("ltr-count", "LTR Count",
          "Specify the ltr count",
          0, 3, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LTR_MARK,
      gst_param_spec_array ("ltr-mark", "LTR Mark Index Array",
          "The ltr mark index array ltr-mark=<<frame,index>, <frame,index>>",
          gst_param_spec_array ("mark-frame", "Mark Frame",
              "The mark frame array <frame,index>",
              g_param_spec_int ("value", "Mark Frame Value",
                  "The value of mark frame and index",
                  0, G_MAXINT, 0,
                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LTR_USE,
      gst_param_spec_array ("ltr-use", "LTR Use Index Array",
          "The ltr use index array ltr-use=<<frame,index>, <frame,index>>",
          gst_param_spec_array ("use-frame", "Use Frame",
              "The use frame array <frame,index>",
              g_param_spec_int ("value", "Use Frame Value",
                  "The value of use frame and index",
                  0, G_MAXINT, 0,
                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_USE_EXTERNAL_POOL, g_param_spec_boolean ("use-external-pool",
          "if allow using external pool",
          "If enabled, encoder will use external buffer pool if supported by upstream.",
          DEFAULT_USE_EXTERNAL_POOL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_qvidc_venc_signals[SIGNAL_FORCE_IDR] = g_signal_new ("force-idr",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstQvidcVencClass, force_idr),
      NULL, NULL, NULL, GST_TYPE_FLOW_RETURN, 0, G_TYPE_NONE);

  video_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_qvidc_venc_stop);
  video_encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_qvidc_venc_set_format);
  video_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_qvidc_venc_handle_frame);
  video_encoder_class->finish = GST_DEBUG_FUNCPTR (gst_qvidc_venc_finish);
  video_encoder_class->open = GST_DEBUG_FUNCPTR (gst_qvidc_venc_open);
  video_encoder_class->close = GST_DEBUG_FUNCPTR (gst_qvidc_venc_close);
  video_encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_qvidc_venc_propose_allocation);
  video_encoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_qvidc_venc_decide_allocation);

  klass->force_idr = GST_DEBUG_FUNCPTR (gst_qvidc_venc_force_idr);

  gst_element_class_set_static_metadata (gstelement_class,
      "vidc video encoder", "Encoder/Video",
      "Video Encoder based on HW codec", "QTI");
}

/* Invoked during object instantiation (equivalent C++ constructor).
 * Initialize only those variables that do not change during state change.
 * For other variables, place initialization into function open.*/
static void
gst_qvidc_venc_init (GstQvidcVenc * enc)
{
  enc->rcMode = RC_OFF;
  enc->mirror = MIRROR_NONE;
  enc->rotation = 0;
  enc->downscale_width = 0;
  enc->downscale_height = 0;
  enc->target_bitrate = 0;
  enc->configured_target_bitrate = 0;
  enc->blur_mode = DEFAULT_BLUR_MODE;
  enc->blur_width = 0;
  enc->blur_height = 0;
  enc->is_ubwc = FALSE;
  enc->roi_array = NULL;
  enc->roi_type = NULL;
  enc->roi_rect_payload = NULL;
  enc->roi_rect_payload_ext = NULL;
  enc->bitrate_saving_mode = DEFAULT_BITRATE_SAVING_MODE;
  enc->silent = FALSE;
  enc->is_heic = FALSE;
  enc->interval_intraframes = DEFAULT_INTERVAL_INTRAFRAMES;
  enc->configured_interval_intraframes = DEFAULT_INTERVAL_INTRAFRAMES;
  enc->inline_sps_pps_headers = DEFAULT_INLINE_HEADERS;

  enc->min_qp_i_frames = 0;
  enc->max_qp_i_frames = 0;
  enc->min_qp_p_frames = 0;
  enc->max_qp_p_frames = 0;
  enc->min_qp_b_frames = 0;
  enc->max_qp_b_frames = 0;
  enc->quant_i_frames = DEFAULT_INIT_QUANT_I_FRAMES;
  enc->quant_p_frames = DEFAULT_INIT_QUANT_P_FRAMES;
  enc->quant_b_frames = DEFAULT_INIT_QUANT_B_FRAMES;
  enc->report_average_frame_qp = FALSE;
  enc->hierp_layers = 0;
  enc->hierb_layers = 0;
  enc->ratio_size = 0;
  enc->bitrate_ratios = NULL;
  enc->ltr_count = 0;
  enc->is_input_dmabuf = FALSE;
  enc->use_external_buf = DEFAULT_USE_EXTERNAL_POOL;

  enc->max_input_buffers = 0;

  g_value_init (&enc->ltr_mark, GST_TYPE_ARRAY);
  g_value_init (&enc->ltr_use, GST_TYPE_ARRAY);
  g_cond_init (&enc->pending_cond);
  g_mutex_init (&enc->pending_lock);
}

gboolean
gst_qvidc_venc_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_qvidc_venc_debug, "qvidcvenc",
      0, "GST QTI VIDC video encoder");

  static gsize res = FALSE;
  static const gchar *tags[] = { NULL };
  if (g_once_init_enter (&res)) {
    gst_meta_register_custom ("GstQVIDCEMeta", tags, NULL, NULL, NULL);
    g_once_init_leave (&res, TRUE);
  }

  guint count = 0;
  for (guint i = 0; i < G_N_ELEMENTS (kENCODER_ELEMENTS); i++) {
    if (gst_element_register (plugin, kENCODER_ELEMENTS[i].element,
            kENCODER_ELEMENTS[i].rank, kENCODER_ELEMENTS[i].register_type ())) {
      count++;
      GST_INFO ("register element %s", kENCODER_ELEMENTS[i].element);
    } else {
      GST_ERROR ("failed to register element %s", kENCODER_ELEMENTS[i].element);
    }
  }

  return count > 0 ? TRUE : FALSE;
}
