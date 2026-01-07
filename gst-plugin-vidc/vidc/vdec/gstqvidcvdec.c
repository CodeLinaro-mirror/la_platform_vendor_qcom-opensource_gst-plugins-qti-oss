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
#include <inttypes.h>

#include "gstqvidcvdec.h"
#include <dlfcn.h>
#include <libdrm/drm_fourcc.h>
#include "gstqvidch264dec.h"
#include "gstqvidch265dec.h"
#include "gstqvidcvp9dec.h"
#include "gstqvidcmpeg2dec.h"
#ifdef GST_SUPPORT_AV1_DEC
#include "gstqvidcav1dec.h"
#endif

GST_DEBUG_CATEGORY (gst_qvidc_vdec_debug);
#define GST_CAT_DEFAULT gst_qvidc_vdec_debug

/* class initialization */
G_DEFINE_TYPE (GstQvidcVdec, gst_qvidc_vdec, GST_TYPE_VIDEO_DECODER);

#define parent_class gst_qvidc_vdec_parent_class
#define NANO_TO_MILLI(x)  ((x) / 1000)
#define EOS_WAITING_TIMEOUT 5
#define EXT_BUF_WAIT_TIMEOUT_MS 100

#define DEFAULT_OUTPUT_PICTURE_ORDER_MODE    (0xffffffff)
#define DEFAULT_LOW_LATENCY_MODE             (FALSE)
#define DEFAULT_SECURE_MODE                  (FALSE)
#define DEFAULT_USE_EXTERNAL_POOL            (FALSE)
#define DEFAULT_RELEASE_INPUT                (TRUE)

/* Function will be named gst_fbuf_modifier_qdata_quark() */
static G_DEFINE_QUARK (FBufModifierQuark, gst_fbuf_modifier_qdata);

#define DECODER_ELEMENT(codec, element, vidc_codec) \
  {"vidc.qti." G_STRINGIFY (codec) ".decoder", \
   "qvidc" G_STRINGIFY (element) "dec", \
   GST_RANK_PRIMARY + 10, \
   gst_qvidc_##element##_dec_get_type, \
   vidc_codec}

static const ElementInfo kDECODER_ELEMENTS[] = {
  DECODER_ELEMENT (avc, h264, VIDC_CODEC_H264),
  DECODER_ELEMENT (hevc, h265, VIDC_CODEC_HEVC),
  DECODER_ELEMENT (vp9, vp9, VIDC_CODEC_VP9),
  DECODER_ELEMENT (mpeg2, mpeg2, VIDC_CODEC_MPEG2),
#ifdef GST_SUPPORT_AV1_DEC
  DECODER_ELEMENT (av1, av1, VIDC_CODEC_AV1),
#endif
};

enum
{
  PROP_0,
  PROP_OUTPUT_PICTURE_ORDER,
  PROP_LOW_LATENCY,
  PROP_USE_EXTERNAL_POOL,
};

/* GstVideoDecoder base class method */
static gboolean gst_qvidc_vdec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_qvidc_vdec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_qvidc_vdec_finish (GstVideoDecoder * decoder);
static GstFlowReturn gst_qvidc_vdec_flush (GstVideoDecoder * decoder);
static gboolean gst_qvidc_vdec_open (GstVideoDecoder * decoder);
static gboolean gst_qvidc_vdec_close (GstVideoDecoder * decoder);
static gboolean gst_qvidc_vdec_stop (GstVideoDecoder * decoder);
static gboolean gst_qvidc_vdec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_qvidc_vdec_src_event (GstVideoDecoder * decoder,
    GstEvent * event);
static void gst_qvidc_vdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_qvidc_vdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_qvidc_vdec_finalize (GObject * object);

static gboolean gst_qvidc_vdec_create_component (GstVideoDecoder * decoder);
static void handle_video_event (const void *handle, EVENT_TYPE type,
    void *data);

static GstFlowReturn gst_qvidc_vdec_decode (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_qvidc_vdec_setup_output (GstVideoDecoder * decoder);
static GstBuffer *gst_qvidc_vdec_wrap_output_buffer (GstVideoDecoder *
    decoder, BufferDescriptor * buffer);
static gboolean gst_qvidc_vdec_caps_has_feature (const GstCaps * caps,
    const gchar * partten);
static GstStateChangeReturn gst_qvidc_vdec_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_qvidc_vdec_sink_event (GstVideoDecoder * decoder,
    GstEvent * event);

static void queue_vidc_buffer (GstBuffer * buffer, gpointer user_data);
static void queue_vidc_bufferDesc (BufferDescriptor * buffer,
    gpointer user_data);

/* pad templates */
static GstStaticPadTemplate gst_vdec_src_template =
    GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (QVIDC_VDEC_RAW_CAPS_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DMABUF, "{ NV12 }")
        ";" QVIDC_VDEC_RAW_CAPS ("{ NV12 }")
        ";" QVIDC_VDEC_RAW_CAPS_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DMABUF, "{ NV12_10LE32 }")
        ";" QVIDC_VDEC_RAW_CAPS ("{ NV12_10LE32 }")
        ";" QVIDC_VDEC_RAW_CAPS_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DMABUF, "{ P010_10LE }")
        ";" QVIDC_VDEC_RAW_CAPS ("{ P010_10LE }")));

static gboolean
_unfixed_caps_has_compression (const GstCaps * caps, const gchar * compression)
{
  GstStructure *structure = NULL;
  gchar *string = NULL;
  guint count = gst_caps_get_size (caps);
  gboolean ret = FALSE;

  for (gint i = 0; i < count; i++) {
    structure = gst_caps_get_structure (caps, i);
    string =
        gst_structure_has_field (structure,
        "compression") ? gst_structure_to_string (structure) : NULL;
    if (string && g_strrstr (string, compression)) {
      ret = TRUE;
    }
    g_free (string);

    if (ret == TRUE) {
      break;
    }
  }

  return ret;
}

static void
modifier_free (gpointer p_modifier)
{
  if (p_modifier) {
    g_slice_free (guint64, p_modifier);
    GST_DEBUG ("modifier_free(%p) val 0x%lx called", p_modifier,
        *(guint64 *) p_modifier);
  } else {
    GST_ERROR ("invalid modifier");
  }

  return;
}

static ConfigParams
make_codec_param (const gchar * name)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_CODEC;
  param.codec = 0;

  guint count = 0;
  for (guint i = 0; i < G_N_ELEMENTS (kDECODER_ELEMENTS); i++) {
    GST_INFO ("element[%d] name %s", i, kDECODER_ELEMENTS[i].codec);
    if (g_strcmp0 (kDECODER_ELEMENTS[i].codec, name) == 0) {
      param.codec = kDECODER_ELEMENTS[i].vidc_codec;
      GST_INFO ("element[%d] codec 0x%x", i, param.codec);
      break;
    }
  }

  GST_INFO ("element name %s, codec 0x%x", name, param.codec);

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

ConfigParams
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
make_output_picture_order_param (guint output_picture_order_mode)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_OUTPUT_PICTURE_ORDER_MODE;
  param.output_picture_order_mode = output_picture_order_mode;

  return param;
}

static ConfigParams
make_low_latency_param (gboolean low_latency_mode)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_DEC_LOW_LATENCY;
  param.low_latency_mode = low_latency_mode;

  return param;
}

static gchar *
get_vidc_comp_name (GstVideoDecoder * decoder, GstStructure * s,
    gboolean low_latency)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  gchar *str = NULL;
  gchar *concat_str = NULL;
  gchar *str_low_latency = g_strdup (".low_latency");
  gchar *str_secure = g_strdup (".secure");
  gchar *str_suffix = NULL;
  gboolean supported = FALSE;
  gboolean secure = dec->secure;
  gint mpegversion = 0;

  if (gst_structure_has_name (s, "video/x-h264")) {
    str = g_strdup ("vidc.qti.avc.decoder");
  } else if (gst_structure_has_name (s, "video/x-h265")) {
    str = g_strdup ("vidc.qti.hevc.decoder");
  } else if (gst_structure_has_name (s, "video/x-vp9")) {
    str = g_strdup ("vidc.qti.vp9.decoder");
  } else if (gst_structure_has_name (s, "video/x-av1")) {
    str = g_strdup ("vidc.qti.av1.decoder");
  } else if (gst_structure_has_name (s, "video/mpeg")) {
    if (gst_structure_get_int (s, "mpegversion", &mpegversion)) {
      if (mpegversion == 2) {
        str = g_strdup ("vidc.qti.mpeg2.decoder");
      }
    }
  }

  if (low_latency) {
    str_suffix = str_low_latency;
    GST_DEBUG_OBJECT (dec, "selected low latency component");
  }
  if (secure) {
    str_suffix = str_secure;
    GST_DEBUG_OBJECT (dec, "selected secure component");
  }

  if (low_latency || secure) {
    concat_str = g_strconcat (str, str_suffix, NULL);
    supported = vidcStore_isComponentSupported (dec->comp_store, concat_str);

    if (supported) {
      if (str)
        g_free (str);
      str = concat_str;
    } else {
      GST_ERROR_OBJECT (dec,
          "selected component %s is not supported, use %s instead!", concat_str,
          str);
      g_warn_if_fail (FALSE && "selected component is not supported!");
      g_free (concat_str);
    }
  }

  if (str_low_latency)
    g_free (str_low_latency);
  if (str_secure)
    g_free (str_secure);

  return str;
}

