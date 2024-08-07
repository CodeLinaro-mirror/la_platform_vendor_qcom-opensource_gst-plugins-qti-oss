// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/**
 * SECTION:element-qvrate
 * @title: qvrate
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! qvrate ! fakesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gbm.h>
#include <gbm_priv.h>

#include "gstqvrate.h"
#include "gstqvratepool.h"

#include <gst/gsttask.h>
#include <gst/video/video.h>
#include <gst/allocators/gstdmabuf.h>
#include <vidc/media/msm_media_info.h>

GST_DEBUG_CATEGORY (gst_qvrate_debug);
#define GST_CAT_DEFAULT gst_qvrate_debug
#define QVRATE_DEFALUT_SLEEP_US 5000
#define QVRATE_DEFAULT_MIN_OUTPUT_BUF_COUNT 6
#define QVRATE_DEFAULT_EXT_OUTPUT_BUF_COUNT 2
#define QVRATE_DEFAULT_MIN_WIDTH 384
#define QVRATE_DEFAULT_MIN_HEIGHT 128
#define QVRATE_DEFAULT_MAX_WIDTH 1920
#define QVRATE_DEFAULT_MAX_HEIGHT 1088
#define QVRATE_DEFAULT_OP_FPS 60
#define QVRATE_THRESHOLD_FOR_FALLBACK 40

#define INVALID_COOKIE 0

static GstElementClass *parent_class = NULL;

G_DEFINE_TYPE (GstQvrate, gst_qvrate, GST_TYPE_ELEMENT);
static void
gst_qvrate_send_message (GstQvrate * self, GstQvrateMessage * msg);
static gpointer
gst_qvrate_message_handler (gpointer user_data);
static void
gst_qvrate_flush_messages (GstQvrate * self);
static void
_queue_output_buf_task (gpointer user_data);

static GstCaps *
  gst_qvrate_transform_caps (GstQvrate * qvrate,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static void
  gst_qvrate_class_init (GstQvrateClass * klass);
static void
  gst_qvrate_init (GstQvrate * self);

static void
  _input_buffer_done(void *pv, struct vpp_buffer *buf);
static void
  _output_buffer_done(void *pv, struct vpp_buffer *buf);
static void
  _vpp_event(void *pv, struct vpp_event e);

gboolean
  qvrate_fill_vppbuf_with_gstbuf (struct vpp_buffer* vpp_buf, GstBuffer* gst_buf, gboolean outport, guint32 buf_size);

#define SINK_FORMATS "{" \
    "NV12, "  /*  8-bit 4:2:0 */ \
    "}"

#define SRC_FORMATS "{" \
    "NV12, "  /*  8-bit 4:2:0 */ \
    "}"

#define QVRATE_COMPRESSION_CAPS_DMABUF(formats) \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES \
    (GST_CAPS_FEATURE_MEMORY_DMABUF, formats) \
    ",compression={linear,ubwc}"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (QVRATE_COMPRESSION_CAPS_DMABUF (SINK_FORMATS))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (QVRATE_COMPRESSION_CAPS_DMABUF (SRC_FORMATS))
    );

/* GObject vmethod implementations */
static GstCaps *
gst_qvrate_transform_caps (GstQvrate * qvrate,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstPad *otherpad;
  GstCaps *result;

  if (GST_PAD_SRC == direction)
    otherpad = qvrate->sinkpad;
  else
    otherpad = qvrate->srcpad;

  result = gst_pad_get_pad_template_caps (otherpad);

  if (filter) {
    GstCaps *temp;
    temp = gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = temp;
  }

  GST_DEBUG_OBJECT (qvrate, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

/* given fixed @caps, fixate @othercaps,
 *
 * qvrate's in/out width/height/format must be same, pass through if
 * in/out caps are all same.
 */
static GstCaps *
gst_qvrate_fixate_caps (GstQvrate * qvrate,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *result;
  GstPad *pad = (GST_PAD_SINK == direction) ? qvrate->sinkpad : qvrate->srcpad;

  GST_DEBUG_OBJECT (pad, "fixate othercaps %" GST_PTR_FORMAT, othercaps);
  GST_DEBUG_OBJECT (pad, "   based on caps %" GST_PTR_FORMAT, caps);

  /* caps must be fixed here, it's an error if it's not */
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);

  result = gst_caps_intersect (othercaps, caps);
  if (gst_caps_is_empty (result)) {
    gst_caps_unref (result);
    result = othercaps;
    GST_DEBUG_OBJECT (pad, "intersection is empty");
  } else {
    gst_caps_unref (othercaps);
  }

  GST_DEBUG_OBJECT (pad, "result %" GST_PTR_FORMAT, result);

  if (!gst_caps_is_fixed (result)) {
    /* copy width/height/format from caps to fixate othercaps */
    GstStructure *s0 = gst_caps_get_structure (caps, 0);
    const GValue *val;

    result = gst_caps_make_writable (result);

    val = gst_structure_get_value (s0, "width");
    if (val)
      gst_caps_set_value (result, "width", val);

    val = gst_structure_get_value (s0, "height");
    if (val)
      gst_caps_set_value (result, "height", val);

    val = gst_structure_get_value (s0, "format");
    if (val)
      gst_caps_set_value (result, "format", val);

    /* fixate remaining fields */
    result = gst_caps_fixate (result);
    GST_DEBUG_OBJECT (pad, "result %" GST_PTR_FORMAT, result);
  }

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result)) {
      GST_DEBUG_OBJECT (pad, "caps is subset of result");
      gst_caps_replace (&result, caps);
    }
  }

  GST_DEBUG_OBJECT (pad, "return %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
_caps_has_compression_ubwc (const GstCaps * caps)
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
_calc_valid_size (const GstVideoInfo * info, gboolean ubwc)
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
        int y_stride = (int) VENUS_Y_STRIDE(vformat, width);
        int uv_stride = (int) VENUS_UV_STRIDE(vformat, width);
        int y_sclines = (int) VENUS_Y_SCANLINES(vformat, height);
        int uv_sclines = (int) VENUS_UV_SCANLINES(vformat, height);
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
_calc_gbm_buf_size (const GstVideoInfo * info, gboolean ubwc)
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
      GST_ERROR ("NOT support format %s", GST_VIDEO_INFO_NAME (info));
      break;
  }

  return size;
}

