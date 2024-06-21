// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/**
 * SECTION:element-qvais
 * @title: qvais
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! qvais ! fakesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gbm.h>
#include <gbm_priv.h>

#include "gstqvais.h"
#include "gstqvaisvpp.h"
#include "gstqvaispool.h"

#include <gst/gsttask.h>
#include <gst/video/video.h>
#include <gst/allocators/gstdmabuf.h>
#include <vidc/media/msm_media_info.h>

GST_DEBUG_CATEGORY (gst_qvais_debug);
#define GST_CAT_DEFAULT gst_qvais_debug

#define QVAIS_DEFALUT_SLEEP_US 5000
#define QVAIS_DEFAULT_MIN_OUTPUT_BUF_COUNT 6
#define QVAIS_DEFAULT_EXT_OUTPUT_BUF_COUNT 2

#define AIS_MIN_INPUT_WIDTH  96
#define AIS_MIN_INPUT_HEIGHT  96
#define AIS_3X_MAX_INPUT_WIDTH  720
#define AIS_3X_MAX_INPUT_HEIGHT  480
#define AIS_2X_MAX_INPUT_WIDTH  1280
#define AIS_2X_MAX_INPUT_HEIGHT  720

#define DEFAULT_SCALE_RATIO 0
#define DEFAULT_CLASSIFICATION 10
#define INVALID_COOKIE 0

enum
{
  PROP_0,
  PROP_SCALE_RATIO,
};

static GstElementClass *parent_class = NULL;

G_DEFINE_TYPE (GstQvais, gst_qvais, GST_TYPE_ELEMENT);
static void gst_qvais_send_message (GstQvais * self, GstQvaisMessage * msg);
static gpointer gst_qvais_message_handler (gpointer user_data);
static void gst_qvais_flush_messages (GstQvais * self);
static void queue_output_buf_task (gpointer user_data);

static GstCaps *gst_qvais_transform_caps (GstQvais * qvais,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static void gst_qvais_class_init (GstQvaisClass * klass);
static void gst_qvais_init (GstQvais * self);

static void input_buffer_done (void *pv, struct vpp_buffer *buf);
static void output_buffer_done (void *pv, struct vpp_buffer *buf);
static void vpp_event (void *pv, struct vpp_event e);
static gint max_support_ratio (gint width, gint height);
static gboolean fill_vppbuf_with_gstbuf (struct vpp_buffer *vpp_buf,
    GstBuffer * gst_buf, gboolean outport, guint32 buf_size);

#define SINK_FORMATS "{ NV12 }"

#define SRC_FORMATS "{ NV12 }"

#define QVAIS_COMPRESSION_CAPS_DMABUF(formats) \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES \
    (GST_CAPS_FEATURE_MEMORY_DMABUF, formats) \
    ",compression={linear,ubwc}"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (QVAIS_COMPRESSION_CAPS_DMABUF (SINK_FORMATS))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (QVAIS_COMPRESSION_CAPS_DMABUF (SRC_FORMATS))
    );


static GstCaps *
gst_qvais_transform_caps (GstQvais * qvais,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstPad *otherpad;
  GstCaps *result;

  if (GST_PAD_SRC == direction)
    otherpad = qvais->sinkpad;
  else
    otherpad = qvais->srcpad;

  result = gst_pad_get_pad_template_caps (otherpad);

  if (filter) {
    GstCaps *temp;
    temp = gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = temp;
  }

  GST_DEBUG_OBJECT (qvais, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

/* given fixed @caps, fixate @othercaps */
static GstCaps *
gst_qvais_fixate_caps (GstQvais * qvais,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *result = NULL;
  GstPad *pad = (GST_PAD_SINK == direction) ? qvais->sinkpad : qvais->srcpad;
  gint out_width_from_caps = -1;
  gint out_height_from_caps = -1;

  GST_DEBUG_OBJECT (pad, "fixate othercaps %" GST_PTR_FORMAT, othercaps);
  GST_DEBUG_OBJECT (pad, "   based on caps %" GST_PTR_FORMAT, caps);

  GstStructure *in_str = gst_caps_get_structure (caps, 0);
  gint in_w, in_h;

  gst_structure_get_int (in_str, "width", &in_w);
  gst_structure_get_int (in_str, "height", &in_h);

  GstStructure *out_str = gst_caps_get_structure (othercaps, 0);
  gint out_w, out_h;

  if (gst_structure_has_field (out_str, "width")) {
    gst_structure_get_int (out_str, "width", &out_width_from_caps);
  }
  if (gst_structure_has_field (out_str, "height")) {
    gst_structure_get_int (out_str, "height", &out_height_from_caps);
  }

  GST_LOG_OBJECT (pad,
      "width/height %dx%d from output caps, -1 is default value",
      out_width_from_caps, out_height_from_caps);

  gint max_ratio = max_support_ratio (in_w, in_h);
  if (qvais->scale_ratio) {
    qvais->scale_ratio = MIN (qvais->scale_ratio, max_ratio);
  } else {
    qvais->scale_ratio = max_ratio;
  }

  out_w = in_w * qvais->scale_ratio;
  out_h = in_h * qvais->scale_ratio;

  gboolean out_width_height_not_expected =
      (out_width_from_caps != -1 || out_height_from_caps != -1) ?
      (out_width_from_caps != out_w || out_height_from_caps != out_h) : false;

  if (out_width_height_not_expected) {
    GST_ERROR_OBJECT (qvais, "width and height(%dx%d) from src caps are"
        " not matched with expected(%dx%d)",
        out_width_from_caps, out_height_from_caps, out_w, out_h);
    result = NULL;
    goto done;
  }

  /* Set output width and height same as input to avoid intersection failure.
   * Later, set the correct width and height for output. */
  gst_structure_set (out_str, "width", G_TYPE_INT, in_w, "height",
      G_TYPE_INT, in_h, NULL);

  result = gst_caps_intersect (othercaps, caps);
  if (gst_caps_is_empty (result)) {
    gst_caps_unref (result);
    result = NULL;
    GST_ERROR_OBJECT (pad, "intersection is empty");
    goto done;
  } else {
    gst_caps_unref (othercaps);
  }

  GST_DEBUG_OBJECT (pad, "result %" GST_PTR_FORMAT, result);
  result = gst_caps_make_writable (result);

  GST_INFO_OBJECT (qvais, "wxh in: %dx%d -> out: %dx%d, scalar ratio %d",
      in_w, in_h, out_w, out_h, qvais->scale_ratio);

  GstStructure *ret_str = gst_caps_get_structure (result, 0);

  gst_structure_set (ret_str, "width", G_TYPE_INT, out_w, "height",
      G_TYPE_INT, out_h, NULL);

  /* fixate remaining fields */
  result = gst_caps_fixate (result);
  GST_DEBUG_OBJECT (pad, "result %" GST_PTR_FORMAT, result);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result)) {
      GST_DEBUG_OBJECT (pad, "caps is subset of result");
      gst_caps_replace (&result, caps);
    }
  }