guint32
gst_to_vidc_pixelformat (GstQvidcVdec * decoder, GstVideoFormat format)
{
  guint32 result = 0;
  GstQvidcVdec *dec = decoder;

  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      if (dec->is_ubwc) {
        result = PIXEL_FORMAT_NV12_UBWC;
      } else {
        result = PIXEL_FORMAT_NV12_LINEAR;
      }
      break;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      result = PIXEL_FORMAT_TP10_UBWC;
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      result = PIXEL_FORMAT_P010;
      break;
    default:
      result = PIXEL_FORMAT_NV12_UBWC;
      GST_WARNING_OBJECT (dec,
          "Invalid pixel format(%d), fallback to NV12 UBWC", format);
      break;
  }

  GST_DEBUG_OBJECT (dec, "GST format (%s), UBWC:%d, VIDC format: %d",
      gst_video_format_to_string (format), dec->is_ubwc, result);

  return result;
}

/* 1. Check whether it's 10bit clip
 * 2. Set 8bit/10bit format */
gboolean
dec_set_vidc_pixel_format (GstQvidcVdec * decoder, GstVideoCodecState * state)
{
  GstQvidcVdec *dec = decoder;
  GstStructure *s = NULL;
  guint bit_depth_luma, bit_depth_chroma;
  GPtrArray *config = NULL;
  GstVideoFormat output_format = GST_VIDEO_FORMAT_NV12;
  ConfigParams pixelformat;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (dec, "dec set format");

  /* check 10bit cases
   * 1: Field bit-depth-luma in caps. It supported since GST 1.13.1 for H265
   *    or GST 1.19.2 for VP9 and AV1.
   * 2. Add bit-depth-luma/chroma in caps explicitly by upstream element
   *    in secure mode*/
  if (dec->check_10bit) {
    GST_DEBUG_OBJECT (dec, "check bit-depth-luma/chroma in caps");
    s = gst_caps_get_structure (state->caps, 0);
    if (s && gst_structure_get_uint (s, "bit-depth-luma", &bit_depth_luma) &&
        gst_structure_get_uint (s, "bit-depth-chroma", &bit_depth_chroma)) {
      if (bit_depth_luma == 10 && bit_depth_chroma == 10) {
        if (dec->is_ubwc) {
          output_format = GST_VIDEO_FORMAT_NV12_10LE32;
        } else {
          output_format = GST_VIDEO_FORMAT_P010_10LE;
        }

        GST_LOG_OBJECT (dec, "set 10bit format: %d (%s)", output_format,
            gst_video_format_to_string (output_format));
      } else if (bit_depth_luma == 12 && bit_depth_chroma == 12) {
        GST_ERROR_OBJECT (dec, "bitdepth 12, not supported yet");
        ret = FALSE;
        goto done;
      }

      /* disable checking and delay_start since bit-depth-chroma parsed */
      dec->check_10bit = FALSE;
      dec->delay_start = FALSE;
    }

    config = g_ptr_array_new ();
    if (config) {
      pixelformat =
          make_pixel_format_param (gst_to_vidc_pixelformat (dec,
              output_format), FALSE);
      GST_LOG_OBJECT (dec, "set vidc output format: %d",
          pixelformat.pixelFormat.fmt);
      g_ptr_array_add (config, &pixelformat);
      if (!vidc_config (dec->comp, config, BLOCK_MODE_MAY_BLOCK)) {
        GST_ERROR_OBJECT (dec, "Failed to set config");
        ret = FALSE;
      }
      g_ptr_array_free (config, TRUE);

      dec->output_format = output_format;
    }

  }
done:

  return ret;
}

static gboolean
gst_qvidc_vdec_create_component (GstVideoDecoder * decoder)
{
  gboolean ret = FALSE;
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);

  GST_DEBUG_OBJECT (dec, "create component");

  if (dec->comp_store) {
    if (!dec->comp) {
      ret =
          vidcStore_createComponent (dec->comp_store, dec->comp_name,
          &dec->comp, &dec->cb);

      if (ret == TRUE) {
        GST_DEBUG_OBJECT (dec, "set listerner to %s component", dec->comp_name);
        ret =
            vidc_setListener (dec->comp, decoder, handle_video_event,
            BLOCK_MODE_MAY_BLOCK);
        if (ret == TRUE) {
          GST_DEBUG_OBJECT (dec, "set listerner done");
        } else {
          GST_ERROR_OBJECT (dec, "Failed to set listerner");
        }
      } else {
        GST_ERROR_OBJECT (dec, "Failed to create component");
      }
    } else {
      GST_WARNING_OBJECT (dec, "already created %s component", dec->comp_name);
      return TRUE;
    }
  } else {
    GST_ERROR_OBJECT (dec, "Component store is Null");
  }

  if (TRUE == ret) {
    if (G_UNLIKELY (dec->gst_vidc_comp)) {
      gst_vidc_comp_unref (dec->gst_vidc_comp);
      GST_DEBUG_OBJECT (dec, "unref previous gst vidc component");
    }

    dec->gst_vidc_comp = gst_vidc_comp_create (dec->comp);
    if (!dec->gst_vidc_comp) {
      ret = FALSE;
      GST_ERROR_OBJECT (dec, "failed to create gst vidc comp");
    }
  }

  if (!ret) {
    if (dec->comp) {
      vidc_delete (dec->comp);
      dec->comp = NULL;
      GST_ERROR_OBJECT (dec, "clean up vidc comp adapter since error happened");
    }
  }

  return ret;
}