static gboolean
gst_qvrate_set_info (GstQvrate * qvrate,
    GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstQvrate *self = GST_QVRATE (qvrate);
  const GstCapsFeatures *features;
  gboolean ret = TRUE;
  struct hqv_control ctrl;
  enum vpp_port in_port = VPP_PORT_INPUT;
  enum vpp_port out_port = VPP_PORT_OUTPUT;
  struct vpp_port_param in_param;
  struct vpp_port_param out_param;

  GST_INFO_OBJECT (self, " in_info=%p,  incaps: %" GST_PTR_FORMAT,
      in_info, incaps);
  GST_INFO_OBJECT (self, "out_info=%p, outcaps: %" GST_PTR_FORMAT,
      out_info, outcaps);

  /* when 1st frame comes, align in info by video meta
   * and out info by buffer pool */
  self->in_info = *in_info;
  self->out_info = *out_info;

  self->in_ubwc = _caps_has_compression_ubwc (incaps);
  self->out_ubwc = _caps_has_compression_ubwc (outcaps);
  GST_INFO_OBJECT (self, "in_ubwc=%u, out_ubwc=%u",
      self->in_ubwc, self->out_ubwc);
  /* Set valid size for _decide_allocation() to create output buffer pool
   * and allocate gstbuffer with the valid size for filesink to dump. */
  GST_VIDEO_INFO_SIZE (&self->out_info) =
      _calc_valid_size (out_info, self->out_ubwc);
  self->vpp_buf_size = _calc_gbm_buf_size(out_info, self->out_ubwc);
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
  ctrl.ctrl_type = HQV_CONTROL_FRC;
  ctrl.frc.num_segments = 1;
  ctrl.frc.segments = (struct vpp_ctrl_frc_segment *)
                        g_new0 (struct vpp_ctrl_frc_segment, ctrl.frc.num_segments);

  ctrl.frc.segments[0].mode = HQV_FRC_MODE_VIDEO_CUSTOM;
  ctrl.frc.segments[0].level = HQV_FRC_LEVEL_HIGH;
  ctrl.frc.segments[0].interp = HQV_FRC_INTERP_CUSTOM;
  ctrl.frc.segments[0].ts_start = 0;
  ctrl.frc.segments->frame_copy_on_fallback = 1;
  ctrl.frc.segments->frame_copy_input = 1;
  ctrl.frc.segments->smart_fallback = QVRATE_THRESHOLD_FOR_FALLBACK;
  ctrl.frc.segments->custom_interp.input_rate = 0; // Use incoming FPS
  ctrl.frc.segments->custom_interp.output_rate = QVRATE_DEFAULT_OP_FPS;

  GST_DEBUG_OBJECT (self, "in_ubwc=%u, out_ubwc=%u",
        self->in_ubwc, self->out_ubwc);
  if (qvratevpp_set_ctrl(self->vpp_ctx, ctrl)) {
    in_param.height = in_info->height;
    in_param.width = in_info->width;
    if (self->in_ubwc) {
      in_param.fmt = VPP_COLOR_FORMAT_UBWC_NV12;
      in_param.stride = VENUS_Y_STRIDE(COLOR_FMT_NV12_UBWC, in_param.width);
      in_param.scanlines = VENUS_Y_SCANLINES(COLOR_FMT_NV12_UBWC, in_param.height);
    } else {
      in_param.fmt = VPP_COLOR_FORMAT_NV12_VENUS;
      in_param.stride = VENUS_Y_STRIDE(COLOR_FMT_NV12, in_param.width);
      in_param.scanlines = VENUS_Y_SCANLINES(COLOR_FMT_NV12, in_param.height);
    }

    out_param.height = out_info->height;
    out_param.width = out_info->width;
    if (self->out_ubwc) {
      out_param.fmt = VPP_COLOR_FORMAT_UBWC_NV12;
      out_param.stride = VENUS_Y_STRIDE(COLOR_FMT_NV12_UBWC, out_param.width);
      out_param.scanlines = VENUS_Y_SCANLINES(COLOR_FMT_NV12_UBWC, out_param.height);
    } else {
      out_param.fmt = VPP_COLOR_FORMAT_NV12_VENUS;
      out_param.stride = VENUS_Y_STRIDE(COLOR_FMT_NV12, out_param.width);
      out_param.scanlines = VENUS_Y_SCANLINES(COLOR_FMT_NV12, out_param.height);
    }

    if (qvratevpp_set_parameter(self->vpp_ctx, in_port, in_param) &&
    qvratevpp_set_parameter(self->vpp_ctx, out_port, out_param))
      ret = TRUE;
  } else {
    ret = FALSE;
  }
  g_free (ctrl.frc.segments);

  return ret;
}