done:
  GST_DEBUG_OBJECT (pad, "return %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
caps_has_compression_ubwc (const GstCaps * caps)
{
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *compression = gst_structure_get_string (s, "compression");

  return g_strcmp0 (compression, "ubwc") == 0 ? TRUE : FALSE;
}

/* Calculate valid size of stride*scanlines with alignment padding of
 * planes but without alignment padding of total size, see format detail
 * in msm_media_info.h. The valid size is for filesink to dump, hence can
 * view the dump correctly by setting line stride and plane scanlines in
 * image player tool. */
static gsize
calc_valid_size (const GstVideoInfo * info, gboolean ubwc)
{
  gsize size = 0;
  gint format = GST_VIDEO_INFO_FORMAT (info);
  gint width = GST_VIDEO_INFO_WIDTH (info);
  gint height = GST_VIDEO_INFO_HEIGHT (info);

  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      if (ubwc) {
        size = VENUS_BUFFER_SIZE_USED (COLOR_FMT_NV12_UBWC, width, height, 0);
        GST_DEBUG ("NV12_UBWC valid size %" G_GSIZE_FORMAT, size);
      } else {
        int vformat = COLOR_FMT_NV12;
        int y_stride = (int) VENUS_Y_STRIDE (vformat, width);
        int uv_stride = (int) VENUS_UV_STRIDE (vformat, width);
        int y_sclines = (int) VENUS_Y_SCANLINES (vformat, height);
        int uv_sclines = (int) VENUS_UV_SCANLINES (vformat, height);
        size = y_stride * y_sclines + uv_stride * uv_sclines;
        GST_DEBUG ("NV12 valid size %" G_GSIZE_FORMAT, size);
      }
      break;

    default:
      GST_ERROR ("NOT support format %s", GST_VIDEO_INFO_NAME (info));
      break;
  }

  return size;
}

static gsize
calc_gbm_buf_size (const GstVideoInfo * info, gboolean ubwc)
{
  gsize size = 0;
  gint format = GST_VIDEO_INFO_FORMAT (info);
  gint width = GST_VIDEO_INFO_WIDTH (info);
  gint height = GST_VIDEO_INFO_HEIGHT (info);

  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      if (ubwc) {
        size = VENUS_BUFFER_SIZE (COLOR_FMT_NV12_UBWC, width, height);
        GST_DEBUG ("NV12_UBWC valid size %" G_GSIZE_FORMAT, size);
      } else {
        size = VENUS_BUFFER_SIZE (COLOR_FMT_NV12, width, height);
        GST_DEBUG ("NV12 valid size %" G_GSIZE_FORMAT, size);
      }
      break;

    default:
      GST_ERROR ("Not supported format %s", GST_VIDEO_INFO_NAME (info));
      break;
  }

  return size;
}

static enum color_fmts
convert_vpp_color_fmt (enum vpp_color_format vpp_format)
{
  enum color_fmts ret = COLOR_FMT_NV12;

  switch (vpp_format) {
    case VPP_COLOR_FORMAT_NV12_VENUS:
      break;
    case VPP_COLOR_FORMAT_UBWC_NV12:
      ret = COLOR_FMT_NV12_UBWC;
      break;
    default:
      GST_ERROR ("Not supported vpp format %d", vpp_format);
      break;
  }

  return ret;
}

static gboolean
reach_ais_limitation (gboolean in_ubwc, gboolean in_height)
{
  gboolean ret = FALSE;

  ret = (in_ubwc && in_height % 8) ? TRUE : FALSE;
  if (ret)
    GST_ERROR("Reach the AIS limitation. For NV12 UBWC,"
        " input height(%d) should be 8 aligned.", in_height);

  return ret;
}

static gboolean
gst_qvais_set_info (GstQvais * qvais,
    GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstQvais *self = GST_QVAIS (qvais);
  const GstCapsFeatures *features;
  gboolean ret = TRUE;
  struct hqv_control ctrl;
  enum vpp_port in_port = VPP_PORT_INPUT;
  enum vpp_port out_port = VPP_PORT_OUTPUT;
  struct vpp_port_param in_param;
  struct vpp_port_param out_param;
  enum color_fmts in_color_fmt;
  enum color_fmts out_color_fmt;

  GST_INFO_OBJECT (self, " in_info=%p,  incaps: %" GST_PTR_FORMAT,
      in_info, incaps);
  GST_INFO_OBJECT (self, "out_info=%p, outcaps: %" GST_PTR_FORMAT,
      out_info, outcaps);

  /* when 1st frame comes, align in info by video meta
   * and out info by buffer pool */
  self->in_info = *in_info;
  self->out_info = *out_info;

  self->in_ubwc = caps_has_compression_ubwc (incaps);
  self->out_ubwc = self->in_ubwc;

  GST_INFO_OBJECT (self, "in_ubwc=%u, out_ubwc=%u",
      self->in_ubwc, self->out_ubwc);

  if (reach_ais_limitation(self->in_ubwc, in_info->height)) {
    ret = FALSE;
    goto exit;
  }

  /* Set valid size for _decide_allocation() to create output buffer pool
   * and allocate gstbuffer with the valid size for filesink to dump. */
  GST_VIDEO_INFO_SIZE (&self->out_info) =
      calc_valid_size (out_info, self->out_ubwc);

  self->in_vpp_buf_size = calc_gbm_buf_size (in_info, self->in_ubwc);
  self->out_vpp_buf_size = calc_gbm_buf_size (out_info, self->out_ubwc);
  features = gst_caps_get_features (incaps, 0);
  self->in_dmabuf = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_DMABUF);

  features = gst_caps_get_features (outcaps, 0);
  self->out_dmabuf = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_DMABUF);

  if (!self->in_dmabuf || !self->out_dmabuf)
    GST_INFO_OBJECT (self, "in_dmabuf=%u, out_dmabuf=%u",
        self->in_dmabuf, self->out_dmabuf);

  ctrl.mode = HQV_MODE_MANUAL;
  ctrl.ctrl_type = HQV_CONTROL_AIS;
  ctrl.ais.mode = HQV_MODE_AUTO;
  ctrl.ais.roi.enable = 0;
  ctrl.ais.classification = qvais->classification;

  if (qvaisvpp_set_ctrl (self->vpp_ctx, ctrl)) {
    in_param.height = in_info->height;
    in_param.width = in_info->width;
    out_param.height = out_info->height;
    out_param.width = out_info->width;

    if (self->in_ubwc) {
      in_param.fmt = VPP_COLOR_FORMAT_UBWC_NV12;
    } else {
      in_param.fmt = VPP_COLOR_FORMAT_NV12_VENUS;
    }

    out_param.fmt = in_param.fmt;

    in_color_fmt = convert_vpp_color_fmt (in_param.fmt);
    out_color_fmt = in_color_fmt;

    in_param.stride = VENUS_Y_STRIDE (in_color_fmt, in_param.width);
    in_param.scanlines = VENUS_Y_SCANLINES (in_color_fmt, in_param.height);

    out_param.stride = VENUS_Y_STRIDE (out_color_fmt, out_param.width);
    out_param.scanlines = VENUS_Y_SCANLINES (out_color_fmt, out_param.height);

    GST_INFO_OBJECT (self, "in: ubwc=%u, w:h:%d:%d stride:scan:%d:%d"
        " out: ubwc=%u, w:h:%d:%d stride:scan:%d:%d",
        self->in_ubwc, in_param.width, in_param.height, in_param.stride,
        in_param.scanlines, self->out_ubwc, out_param.width, out_param.height,
        out_param.stride, out_param.scanlines);

    if (qvaisvpp_set_parameter (self->vpp_ctx, in_port, in_param) &&
        qvaisvpp_set_parameter (self->vpp_ctx, out_port, out_param)) {
      ret = TRUE;
    } else {
      ret = FALSE;
    }
  } else {
    ret = FALSE;
  }