gboolean
gst_qvidc_vdec_config_pool (GstVideoDecoder * decoder, GstQuery * query,
    BUFFER_PORT_TYPE port)
{
  GstCaps *outcaps;
  GstFlowReturn ret = GST_FLOW_OK;
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  guint size = 0, metasize = 0;
  guint min = 0, max = 0;
  GstBufferPoolInitParam param;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  gboolean update = FALSE;
  gboolean use_dmabuf = FALSE;
  gboolean use_peer_pool = FALSE;
  GstAllocationParams params = { (GstMemoryFlags) 0 };

  GST_DEBUG_OBJECT (dec, "start config pool port %d", port);

  if (port == BUFFER_PORT_OUTPUT) {
    gst_query_parse_allocation (query, &outcaps, NULL);

    GST_DEBUG_OBJECT (dec, "allocation caps: %" GST_PTR_FORMAT, outcaps);
    GST_DEBUG_OBJECT (dec, "allocation params: %" GST_PTR_FORMAT, query);

    if (gst_qvidc_vdec_caps_has_feature (outcaps,
            GST_CAPS_FEATURE_MEMORY_DMABUF)) {
      use_dmabuf = TRUE;
      GST_INFO_OBJECT (dec, "peer supports DMA buffer");
    } else {
      GST_INFO_OBJECT (dec,
          "peer doesn't support DMA buffer, use FD buffer instead");
      use_dmabuf = FALSE;
    }

    use_peer_pool = dec->use_external_buf;
  }

  if (!vidc_getAllocationCountAndSize (dec->comp, port, &min, &size, &metasize)) {
    GST_ERROR_OBJECT (dec, "get allocation failed");
    return FALSE;
  }

  memset (&param, 0, sizeof (GstBufferPoolInitParam));

  /* round up input buffer size with 1M alignment to get nearly optimal
   * balance of dec input buffer size.
   */
  if (port == BUFFER_PORT_INPUT) {
    GST_DEBUG_OBJECT (dec, "original %d, resize input with 1M alignment", size);
    size = GST_ROUND_UP_N (size, 1024 * 1024);
  }

  if (query) {
    GST_DEBUG_OBJECT (dec, "allocation params: %" GST_PTR_FORMAT, query);

    if (gst_query_get_n_allocation_params (query) > 0) {
      gst_query_parse_nth_allocation_param (query, 0, NULL, &params);
      GST_DEBUG_OBJECT (dec, "peer query has params flag 0x%x", params.flags);
    }

    if (gst_query_get_n_allocation_pools (query) > 0) {
      GST_DEBUG_OBJECT (dec, "peer query has pool");
      update = TRUE;
      guint size_ext = 0;
      guint min_ext = 0, max_ext = 0;
      GstStructure *config_ext;

      gst_query_parse_nth_allocation_pool (query, 0, &pool, &size_ext, &min_ext,
          &max_ext);
      GST_DEBUG_OBJECT (dec,
          "Use buffer pool from peer: %s, pool: %p, size: %u, "
          "min_buffers: %u, max_buffers: %u",
          dec->use_external_buf ? "true" : "false", pool, size_ext, min_ext,
          max_ext);

      if (pool) {
        if (use_peer_pool) {
          min = MAX (min, min_ext);
          size = MAX (size, size_ext);
          max = min;

          GST_DEBUG_OBJECT (dec, "config external pool size %u, min %u, max %u",
              size, min, max);

          config_ext = gst_buffer_pool_get_config (pool);
          gst_buffer_pool_config_set_params (config_ext, outcaps, size, min,
              max);
          gst_buffer_pool_set_config (pool, config_ext);
          gst_buffer_pool_set_active (pool, TRUE);

          param.ext_pool = pool;
          param.is_ext_pool = TRUE;
          pool = NULL;
          use_peer_pool = FALSE;
        } else {
          GST_DEBUG_OBJECT (dec, "ignore buffer pool from peer");
          gst_object_unref (pool);
          pool = NULL;
        }
      } else if (use_peer_pool) {
        dec->use_external_buf = FALSE;
        GST_WARNING_OBJECT (dec, "Failed to parse peer proposed pool, "
            "reset use_external_buf flag to false");
      }
    } else if (use_peer_pool) {
      dec->use_external_buf = FALSE;
      GST_WARNING_OBJECT (dec, "peer does not propose buffer pool, reset "
          "use_external_buf flag to false");
    }
  }

  if (port == BUFFER_PORT_INPUT) {
    param.info = dec->input_state->info;
    param.info.size = size;
    param.is_outport = FALSE;
  } else {
    param.is_ubwc = dec->is_ubwc;
    param.info = dec->output_state->info;
    param.is_outport = TRUE;
  }

  if (use_dmabuf) {
    param.mode = GST_QVIDC_DMABUF_HEAP_MODE;
  } else {
    param.mode = GST_QVIDC_FDBUF_HEAP_MODE;
  }

  param.gst_vidc_comp = gst_vidc_comp_ref (dec->gst_vidc_comp);
  param.metasize = metasize;

  // adjust qvidc buffer pool count to vidc driver needs
  max = min;

  pool = gst_qvidc_buffer_pool_new (&param);
  GST_DEBUG_OBJECT (dec, "allocation: size:%u min:%u max:%u pool:%"
      GST_PTR_FORMAT, size, min, max, pool);

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config,
      port == BUFFER_PORT_OUTPUT ?
      dec->output_state->caps : dec->input_state->caps, size, min, max);

  GST_DEBUG_OBJECT (dec, "setting own pool config to %" GST_PTR_FORMAT, config);

  /* configure own pool */
  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (dec, "configure our own buffer pool failed");
    gst_structure_free (config);
    goto cleanup;
  }

  /* For simplicity, simply read back the active configuration, so our base
   * class get the right information */
  config = gst_buffer_pool_get_config (pool);
  if (!gst_buffer_pool_config_get_params (config, NULL, &size, &min, &max)) {
    GST_ERROR_OBJECT (dec, "Can't get buffer pool config param");
    gst_structure_free (config);
    goto cleanup;
  }
  gst_structure_free (config);

  GST_DEBUG_OBJECT (dec, "setting pool with size %d, min: %d, max: %d",
      size, min, max);

  if (query) {
    if (update) {
      if (use_peer_pool) {
        GST_DEBUG_OBJECT (dec,
            "update peer pool %p size %d, min %d, max %d to query %p",
            param.ext_pool, size, min, max, query);
        gst_query_set_nth_allocation_pool (query, 0, param.ext_pool, size, min,
            max);
      } else {
        GST_DEBUG_OBJECT (dec,
            "update buffer pool %p size %d, min %d, max %d to query %p", pool,
            size, min, max, query);
        gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
      }
    } else {
      GST_DEBUG_OBJECT (dec,
          "add buffer pool %p size %d, min %d, max %d to query %p", pool, size,
          min, max, query);
      gst_query_add_allocation_pool (query, pool, size, min, max);
    }
  }

  GST_DEBUG_OBJECT (dec, "activate pool %" GST_PTR_FORMAT, pool);
  gst_buffer_pool_set_active (pool, TRUE);


  for (gint i = 0; i < min; i++) {
    GstBuffer *buffer = NULL;
    GstMemory *memory = NULL;

    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (dec, "quit use buffer loop");
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
        GST_DEBUG_OBJECT (dec,
            "Acquired buffer fd: %d in buffer: %p from pool: %p", fd,
            buffer, pool);

        gsize offset = 0;
        gsize maxsize = 0;
        gst_memory_get_sizes (memory, &offset, &maxsize);
        GST_DEBUG_OBJECT (dec, "mem offset %d, maxsize %d", offset, maxsize);

        BufferDescriptor buf;
        memset (&buf, 0, sizeof (BufferDescriptor));
        buf.fd = fd;
        buf.size = maxsize - offset;
        buf.port_type = port;

        if (!vidc_alloc (dec->comp, &buf)) {
          GST_ERROR_OBJECT (dec, "setBuffer %d failed pool: %p", buf.fd, pool);
          gst_buffer_unref (buffer);
          ret = GST_FLOW_NOT_NEGOTIATED;
          goto cleanup;
        }
      }

      gst_buffer_pool_release_buffer (pool, buffer);
    } else {
      GST_ERROR_OBJECT (dec, "no buffer found from pool %p", pool);
      ret = GST_FLOW_NOT_NEGOTIATED;
      goto cleanup;
    }
  }

  if (port == BUFFER_PORT_INPUT) {
    if (dec->in_port_pool) {
      gst_object_unref (dec->in_port_pool);
    }
    dec->in_port_pool = pool;
  } else {
    if (dec->out_port_pool) {
      gst_object_unref (dec->out_port_pool);
    }
    dec->out_port_pool = pool;
  }

  return (ret == GST_FLOW_OK ? TRUE : FALSE);

cleanup:
  if (pool) {
    gst_object_unref (pool);
  }

  return FALSE;
}

static GstFlowReturn
gst_qvidc_vdec_setup_output (GstVideoDecoder * decoder)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoFormat output_format = GST_VIDEO_FORMAT_NV12;

  GstCaps *templ_caps, *intersection = NULL;
  GstStructure *s;
  const gchar *format_str;

  if (dec->output_state) {
    gst_video_codec_state_unref (dec->output_state);
  }

  /* Set decoder output format to NV12 by default */
  dec->output_state =
      gst_video_decoder_set_output_state (decoder,
      output_format, dec->width, dec->height, dec->input_state);

  /* state->caps should be NULL */
  if (dec->output_state->caps) {
    gst_caps_unref (dec->output_state->caps);
  }

  /* Fixate decoder output caps */
  templ_caps =
      gst_pad_get_pad_template_caps (GST_VIDEO_DECODER_SRC_PAD (decoder));
  intersection =
      gst_pad_peer_query_caps (GST_VIDEO_DECODER_SRC_PAD (decoder), templ_caps);
  gst_caps_unref (templ_caps);

  GST_DEBUG_OBJECT (dec, "Allowed downstream caps: %" GST_PTR_FORMAT,
      intersection);

  if (gst_caps_is_empty (intersection)) {
    gst_caps_unref (intersection);
    GST_ERROR_OBJECT (dec, "Empty caps");
    goto error_setup_output;
  }

  /* Secure mode only support UBWC output */
  dec->is_ubwc =
      _unfixed_caps_has_compression (intersection, "ubwc") | dec->secure;

  /* Fixate color format */
  intersection = gst_caps_truncate (intersection);
  intersection = gst_caps_fixate (intersection);
  GST_DEBUG_OBJECT (dec, "intersection caps: %" GST_PTR_FORMAT, intersection);

  s = gst_caps_get_structure (intersection, 0);
  format_str = gst_structure_get_string (s, "format");
  GST_DEBUG_OBJECT (dec, "Fixed color format:%s, UBWC:%d", format_str,
      dec->is_ubwc);

  if (!format_str || (output_format = gst_video_format_from_string (format_str))
      == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (dec, "Invalid caps: %" GST_PTR_FORMAT, intersection);
    gst_caps_unref (intersection);
    goto error_setup_output;
  }

  GST_DEBUG_OBJECT (dec,
      "Set decoder output state: color format: %d, width: %d, height: %d",
      output_format, dec->width, dec->height);

  /* Fill actual width/height into output caps */
  GValue g_width = { 0, };
  GValue g_height = { 0, };
  g_value_init (&g_width, G_TYPE_INT);
  g_value_set_int (&g_width, dec->width);

  g_value_init (&g_height, G_TYPE_INT);
  g_value_set_int (&g_height, dec->height);
  gst_caps_set_value (intersection, "width", &g_width);
  gst_caps_set_value (intersection, "height", &g_height);

  /* Check if fixed caps supports DMA buffer */
  if (gst_qvidc_vdec_caps_has_feature (intersection,
          GST_CAPS_FEATURE_MEMORY_DMABUF)) {
    dec->downstream_supports_dma = TRUE;
    GST_DEBUG_OBJECT (dec, "Downstream supports DMA buffer");
  }

  GST_INFO_OBJECT (dec, "DMA output feature is %s",
      (dec->downstream_supports_dma ? "enabled" : "disabled"));

  dec->output_state->caps = intersection;
  GST_INFO_OBJECT (dec, "output caps: %" GST_PTR_FORMAT,
      dec->output_state->caps);

  dec->output_format = output_format;

  GST_LOG_OBJECT (dec, "output width: %d, height: %d, format: %d(%s)",
      dec->width, dec->height, output_format,
      gst_video_format_to_string (output_format));

  GST_DEBUG_OBJECT (dec, "Complete setup output");

  return ret;

error_setup_output:
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_qvidc_vdec_acquire_buffer (GstVideoDecoder * decoder, BUFFER_PORT_TYPE port,
    GstBuffer ** buffer)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstBufferPoolAcquireParamsExt params_ext;
  GstBufferPool *pool = NULL;
  guint try_cnt = 0;
  gint64 end_time = 0;

  GST_DEBUG_OBJECT (dec, "port %d acquire_buffer ", port);
  if (port == BUFFER_PORT_INPUT) {
    pool = dec->in_port_pool;
  } else {
    pool = dec->out_port_pool;
  }

  do {
    g_mutex_lock (&dec->pending_lock);
    if (try_cnt > MAX_TRY_CNT || dec->error_detected) {
      GST_ERROR_OBJECT (dec, "reach max try %u or error detected %u",
          try_cnt, (guint) dec->error_detected);
      g_mutex_unlock (&dec->pending_lock);
      break;
    }
    g_mutex_unlock (&dec->pending_lock);

    memset (&params_ext, 0, sizeof (GstBufferPoolAcquireParamsExt));
    params_ext.params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
    ret = gst_buffer_pool_acquire_buffer (pool, buffer, &params_ext);
    if (ret == GST_FLOW_OK && *buffer != NULL) {
      break;
    }

    end_time =
        g_get_monotonic_time () + G_TIME_SPAN_MILLISECOND * 100;
    g_mutex_lock (&dec->pending_lock);
    if (!g_cond_wait_until (&dec->pending_cond, &dec->pending_lock, end_time)) {
      GST_ERROR_OBJECT (dec, "Timed out on wait, try_cnt %u", try_cnt);
    }
    g_mutex_unlock (&dec->pending_lock);
    try_cnt++;
  } while (ret != GST_FLOW_OK || *buffer == NULL);

  GST_DEBUG_OBJECT (dec, "port %d acquire_buffer %p ", port, *buffer);

  return ret;
}