static gboolean
gst_qvrate_decide_allocation (GstQvrate * qvrate, GstQuery * query)
{
  GstQvrate *self = GST_QVRATE (qvrate);
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
  if (qvratevpp_get_buf_requirements(self->vpp_ctx, &req)) {
    self->in_req_cnt = req.buf_req[VPP_RESOLUTION_HD].in_req;
    self->out_req_cnt = req.buf_req[VPP_RESOLUTION_HD].out_req;
  } else {
    self->out_req_cnt = QVRATE_DEFAULT_MIN_OUTPUT_BUF_COUNT;
  }

  min = self->out_req_cnt;
  max = min + QVRATE_DEFAULT_EXT_OUTPUT_BUF_COUNT;

  GST_INFO_OBJECT (self, "size %u, min %u, max %u", size, min, max);

  if (pool)
    gst_object_unref (pool);

  /* always use its own pool at this time */
  pool = gst_qvrate_pool_new (self->out_ubwc);
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

  GST_DEBUG_OBJECT (self, "qvrate pool %p, allocator %p", pool, allocator);

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
    gst_object_unref(self->pool);
  self->pool = pool;

  return TRUE;
}

static gboolean
gst_qvrate_start (GstQvrate * self)
{
  if (!self->active) {
    if (!qvratevpp_open (self->vpp_ctx)) {
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
gst_qvrate_open (GstQvrate * self)
{
  GST_INFO_OBJECT (self, "qvrate open");

  gboolean ret = TRUE;

  self->cb.pv = self;
  self->cb.input_buffer_done = _input_buffer_done;
  self->cb.output_buffer_done = _output_buffer_done;
  self->cb.vpp_event = _vpp_event;
  self->vpp_ctx = qvratevpp_init (0, self->cb);

  if (self->vpp_ctx) {
    self->msg_thread = g_thread_new ("qvrate-message", (GThreadFunc) gst_qvrate_message_handler, self);
    self->outbuf_task = gst_task_new (((GstTaskFunction) _queue_output_buf_task), self, NULL);

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
      GST_ERROR_OBJECT (self, "failed to create message thread %p or output task %p",
          self->msg_thread, self->outbuf_task);

      /* release resource if error occurs */
      qvratevpp_term (self->vpp_ctx);
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
gst_qvrate_close (GstQvrate * self)
{
  GST_INFO_OBJECT (self, "qvrate close");

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
    qvratevpp_flush(self->vpp_ctx, VPP_PORT_INPUT);
    qvratevpp_flush(self->vpp_ctx, VPP_PORT_OUTPUT);

    g_mutex_lock(&self->flush_lock);
    while (self->input_flushing || self->output_flushing) {
      GST_DEBUG_OBJECT(self, "begin wait flush");
      gint64 wait_until = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
      if (!g_cond_wait_until (&self->flush_cond, &self->flush_lock, wait_until)) {
        GST_DEBUG_OBJECT(self, "end wait flush");
        if (self->input_flushing)
          self->input_flushing = FALSE;
        if (self->output_flushing)
          self->output_flushing = FALSE;
      }
    }
    g_mutex_unlock(&self->flush_lock);

    if (self->vpp_ctx != NULL) {
      qvratevpp_close (self->vpp_ctx);
    }
    self->active = FALSE;
  }

  if (self->vpp_ctx != NULL) {
    qvratevpp_term (self->vpp_ctx);
    self->vpp_ctx = NULL;
  }

  if (self->pool) {
    gst_buffer_pool_set_active (self->pool, FALSE);
    GST_DEBUG_OBJECT (self, "set pool %p active FALSE, ref cnt: %d",
        self->pool, GST_OBJECT_REFCOUNT (self->pool));
    gst_object_unref (self->pool);
    self->pool = NULL;
  }

  gst_qvrate_flush_messages(self);
  g_mutex_clear (&self->messages_lock);
  g_cond_clear (&self->flush_cond);
  g_mutex_clear (&self->flush_lock);
  g_cond_clear (&self->drain_cond);
  g_mutex_clear (&self->drain_lock);
  g_thread_join (self->msg_thread);
}

gboolean qvrate_fill_vppbuf_with_gstbuf (struct vpp_buffer* vpp_buf, GstBuffer* gst_buf, gboolean outport, guint32 buf_size)
{
  gboolean ret = FALSE;
  GstMemory *memory = NULL;

  if (gst_buffer_n_memory (gst_buf)) {
    memory = gst_buffer_get_memory (gst_buf, 0);
  }
  GST_DEBUG("gst_buf %p, vpp_buf %p, memory %p, outport %d, buf_size %d", gst_buf, vpp_buf, memory, outport, buf_size);
  if (gst_buf && vpp_buf && memory)
  {
    vpp_buf->pixel.fd = gst_fd_memory_get_fd(memory);
    vpp_buf->pixel.alloc_len = buf_size;
    vpp_buf->pixel.filled_len = buf_size;
    vpp_buf->pixel.valid_data_len = buf_size;
    vpp_buf->pvGralloc = gst_buf;
    vpp_buf->extradata.fd = -1;
    if (!outport) {
      vpp_buf->timestamp = GST_BUFFER_PTS(gst_buf)/1000;
      GST_DEBUG("input buf timestamp %ld, buf size %d, fd %d", vpp_buf->timestamp, buf_size, vpp_buf->pixel.fd);
    }

    ret = TRUE;
  }

  return ret;
}

static void
gst_qvrate_finalize (GObject * obj)
{
  GstQvrate *self = GST_QVRATE (obj);

  GST_INFO_OBJECT (self, "finalize qvrate %p", self);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GstStateChangeReturn
gst_qvrate_change_state (GstElement * element, GstStateChange transition)
{
  GstQvrate *qvrate;
  GstStateChangeReturn ret;

  qvrate = GST_QVRATE (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* open device/library if needed */
      if (!gst_qvrate_open (qvrate))
        goto open_failed;
      break;

    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* close device/library if needed */
      gst_qvrate_close(qvrate);
      break;
    default:
      break;
  }

  return ret;

  /* Errors */
open_failed:
  {
    GST_ELEMENT_ERROR (qvrate, LIBRARY, INIT, (NULL),
        ("Failed to open qvrate"));
    return GST_STATE_CHANGE_FAILURE;
  }
}

static GstFlowReturn
gst_qvrate_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstQvrate *qvrate;
  GstFlowReturn ret = GST_FLOW_ERROR;
  enum vpp_port in_port = VPP_PORT_INPUT;
  struct vpp_buffer vpp_in_buf;

  qvrate = GST_QVRATE (parent);
  if (qvrate->passthrough) {
    gst_pad_push (qvrate->srcpad, buf);
    ret = GST_FLOW_OK;
    goto done;
  }

  memset(&vpp_in_buf, 0, sizeof (struct vpp_buffer));
  if (qvrate_fill_vppbuf_with_gstbuf (&vpp_in_buf, buf, false, qvrate->vpp_buf_size)) {
    qvrate->frame_number = (qvrate->frame_number + 1 == 0) ? 1 : qvrate->frame_number + 1;
    vpp_in_buf.cookie_in_to_out = (void*)(qvrate->frame_number);

    if (qvratevpp_queue_buf(qvrate->vpp_ctx, in_port, &vpp_in_buf)) {
      GST_DEBUG_OBJECT (qvrate, "queue vpp input buf %p successfully", buf);
      ret = GST_FLOW_OK;
    } else {
      GST_ERROR_OBJECT (qvrate, "queue vpp input buf failed");
      gst_buffer_unref (buf);
      ret = GST_FLOW_ERROR;
    }
  } else {
    GST_ERROR_OBJECT (qvrate, "fill vpp input buf failed");
    gst_buffer_unref (buf);
    ret = GST_FLOW_ERROR;
  }

done:
  return ret;
}

/* initialize the qvrate's class */
static void
gst_qvrate_class_init (GstQvrateClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_qvrate_finalize);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_qvrate_change_state);
  gst_element_class_set_static_metadata (gstelement_class,
      "QTI Video frame rate convert",
      "Qvrate/Video",
      "Video frame rate convert from low to 60fps", "QTI");
}

static void _input_buffer_done(void *pv, struct vpp_buffer *buf)
{
  GstQvrate * self = (GstQvrate *)pv;
  GstQvrateMessage *msg = g_slice_new (GstQvrateMessage);

  GST_DEBUG_OBJECT (self, "input buffer done, gst buf %p", buf->pvGralloc);

  msg->type = GST_QVRATE_MESSAGE_INPUT_BUF_DONE;
  memcpy (&(msg->content.qvrate_vpp_buf.buf), buf, sizeof (struct vpp_buffer));
  gst_qvrate_send_message(self, msg);
}

static void _output_buffer_done(void *pv, struct vpp_buffer *buf)
{
  GstQvrate * self = (GstQvrate *)pv;
  GstQvrateMessage *msg = g_slice_new (GstQvrateMessage);

  GST_DEBUG_OBJECT (self, "output buffer done, gst buf %p", buf->pvGralloc);

  if (self->vpp_ctx) {
    msg->type = GST_QVRATE_MESSAGE_OUTPUT_BUF_DONE;
    memcpy (&(msg->content.qvrate_vpp_buf.buf), buf, sizeof (struct vpp_buffer));
    gst_qvrate_send_message(self, msg);
  }
}

void qvrate_handle_output_buf_done (GstQvrate * self, struct vpp_buffer *buf)
{
  GstPad *srcpad = NULL;
  GstBuffer *gst_buf = NULL;

  if (self->vpp_ctx) {
    gst_buf = (GstBuffer *)(buf->pvGralloc);
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
    GST_DEBUG_OBJECT (self, "handle message: output buffer done, gst buf %p, buf->timestamp %ld, PTS %" GST_TIME_FORMAT,
      gst_buf, buf->timestamp, GST_TIME_ARGS(GST_BUFFER_PTS (gst_buf)));
    if (self->output_flushing || !self->active || self->eos) {
      gst_buffer_unref (gst_buf);
    } else {
      srcpad = gst_element_get_static_pad ((GstElement*) self, "src");

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

static void _vpp_event(void *pv, struct vpp_event e)
{
  GstQvrate * self = (GstQvrate *)pv;
  GstQvrateMessage *msg = g_slice_new (GstQvrateMessage);

  switch (e.type) {
    case VPP_EVENT_FLUSH_DONE:
      GST_DEBUG("flush done port:%d", e.flush_done.port);
      if (e.flush_done.port == VPP_PORT_INPUT){
        GST_DEBUG("input port flush done");
      } else if (e.flush_done.port == VPP_PORT_OUTPUT) {
        GST_DEBUG("output port flush done");
      }
      msg->type = GST_QVRATE_MESSAGE_FLUSH_DONE;
      memcpy (&(msg->content.qvrate_vpp_event.event), &e, sizeof (struct vpp_event));
      gst_qvrate_send_message(self, msg);
      break;
    case VPP_EVENT_ERROR:
      GST_ERROR("receive VPP_EVENT_ERROR");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("VPP driver posts an error"), (NULL));
      break;
    case VPP_EVENT_DRAIN_DONE:
      GST_DEBUG ("receive drain done");
      msg->type = GST_QVRATE_MESSAGE_DRAIN_DONE;
      gst_buffer_pool_set_flushing (self->pool, TRUE);
      if (self->outbuf_task) {
        gst_task_stop (self->outbuf_task);
        gst_task_join (self->outbuf_task);
        g_object_unref (self->outbuf_task);
        self->outbuf_task = NULL;
      }
      gst_qvrate_send_message (self, msg);
      break;
    default:
      break;
  }
}

static GstCaps *
gst_qvrate_find_caps (GstQvrate * qvrate, GstPad * pad,
    GstCaps * caps)
{
  GstPad *otherpad, *otherpeer;
  GstCaps *othercaps;
  gboolean is_fixed;

  /* caps must be fixed here, this is a programming error if it's not */
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);

  otherpad = (pad == qvrate->srcpad) ? qvrate->sinkpad : qvrate->srcpad;
  otherpeer = gst_pad_get_peer (otherpad);

  othercaps = gst_qvrate_transform_caps (qvrate,
      GST_PAD_DIRECTION (pad), caps, NULL);

  if (othercaps && !gst_caps_is_empty (othercaps)) {
    GstCaps *intersect, *templ_caps;

    templ_caps = gst_pad_get_pad_template_caps (otherpad);
    GST_DEBUG_OBJECT (qvrate,
        "intersecting against padtemplate %" GST_PTR_FORMAT, templ_caps);

    intersect =
        gst_caps_intersect_full (othercaps, templ_caps,
        GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (othercaps);
    gst_caps_unref (templ_caps);
    othercaps = intersect;
  }

  /* check if transform is empty */
  if (!othercaps || gst_caps_is_empty (othercaps))
    goto no_transform;

  /* if the othercaps are not fixed, we need to fixate them, first attempt
   * is by attempting passthrough if the othercaps are a superset of caps. */
  /* FIXME. maybe the caps is not fixed because it has multiple structures of
   * fixed caps */
  is_fixed = gst_caps_is_fixed (othercaps);
  if (!is_fixed) {
    GST_DEBUG_OBJECT (qvrate,
        "transform returned non fixed  %" GST_PTR_FORMAT, othercaps);

    /* Now let's see what the peer suggests based on our transformed caps */
    if (otherpeer) {
      GstCaps *peercaps, *intersection, *templ_caps;

      GST_DEBUG_OBJECT (qvrate,
          "Checking peer caps with filter %" GST_PTR_FORMAT, othercaps);

      peercaps = gst_pad_query_caps (otherpeer, othercaps);
      GST_DEBUG_OBJECT (qvrate, "Resulted in %" GST_PTR_FORMAT, peercaps);
      if (!gst_caps_is_empty (peercaps)) {
        templ_caps = gst_pad_get_pad_template_caps (otherpad);

        GST_DEBUG_OBJECT (qvrate,
            "Intersecting with template caps %" GST_PTR_FORMAT, templ_caps);

        intersection =
            gst_caps_intersect_full (peercaps, templ_caps,
            GST_CAPS_INTERSECT_FIRST);
        GST_DEBUG_OBJECT (qvrate, "Intersection: %" GST_PTR_FORMAT,
            intersection);
        gst_caps_unref (peercaps);
        gst_caps_unref (templ_caps);
        peercaps = intersection;

        GST_DEBUG_OBJECT (qvrate,
            "Intersecting with transformed caps %" GST_PTR_FORMAT, othercaps);
        intersection =
            gst_caps_intersect_full (peercaps, othercaps,
            GST_CAPS_INTERSECT_FIRST);
        GST_DEBUG_OBJECT (qvrate, "Intersection: %" GST_PTR_FORMAT,
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
      GST_DEBUG_OBJECT (qvrate, "no peer, doing passthrough");
      gst_caps_unref (othercaps);
      othercaps = gst_caps_ref (caps);
      is_fixed = TRUE;
    }
  }
  if (gst_caps_is_empty (othercaps))
    goto no_transform_possible;

  GST_DEBUG ("have %sfixed caps %" GST_PTR_FORMAT, (is_fixed ? "" : "non-"),
      othercaps);

  /* second attempt at fixation, call the fixate vmethod */
  /* caps could be fixed but the subclass may want to add fields */

  GST_DEBUG_OBJECT (qvrate, "calling fixate_caps for %" GST_PTR_FORMAT
      " using caps %" GST_PTR_FORMAT " on pad %s:%s", othercaps, caps,
      GST_DEBUG_PAD_NAME (otherpad));
  /* note that we pass the complete array of structures to the fixate
   * function, it needs to truncate itself */
  othercaps =
      gst_qvrate_fixate_caps (qvrate, GST_PAD_DIRECTION (pad), caps, othercaps);

  if (!othercaps) {
    g_critical ("qvrate: second attempt to fixate caps returned "
        "invalid (NULL) caps on pad %s:%s", GST_DEBUG_PAD_NAME (pad));
  }
  is_fixed = othercaps && gst_caps_is_fixed (othercaps);
  GST_DEBUG_OBJECT (qvrate, "after fixating %" GST_PTR_FORMAT, othercaps);

  /* caps should be fixed now, if not we have to fail. */
  if (!is_fixed)
    goto could_not_fixate;

  /* and peer should accept */
  if (otherpeer && !gst_pad_query_accept_caps (otherpeer, othercaps))
    goto peer_no_accept;

  GST_DEBUG_OBJECT (qvrate, "Input caps were %" GST_PTR_FORMAT
      ", and got final caps %" GST_PTR_FORMAT, caps, othercaps);

  if (otherpeer)
    gst_object_unref (otherpeer);

  return othercaps;

  /* ERRORS */
no_transform:
  {
    GST_DEBUG_OBJECT (qvrate,
        "qvrate returned useless  %" GST_PTR_FORMAT, othercaps);
    goto error_cleanup;
  }
no_transform_possible:
  {
    GST_DEBUG_OBJECT (qvrate,
        "qvrate could not transform %" GST_PTR_FORMAT
        " in anything we support", caps);
    goto error_cleanup;
  }
could_not_fixate:
  {
    GST_DEBUG_OBJECT (qvrate, "FAILED to fixate %" GST_PTR_FORMAT, othercaps);
    goto error_cleanup;
  }
peer_no_accept:
  {
    GST_DEBUG_OBJECT (qvrate, "FAILED to get peer of %" GST_PTR_FORMAT
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

static gboolean
gst_qvrate_setcaps (GstQvrate * qvrate, GstPad * pad,
    GstCaps * incaps)
{
  GstCaps *outcaps, *prev_incaps = NULL, *prev_outcaps = NULL;
  gboolean ret = TRUE;
  GstQuery *query;
  GstVideoInfo in_info, out_info;

  GST_DEBUG_OBJECT (pad, "have new caps %p %" GST_PTR_FORMAT, incaps, incaps);

  /* find best possible caps for the other pad */
  outcaps = gst_qvrate_find_caps (qvrate, pad, incaps);
  if (!outcaps || gst_caps_is_empty (outcaps))
    goto no_caps_possible;

  /* configure the element now */

  /* if we have the same caps, we can optimize and reuse the input caps */
  if (gst_caps_is_equal (incaps, outcaps)) {
    GST_INFO_OBJECT (qvrate, "reuse caps");
    gst_caps_unref (outcaps);
    outcaps = gst_caps_ref (incaps);
  }

  prev_incaps = qvrate->sink_caps;
  prev_outcaps = qvrate->src_caps;
  GST_DEBUG_OBJECT (qvrate,
    "prev in caps: %" GST_PTR_FORMAT ", prev out caps %" GST_PTR_FORMAT,
    prev_incaps, prev_outcaps);
  if (prev_incaps && prev_outcaps && gst_caps_is_equal (prev_incaps, incaps)
      && gst_caps_is_equal (prev_outcaps, outcaps)) {
    GST_DEBUG_OBJECT (qvrate,
        "New caps equal to old ones: %" GST_PTR_FORMAT " -> %" GST_PTR_FORMAT,
        incaps, outcaps);
    ret = TRUE;
  } else {
    /* need reconfigure here, currently not support for vpp does not support yet*/
    GST_DEBUG_OBJECT (qvrate, "todo: vpp reconfigure");
     if (!gst_qvrate_transform_caps (qvrate, GST_PAD_DIRECTION (pad), incaps, outcaps))
       goto failed_configure;

    if (!prev_outcaps || !gst_caps_is_equal (outcaps, prev_outcaps)) {
      /* let downstream know about our caps */
      ret = gst_pad_set_caps (qvrate->srcpad, outcaps);
      gst_caps_replace (&(qvrate->src_caps), outcaps);
    }
    if (!prev_incaps || !gst_caps_is_equal (incaps, prev_incaps))
      gst_caps_replace (&(qvrate->sink_caps), incaps);
  }

  /* input caps */
  if (!gst_video_info_from_caps (&in_info, incaps))
    goto invalid_caps;

  /* output caps */
  if (!gst_video_info_from_caps (&out_info, outcaps))
    goto invalid_caps;
  if (in_info.width < QVRATE_DEFAULT_MIN_WIDTH || in_info.width > QVRATE_DEFAULT_MAX_WIDTH ||
    in_info.height < QVRATE_DEFAULT_MIN_HEIGHT || in_info.height > QVRATE_DEFAULT_MAX_HEIGHT) {
    GST_DEBUG_OBJECT (qvrate, "passthrough width %d, height %d", in_info.width, in_info.height);
    qvrate->passthrough = TRUE;
    goto done;
  } else {
    qvrate->passthrough = FALSE;
  }

  if (!gst_qvrate_set_info(qvrate, incaps, &in_info, outcaps, &out_info))
    goto invalid_info;
  if (ret) {
    /* try to get a pool when needed */
    query = gst_query_new_allocation (outcaps, TRUE);
    ret = gst_qvrate_decide_allocation (qvrate, query);
  }
  if (ret) {
    ret = gst_qvrate_start (qvrate);
  }
done:
  if (outcaps)
    gst_caps_unref (outcaps);

  return ret;

  /* ERRORS */
no_caps_possible:
  {
    GST_WARNING_OBJECT (qvrate,
        "qvrate could not transform %" GST_PTR_FORMAT
        " in anything we support", incaps);
    ret = FALSE;
    goto done;
  }
failed_configure:
  {
    GST_WARNING_OBJECT (qvrate, "FAILED to configure incaps %" GST_PTR_FORMAT
        " and outcaps %" GST_PTR_FORMAT, incaps, outcaps);
    ret = FALSE;
    goto done;
  }
invalid_caps:
  {
    GST_WARNING_OBJECT (qvrate, "invalid caps %" GST_PTR_FORMAT
      " and outcaps %" GST_PTR_FORMAT, incaps, outcaps);
    if (qvrate->sink_caps)
      gst_caps_replace (&(qvrate->sink_caps), NULL);
    if (qvrate->src_caps)
      gst_caps_replace (&(qvrate->src_caps), NULL);
    ret = FALSE;
    goto done;
  }
invalid_info:
  {
    GST_WARNING_OBJECT (qvrate, "invalid caps %" GST_PTR_FORMAT
      " and outcaps %" GST_PTR_FORMAT, incaps, outcaps);
    ret = FALSE;
    goto done;
  }

}

static gboolean
gst_qvrate_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstQvrate *qvrate = GST_QVRATE(parent);
  gboolean ret = TRUE;
  gint64 wait_until = 0;
  gboolean forward = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (qvrate, "received flush start event");
      if (qvrate->passthrough)
        break;

      gst_task_pause (qvrate->outbuf_task);
      gst_buffer_pool_set_flushing (qvrate->pool, TRUE);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (qvrate, "received flush stop event");
      if (qvrate->passthrough)
        break;

      qvrate->input_flushing = TRUE;
      qvrate->output_flushing = TRUE;
      qvratevpp_flush(qvrate->vpp_ctx, VPP_PORT_INPUT);
      qvratevpp_flush(qvrate->vpp_ctx, VPP_PORT_OUTPUT);
      GST_DEBUG_OBJECT (qvrate, "begin waiting vpp flush done");
      g_mutex_lock(&qvrate->flush_lock);
      while (qvrate->input_flushing || qvrate->output_flushing) {
        wait_until = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
        if (!g_cond_wait_until (&qvrate->flush_cond, &qvrate->flush_lock, wait_until)) {
          if (qvrate->input_flushing)
            qvrate->input_flushing = FALSE;
          if (qvrate->output_flushing)
            qvrate->output_flushing = FALSE;
        }
      }
      g_mutex_unlock(&qvrate->flush_lock);
      GST_DEBUG_OBJECT (qvrate, "end waiting vpp flush done");

      gst_buffer_pool_set_flushing (qvrate->pool, FALSE);
      gst_task_resume (qvrate->outbuf_task);
      break;
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (qvrate, "received eos event, passthrough is %d", (int)qvrate->passthrough);
      if (qvrate->passthrough)
        break;

      qvratevpp_drain(qvrate->vpp_ctx);

      GST_DEBUG_OBJECT (qvrate, "begin waiting vpp drain done");
      g_mutex_lock(&qvrate->drain_lock);
      while (!qvrate->eos) {
        wait_until = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
        if (!g_cond_wait_until (&qvrate->drain_cond, &qvrate->drain_lock, wait_until)) {
          if (!qvrate->eos)
            qvrate->eos = TRUE;
        }
      }
      g_mutex_unlock(&qvrate->drain_lock);
      GST_DEBUG_OBJECT (qvrate, "end waiting vpp drain done");
      break;
    case GST_EVENT_TAG:
      break;
    case GST_EVENT_CAPS:
      GstCaps *caps;
      GST_DEBUG_OBJECT (qvrate, "received caps event");
      gst_event_parse_caps (event, &caps);
      /* clear any pending reconfigure flag */
      gst_pad_check_reconfigure (qvrate->srcpad);
      ret = gst_qvrate_setcaps (qvrate, qvrate->sinkpad, caps);
      if (!ret)
        gst_pad_mark_reconfigure (qvrate->srcpad);

      forward = FALSE;
      break;
    case GST_EVENT_SEGMENT:
      gst_event_copy_segment (event, &qvrate->segment);

      GST_DEBUG_OBJECT (qvrate, "received SEGMENT %" GST_SEGMENT_FORMAT,
         &qvrate->segment);
      break;

    default:
      break;
  }

  if (ret && forward)
    ret = gst_pad_push_event (qvrate->srcpad, event);
  else
    gst_event_unref (event);

  return ret;
}

/* NOTE: comp->messages_lock will be used */
static void
gst_qvrate_send_message (GstQvrate * self, GstQvrateMessage * msg)
{
  g_mutex_lock (&self->messages_lock);
  if (msg)
    g_queue_push_tail (&self->messages, msg);
  g_mutex_unlock (&self->messages_lock);
}

/* NOTE: comp->messages_lock will be used */
static void
gst_qvrate_flush_messages (GstQvrate * self)
{
  GstQvrateMessage *msg;

  g_mutex_lock (&self->messages_lock);
  while (!g_queue_is_empty (&self->messages) && (msg = g_queue_pop_head (&self->messages))) {
    g_slice_free (GstQvrateMessage, msg);
  }
  g_mutex_unlock (&self->messages_lock);
}

static gpointer
gst_qvrate_message_handler (gpointer user_data)
{
  GstQvrate * self = (GstQvrate *) user_data;
  GstQvrateMessage *msg;

  while (self->vpp_ctx) {
    if (!g_queue_is_empty (&self->messages)) {
      g_mutex_lock (&self->messages_lock);
      while ((msg = g_queue_pop_head (&self->messages))) {
        g_mutex_unlock (&self->messages_lock);
        switch (msg->type) {
          case GST_QVRATE_MESSAGE_INPUT_BUF_DONE:
            GstBuffer *input_buf = (GstBuffer*) msg->content.qvrate_vpp_buf.buf.pvGralloc;
            if (input_buf) {
              GST_DEBUG_OBJECT (self, "handle message: input buf done, gst buf %p, fd %d",
                  input_buf, msg->content.qvrate_vpp_buf.buf.pixel.fd);
              gst_buffer_unref (input_buf);
            }
            break;

          case GST_QVRATE_MESSAGE_OUTPUT_BUF_DONE:
            qvrate_handle_output_buf_done(self, &(msg->content.qvrate_vpp_buf.buf));
            break;

          case GST_QVRATE_MESSAGE_FLUSH_DONE:
            enum vpp_port port = msg->content.qvrate_vpp_event.event.flush_done.port;

            GST_DEBUG_OBJECT (self, "handle message: flush done, port %d", port);

            if (port == VPP_PORT_INPUT) {
              self->input_flushing = FALSE;
            } else if (port == VPP_PORT_OUTPUT) {
              self->output_flushing = FALSE;
            }

            if (!self->input_flushing && !self->output_flushing) {
              g_mutex_lock(&self->flush_lock);
              g_cond_signal (&self->flush_cond);
              GST_DEBUG_OBJECT (self, "signal flush done");
              g_mutex_unlock(&self->flush_lock);
            }

            break;

          case GST_QVRATE_MESSAGE_DRAIN_DONE:
              g_mutex_lock (&self->drain_lock);
              self->eos = true;
              g_cond_signal (&self->drain_cond);
              GST_DEBUG_OBJECT (self, "signal drain done, it means end-of-stream");
              g_mutex_unlock (&self->drain_lock);
            break;

          default:
            break;
        }
        g_slice_free (GstQvrateMessage, msg);
        g_mutex_lock (&self->messages_lock);
      }
      g_mutex_unlock (&self->messages_lock);
    } else {
      g_usleep(QVRATE_DEFALUT_SLEEP_US);
    }
  }
  gst_qvrate_flush_messages(self);
  g_thread_exit(NULL);
  return NULL;
}

static void
_queue_output_buf_task (gpointer user_data)
{
  GstQvrate *self = (GstQvrate *) user_data;
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
    if (qvrate_fill_vppbuf_with_gstbuf (&vpp_out_buf, buffer, true, self->vpp_buf_size)) {
      if (qvratevpp_queue_buf (self->vpp_ctx, VPP_PORT_OUTPUT, &vpp_out_buf)) {
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
gst_qvrate_init (GstQvrate * self)
{
  GST_INFO ("init qvrate %p", self);

  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_qvrate_sink_event));
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_qvrate_chain));
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
  self->vpp_buf_size = 0;
  self->active = FALSE;
  self->passthrough = FALSE;
  self->sink_caps = NULL;
  self->src_caps = NULL;
  self->eos = FALSE;
  self->frame_number = 0;

  GST_INFO_OBJECT (self, "init qvrate done");
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
qvrate_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_qvrate_debug, "qvrate", 0,
      "qvrate debug category");

  return gst_element_register (plugin, "qvrate",
      GST_RANK_SECONDARY, GST_TYPE_QVRATE);
}

/* gstreamer looks for this structure to register qvrate */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qvrate,
    "QTI video frame rate convert",
    qvrate_init,
    PACKAGE_VERSION "-" G_STRINGIFY(GST_VERSION_MAJOR) "/" G_STRINGIFY(GST_VERSION_MINOR) "/" G_STRINGIFY(GST_VERSION_MICRO), GST_LICENSE_UNKNOWN, PACKAGE_NAME, "-")