exit:
  return ret;
}

static gboolean
gst_qvais_decide_allocation (GstQvais * qvais, GstQuery * query)
{
  GstQvais *self = GST_QVAIS (qvais);
  GstVideoInfo *info = &self->out_info;
  GstBufferPool *pool = NULL;
  GstCaps *outcaps = NULL;
  GstAllocator *allocator = NULL;
  GstStructure *config;
  guint min = 0, max = 0, size = 0;
  gboolean update_pool;
  struct vpp_requirements req;

  GST_INFO_OBJECT (self, "%" GST_PTR_FORMAT, query);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    GST_INFO_OBJECT (self, "downstream pool %p, size %u, min %u, max %u",
        pool, size, min, max);
    update_pool = TRUE;
  } else {
    GST_INFO_OBJECT (self, "downstream not propose pool");
    size = 0;
    update_pool = FALSE;
  }

  gst_query_parse_allocation (query, &outcaps, NULL);

  GST_INFO_OBJECT (self, "size %u, info size %u", size, (guint) info->size);
  size = MAX (size, info->size);
  if (qvaisvpp_get_buf_requirements (self->vpp_ctx, &req)) {
    self->in_req_cnt = req.buf_req[VPP_RESOLUTION_HD].in_req;
    self->out_req_cnt = req.buf_req[VPP_RESOLUTION_HD].out_req;
  } else {
    self->out_req_cnt = QVAIS_DEFAULT_MIN_OUTPUT_BUF_COUNT;
  }

  min = self->out_req_cnt;
  max = min + QVAIS_DEFAULT_EXT_OUTPUT_BUF_COUNT;

  GST_INFO_OBJECT (self, "size %u, min %u, max %u", size, min, max);

  if (pool)
    gst_object_unref (pool);

  /* always use its own pool at this time */
  pool = gst_qvais_pool_new (self->out_ubwc);
  if (!pool) {
    GST_ERROR_OBJECT (self, "pool new error");

    return FALSE;
  }

  /* only support dmabuf allocator at this time */
  allocator = gst_dmabuf_allocator_new ();
  if (!allocator) {
    GST_ERROR_OBJECT (self, "allocator new error");
    gst_clear_object (&pool);

    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "qvais pool %p, allocator %p", pool, allocator);

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_set_config (pool, config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  if (self->pool != NULL)
    gst_object_unref (self->pool);
  self->pool = pool;

  return TRUE;
}

static gboolean
gst_qvais_start (GstQvais * self)
{
  if (!self->active) {
    if (!qvaisvpp_open (self->vpp_ctx)) {
      GST_ERROR_OBJECT (self, "vpp open error");
      return FALSE;
    }
  }
  self->active = TRUE;

  if (!gst_buffer_pool_is_active (self->pool)) {
    GST_DEBUG_OBJECT (self, "setting pool %p active", self->pool);
    if (!gst_buffer_pool_set_active (self->pool, TRUE)) {
      GST_ERROR_OBJECT (self, "setting pool %p active failed", self->pool);
      return FALSE;
    }
  }

  gst_task_start (self->outbuf_task);

  return TRUE;
}

static gboolean
gst_qvais_open (GstQvais * self)
{
  GST_INFO_OBJECT (self, "qvais open");

  gboolean ret = TRUE;

  self->cb.pv = self;
  self->cb.input_buffer_done = input_buffer_done;
  self->cb.output_buffer_done = output_buffer_done;
  self->cb.vpp_event = vpp_event;
  self->vpp_ctx = qvaisvpp_init (0, self->cb);

  if (self->vpp_ctx) {
    self->msg_thread =
        g_thread_new ("qvais-message", (GThreadFunc) gst_qvais_message_handler,
        self);
    self->outbuf_task =
        gst_task_new (((GstTaskFunction) queue_output_buf_task), self, NULL);

    if (self->msg_thread && self->outbuf_task) {
      g_mutex_init (&self->messages_lock);
      g_queue_init (&self->messages);
      g_mutex_init (&self->flush_lock);
      g_cond_init (&self->flush_cond);
      g_mutex_init (&self->drain_lock);
      g_cond_init (&self->drain_cond);

      g_rec_mutex_init (&self->outbuf_lock);
      gst_task_set_lock (self->outbuf_task, &self->outbuf_lock);
    } else {
      GST_ERROR_OBJECT (self,
          "failed to create message thread %p or output task %p",
          self->msg_thread, self->outbuf_task);

      /* release resource if error occurs */
      qvaisvpp_term (self->vpp_ctx);
      self->vpp_ctx = NULL;

      if (self->msg_thread) {
        g_thread_join (self->msg_thread);
        self->msg_thread = NULL;
      }
      if (self->outbuf_task) {
        gst_task_join (self->outbuf_task);
        g_object_unref (self->outbuf_task);
        self->outbuf_task = NULL;
      }

      ret = FALSE;
    }
  } else {
    GST_ERROR_OBJECT (self, "vpp init error");
    ret = FALSE;
  }

  return ret;
}