static GstFlowReturn
gst_qvidc_vdec_queue_eos (GstVideoDecoder * decoder)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *inter_buf = NULL;
  GstMemory *inter_mem = NULL;

  GST_DEBUG_OBJECT (dec, "queue EOS");

  ret = gst_qvidc_vdec_acquire_buffer (decoder, BUFFER_PORT_INPUT, &inter_buf);

  if (ret != GST_FLOW_OK || inter_buf == NULL) {
    GST_ERROR_OBJECT (dec, "Failed to acquire_buffer from in port pool");
    ret = GST_FLOW_ERROR;
  } else {
    GST_DEBUG_OBJECT (dec, "acquire_inter_buffer done");
    inter_mem = gst_buffer_peek_memory (inter_buf, 0);
    if (inter_mem) {
      gint fd = -1;
      if (gst_is_dmabuf_memory (inter_mem)) {
        fd = gst_dmabuf_memory_get_fd (inter_mem);
      } else {
        fd = gst_fd_memory_get_fd (inter_mem);
      }
      GST_DEBUG_OBJECT (dec,
          "Acquired internal buffer fd: %d in buffer: %p mem %p from pool: %p",
          fd, inter_buf, inter_mem, dec->in_port_pool);

      gint meta_fd = -1;
      guint metasize = 0;
      gst_vidc_buffer_get_custom_meta (inter_buf, "GstQVIDCDMeta", &meta_fd, &metasize);

      BufferDescriptor inBuf;
      memset (&inBuf, 0, sizeof (BufferDescriptor));
      inBuf.fd = fd;
      inBuf.capacity = gst_memory_get_sizes (inter_mem, NULL, NULL);
      inBuf.port_type = BUFFER_PORT_INPUT;
      inBuf.flag = FLAG_TYPE_END_OF_STREAM;
      inBuf.meta_fd = meta_fd;
      inBuf.metasize = metasize;

      if (!vidc_queue (dec->comp, &inBuf)) {
        GST_ERROR_OBJECT (dec, "queueBuffer %d failed, buf %p", inBuf.fd, inter_buf);
        gst_buffer_pool_release_buffer (dec->in_port_pool, inter_buf);
        ret = GST_FLOW_ERROR;
      }
    } else {
      GST_ERROR_OBJECT (dec, "failed to get mem from buf %p", inter_buf);
      gst_buffer_pool_release_buffer (dec->in_port_pool, inter_buf);
      ret = GST_FLOW_ERROR;
    }
  }

  return ret;
}

/* Dispatch any pending remaining data at EOS. Class can refuse to decode new data after. */
static GstFlowReturn
gst_qvidc_vdec_finish (GstVideoDecoder * decoder)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstFlowReturn ret = GST_FLOW_OK;
  gint64 end_time = 0;

  GST_DEBUG_OBJECT (dec, "finish");

  /* Setup EOS work */

  /* wait for all the pending buffers to return */
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  ret = gst_qvidc_vdec_queue_eos (decoder);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (dec, "queue EOS failed");
    goto out;
  }

  g_mutex_lock (&dec->pending_lock);

  end_time =
      g_get_monotonic_time () + (EOS_WAITING_TIMEOUT * G_TIME_SPAN_SECOND);

  GST_DEBUG_OBJECT (dec, "start waiting until EOS signal");
  while (!dec->eos_reached) {
    GST_DEBUG_OBJECT (dec, "wait until EOS signal is triggered");

    if (dec->error_detected) {
      GST_ERROR_OBJECT (dec, "error detected %u", (guint) dec->error_detected);
      break;
    }

    if (!g_cond_wait_until (&dec->pending_cond, &dec->pending_lock, end_time)) {
      GST_ERROR_OBJECT (dec, "Timed out on wait EOS, exiting!");
      break;
    }
  }

  if (!dec->eos_reached) {
    GST_ERROR_OBJECT (dec, "EOS not reached");
    ret = GST_FLOW_ERROR;
  }

  g_mutex_unlock (&dec->pending_lock);

out:
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  GST_DEBUG_OBJECT (dec, "EOS reached %d", dec->eos_reached);

  return ret;
}

static GstFlowReturn
gst_qvidc_vdec_flush (GstVideoDecoder * decoder)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (dec, "flush");

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  //TODO: support flush
  GST_DEBUG_OBJECT (dec, "no flush support now");
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  dec->is_flushing = FALSE;

  return ret;
}

/* Called to inform the caps describing input video data that decoder is about to receive.
  Might be called more than once, if changing input parameters require reconfiguration.*/
static gboolean
gst_qvidc_vdec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstQvidcVdecClass *dec_class = GST_QVIDC_VDEC_GET_CLASS (decoder);
  GstStructure *structure = NULL;
  const gchar *mode_str;
  gint retval = 0;
  gboolean ret = FALSE;
  gint width = 0;
  gint height = 0;
  GstVideoInterlaceMode interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
  gchar *comp_name = NULL;
  GPtrArray *config = NULL;
  ConfigParams codectype;
  ConfigParams resolution;
  ConfigParams output_picture_order_mode;
  ConfigParams low_latency_mode;
  ConfigParams frame_rate;
  GstVideoInfo input_info;
  gfloat fps = COMMON_FRAMERATE;

  GST_DEBUG_OBJECT (dec, "set format in caps:%" GST_PTR_FORMAT, state->caps);

  structure = gst_caps_get_structure (state->caps, 0);
  comp_name = get_vidc_comp_name (decoder, structure, dec->low_latency_mode);
  if (!comp_name) {
    GST_ERROR_OBJECT (dec, "Failed to get relevant component name, caps:%"
        GST_PTR_FORMAT, state->caps);
    return FALSE;
  }

  gst_video_info_from_caps (&input_info, state->caps);

  if (!dec->output_setup) {
    retval = gst_structure_get_int (structure, "width", &width);
    retval &= gst_structure_get_int (structure, "height", &height);
    if (!retval) {
      goto error_res;
    }

    if ((mode_str = gst_structure_get_string (structure, "interlace-mode"))) {
      if (g_str_equal ("progressive", mode_str)) {
        interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
      } else if (g_str_equal ("interleaved", mode_str)) {
        interlace_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
      } else if (g_str_equal ("mixed", mode_str)) {
        interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
      } else if (g_str_equal ("fields", mode_str)) {
        interlace_mode = GST_VIDEO_INTERLACE_MODE_FIELDS;
      }
    }

    dec->width = width;
    dec->height = height;
    dec->interlace_mode = interlace_mode;
    if (dec->comp_name) {
      g_free (dec->comp_name);
    }
    dec->comp_name = comp_name;

    if (dec->input_state) {
      gst_video_codec_state_unref (dec->input_state);
    }

    dec->input_state = gst_video_codec_state_ref (state);

    if (GST_FLOW_OK != gst_qvidc_vdec_setup_output (decoder)) {
      goto error_set_format;
    } else if (dec->use_external_buf) {
      GST_DEBUG_OBJECT (dec, "use external buffer pool");
    }
  }

  if (dec->comp_started) {
    GST_DEBUG_OBJECT (dec, "vidc comp has started yet");
    /* start vidc component only once */
    goto done;
  }

  if (!gst_qvidc_vdec_create_component (decoder)) {
    goto error_set_format;
  }

  if (dec_class->set_format) {
    GST_DEBUG_OBJECT (dec, "Subclass set format");
    if (!dec_class->set_format (dec, state)) {
      GST_ERROR_OBJECT (dec, "Subclass failed to set format");
      goto error_set_format;
    }
  }

  config = g_ptr_array_new ();

  codectype = make_codec_param (dec->comp_name);
  g_ptr_array_add (config, &codectype);

  resolution = make_resolution_param (width, height, TRUE);
  g_ptr_array_add (config, &resolution);

  if (dec->output_picture_order_mode != DEFAULT_OUTPUT_PICTURE_ORDER_MODE) {
    output_picture_order_mode =
        make_output_picture_order_param (dec->output_picture_order_mode);
    g_ptr_array_add (config, &output_picture_order_mode);
  }

  if (dec->low_latency_mode) {
    low_latency_mode = make_low_latency_param (dec->low_latency_mode);
    g_ptr_array_add (config, &low_latency_mode);
  }

  if (input_info.fps_n != 0 && input_info.fps_d != 0) {
    fps = (float) input_info.fps_n / input_info.fps_d;
    GST_DEBUG_OBJECT (dec, "got fps %0.2f from caps", fps);
  }
#ifdef SET_DEC_INPUT_FRAMERATE
  frame_rate = make_framerate_param (fps, TRUE);
#else
  frame_rate = make_framerate_param (fps, FALSE);
#endif
  g_ptr_array_add (config, &frame_rate);
  GST_DEBUG_OBJECT (dec, "set framerate %0.2f", fps);

  BLOCK_MODE_TYPE mode = BLOCK_MODE_DONT_BLOCK;
  if (codectype.codec == VIDC_CODEC_VP9 && dec->check_10bit) {
    GST_DEBUG_OBJECT (dec,
        "vp9, 10bit not indicated in caps, do check in handle frame func");
    mode = BLOCK_MODE_MAY_BLOCK;
  }

  if (!vidc_config (dec->comp, config, mode)) {
    GST_WARNING_OBJECT (dec, "Failed to set config");
    goto error_set_format;
  }

  g_ptr_array_free (config, TRUE);

  if (!dec->delay_start) {
    ret = gst_qvidc_vdec_config_pool (decoder, NULL, BUFFER_PORT_INPUT);
    if (ret == FALSE) {
      GST_ERROR_OBJECT (dec, "failed to start component");
      goto error_set_format;
    }
  }

done:
  GST_DEBUG_OBJECT (dec, "done");
  dec->comp_started = TRUE;
  return TRUE;

  /* Errors */
error_res:
  {
    GST_ERROR_OBJECT (dec, "Unable to get width/height value");
    return FALSE;
  }
error_set_format:
  {
    if (config) {
      g_ptr_array_free (config, TRUE);
    }
    GST_ERROR_OBJECT (dec, "failed to setup input");
    return FALSE;
  }
}

/* Called when the element changes to GST_STATE_READY */
static gboolean
gst_qvidc_vdec_open (GstVideoDecoder * decoder)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstQvidcVdecClass *dec_class = GST_QVIDC_VDEC_GET_CLASS (decoder);
  gboolean ret = TRUE;

  dec->comp_started = FALSE;
  dec->output_setup = FALSE;
  dec->eos_reached = FALSE;
  dec->error_detected = FALSE;
  dec->frame_index = 0;
  dec->num_output_done = 0;
  dec->downstream_supports_dma = FALSE;
  dec->comp = NULL;
  dec->comp_intf = NULL;
  dec->in_port_pool = NULL;
  dec->out_port_pool = NULL;
  dec->check_10bit = FALSE;
  dec->delay_start = FALSE;
  dec->max_external_buf_cnt = QVIDC_MIN_OUTBUFFERS;
  dec->gst_vidc_comp = NULL;
  dec->comp_name = NULL;
  dec->input_state = NULL;
  dec->output_state = NULL;

  memset (&dec->start_time, 0, sizeof (struct timeval));
  memset (&dec->first_frame_time, 0, sizeof (struct timeval));
  memset (&dec->first_bitstream_receive_time, 0, sizeof (struct timeval));
  gettimeofday (&dec->start_time, NULL);

  GST_DEBUG_OBJECT (dec, "open");

  /* Create component store */
  dec->comp_store = vidcStore_create ();

  if (dec_class->open) {
    GST_DEBUG_OBJECT (dec, "Subclass open");
    if (!dec_class->open (dec)) {
      GST_ERROR_OBJECT (dec, "Subclass failed to open");
      ret = FALSE;
    }
  }

  return ret;
}

static gboolean
gst_qvidc_vdec_stop (GstVideoDecoder * decoder)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);

  GST_DEBUG_OBJECT (dec, "stop");

  /* handle state change from PAUSE to READY, then back to PAUSE */
  dec->output_setup = FALSE;

  /* Stop the component */
  if (dec->comp) {
    vidc_stop (dec->comp, BUFFER_PORT_INPUT);
    vidc_stop (dec->comp, BUFFER_PORT_OUTPUT);
    dec->comp_started = FALSE;
  }

  return TRUE;
}

/* Called when the element changes to GST_STATE_NULL */
static gboolean
gst_qvidc_vdec_close (GstVideoDecoder * decoder)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);

  GST_DEBUG_OBJECT (dec, "close");

  if (dec->out_port_pool) {
    GST_DEBUG_OBJECT (dec, "out pool ref cnt:%d",
        GST_OBJECT_REFCOUNT (dec->out_port_pool));
    gst_object_unref (dec->out_port_pool);
    dec->out_port_pool = NULL;
  }

  if (dec->in_port_pool) {
    GST_DEBUG_OBJECT (dec, "in pool ref cnt:%d",
        GST_OBJECT_REFCOUNT (dec->in_port_pool));
    gst_object_unref (dec->in_port_pool);
    dec->in_port_pool = NULL;
  }

  if (dec->gst_vidc_comp) {
    gst_vidc_comp_unref (dec->gst_vidc_comp);
    dec->gst_vidc_comp = NULL;
  }

  if (dec->comp_name) {
    g_free (dec->comp_name);
    dec->comp_name = NULL;
  }

  if (dec->comp_store) {
    vidcStore_delete (dec->comp_store);
    dec->comp_store = NULL;
  }

  if (dec->input_state) {
    gst_video_codec_state_unref (dec->input_state);
    dec->input_state = NULL;
  }

  if (dec->output_state) {
    gst_video_codec_state_unref (dec->output_state);
    dec->output_state = NULL;
  }

  return TRUE;
}

/* Called whenever a input frame from the upstream is sent to decoder */
static GstFlowReturn
gst_qvidc_vdec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstQvidcVdecClass *dec_class = GST_QVIDC_VDEC_GET_CLASS (decoder);
  GstFlowReturn ret = GST_FLOW_ERROR;

  GST_DEBUG_OBJECT (dec, "handle_frame frame %p", frame);

  g_return_val_if_fail (frame != NULL, GST_FLOW_ERROR);

  if (frame->system_frame_number == 0) {
    gettimeofday (&dec->first_bitstream_receive_time, NULL);
  }

  if (!dec->comp_started) {
    GST_ERROR_OBJECT (dec, "component not started");
    goto done;
  }

  GST_DEBUG_OBJECT (dec,
      "Frame number : %d, Distance from Sync : %d, Presentation timestamp : %"
      GST_TIME_FORMAT, frame->system_frame_number, frame->distance_from_sync,
      GST_TIME_ARGS (frame->pts));

  if (dec_class->handle_frame) {
    GST_DEBUG_OBJECT (dec, "dec_class->handle_fram");
    ret = dec_class->handle_frame (dec, frame);
    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (dec, "Subclass failed to handle format");
      goto done;
    }
  }

  /* Decode frame */
  ret = gst_qvidc_vdec_decode (decoder, frame);

done:
  gst_video_codec_frame_unref (frame);

  return ret;
}

static gboolean
gst_qvidc_vdec_caps_has_feature (const GstCaps * caps, const gchar * partten)
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
gst_qvidc_vdec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  gboolean ret = FALSE;
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);

  GST_DEBUG_OBJECT (dec, "decide allocation");

  ret = gst_qvidc_vdec_config_pool (decoder, query, BUFFER_PORT_OUTPUT);
  GST_DEBUG_OBJECT (dec, "after config output pool, ret %d", ret);

  return ret;
}

static GstBuffer *
gst_qvidc_vdec_wrap_output_buffer (GstVideoDecoder * decoder,
    BufferDescriptor * decode_buf)
{
  GstBuffer *out_buf = NULL;
  GstVideoCodecState *state;
  GstFlowReturn ret = GST_FLOW_OK;
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstBufferPoolAcquireParamsExt param_ext;
  guint64 *p_modifier = NULL;

  state = gst_video_decoder_get_output_state (decoder);
  if (!state) {
    GST_ERROR_OBJECT (dec, "Failed to get decoder output state");
    return NULL;
  }

  memset (&param_ext, 0, sizeof (GstBufferPoolAcquireParamsExt));
  param_ext.fd = decode_buf->fd;
  param_ext.meta_fd = decode_buf->meta_fd;
  param_ext.index = decode_buf->index;
  param_ext.size = decode_buf->size;
  param_ext.vidc_buf = decode_buf->vidcBuffer;
  param_ext.params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
  ret = gst_buffer_pool_acquire_buffer (dec->out_port_pool, &out_buf,
      (GstBufferPoolAcquireParams *) & param_ext);

  if (ret == GST_FLOW_OK && out_buf) {
    if (gst_buffer_is_writable (out_buf)) {
      gst_buffer_resize (out_buf, 0, decode_buf->size);

      GstVideoInfo *vinfo = &state->info;
      for (guint i = 0; i < vidc_getPlaneCount (dec->comp); i++) {
        GST_VIDEO_INFO_PLANE_STRIDE (vinfo, i) =
            vidc_getPlaneStride (dec->comp, i);
        GST_VIDEO_INFO_PLANE_OFFSET (vinfo, i) =
            vidc_getPlaneOffset (dec->comp, i);
        GST_ERROR_OBJECT (dec, "plane[%d] stride %d, offset 0x%x, n_plane %d",
            i, vinfo->stride[i], vinfo->offset[i],
            GST_VIDEO_INFO_N_PLANES (vinfo));
      }

      gst_buffer_add_video_meta_full (out_buf, GST_VIDEO_FRAME_FLAG_NONE,
          GST_VIDEO_INFO_FORMAT (vinfo), GST_VIDEO_INFO_WIDTH (vinfo),
          GST_VIDEO_INFO_HEIGHT (vinfo), GST_VIDEO_INFO_N_PLANES (vinfo),
          vinfo->offset, vinfo->stride);

      if (dec->is_ubwc) {
        if (!gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (out_buf),
                gst_fbuf_modifier_qdata_quark ())) {
          GST_DEBUG_OBJECT (dec, "no modifier quark");

          p_modifier = g_slice_new (guint64);
          *p_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;

          GST_DEBUG_OBJECT (dec, "modifier quark 0x%x", *p_modifier);
          gst_mini_object_set_qdata (GST_MINI_OBJECT (out_buf),
              gst_fbuf_modifier_qdata_quark (), p_modifier,
              (GDestroyNotify) modifier_free);
          GST_DEBUG_OBJECT (dec,
              "Attach modifier quark %p, value:0x%lx on gstbuf %p", p_modifier,
              *p_modifier, out_buf);
        }
      }
    } else {
      GST_WARNING_OBJECT (dec,
          "output buffer not writable may have wrong rendering");
      gst_video_codec_state_unref (state);
      return NULL;
    }
  } else {
    GST_ERROR_OBJECT (dec, "Fail to acquire output gst buffer");
    gst_video_codec_state_unref (state);
    return NULL;
  }

  gst_video_codec_state_unref (state);

  return out_buf;
}