static void
gst_qvais_close (GstQvais * self)
{
  GST_INFO_OBJECT (self, "qvais close");

  /* stop thread of queuing output buffer */
  if (self->pool)
    gst_buffer_pool_set_flushing (self->pool, TRUE);

  if (self->outbuf_task) {
    gst_task_stop (self->outbuf_task);
    gst_task_join (self->outbuf_task);
    g_object_unref (self->outbuf_task);
    g_rec_mutex_clear (&self->outbuf_lock);
    self->outbuf_task = NULL;
  }

  if (self->active) {
    self->input_flushing = TRUE;
    self->output_flushing = TRUE;
    qvaisvpp_flush (self->vpp_ctx, VPP_PORT_INPUT);
    qvaisvpp_flush (self->vpp_ctx, VPP_PORT_OUTPUT);

    g_mutex_lock (&self->flush_lock);
    while (self->input_flushing || self->output_flushing) {
      GST_DEBUG_OBJECT (self, "waiting for flush");
      gint64 wait_until = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
      if (!g_cond_wait_until (&self->flush_cond, &self->flush_lock, wait_until)) {
        GST_DEBUG_OBJECT (self, "waited for flush");
        if (self->input_flushing)
          self->input_flushing = FALSE;
        if (self->output_flushing)
          self->output_flushing = FALSE;
      }
    }
    g_mutex_unlock (&self->flush_lock);

    if (self->vpp_ctx != NULL) {
      qvaisvpp_close (self->vpp_ctx);
    }
    self->active = FALSE;
  }

  if (self->vpp_ctx != NULL) {
    qvaisvpp_term (self->vpp_ctx);
    self->vpp_ctx = NULL;
  }

  if (self->pool) {
    gst_buffer_pool_set_active (self->pool, FALSE);
    GST_DEBUG_OBJECT (self, "set pool %p active FALSE, ref cnt: %d",
        self->pool, GST_OBJECT_REFCOUNT (self->pool));
    gst_object_unref (self->pool);
    self->pool = NULL;
  }

  gst_qvais_flush_messages (self);
  g_mutex_clear (&self->messages_lock);
  g_cond_clear (&self->flush_cond);
  g_mutex_clear (&self->flush_lock);
  g_cond_clear (&self->drain_cond);
  g_mutex_clear (&self->drain_lock);
  g_thread_join (self->msg_thread);
}

gboolean
fill_vppbuf_with_gstbuf (struct vpp_buffer *vpp_buf, GstBuffer * gst_buf,
    gboolean outport, guint32 buf_size)
{
  gboolean ret = FALSE;
  GstMemory *memory = NULL;

  if (gst_buffer_n_memory (gst_buf)) {
    memory = gst_buffer_get_memory (gst_buf, 0);
  }
  GST_DEBUG ("gst_buf %p, vpp_buf %p, memory %p, outport %d, buf_size %d",
      gst_buf, vpp_buf, memory, outport, buf_size);
  if (gst_buf && vpp_buf && memory) {
    vpp_buf->pixel.fd = gst_fd_memory_get_fd (memory);
    vpp_buf->pixel.alloc_len = buf_size;
    vpp_buf->pixel.filled_len = buf_size;
    vpp_buf->pixel.valid_data_len = buf_size;
    vpp_buf->pvGralloc = gst_buf;
    vpp_buf->extradata.fd = -1;
    if (!outport) {
      vpp_buf->timestamp = GST_BUFFER_PTS (gst_buf) / 1000;
      GST_DEBUG ("input buf timestamp %ld, buf size %d, fd %d",
          vpp_buf->timestamp, buf_size, vpp_buf->pixel.fd);
    }

    ret = TRUE;
  }

  return ret;
}

static void
gst_qvais_finalize (GObject * obj)
{
  GstQvais *self = GST_QVAIS (obj);

  GST_INFO_OBJECT (self, "finalize qvais %p", self);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GstStateChangeReturn
gst_qvais_change_state (GstElement * element, GstStateChange transition)
{
  GstQvais *qvais;
  GstStateChangeReturn ret;

  qvais = GST_QVAIS (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* open device/library if needed */
      if (!gst_qvais_open (qvais))
        goto open_failed;
      break;

    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* close device/library if needed */
      gst_qvais_close (qvais);
      break;
    default:
      break;
  }

  return ret;

  /* Errors */
open_failed:
  {
    GST_ELEMENT_ERROR (qvais, LIBRARY, INIT, (NULL), ("Failed to open qvais"));
    return GST_STATE_CHANGE_FAILURE;
  }
}

static GstFlowReturn
gst_qvais_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstQvais *qvais;
  GstFlowReturn ret = GST_FLOW_ERROR;
  enum vpp_port in_port = VPP_PORT_INPUT;
  struct vpp_buffer vpp_in_buf;

  qvais = GST_QVAIS (parent);
  if (qvais->passthrough) {
    ret = gst_pad_push (qvais->srcpad, buf);
    goto done;
  }

  memset (&vpp_in_buf, 0, sizeof (struct vpp_buffer));
  if (fill_vppbuf_with_gstbuf (&vpp_in_buf, buf, false, qvais->in_vpp_buf_size)) {
    qvais->frame_number = (qvais->frame_number + 1 == 0) ? 1 : qvais->frame_number + 1;
    vpp_in_buf.cookie_in_to_out = (void*)(qvais->frame_number);

    if (qvaisvpp_queue_buf (qvais->vpp_ctx, in_port, &vpp_in_buf)) {
      GST_DEBUG_OBJECT (qvais, "queue vpp input buf %p successfully", buf);
      ret = GST_FLOW_OK;
    } else {
      GST_ERROR_OBJECT (qvais, "failed to queue vpp input buf");
      gst_buffer_unref (buf);
      ret = GST_FLOW_ERROR;
    }
  } else {
    GST_ERROR_OBJECT (qvais, "failed to fill vpp input buf");
    gst_buffer_unref (buf);
    ret = GST_FLOW_ERROR;
  }

done:
  return ret;
}