/* Push decoded frame to downstream element */
static GstFlowReturn
push_frame_downstream (GstVideoDecoder * decoder, BufferDescriptor * decode_buf)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstBuffer *outbuf = NULL;
  GstVideoCodecFrame *frame = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecState *state = NULL;
  GstVideoInfo *vinfo = NULL;

  GST_DEBUG_OBJECT (dec, "push frame to downstream");

  state = gst_video_decoder_get_output_state (decoder);
  if (state) {
    vinfo = &state->info;
  } else {
    if (dec->output_setup) {
      GST_ERROR_OBJECT (dec, "video codec state is NULL, unexpected!");
    } else {
      GST_FIXME_OBJECT (dec, "code reach here is unexpected");
    }
    ret = GST_FLOW_ERROR;
    goto out;
  }

  GST_DEBUG_OBJECT (dec,
      "buffer: %p, fd: %d, index %" PRIu64
      ", meta_fd: %d, timestamp: %" PRIu64 ", flag 0x%x",
      decode_buf->data, decode_buf->fd, decode_buf->index,
      decode_buf->meta_fd, decode_buf->timestamp, decode_buf->flag);

  if (decode_buf->flag & FLAG_TYPE_DROP_FRAME) {
    GST_DEBUG_OBJECT (dec, "read-only/drop frame queue to vidc");
    queue_vidc_bufferDesc (decode_buf, decoder);
    ret = GST_FLOW_OK;
    goto out;
  }

  frame = gst_video_decoder_get_frame (decoder, decode_buf->index);
  if (frame == NULL) {
    GST_DEBUG_OBJECT (dec,
        "seek: can't get frame (%" PRIu64 "), which was released during FLUSH-STOP event",
        decode_buf->index);
    /* free old output buffer since of seeking */
    queue_vidc_bufferDesc (decode_buf, decoder);
    GST_DEBUG_OBJECT (dec, "seek: release old buffer since of seeking");
    ret = GST_FLOW_OK;
    goto out;
  }

  outbuf = gst_qvidc_vdec_wrap_output_buffer (decoder, decode_buf);
  if (outbuf) {
    GST_INFO_OBJECT (dec, "decode_buf->pts (%" G_GUINT64_FORMAT ")",
        decode_buf->timestamp);

    GST_BUFFER_PTS (outbuf) =
        gst_util_uint64_scale (decode_buf->timestamp, GST_SECOND,
        TICKS_PER_SECOND);

    if (dec->set_gstbuf_interlace_flag) {
      if (decode_buf->interlaceMode == INTERLACE_MODE_FIELD_TOP_FIRST) {
        GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_INTERLACED);
        GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);
        GST_DEBUG_OBJECT (dec, "interlaced top field");
      } else if (decode_buf->interlaceMode == INTERLACE_MODE_FIELD_BOTTOM_FIRST) {
        GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_INTERLACED);
        GST_DEBUG_OBJECT (dec, "interlaced bottom field");
      }
    }

    if (state->info.fps_d != 0 && state->info.fps_n != 0) {
      GST_BUFFER_DURATION (outbuf) = gst_util_uint64_scale (GST_SECOND,
          vinfo->fps_d, vinfo->fps_n);
    }
    frame->output_buffer = outbuf;

    GST_DEBUG_OBJECT (dec,
        "out buffer: PTS: %lu, duration: %lu, fps_d: %d, fps_n: %d interlace:%d",
        GST_BUFFER_PTS (outbuf), GST_BUFFER_DURATION (outbuf), vinfo->fps_d,
        vinfo->fps_n, decode_buf->interlaceMode);
  }

  ret = gst_video_decoder_finish_frame (decoder, frame);
  if (ret == GST_FLOW_FLUSHING) {
    GST_DEBUG_OBJECT (dec, "downstream is flushing");
  } else if (ret == GST_FLOW_EOS) {
    GST_DEBUG_OBJECT (dec, "downstream is in eos");
  } else if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (dec, "Failed(%d) to push frame downstream", ret);
  }

  GST_DEBUG_OBJECT (dec, "downstream finish");

out:
  if (state)
    gst_video_codec_state_unref (state);
  return ret;
}

static void
queue_vidc_buffer (GstBuffer * buffer, gpointer user_data)
{
  GstVideoDecoder *decoder = (GstVideoDecoder *) user_data;
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstMemory *memory = NULL;

  GST_LOG_OBJECT (dec, "queue_vidc_buffer, buffer=%p", buffer);

  memory = gst_buffer_peek_memory (buffer, 0);
  if (memory) {
    gint fd;
    if (gst_is_dmabuf_memory (memory)) {
      fd = gst_dmabuf_memory_get_fd (memory);
    } else {
      fd = gst_fd_memory_get_fd (memory);
    }
    gsize offset = 0;
    gsize maxsize = 0;
    gst_memory_get_sizes (memory, &offset, &maxsize);

    gint meta_fd = -1;
    guint metasize = 0;
    gst_vidc_buffer_get_custom_meta (buffer, "GstQVIDCDMeta", &meta_fd, &metasize);

    /* Attach the fd to vidc */
    BufferDescriptor outbuf;
    memset (&outbuf, 0, sizeof (BufferDescriptor));
    outbuf.fd = fd;
    outbuf.capacity = maxsize - offset;
    outbuf.size = 0;
    outbuf.port_type = BUFFER_PORT_OUTPUT;
    outbuf.meta_fd = meta_fd;
    outbuf.metasize = metasize;

    GST_DEBUG_OBJECT (dec, "mem fd %d, offset %d, maxsize %d",
        fd, offset, maxsize);

    if (!vidc_queue (dec->comp, &outbuf)) {
      GST_ERROR_OBJECT (dec, "queueBuffer %d failed", outbuf.fd);
    }
  }
}

static void
queue_vidc_bufferDesc (BufferDescriptor * buffer, gpointer user_data)
{
  GstVideoDecoder *decoder = (GstVideoDecoder *) user_data;
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GST_DEBUG_OBJECT (dec,
      "buffer=%p, mem fd %d, capacity %d, size %d, port %d, meta fd %d, index %" PRIu64,
      buffer, buffer->fd, buffer->capacity, buffer->size, buffer->port_type,
      buffer->meta_fd, buffer->index);

  buffer->size = 0;

  if (!vidc_queue (dec->comp, buffer)) {
    GST_ERROR_OBJECT (dec, "queueBuffer %d failed", buffer->fd);
  }
}