static void
gst_qvais_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQvais *qvais = GST_QVAIS (object);

  GST_DEBUG_OBJECT (qvais, "qvais set property");

  switch (prop_id) {
    case PROP_SCALE_RATIO:
      qvais->scale_ratio = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qvais_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstQvais *qvais = GST_QVAIS (object);

  GST_DEBUG_OBJECT (qvais, "qvais get property");

  switch (prop_id) {
    case PROP_SCALE_RATIO:
      g_value_set_uint (value, qvais->scale_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* initialize the qvais's class */
static void
gst_qvais_class_init (GstQvaisClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);
  /* Set GObject class property */
  gobject_class->set_property = gst_qvais_set_property;
  gobject_class->get_property = gst_qvais_get_property;
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_qvais_finalize);

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_SCALE_RATIO, g_param_spec_uint ("scale-ratio",
          "upscaling ratio",
          "upscaling ratio (0: default, only support 2X or 3X)",
          0, G_MAXUINT, DEFAULT_SCALE_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_qvais_change_state);
  gst_element_class_set_static_metadata (gstelement_class,
      "QTI Video AIS(AI based upscaling)",
      "Qvais/Video", "Video AIS(AI based upscaling)", "QTI");
}

static void
input_buffer_done (void *pv, struct vpp_buffer *buf)
{
  GstQvais *self = (GstQvais *) pv;
  GstQvaisMessage *msg = g_slice_new (GstQvaisMessage);

  GST_DEBUG_OBJECT (self, "input buffer done, gst buf %p", buf->pvGralloc);

  msg->type = GST_QVAIS_MESSAGE_INPUT_BUF_DONE;
  memcpy (&(msg->content.buf), buf, sizeof (struct vpp_buffer));
  gst_qvais_send_message (self, msg);
}

static void
output_buffer_done (void *pv, struct vpp_buffer *buf)
{
  GstQvais *self = (GstQvais *) pv;
  GstQvaisMessage *msg = g_slice_new (GstQvaisMessage);

  GST_DEBUG_OBJECT (self, "output buffer done, gst buf %p", buf->pvGralloc);

  if (self->vpp_ctx) {
    msg->type = GST_QVAIS_MESSAGE_OUTPUT_BUF_DONE;
    memcpy (&(msg->content.buf), buf, sizeof (struct vpp_buffer));
    gst_qvais_send_message (self, msg);
  }
}

void
qvais_handle_output_buf_done (GstQvais * self, struct vpp_buffer *buf)
{
  GstPad *srcpad = NULL;
  GstBuffer *gst_buf = NULL;

  if (self->vpp_ctx) {
    gst_buf = (GstBuffer *) (buf->pvGralloc);
    if (NULL == gst_buf) {
      GST_ERROR_OBJECT (self, "error occurred, gst_buf is %p", gst_buf);
      return;
    }

    if (buf->cookie_in_to_out == INVALID_COOKIE) {
      GST_DEBUG_OBJECT (self, "Dropping unprocessed buffer");
      gst_buffer_unref (gst_buf);
      return;
    }

    GST_BUFFER_PTS (gst_buf) = buf->timestamp * 1000;
    GST_DEBUG_OBJECT (self,
        "handle message: output buffer done, gst buf %p, buf->timestamp %ld, PTS %"
        GST_TIME_FORMAT, gst_buf, buf->timestamp,
        GST_TIME_ARGS (GST_BUFFER_PTS (gst_buf)));
    if (self->output_flushing || !self->active || self->eos) {
      gst_buffer_unref (gst_buf);
    } else {
      srcpad = gst_element_get_static_pad ((GstElement *) self, "src");

      if (srcpad) {
        gst_pad_push (srcpad, gst_buf);
        gst_object_unref (srcpad);
      } else {
        gst_buffer_unref (gst_buf);
        GST_ERROR_OBJECT (self, "source pad is invalid");
      }
    }
  }
}

static void
vpp_event (void *pv, struct vpp_event e)
{
  GstQvais *self = (GstQvais *) pv;
  GstQvaisMessage *msg = g_slice_new (GstQvaisMessage);

  switch (e.type) {
    case VPP_EVENT_FLUSH_DONE:
      GST_DEBUG ("flush done port:%d", e.flush_done.port);
      if (e.flush_done.port == VPP_PORT_INPUT) {
        GST_DEBUG ("input port flush done");
      } else if (e.flush_done.port == VPP_PORT_OUTPUT) {
        GST_DEBUG ("output port flush done");
      }
      msg->type = GST_QVAIS_MESSAGE_FLUSH_DONE;
      memcpy (&(msg->content.event), &e, sizeof (struct vpp_event));
      gst_qvais_send_message (self, msg);
      break;
    case VPP_EVENT_ERROR:
      GST_ERROR ("receive VPP_EVENT_ERROR");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("VPP driver posts an error"),
          (NULL));
      break;
    case VPP_EVENT_DRAIN_DONE:
      GST_DEBUG ("receive drain done");
      msg->type = GST_QVAIS_MESSAGE_DRAIN_DONE;
      gst_buffer_pool_set_flushing (self->pool, TRUE);
      if (self->outbuf_task) {
        gst_task_stop (self->outbuf_task);
        gst_task_join (self->outbuf_task);
        g_object_unref (self->outbuf_task);
        self->outbuf_task = NULL;
      }
      gst_qvais_send_message (self, msg);
      break;
    default:
      break;
  }
}