/* Handle event from VIDC */
static void
handle_video_event (const void *handle, EVENT_TYPE type, void *data)
{
  GstVideoDecoder *decoder = (GstVideoDecoder *) handle;
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstVideoInfo *info = NULL;

  GST_LOG_OBJECT (dec, "handle_video_event, type=%d", type);

  switch (type) {
    case EVENT_INPUTS_DONE:{
      BufferDescriptor *in_buf = (BufferDescriptor *) data;
      GstBufferPool *pool = dec->in_port_pool;
      GST_DEBUG_OBJECT (dec,
          "EVENT_INPUTS_DONE buffer fd %d, index %" PRIu64 " to pool %p",
          in_buf->fd, in_buf->index, pool);

      gint64 key = ((gint64) in_buf->fd << 32) | ((gint64) in_buf->meta_fd);
      GstBuffer *gst_buf = gst_qvidc_buffer_pool_find_buffer (pool, key);
      if (gst_buf) {
        gst_buffer_pool_release_buffer (pool, gst_buf);
      }

      g_mutex_lock (&dec->pending_lock);
      g_cond_signal (&dec->pending_cond);
      g_mutex_unlock (&dec->pending_lock);

      break;
    }

    case EVENT_OUTPUTS_DONE:{
      BufferDescriptor *out_buf = (BufferDescriptor *) data;

      if (out_buf->size) {
        if (!dec->first_frame_time.tv_sec && !dec->first_frame_time.tv_usec) {
          gettimeofday (&dec->first_frame_time, NULL);
          int time_1st_cost_us =
              (dec->first_frame_time.tv_sec -
              dec->start_time.tv_sec) * 1000000 +
              (dec->first_frame_time.tv_usec - dec->start_time.tv_usec);
          GST_DEBUG_OBJECT (dec, "first frame latency from dec open:%d us",
              time_1st_cost_us);

          time_1st_cost_us =
              (dec->first_frame_time.tv_sec -
              dec->first_bitstream_receive_time.tv_sec) * 1000000 +
              (dec->first_frame_time.tv_usec -
              dec->first_bitstream_receive_time.tv_usec);
          GST_DEBUG_OBJECT (dec,
              "first frame latency from dec 1st frame in:%d us",
              time_1st_cost_us);
        }

        dec->num_output_done++;
        GST_LOG_OBJECT (dec, "output done, count: %lu", dec->num_output_done);

        GstFlowReturn ret = push_frame_downstream (decoder, out_buf);
      } else if (out_buf->flag & FLAG_TYPE_END_OF_STREAM) {
        GST_INFO_OBJECT (dec, "Decoder reached EOS");
        g_mutex_lock (&dec->pending_lock);
        dec->eos_reached = TRUE;
        g_cond_signal (&dec->pending_cond);
        g_mutex_unlock (&dec->pending_lock);
      }
      break;
    }

    case EVENT_ERROR:{
      g_mutex_lock (&dec->pending_lock);
      dec->error_detected = TRUE;
      g_cond_signal (&dec->pending_cond);
      g_mutex_unlock (&dec->pending_lock);

      gst_buffer_pool_set_flushing (dec->in_port_pool, TRUE);

      GST_ERROR_OBJECT (dec, "Something un-expected happened(%d)",
          *(gint32 *) data);
      GST_ELEMENT_ERROR (dec, STREAM, DECODE, ("Decoder posts an error"),
          (NULL));
      break;
    }
    case EVENT_RECONFIG:{
      g_mutex_lock (&(dec->pending_lock));
      GstFlowReturn ret = GST_FLOW_OK;
      GstVideoCodecState *output_state = NULL;
      GstVideoInterlaceMode interlace_mode =
          GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

      gboolean started = *(gboolean *) data;
      gboolean is_progressive = true;
      guint size, min, max;
      size = min = max = 0;

      GST_DEBUG_OBJECT (dec, "Receive reconfig event, started is %u", started);

      if (started) {
        if (!vidc_stop (dec->comp, BUFFER_PORT_OUTPUT)) {
          GST_ERROR_OBJECT (dec, "vidc_stop outport failed");
          g_mutex_unlock (&(dec->pending_lock));
          break;
        }
      }

      is_progressive = vidc_isProgressive (dec->comp);
      GST_DEBUG_OBJECT (dec, "start reconfig pool, caps interlace_mode %d, progressive %d",
          dec->interlace_mode, is_progressive);

      if (GST_IS_QVIDC_MPEG2_DEC (dec)) {
        if (dec->interlace_mode == GST_VIDEO_INTERLACE_MODE_PROGRESSIVE
            || is_progressive)
          interlace_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
        else
          interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
      }

      if (GST_IS_QVIDC_H264_DEC (dec)) {
        if (dec->interlace_mode != GST_VIDEO_INTERLACE_MODE_PROGRESSIVE
            || !is_progressive)
          interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
      }

      output_state = gst_video_decoder_set_interlaced_output_state (decoder,
          dec->output_format, interlace_mode,
          dec->width, dec->height, dec->input_state);
      if (!output_state) {
        GST_ERROR_OBJECT (dec, "Failed to get output state");
        g_mutex_unlock (&(dec->pending_lock));
        break;
      }

      output_state->caps = gst_video_info_to_caps (&output_state->info);

      GST_DEBUG_OBJECT (dec, "set interlace mode %s in caps",
          gst_video_interlace_mode_to_string (interlace_mode));

      if (dec->downstream_supports_dma) {
        gst_caps_set_features (output_state->caps, 0,
            gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_DMABUF));
        GST_DEBUG_OBJECT (dec, "set DMA feature in Caps");
      }
      if (dec->is_ubwc) {
        gst_caps_set_simple (output_state->caps, "compression",
            G_TYPE_STRING, "ubwc", NULL);
      } else {
        gst_caps_set_simple (output_state->caps, "compression",
            G_TYPE_STRING, "linear", NULL);
      }
      GST_INFO_OBJECT (dec, "output caps: %" GST_PTR_FORMAT,
          output_state->caps);

      if (dec->output_state) {
        gst_video_codec_state_unref (dec->output_state);
      }
      dec->output_state = output_state;

      if (!gst_video_decoder_negotiate (decoder)) {
        GST_ERROR_OBJECT (dec, "Failed to negotiate");
        g_mutex_unlock (&(dec->pending_lock));
        break;
      }
      gst_pad_check_reconfigure (decoder->srcpad);
      dec->output_setup = TRUE;

      GST_ERROR_OBJECT (dec, "reconfig negotiate done");
      GstBufferPool *pool = dec->out_port_pool;

      GstStructure *config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_get_params (config, NULL, &size, &min, &max);
      gst_structure_free (config);

      GST_DEBUG_OBJECT (dec, "pool %p config size %u, min %u, max %u", pool,
          size, min, max);

      GPtrArray *buffers = NULL;
      buffers = g_ptr_array_new ();

      for (gint i = 0; i < min; i++) {
        GstBuffer *buffer = NULL;

        GstBufferPoolAcquireParamsExt params_ext;
        memset (&params_ext, 0, sizeof (GstBufferPoolAcquireParamsExt));
        params_ext.params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
        ret = gst_buffer_pool_acquire_buffer (pool, &buffer, &params_ext);
        if (ret == GST_FLOW_OK) {
          GST_DEBUG_OBJECT (dec, "array add %p from pool %p", buffer, pool);
          g_ptr_array_add (buffers, buffer);
        } else {
          GST_ERROR_OBJECT (dec, "acquire buffer failed");
          //TODO: handle fail case
        }

        gst_buffer_pool_release_buffer (pool, buffer);
      }

      if (!vidc_start (dec->comp, BUFFER_PORT_OUTPUT)) {
        GST_ERROR_OBJECT (dec, "vidc_start outport failed");
      } else {
        g_ptr_array_foreach (buffers, (GFunc) queue_vidc_buffer, decoder);
      }

      g_ptr_array_free (buffers, TRUE);
      GST_DEBUG_OBJECT (dec, "free buffers array");
      g_mutex_unlock (&(dec->pending_lock));

      break;
    }
    case EVENT_DROP_FRAME: {
      BufferDescriptor *out_buf = (BufferDescriptor *) data;
      if (out_buf) {
        GST_DEBUG_OBJECT (dec, "drop frame %" PRIu64 ", fd %d",
            out_buf->index, out_buf->fd);
        GstFlowReturn ret = push_frame_downstream (decoder, out_buf);
      }
      break;
    }

    default:{
      GST_ERROR_OBJECT (dec, "Invalid Event(%d)", type);
      break;
    }
  }
}

/* Push frame to VIDC */
static GstFlowReturn
gst_qvidc_vdec_decode (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstMapInfo mapinfo = { 0, };
  GstBuffer *buf = NULL;
  GstMemory *mem = NULL;
  GstBuffer *inter_buf = NULL;
  GstMemory *inter_mem = NULL;
  BufferDescriptor inBuf;
  gboolean status = FALSE;
  gboolean mem_mapped = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (dec, "decode");

  memset (&inBuf, 0, sizeof (BufferDescriptor));

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  buf = frame->input_buffer;
  mem = gst_buffer_peek_memory (buf, 0);
  if (gst_is_dmabuf_memory (mem)) {
    inBuf.fd = gst_dmabuf_memory_get_fd (mem);
    inBuf.data = NULL;
    inBuf.size = gst_memory_get_sizes (mem, NULL, NULL);
    GST_DEBUG_OBJECT (dec, "Input dma buffer with fd=%d, size=%d",
        inBuf.fd, inBuf.size);
  } else {
    gst_buffer_map (buf, &mapinfo, GST_MAP_READ);
    inBuf.fd = -1;
    inBuf.data = mapinfo.data;
    inBuf.size = mapinfo.size;
    mem_mapped = TRUE;
    gst_buffer_unmap (buf, &mapinfo);
  }
  inBuf.timestamp = frame->pts;
  GST_INFO_OBJECT (dec,
      "fd %d, frame->size %d frame->pts (%" G_GUINT64_FORMAT ")", inBuf.fd,
      inBuf.size, frame->pts);

  GST_DEBUG_OBJECT (dec, "acquire_inter_buffer");

  if (dec->comp_started) {
    ret = gst_qvidc_vdec_acquire_buffer (decoder, BUFFER_PORT_INPUT, &inter_buf);
  }

  if (ret != GST_FLOW_OK || inter_buf == NULL) {
    GST_ERROR_OBJECT (dec, "Failed to acquire_buffer from in port pool");
    ret = GST_FLOW_ERROR;
    goto out;
  } else {
    GST_DEBUG_OBJECT (dec, "acquire_inter_buffer done");
    inter_mem = gst_buffer_peek_memory (inter_buf, 0);
    if (inter_mem) {
      gint fd = -1;
      if (gst_is_dmabuf_memory (inter_mem)) {
        fd = gst_dmabuf_memory_get_fd (inter_mem);
      } else {
        fd = gst_fd_memory_get_fd (inter_mem);
      }
      GST_DEBUG_OBJECT (dec,
          "Acquired internal buffer fd: %d in buffer: %p mem %p from pool: %p",
          fd, inter_buf, inter_mem, dec->in_port_pool);
      gsize offset = 0;
      gsize maxsize = 0;
      gst_memory_get_sizes (inter_mem, &offset, &maxsize);
      GST_DEBUG_OBJECT (dec, "mem offset %d, maxsize %d", offset, maxsize);

      gint meta_fd = -1;
      guint metasize = 0;
      gst_vidc_buffer_get_custom_meta (inter_buf, "GstQVIDCDMeta", &meta_fd, &metasize);

      BufferDescriptor vidcbuf;
      memset (&vidcbuf, 0, sizeof (BufferDescriptor));
      vidcbuf.fd = fd;
      vidcbuf.port_type = BUFFER_PORT_INPUT;

      GstMapInfo info;
      gst_memory_map (inter_mem, &info, GST_MAP_WRITE);
      GST_DEBUG_OBJECT (dec, "mem data %p, size %d, maxsize %d", info.data,
          info.size, info.maxsize);
      //TODO: zero-copy for dmabuf
      if (inBuf.data) {
        /* FIXME: WA to fix coredump if input frame size bigger than buffer's
         * This will lead to potential frame drop.
         */
        if (inBuf.size > info.maxsize) {
          GST_ERROR_OBJECT (dec, "ignore: input size %u exceeds buffer size %u",
              inBuf.size, info.maxsize);
          gst_memory_unmap (inter_mem, &info);
          gst_buffer_pool_release_buffer (dec->in_port_pool, inter_buf);
          goto out;
        } else {
          memcpy (info.data, inBuf.data, inBuf.size);
        }
      }
      vidcbuf.data = info.data;
      vidcbuf.capacity = info.size;
      vidcbuf.size = inBuf.size;
      vidcbuf.index = dec->frame_index;
      vidcbuf.timestamp = inBuf.timestamp / 1000;
      vidcbuf.meta_fd = meta_fd;
      vidcbuf.metasize = metasize;
      gst_memory_unmap (inter_mem, &info);

      if (!vidc_queue (dec->comp, &vidcbuf)) {
        GST_ERROR_OBJECT (dec, "queueBuffer %d failed, buf %p", vidcbuf.fd, inter_buf);
        gst_buffer_pool_release_buffer (dec->in_port_pool, inter_buf);
        ret = GST_FLOW_ERROR;
        goto out;
      }
    } else {
      GST_ERROR_OBJECT (dec, "failed to get mem from buf %p", inter_buf);
      gst_buffer_pool_release_buffer (dec->in_port_pool, inter_buf);
      ret = GST_FLOW_ERROR;
      goto out;
    }
  }

  dec->frame_index += 1;

out:
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  return ret;
}