static GstCaps *
gst_qvais_find_caps (GstQvais * qvais, GstPad * pad, GstCaps * caps)
{
  GstPad *otherpad, *otherpeer;
  GstCaps *othercaps;
  gboolean is_fixed;

  /* caps must be fixed here, this is a programming error if it's not */
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);

  otherpad = (pad == qvais->srcpad) ? qvais->sinkpad : qvais->srcpad;
  otherpeer = gst_pad_get_peer (otherpad);

  othercaps = gst_qvais_transform_caps (qvais,
      GST_PAD_DIRECTION (pad), caps, NULL);

  if (!othercaps || gst_caps_is_empty (othercaps))
    goto no_transform;

  /* if the othercaps are not fixed, we need to fixate them, first attempt
   * is by attempting passthrough if the othercaps are a superset of caps. */
  /* FIXME. maybe the caps is not fixed because it has multiple structures of
   * fixed caps */
  is_fixed = gst_caps_is_fixed (othercaps);
  if (!is_fixed) {
    GST_DEBUG_OBJECT (qvais,
        "transform returned non fixed  %" GST_PTR_FORMAT, othercaps);

    /* Now let's see what the peer suggests based on our transformed caps */
    if (otherpeer) {
      GstCaps *peercaps, *intersection, *templ_caps;

      GST_DEBUG_OBJECT (qvais,
          "Checking peer caps with filter %" GST_PTR_FORMAT, othercaps);

      peercaps = gst_pad_query_caps (otherpeer, othercaps);
      GST_DEBUG_OBJECT (qvais, "Resulted in %" GST_PTR_FORMAT, peercaps);
      if (!gst_caps_is_empty (peercaps)) {
        templ_caps = gst_pad_get_pad_template_caps (otherpad);

        GST_DEBUG_OBJECT (qvais,
            "Intersecting with template caps %" GST_PTR_FORMAT, templ_caps);

        intersection =
            gst_caps_intersect_full (peercaps, templ_caps,
            GST_CAPS_INTERSECT_FIRST);
        GST_DEBUG_OBJECT (qvais, "Intersection: %" GST_PTR_FORMAT,
            intersection);
        gst_caps_unref (peercaps);
        gst_caps_unref (templ_caps);
        peercaps = intersection;

        GST_DEBUG_OBJECT (qvais,
            "Intersecting with transformed caps %" GST_PTR_FORMAT, othercaps);
        intersection =
            gst_caps_intersect_full (peercaps, othercaps,
            GST_CAPS_INTERSECT_FIRST);
        GST_DEBUG_OBJECT (qvais, "Intersection: %" GST_PTR_FORMAT,
            intersection);
        gst_caps_unref (peercaps);
        gst_caps_unref (othercaps);
        othercaps = intersection;
      } else {
        gst_caps_unref (othercaps);
        othercaps = peercaps;
      }

      is_fixed = gst_caps_is_fixed (othercaps);
    } else {
      GST_DEBUG_OBJECT (qvais, "no peer, doing passthrough");
      gst_caps_unref (othercaps);
      othercaps = gst_caps_ref (caps);
      is_fixed = TRUE;
    }
  }
  if (gst_caps_is_empty (othercaps))
    goto no_transform_possible;

  GST_DEBUG ("have %sfixed caps %" GST_PTR_FORMAT, (is_fixed ? "" : "non-"),
      othercaps);

  /* second attempt at fixation, call the fixate */
  /* caps could be fixed but the subclass may want to add fields */

  GST_DEBUG_OBJECT (qvais, "calling fixate_caps for %" GST_PTR_FORMAT
      " using caps %" GST_PTR_FORMAT " on pad %s:%s", othercaps, caps,
      GST_DEBUG_PAD_NAME (otherpad));
  /* note that we pass the complete array of structures to the fixate
   * function, it needs to truncate itself */
  othercaps =
      gst_qvais_fixate_caps (qvais, GST_PAD_DIRECTION (pad), caps, othercaps);

  if (!othercaps) {
    g_critical ("qvais: second attempt to fixate caps returned "
        "invalid (NULL) caps on pad %s:%s", GST_DEBUG_PAD_NAME (pad));
  }
  is_fixed = othercaps && gst_caps_is_fixed (othercaps);
  GST_DEBUG_OBJECT (qvais, "after fixating %" GST_PTR_FORMAT, othercaps);

  /* caps should be fixed now, if not we have to fail. */
  if (!is_fixed)
    goto could_not_fixate;

  /* and peer should accept */
  if (otherpeer && !gst_pad_query_accept_caps (otherpeer, othercaps))
    goto peer_no_accept;

  GST_DEBUG_OBJECT (qvais, "Input caps were %" GST_PTR_FORMAT
      ", and got final caps %" GST_PTR_FORMAT, caps, othercaps);

  if (otherpeer)
    gst_object_unref (otherpeer);

  return othercaps;

  /* ERRORS */
no_transform:
  {
    GST_DEBUG_OBJECT (qvais,
        "qvais returned useless %" GST_PTR_FORMAT, othercaps);
    goto error_cleanup;
  }
no_transform_possible:
  {
    GST_DEBUG_OBJECT (qvais,
        "qvais could not transform %" GST_PTR_FORMAT
        " in anything we support", caps);
    goto error_cleanup;
  }
could_not_fixate:
  {
    GST_DEBUG_OBJECT (qvais, "FAILED to fixate %" GST_PTR_FORMAT, othercaps);
    goto error_cleanup;
  }
peer_no_accept:
  {
    GST_DEBUG_OBJECT (qvais, "FAILED to get peer of %" GST_PTR_FORMAT
        " to accept %" GST_PTR_FORMAT, otherpad, othercaps);
    goto error_cleanup;
  }
error_cleanup:
  {
    if (otherpeer)
      gst_object_unref (otherpeer);
    if (othercaps)
      gst_caps_unref (othercaps);

    return NULL;
  }
}

static gint
max_support_ratio (gint width, gint height)
{
  gint ratio = 1;

  if (width < AIS_MIN_INPUT_WIDTH || height < AIS_MIN_INPUT_HEIGHT
      || width > AIS_2X_MAX_INPUT_WIDTH || height > AIS_2X_MAX_INPUT_HEIGHT) {
    ratio = 1;
  } else if (width <= AIS_3X_MAX_INPUT_WIDTH
      && height <= AIS_3X_MAX_INPUT_HEIGHT) {
    ratio = 3;
  } else {
    ratio = 2;
  }

  return ratio;
}