static void
gst_qvidc_vdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (object);

  GST_DEBUG_OBJECT (dec, "qvidc_vdec_set_property");

  switch (prop_id) {
    case PROP_OUTPUT_PICTURE_ORDER:
      dec->output_picture_order_mode = g_value_get_uint (value);
      break;
    case PROP_LOW_LATENCY:
      dec->low_latency_mode = g_value_get_boolean (value);
      break;
    case PROP_USE_EXTERNAL_POOL:
      dec->use_external_buf = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qvidc_vdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (object);

  GST_DEBUG_OBJECT (dec, "qvidc_vdec_get_property");

  switch (prop_id) {
    case PROP_OUTPUT_PICTURE_ORDER:
      g_value_set_uint (value, dec->output_picture_order_mode);
      break;
    case PROP_LOW_LATENCY:
      g_value_set_boolean (value, dec->low_latency_mode);
      break;
    case PROP_USE_EXTERNAL_POOL:
      g_value_set_boolean (value, dec->use_external_buf);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_qvidc_vdec_src_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);

  gboolean ret = TRUE;
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstFormat format;
      gdouble rate;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      guint32 seqnum;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);
      seqnum = gst_event_get_seqnum (event);
      GST_DEBUG_OBJECT (dec,
          "seek: start time:%" GST_TIME_FORMAT " stop time:%" GST_TIME_FORMAT
          " rate:%f format:%u flags:%u start_type:%u stop_type:%u seqnum:%u",
          GST_TIME_ARGS (start), GST_TIME_ARGS (stop), rate, format, flags,
          start_type, stop_type, seqnum);
      break;
    }
    default:
      break;
  }

  ret = GST_VIDEO_DECODER_CLASS (parent_class)->src_event (decoder, event);

  return ret;
}

static gboolean
gst_qvidc_vdec_sink_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (dec, "flush start");
      dec->is_flushing = TRUE;
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->sink_event (decoder, event);
}

static gboolean
gst_qvidc_vdec_negotiate (GstVideoDecoder * decoder)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (decoder);
  GstCaps *caps;
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (dec, "enter");
  if (GST_FLOW_OK != gst_qvidc_vdec_setup_output (decoder)) {
    goto error_setup_output;
  }

  caps = dec->output_state->caps;
  if (caps) {
    GST_DEBUG_OBJECT (dec, "parent negotiate");
    ret = GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
  }

  return ret;

error_setup_output:
  {
    GST_ERROR_OBJECT (dec, "failed to setup output");
  }

  return FALSE;
}

/* Called during object destruction process */
static void
gst_qvidc_vdec_finalize (GObject * object)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (object);

  GST_DEBUG_OBJECT (dec, "finalize");

  g_mutex_clear (&dec->pending_lock);
  g_cond_clear (&dec->pending_cond);

  /* Lastly chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_qvidc_vdec_change_state (GstElement * element, GstStateChange transition)
{
  GstQvidcVdec *dec = GST_QVIDC_VDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_LOG_OBJECT (dec, "decoder state change from PAUSED to READY");
      break;
    default:
      break;
  }
  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static void
gst_qvidc_vdec_class_init (GstQvidcVdecClass * klass)
{
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_vdec_src_template));

  /* Set GObject class property */
  gobject_class->set_property = gst_qvidc_vdec_set_property;
  gobject_class->get_property = gst_qvidc_vdec_get_property;
  gobject_class->finalize = gst_qvidc_vdec_finalize;

  /* Add property to this class */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_OUTPUT_PICTURE_ORDER, g_param_spec_uint ("output-picture-order-mode",
          "output picture order mode",
          "output picture order (0xffffffff=component default, 1: display order, 2: decoder order)",
          0, G_MAXUINT, DEFAULT_OUTPUT_PICTURE_ORDER_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LOW_LATENCY,
      g_param_spec_boolean ("low-latency-mode", "Low latency mode",
          "If enabled, decoder should be in low latency mode",
          DEFAULT_LOW_LATENCY_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_USE_EXTERNAL_POOL, g_param_spec_boolean ("use-external-pool",
          "if allow using external pool",
          "If enabled, decoder will use external buffer pool if supported by downstream.",
          DEFAULT_USE_EXTERNAL_POOL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  video_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_qvidc_vdec_set_format);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_qvidc_vdec_handle_frame);
  video_decoder_class->finish = GST_DEBUG_FUNCPTR (gst_qvidc_vdec_finish);
  video_decoder_class->open = GST_DEBUG_FUNCPTR (gst_qvidc_vdec_open);
  video_decoder_class->close = GST_DEBUG_FUNCPTR (gst_qvidc_vdec_close);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_qvidc_vdec_stop);
  video_decoder_class->flush = GST_DEBUG_FUNCPTR (gst_qvidc_vdec_flush);
  video_decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_qvidc_vdec_decide_allocation);
  video_decoder_class->src_event =
      GST_DEBUG_FUNCPTR (gst_qvidc_vdec_src_event);
  video_decoder_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_qvidc_vdec_sink_event);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "vidc decoder", "Decoder/Video",
      "Video Decoder based on HW codec", "QTI");
}

/* Invoked during object instantiation (equivalent C++ constructor).
 * Initialize only those variables that do not change during state change.
 * For other variables, place initialization into function open.*/
static void
gst_qvidc_vdec_init (GstQvidcVdec * dec)
{
  GstVideoDecoder *decoder = (GstVideoDecoder *) dec;

  gst_video_decoder_set_packetized (decoder, TRUE);

  GST_INFO_OBJECT (dec, "%s", __func__);

  dec->output_picture_order_mode = DEFAULT_OUTPUT_PICTURE_ORDER_MODE;
  dec->low_latency_mode = DEFAULT_LOW_LATENCY_MODE;
  dec->cb.data_copy_func = NULL;
  dec->cb.data_copy_func_param = NULL;
  dec->set_gstbuf_interlace_flag = DEFAULT_SET_GSTBUF_INTERLACE_FLAG;
  dec->use_external_buf = DEFAULT_USE_EXTERNAL_POOL;
  dec->is_flushing = FALSE;

  g_cond_init (&dec->pending_cond);
  g_mutex_init (&dec->pending_lock);

  dec->silent = FALSE;
}

gboolean
gst_qvidc_vdec_plugin_init (GstPlugin * plugin)
{
  /* debug category for filtering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_qvidc_vdec_debug, "qvidcvdec",
      0, "GST QTI VIDC video decoder");

  static gsize res = FALSE;
  static const gchar *tags[] = { NULL };
  if (g_once_init_enter (&res)) {
    gst_meta_register_custom ("GstQVIDCDMeta", tags, NULL, NULL, NULL);
    g_once_init_leave (&res, TRUE);
  }

  guint count = 0;
  for (guint i = 0; i < G_N_ELEMENTS (kDECODER_ELEMENTS); i++) {
    if (gst_element_register (plugin, kDECODER_ELEMENTS[i].element,
            kDECODER_ELEMENTS[i].rank, kDECODER_ELEMENTS[i].register_type ())) {
      count++;
      GST_INFO ("register element %s", kDECODER_ELEMENTS[i].element);
    } else {
      GST_ERROR ("failed to register element %s", kDECODER_ELEMENTS[i].element);
    }
  }

  return count > 0 ? TRUE : FALSE;
}