static gboolean
gst_qvais_setcaps (GstQvais * qvais, GstPad * pad, GstCaps * incaps)
{
  GstCaps *outcaps, *prev_incaps = NULL, *prev_outcaps = NULL;
  gboolean ret = TRUE;
  GstQuery *query;
  GstVideoInfo in_info, out_info;

  GST_DEBUG_OBJECT (pad, "have new caps %p %" GST_PTR_FORMAT, incaps, incaps);

  /* find best possible caps for the other pad */
  outcaps = gst_qvais_find_caps (qvais, pad, incaps);
  if (!outcaps || gst_caps_is_empty (outcaps))
    goto no_caps_possible;

  /* if we have the same caps, we can optimize and reuse the input caps */
  if (gst_caps_is_equal (incaps, outcaps)) {
    GST_INFO_OBJECT (qvais, "reuse caps");
    gst_caps_unref (outcaps);
    outcaps = gst_caps_ref (incaps);
  }

  prev_incaps = qvais->sink_caps;
  prev_outcaps = qvais->src_caps;
  GST_DEBUG_OBJECT (qvais,
      "prev in caps: %" GST_PTR_FORMAT ", prev out caps %" GST_PTR_FORMAT,
      prev_incaps, prev_outcaps);
  if (prev_incaps && prev_outcaps && gst_caps_is_equal (prev_incaps, incaps)
      && gst_caps_is_equal (prev_outcaps, outcaps)) {
    GST_DEBUG_OBJECT (qvais,
        "New caps equal to old ones: %" GST_PTR_FORMAT " -> %" GST_PTR_FORMAT,
        incaps, outcaps);
    ret = TRUE;
  } else {
    /* need reconfigure here, currently not supported since vpp does not support yet */
    GST_DEBUG_OBJECT (qvais, "todo: vpp reconfigure %" GST_PTR_FORMAT, outcaps);
    if (!gst_qvais_transform_caps (qvais, GST_PAD_DIRECTION (pad), incaps,
            outcaps))
      goto failed_configure;

    if (!prev_outcaps || !gst_caps_is_equal (outcaps, prev_outcaps)) {
      /* let downstream know about our caps */
      ret = gst_pad_set_caps (qvais->srcpad, outcaps);
      gst_caps_replace (&(qvais->src_caps), outcaps);
    }
    if (!prev_incaps || !gst_caps_is_equal (incaps, prev_incaps))
      gst_caps_replace (&(qvais->sink_caps), incaps);
  }

  /* input caps */
  if (!gst_video_info_from_caps (&in_info, incaps))
    goto invalid_caps;

  /* output caps */
  if (!gst_video_info_from_caps (&out_info, outcaps))
    goto invalid_caps;

  if (qvais->scale_ratio == 1) {
    GST_INFO_OBJECT (qvais, "work in passthrough mode, width %d, height %d",
        in_info.width, in_info.height);
    qvais->passthrough = TRUE;
    goto done;
  } else {
    qvais->passthrough = FALSE;
  }

  if (!gst_qvais_set_info (qvais, incaps, &in_info, outcaps, &out_info))
    goto invalid_info;
  if (ret) {
    /* try to get a pool when needed */
    query = gst_query_new_allocation (outcaps, TRUE);
    ret = gst_qvais_decide_allocation (qvais, query);
  }
  if (ret) {
    ret = gst_qvais_start (qvais);
  }
done:
  if (outcaps)
    gst_caps_unref (outcaps);

  return ret;

  /* ERRORS */
no_caps_possible:
  {
    GST_WARNING_OBJECT (qvais,
        "qvais could not transform %" GST_PTR_FORMAT
        " in anything we support", incaps);
    ret = FALSE;
    goto done;
  }
failed_configure:
  {
    GST_WARNING_OBJECT (qvais, "FAILED to configure incaps %" GST_PTR_FORMAT
        " and outcaps %" GST_PTR_FORMAT, incaps, outcaps);
    ret = FALSE;
    goto done;
  }
invalid_caps:
  {
    GST_WARNING_OBJECT (qvais, "invalid caps %" GST_PTR_FORMAT
        " and outcaps %" GST_PTR_FORMAT, incaps, outcaps);
    if (qvais->sink_caps)
      gst_caps_replace (&(qvais->sink_caps), NULL);
    if (qvais->src_caps)
      gst_caps_replace (&(qvais->src_caps), NULL);
    ret = FALSE;
    goto done;
  }
invalid_info:
  {
    GST_WARNING_OBJECT (qvais, "invalid caps %" GST_PTR_FORMAT
        " and outcaps %" GST_PTR_FORMAT, incaps, outcaps);
    ret = FALSE;
    goto done;
  }

}

static gboolean
gst_qvais_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstQvais *qvais = GST_QVAIS (parent);
  gboolean ret = TRUE;
  gint64 wait_until = 0;
  gboolean forward = TRUE;
  GstCaps *caps = NULL;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (qvais, "received flush start event");
      if (qvais->passthrough)
        break;

      gst_task_pause (qvais->outbuf_task);
      gst_buffer_pool_set_flushing (qvais->pool, TRUE);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (qvais, "received flush stop event");
      if (qvais->passthrough)
        break;

      qvais->input_flushing = TRUE;
      qvais->output_flushing = TRUE;
      qvaisvpp_flush (qvais->vpp_ctx, VPP_PORT_INPUT);
      qvaisvpp_flush (qvais->vpp_ctx, VPP_PORT_OUTPUT);
      GST_DEBUG_OBJECT (qvais, "waiting for vpp flush done");
      g_mutex_lock (&qvais->flush_lock);
      while (qvais->input_flushing || qvais->output_flushing) {
        wait_until = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
        if (!g_cond_wait_until (&qvais->flush_cond, &qvais->flush_lock,
                wait_until)) {
          if (qvais->input_flushing)
            qvais->input_flushing = FALSE;
          if (qvais->output_flushing)
            qvais->output_flushing = FALSE;
        }
      }
      g_mutex_unlock (&qvais->flush_lock);
      GST_DEBUG_OBJECT (qvais, "waited for vpp flush done");

      gst_buffer_pool_set_flushing (qvais->pool, FALSE);
      gst_task_resume (qvais->outbuf_task);
      break;
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (qvais, "received eos event, passthrough is %d",
          (int) qvais->passthrough);
      if (qvais->passthrough)
        break;

      qvaisvpp_drain (qvais->vpp_ctx);

      GST_DEBUG_OBJECT (qvais, "waiting for vpp drain done");
      g_mutex_lock (&qvais->drain_lock);
      while (!qvais->eos) {
        wait_until = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
        if (!g_cond_wait_until (&qvais->drain_cond, &qvais->drain_lock,
                wait_until)) {
          if (!qvais->eos)
            qvais->eos = TRUE;
        }
      }
      g_mutex_unlock (&qvais->drain_lock);
      GST_DEBUG_OBJECT (qvais, "waited for vpp drain done");
      break;
    case GST_EVENT_TAG:
      break;
    case GST_EVENT_CAPS:
      GST_DEBUG_OBJECT (qvais, "received caps event");
      gst_event_parse_caps (event, &caps);
      /* clear any pending reconfigure flag */
      gst_pad_check_reconfigure (qvais->srcpad);
      ret = gst_qvais_setcaps (qvais, qvais->sinkpad, caps);
      if (!ret)
        gst_pad_mark_reconfigure (qvais->srcpad);

      forward = FALSE;
      break;
    case GST_EVENT_SEGMENT:
      gst_event_copy_segment (event, &qvais->segment);

      GST_DEBUG_OBJECT (qvais, "received SEGMENT %" GST_SEGMENT_FORMAT,
          &qvais->segment);
      break;

    default:
      break;
  }

  if (ret && forward) {
    ret = gst_pad_push_event (qvais->srcpad, event);
  } else {
    gst_event_unref (event);
  }

  return ret;
}

/* NOTE: comp->messages_lock will be used */
static void
gst_qvais_send_message (GstQvais * self, GstQvaisMessage * msg)
{
  g_mutex_lock (&self->messages_lock);
  if (msg)
    g_queue_push_tail (&self->messages, msg);
  g_mutex_unlock (&self->messages_lock);
}

/* NOTE: comp->messages_lock will be used */
static void
gst_qvais_flush_messages (GstQvais * self)
{
  GstQvaisMessage *msg;

  g_mutex_lock (&self->messages_lock);
  while (!g_queue_is_empty (&self->messages)
      && (msg = g_queue_pop_head (&self->messages))) {
    g_slice_free (GstQvaisMessage, msg);
  }
  g_mutex_unlock (&self->messages_lock);
}

static gpointer
gst_qvais_message_handler (gpointer user_data)
{
  GstQvais *self = (GstQvais *) user_data;
  GstQvaisMessage *msg;

  while (self->vpp_ctx) {
    if (!g_queue_is_empty (&self->messages)) {
      g_mutex_lock (&self->messages_lock);
      while ((msg = g_queue_pop_head (&self->messages))) {
        g_mutex_unlock (&self->messages_lock);
        switch (msg->type) {
          case GST_QVAIS_MESSAGE_INPUT_BUF_DONE:
          {
            GstBuffer *input_buf = (GstBuffer *) msg->content.buf.pvGralloc;
            if (input_buf) {
              GST_DEBUG_OBJECT (self,
                  "handle message: input buf done, gst buf %p, fd %d",
                  input_buf, msg->content.buf.pixel.fd);
              gst_buffer_unref (input_buf);
            }
            break;
          }
          case GST_QVAIS_MESSAGE_OUTPUT_BUF_DONE:
          {
            qvais_handle_output_buf_done (self, &(msg->content.buf));
            break;
          }
          case GST_QVAIS_MESSAGE_FLUSH_DONE:
          {
            enum vpp_port port = msg->content.event.flush_done.port;

            GST_DEBUG_OBJECT (self, "handle message: flush done, port %d",
                port);

            if (port == VPP_PORT_INPUT) {
              self->input_flushing = FALSE;
            } else if (port == VPP_PORT_OUTPUT) {
              self->output_flushing = FALSE;
            }

            if (!self->input_flushing && !self->output_flushing) {
              g_mutex_lock (&self->flush_lock);
              g_cond_signal (&self->flush_cond);
              GST_DEBUG_OBJECT (self, "signal flush done");
              g_mutex_unlock (&self->flush_lock);
            }

            break;
          }
          case GST_QVAIS_MESSAGE_DRAIN_DONE:
          {
            g_mutex_lock (&self->drain_lock);
            self->eos = true;
            g_cond_signal (&self->drain_cond);
            GST_DEBUG_OBJECT (self,
                "signal drain done, it means end-of-stream");
            g_mutex_unlock (&self->drain_lock);
            break;
          }
          default:
            break;
        }
        g_slice_free (GstQvaisMessage, msg);
        g_mutex_lock (&self->messages_lock);
      }
      g_mutex_unlock (&self->messages_lock);
    } else {
      g_usleep (QVAIS_DEFALUT_SLEEP_US);
    }
  }
  gst_qvais_flush_messages (self);
  g_thread_exit (NULL);
  return NULL;
}

static void
queue_output_buf_task (gpointer user_data)
{
  GstQvais *self = (GstQvais *) user_data;
  GstBuffer *buffer = NULL;
  GstFlowReturn flow;
  struct vpp_buffer vpp_out_buf;

  if (self->active && self->pool) {
    /* Once drain done event received from driver, all processed buffers are returned
     * from driver. Stop queuing more output buffers since driver does not need them. */
    flow = gst_buffer_pool_acquire_buffer (self->pool, &buffer, NULL);

    if (flow == GST_FLOW_FLUSHING) {
      GST_DEBUG_OBJECT (self, "in flushing, this thread will be paused");
      goto quit;
    } else if (flow != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "couldn't acquire output buffer, flow %s",
          gst_flow_get_name (flow));
      goto quit;
    }
    memset (&vpp_out_buf, 0, sizeof (struct vpp_buffer));
    if (fill_vppbuf_with_gstbuf (&vpp_out_buf, buffer, true,
            self->out_vpp_buf_size)) {
      if (qvaisvpp_queue_buf (self->vpp_ctx, VPP_PORT_OUTPUT, &vpp_out_buf)) {
      } else {
        GST_ERROR_OBJECT (self, "queue vpp output buffer failed");
      }
    } else {
      GST_ERROR_OBJECT (self, "fill vpp output buf failed");
    }
  }

quit:
  return;
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_qvais_init (GstQvais * self)
{
  GST_INFO ("init qvais %p", self);

  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_qvais_sink_event));
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_qvais_chain));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
  gst_video_info_init (&self->in_info);
  gst_video_info_init (&self->out_info);
  self->vpp_ctx = NULL;
  self->pool = NULL;
  self->in_dmabuf = FALSE;
  self->out_dmabuf = FALSE;
  self->in_ubwc = FALSE;
  self->out_ubwc = FALSE;
  self->input_flushing = FALSE;
  self->output_flushing = FALSE;
  self->msg_thread = NULL;
  self->out_vpp_buf_size = 0;
  self->in_vpp_buf_size = 0;
  self->active = FALSE;
  self->passthrough = FALSE;
  self->sink_caps = NULL;
  self->src_caps = NULL;
  self->eos = FALSE;

  self->scale_ratio = 0;
  self->classification = DEFAULT_CLASSIFICATION;
  self->frame_number = 0;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
qvais_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_qvais_debug, "qvais", 0, "qvais debug category");

  return gst_element_register (plugin, "qvais",
      GST_RANK_SECONDARY, GST_TYPE_QVAIS);
}

/* gstreamer looks for this structure to register qvais */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qvais,
    "QTI video AIS(AI based upscaling)",
    qvais_init, PACKAGE_VERSION, GST_LICENSE_UNKNOWN, PACKAGE_NAME, "-")
