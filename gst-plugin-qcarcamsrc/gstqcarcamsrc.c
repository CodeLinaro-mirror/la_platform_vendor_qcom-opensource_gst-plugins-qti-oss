// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <stdio.h>
#include "gstqcarcamsrc.h"
#include <gst/allocators/gstdmabuf.h>
#ifdef GST_USE_MMM_COLOR_FMT
#include <media/mmm_color_fmt.h>

enum color_fmts {
    COLOR_FMT_NV12 = MMM_COLOR_FMT_NV12,
    COLOR_FMT_NV21 = MMM_COLOR_FMT_NV21,
    COLOR_FMT_NV12_UBWC = MMM_COLOR_FMT_NV12_UBWC,
    COLOR_FMT_NV12_BPP10_UBWC = MMM_COLOR_FMT_NV12_BPP10_UBWC,
    COLOR_FMT_RGBA8888 = MMM_COLOR_FMT_RGBA8888,
    COLOR_FMT_RGBA8888_UBWC = MMM_COLOR_FMT_RGBA8888_UBWC,
    COLOR_FMT_RGBA1010102_UBWC = MMM_COLOR_FMT_RGBA1010102_UBWC,
    COLOR_FMT_RGB565_UBWC = MMM_COLOR_FMT_RGB565_UBWC,
    COLOR_FMT_P010_UBWC = MMM_COLOR_FMT_P010_UBWC,
    COLOR_FMT_P010 = MMM_COLOR_FMT_P010,
    COLOR_FMT_NV12_512 = MMM_COLOR_FMT_NV12_512,
};

#define VENUS_Y_STRIDE MMM_COLOR_FMT_Y_STRIDE
#define VENUS_UV_STRIDE MMM_COLOR_FMT_UV_STRIDE
#define VENUS_Y_SCANLINES MMM_COLOR_FMT_Y_SCANLINES
#define VENUS_UV_SCANLINES MMM_COLOR_FMT_UV_SCANLINES
#define VENUS_Y_META_STRIDE MMM_COLOR_FMT_Y_META_STRIDE
#define VENUS_UV_META_STRIDE MMM_COLOR_FMT_UV_META_STRIDE
#define VENUS_Y_META_SCANLINES MMM_COLOR_FMT_Y_META_SCANLINES
#define VENUS_UV_META_SCANLINES MMM_COLOR_FMT_UV_META_SCANLINES
#define VENUS_BUFFER_SIZE MMM_COLOR_FMT_BUFFER_SIZE
#define VENUS_BUFFER_SIZE_USED MMM_COLOR_FMT_BUFFER_SIZE_USED

#else
#include <media/msm_media_info.h>
#endif

GST_DEBUG_CATEGORY (gst_qcarcam_src_debug);
#define GST_CAT_DEFAULT (gst_qcarcam_src_debug)

#define QCARCAMSRC_MIN_BUFFERS 3
#define QCARCAMSRC_BUFFERS (QCARCAMSRC_MIN_BUFFERS + 3)
#define DEFAULT_PROP_INPUT 0
#define DEFALUT_WAIT_TIME 2 //2 seconds

enum
{
  PROP_0,
  PROP_INPUT,
};

#define SRC_FORMATS "{" \
    "NV12, "  /*  8-bit 4:2:0 */ \
    "}"

#define QCARCAM_SRC_COMPRESSION_CAPS_DMABUF(formats) \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES \
    (GST_CAPS_FEATURE_MEMORY_DMABUF, formats) \
    ",compression={linear,ubwc}"

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (QCARCAM_SRC_COMPRESSION_CAPS_DMABUF (SRC_FORMATS))
    );

#define gst_qcarcam_src_parent_class parent_class
G_DEFINE_TYPE (GstQcarcamSrc, gst_qcarcam_src, GST_TYPE_PUSH_SRC);

static void gst_qcarcam_src_finalize (GObject * gobject);

static void gst_qcarcam_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_qcarcam_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_qcarcam_src_start (GstBaseSrc * basesrc);
static gboolean gst_qcarcam_src_stop (GstBaseSrc * basesrc);

static GstStateChangeReturn
    gst_qcarcam_src_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_qcarcam_src_decide_allocation (GstBaseSrc * src,
    GstQuery * query);
static gboolean
gst_qcarcam_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean
gst_qcarcam_src_buffer_dispose (GstBuffer * qcarcamsrcbuf);
static GstCaps *
gst_qcarcamsrc_fixate (GstBaseSrc * basesrc, GstCaps * caps);
static GstFlowReturn
gst_qcarcamsrc_create (GstPushSrc * src, GstBuffer ** buf);
static gboolean
gst_qcarcamsrc_negotiate (GstBaseSrc * src);

static void
gst_qcarcam_src_class_init (GstQcarcamSrcClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *pushsrc_class = GST_PUSH_SRC_CLASS (klass);

  object_class->finalize = gst_qcarcam_src_finalize;
  object_class->get_property = gst_qcarcam_src_get_property;
  object_class->set_property = gst_qcarcam_src_set_property;
  element_class->change_state = gst_qcarcam_src_change_state;
  gst_element_class_add_static_pad_template (element_class, &src_template);
  g_object_class_install_property (object_class, PROP_INPUT,
    g_param_spec_uint ("input", "Input", "The input of QCarCam", 0, G_MAXUINT,
    DEFAULT_PROP_INPUT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
    "Qualcomm Technologies Inc gstreamer qcarqcam source",
      "Source/Video",
      "Reads frames from QCarCam device",
      "Lily Li <lali@codeaurora.org>");

  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_qcarcam_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_qcarcam_src_stop);
  basesrc_class->fixate = GST_DEBUG_FUNCPTR (gst_qcarcamsrc_fixate);
  basesrc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_qcarcam_src_decide_allocation);
  basesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_qcarcam_src_set_caps);
  basesrc_class->negotiate = GST_DEBUG_FUNCPTR (gst_qcarcamsrc_negotiate);
  pushsrc_class->create = GST_DEBUG_FUNCPTR (gst_qcarcamsrc_create);
}

static void
gst_qcarcam_src_init (GstQcarcamSrc * qcarcamsrc)
{
  QCarCamInit_t qcarcam_init = {0};
  qcarcam_init.apiVersion = QCARCAM_VERSION;
  gboolean ret = FALSE;

  qcarcamsrc->hndl = 0;
  qcarcamsrc->is_ubwc = FALSE;
  qcarcamsrc->started = FALSE;
  qcarcamsrc->request_id = 0;
  qcarcamsrc->allocator = gst_dmabuf_allocator_new ();
  g_mutex_init (&qcarcamsrc->lock);
  g_mutex_init (&qcarcamsrc->buf_lock);
  g_cond_init (&qcarcamsrc->buf_cond);
  gst_base_src_set_format (GST_BASE_SRC (qcarcamsrc), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (qcarcamsrc), TRUE);
  ret = qcarcam_dmabuf_load_libs_once();
  if (!ret)
    GST_ERROR_OBJECT (qcarcamsrc, "qcarcamsrc load libs failed");
  ret = qcarcamqcx_init(&qcarcam_init);
  if (!ret)
    GST_ERROR_OBJECT (qcarcamsrc, "qcarcamsrc init failed");

  //log_heartbeat_init(&qcarcamsrc->logbeat, LOG_HEARTBEAT_TS_PERIOD_INIT, LOG_HEARTBEAT_TS_PERIOD_MIN, LOG_HEARTBEAT_TS_PERIOD_MAX);
  kpi_place_marker("M - qcarcamsrc init");
}

static void
gst_qcarcam_src_finalize (GObject * object)
{
  GstQcarcamSrc *qcarcamsrc = GST_QCARCAM_SRC (object);
  g_mutex_clear (&qcarcamsrc->lock);
  g_cond_clear (&qcarcamsrc->buf_cond);
  g_mutex_clear (&qcarcamsrc->buf_lock);
  if (qcarcamsrc->hndl != QCARCAM_HNDL_INVALID) {
    qcarcamqcx_release(qcarcamsrc->hndl);
    qcarcamqcx_close(qcarcamsrc->hndl);
    qcarcamsrc->hndl = QCARCAM_HNDL_INVALID;
  }
  qcarcamqcx_uninit();
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_qcarcam_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQcarcamSrc *qcarcamsrc = GST_QCARCAM_SRC (object);
  GST_DEBUG_OBJECT (qcarcamsrc, "prop_id %u", prop_id);

  switch (prop_id) {
    case PROP_INPUT:
      qcarcamsrc->input_id = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qcarcam_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQcarcamSrc *qcarcamsrc = GST_QCARCAM_SRC (object);

  GST_DEBUG_OBJECT (qcarcamsrc, "prop_id %u", prop_id);

  switch (prop_id) {
    case PROP_INPUT:
      g_value_set_uint (value, qcarcamsrc->input_id);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_qcarcam_src_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstQcarcamSrc *src = GST_QCARCAM_SRC (element);
  char tips[39];
  GST_INFO_OBJECT(src, "Doing self transition: 0x%x(%s)", transition, gst_state_change_get_name(transition));
  snprintf(tips, sizeof(tips), "M - qcarcamsrc trans<0x%x>", transition);
  kpi_place_marker(tips);
  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    break;
    default:
    break;
  }

  GST_INFO_OBJECT(src, "Doing parent transition: 0x%x", transition);
  snprintf(tips, sizeof(tips), "M - qcarcamsrc trans<0x%x>", transition);
  kpi_place_marker(tips);
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  snprintf(tips, sizeof(tips), "M - qcarcamsrc trans d<0x%x,%d>", transition, ret);
  kpi_place_marker(tips);
  GST_INFO_OBJECT(src, "Done transition: 0x%x, ret %d(%s)", transition, ret, gst_element_state_change_return_get_name(ret));
  return ret;
}
static gboolean
_caps_has_compression_ubwc (const GstCaps * caps)
{
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *compression = gst_structure_get_string (s, "compression");

  return g_strcmp0 (compression, "ubwc") == 0 ? TRUE : FALSE;
}

static GstCaps *
gst_qcarcamsrc_fixate (GstBaseSrc * basesrc, GstCaps * caps)
{
  GstStructure *structure;
  guint i;
  GstQcarcamSrc *qcarcamsrc = GST_QCARCAM_SRC (basesrc);

  GST_DEBUG_OBJECT (basesrc, "fixating caps %" GST_PTR_FORMAT, caps);

  caps = gst_caps_make_writable (caps);

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    structure = gst_caps_get_structure (caps, i);
    if (gst_structure_has_field (structure, "width"))
      gst_structure_fixate_field_nearest_int (structure, "width", qcarcamsrc->source.width);

    if (gst_structure_has_field (structure, "height"))
      gst_structure_fixate_field_nearest_int (structure, "height", qcarcamsrc->source.height);
    if (gst_structure_has_field (structure, "framerate"))
      gst_structure_fixate_field_nearest_fraction (structure, "framerate",
          (gint) (qcarcamsrc->source.fps), 1);

    if (gst_structure_has_field (structure, "format"))
      gst_structure_fixate_field (structure, "format");
  }

  GST_DEBUG_OBJECT (basesrc, "fixated caps %" GST_PTR_FORMAT, caps);
  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (basesrc, caps);
  return caps;
}

gboolean qcarcam_handle_new_frame(GstQcarcamSrc *self, QCarCamFrameInfo_t *frame_info)
{
  gboolean ret = FALSE;
  QCarCamFrameInfo_t *frame = (QCarCamFrameInfo_t *)malloc(sizeof(QCarCamFrameInfo_t));
  GST_ERROR_OBJECT(self,"enter self %p, frameinfo %p, frame %p", self, frame_info, frame);
  memcpy (frame, frame_info, sizeof(QCarCamFrameInfo_t));
  g_queue_push_tail(&self->buffers, frame);
  GST_ERROR_OBJECT(self,"exit self %p", self);
  return ret;

}

static QCarCamRet_e qcarcam_event_cb(const QCarCamHndl_t hndl,
          const uint32_t eventId,
          const QCarCamEventPayload_t *pPayload,
          void  *pPrivateData)
{
  GstQcarcamSrc *self = GST_QCARCAM_SRC (pPrivateData);
  if (hndl != self->hndl)
  {
    GST_ERROR_OBJECT(self,"event_cb called with invalid qcarcam handle 0x%lx", hndl);
    return QCARCAM_RET_FAILED;
  }

  switch (eventId)
  {
    case QCARCAM_EVENT_FRAME_READY:
      GST_DEBUG_OBJECT (self, "started %d", self->started);
      if (self->started)
      {
        g_mutex_lock(&self->lock);
        GST_INFO_OBJECT(self, "received QCARCAM_EVENT_FRAME_READY");
        qcarcam_handle_new_frame(self, &(pPayload->frameInfo));
        g_mutex_unlock(&self->lock);
        g_mutex_lock(&self->buf_lock);
        g_cond_signal (&self->buf_cond);
        GST_DEBUG_OBJECT (self, "signal frame done");
        g_mutex_unlock(&self->buf_lock);
      }
      break;
    case QCARCAM_EVENT_INPUT_SIGNAL:
      break;
    case QCARCAM_EVENT_ERROR:
      if (pPayload->errInfo.errorId == QCARCAM_ERROR_FATAL)
      {
        GST_ERROR_OBJECT(self, "Fatal Error, abort");
      }
      else
      {
        GST_ERROR_OBJECT(self, "Unhandled Error %d %d",
          pPayload->errInfo.errorId,
          pPayload->errInfo.errorCode);
      }
      break;
    default:
      GST_ERROR_OBJECT(self, "Received unsupported event %d", eventId);
      break;
  }
   return QCARCAM_RET_OK;
}

static gboolean
gst_qcarcamsrc_negotiate (GstBaseSrc * src)
{
  gboolean ret = TRUE;
  GstQcarcamSrc *qcarcamsrc = (GstQcarcamSrc *) src;

  if (!(ret = GST_BASE_SRC_CLASS (parent_class)->negotiate (src))) {
    GST_ERROR_OBJECT (qcarcamsrc, "failed in parent negotiate !!!");
  }

  return ret;
}

static GstFlowReturn
gst_qcarcamsrc_create (GstPushSrc * src, GstBuffer ** buffer)
{
  GstQcarcamSrc *qcarcamsrc = (GstQcarcamSrc *) src;
  gint32 retry_time = 0, retry_time_residual;
  gint32 recv_len = 0;
  GstFlowReturn error = GST_FLOW_OK;
  QCarCamFrameInfo_t *frame_info;
  char tips[39];
  int need_show_log = 0;
  GstMemory *mem;
  gint fd = -1;
  gsize size = 0, info_size = 0;
  GstPad *srcpad = NULL;
  gint64 wait_until = 0;
  GstBuffer *gst_buf;
  GstVideoInfo *info = &qcarcamsrc->info;
  GstVideoInfo *ainfo = &qcarcamsrc->aligned_info;

  GST_ERROR_OBJECT (qcarcamsrc, "enter src %p", qcarcamsrc);

  if (qcarcamsrc->started) {
    GST_ERROR_OBJECT (qcarcamsrc, "src %p", qcarcamsrc);
    g_mutex_lock(&qcarcamsrc->buf_lock);
    while (g_queue_is_empty (&qcarcamsrc->buffers)) {
      wait_until = g_get_monotonic_time () + DEFALUT_WAIT_TIME * G_TIME_SPAN_SECOND;
        if (!g_cond_wait_until (&qcarcamsrc->buf_cond, &qcarcamsrc->buf_lock, wait_until)) {
          GST_INFO_OBJECT (qcarcamsrc, "timed out to wait a valid frame");
            g_mutex_unlock(&qcarcamsrc->buf_lock);
            return GST_FLOW_FLUSHING;
        }
    }
    g_mutex_unlock(&qcarcamsrc->buf_lock);
    g_mutex_lock(&qcarcamsrc->lock);
    if (!g_queue_is_empty (&qcarcamsrc->buffers)) {
      frame_info = (QCarCamFrameInfo_t *) g_queue_pop_head (&qcarcamsrc->buffers);
      GST_ERROR_OBJECT (qcarcamsrc, "frame info %p", frame_info);
      *buffer = gst_buffer_new ();
      gst_buf = *buffer;
      if (gst_buf == NULL) {
        GST_ERROR_OBJECT (qcarcamsrc, "buffer new error");
        error = GST_FLOW_ERROR;
        return error;
      }
      //GST_BUFFER_PTS(gst_buf) = frame_info->sofTimestamp.timestamp;
      //GST_INFO_OBJECT(qcarcamsrc, "timestamp %ld\n", frame_info->sofTimestamp.timestamp);
      fd = qcarcamsrc->buffer_list.pBuffers[frame_info->bufferIndex].planes[0].memHndl;
      *ainfo = *info;
      info_size = GST_VIDEO_INFO_SIZE (ainfo);
          GST_DEBUG_OBJECT (qcarcamsrc, "fd %p, info size %d", fd, info_size);
      mem = gst_dmabuf_allocator_alloc_with_flags (qcarcamsrc->allocator, fd, info_size,
        GST_FD_MEMORY_FLAG_DONT_CLOSE | GST_FD_MEMORY_FLAG_KEEP_MAPPED);
      if (mem) {
        GST_DEBUG_OBJECT (qcarcamsrc, "dmabuf mem %p", mem);
        gst_buffer_append_memory (gst_buf, mem);
        _add_qcarcam_meta (gst_buf, &(qcarcamsrc->desc[frame_info->bufferIndex]));
        _modifier_attach (gst_buf, &(qcarcamsrc->desc[frame_info->bufferIndex]));
        gst_buffer_add_video_meta_full (gst_buf, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_INFO_FORMAT (ainfo),
      GST_VIDEO_INFO_WIDTH (ainfo), GST_VIDEO_INFO_HEIGHT (ainfo),
      GST_VIDEO_INFO_N_PLANES (ainfo), ainfo->offset, ainfo->stride);
        GST_DEBUG_OBJECT (qcarcamsrc, "buffer %p, mem %p, width %d", gst_buf, mem, GST_VIDEO_INFO_WIDTH (&(qcarcamsrc->aligned_info)));
        GST_MINI_OBJECT_CAST (gst_buf)->dispose =
        (GstMiniObjectDisposeFunction) gst_qcarcam_src_buffer_dispose;
        error = GST_FLOW_OK;
      } else {
        GST_ERROR_OBJECT (qcarcamsrc, "dmabuf mem error");
        gst_memory_unref (mem);
        error = GST_FLOW_ERROR;
      }
      g_free(frame_info);
    }
    g_mutex_unlock(&qcarcamsrc->lock);
  }

  return error;
}

static gboolean
gst_qcarcam_src_start (GstBaseSrc * basesrc)
{
  gboolean ret = FALSE;
  char tips[39];
  unsigned int num_inputs = 0;
  QCarCamInput_t *inputs;
  QCarCamOpen_t openParams = {};
  uint32_t param = 0;
  GstQcarcamSrc *qcarcamsrc = GST_QCARCAM_SRC (basesrc);

  GST_INFO_OBJECT(qcarcamsrc,"qcarcamsrc start");
  kpi_place_marker("M - qcarcamsrc start");

  g_queue_init (&qcarcamsrc->buffers);
  ret = qcarcamqcx_query_inputs(NULL, 0, &num_inputs);
  if (!ret)
    return ret;
  snprintf(tips, sizeof(tips), "M - qcarcamsrc query inputs num<%d>", num_inputs);
  kpi_place_marker(tips);
  inputs = (QCarCamInput_t *)malloc(sizeof(QCarCamInput_t) * num_inputs);
  if (!inputs) {
    GST_ERROR_OBJECT(qcarcamsrc, "Failed to allocate inputs");
    return FALSE;
  }
  ret = qcarcamqcx_query_inputs(inputs, num_inputs, &(qcarcamsrc->queryfilled));
  if (!ret || qcarcamsrc->queryfilled != num_inputs) {
    snprintf(tips, sizeof(tips), "M - qcarcamsrc query inputs failed<%d>", ret);
    kpi_place_marker(tips);
    snprintf(tips, sizeof(tips), "M - qcarcamsrc queryfilled<%d>, numinput<%d>", qcarcamsrc->queryfilled, num_inputs);
    kpi_place_marker(tips);
    GST_ERROR_OBJECT (qcarcamsrc,"qcarcamsrc query inputs failed %d, queryfilled %d, numinput %d exit!", ret, qcarcamsrc->queryfilled, num_inputs);
    goto error_free_inputs;
  }

  for (int i = 0; i < num_inputs; i++) {
    GST_INFO_OBJECT (qcarcamsrc, "inputs[%d] is %d", i, inputs[i].inputId);
    if (inputs[i].inputId == qcarcamsrc->input_id) {
      memcpy(&qcarcamsrc->input, &inputs[i], sizeof(QCarCamInput_t));
      break;
    }
  }

  if (qcarcamsrc->input.numModes > QCARCAM_MAX_NUM_MODES) {
    GST_ERROR_OBJECT(qcarcamsrc, "Invalid number of modes %d for input %d",
        qcarcamsrc->input.numModes, qcarcamsrc->input.inputId);
    goto error_free_inputs;
  }

  QCarCamMode_t *modes = (QCarCamMode_t *)malloc(sizeof(QCarCamMode_t) * qcarcamsrc->input.numModes);
  if (!modes) {
    GST_ERROR_OBJECT(qcarcamsrc, "Failed to allocate modes");
    goto error_free_inputs;
  }
  QCarCamInputModes_t querymodes = {};
  querymodes.numModes = qcarcamsrc->input.numModes;
  querymodes.pModes = modes;

  ret = qcarcamqcx_query_input_modes(qcarcamsrc->input_id, &querymodes);
  if (!ret || modes[querymodes.currentMode].numSources > QCARCAM_INPUT_MAX_NUM_SOURCES) {
    GST_ERROR_OBJECT(qcarcamsrc, "qcarcamsrc query input mode failed %d", ret);
    goto error_free_modes;
  }
  memcpy(&qcarcamsrc->source, &(modes[querymodes.currentMode].sources[0]), sizeof(QCarCamInputSrc_t));
 
  GST_DEBUG_OBJECT (qcarcamsrc,"will open qcarcam qcarcamsrc->input_id %d", qcarcamsrc->input_id);
  kpi_place_marker("M - qcarcamsrc will open qcarcam");
  openParams.opMode = QCARCAM_OPMODE_ISP;
  openParams.numInputs = 1;
  openParams.inputs[0].inputId = qcarcamsrc->input_id;
  openParams.inputs[0].srcId = 0;
  openParams.inputs[0].inputMode = querymodes.currentMode;
  openParams.flags |= QCARCAM_OPEN_FLAGS_REQUEST_MODE;
  ret = qcarcamqcx_open(&openParams, &qcarcamsrc->hndl);
  if (!ret || qcarcamsrc->hndl == QCARCAM_HNDL_INVALID) {
    GST_ERROR_OBJECT(qcarcamsrc, "Failed to open qcarcam");
    goto error_free_inputs;
  }
  ret = qcarcamqcx_register_event_callback(qcarcamsrc->hndl, &qcarcam_event_cb, qcarcamsrc);
  param = QCARCAM_EVENT_FRAME_READY | QCARCAM_EVENT_INPUT_SIGNAL | QCARCAM_EVENT_ERROR;
  ret = qcarcamqcx_setparam(qcarcamsrc->hndl, QCARCAM_STREAM_CONFIG_PARAM_EVENT_MASK, &param, sizeof(param));
  if (!ret) {
    snprintf(tips, sizeof(tips), "M - qcarcamsrc set event param err");
    kpi_place_marker(tips);
    GST_ERROR_OBJECT (qcarcamsrc,"qcarcamsrc set event param err, exit!");
    goto error_close;
  }
  qcarcamsrc->isp_config.id = 0;
  qcarcamsrc->isp_config.cameraId = 0;
#ifndef _ENABLE_UMD_
  qcarcamsrc->isp_config.usecaseId = 3;
#else
  qcarcamsrc->isp_config.usecaseId = 64;
#endif
  ret = qcarcamqcx_setparam(qcarcamsrc->hndl,
                  QCARCAM_STREAM_CONFIG_PARAM_ISP_USECASE,
                  &qcarcamsrc->isp_config,
                  sizeof(QCarCamIspUsecaseConfig_t));
  if (!ret) {
    snprintf(tips, sizeof(tips), "M - qcarcamsrc set isp param err");
    kpi_place_marker(tips);
    GST_ERROR_OBJECT (qcarcamsrc,"qcarcamsrc set isp param err, exit!");
    goto error_close;
  }
  qcarcamsrc->started = TRUE;

  GST_INFO_OBJECT(qcarcamsrc,"qcarcamsrc start %d", qcarcamsrc->started);
  return TRUE;

error_close:
  if (qcarcamsrc->hndl != QCARCAM_HNDL_INVALID) {
    qcarcamqcx_release(qcarcamsrc->hndl);
    qcarcamqcx_close(qcarcamsrc->hndl);
    qcarcamqcx_uninit();
  }
error_free_modes:
  GST_DEBUG_OBJECT (qcarcamsrc,"free modes");
  if (modes) {
    free(modes);
    modes = NULL;
    snprintf(tips, sizeof(tips), "M - qcarcamsrc free modes");
    kpi_place_marker(tips);
    GST_ERROR_OBJECT (qcarcamsrc,"qcarcamsrc free modes!");
  }
error_free_inputs:
  if (inputs) {
    free(inputs);
    inputs = NULL;
  }
  kpi_place_marker("M - qcarcamsrc started fail!");
  return FALSE;
}

static gboolean
gst_qcarcam_src_stop (GstBaseSrc * basesrc)
{
  gboolean ret = FALSE;
  QCarCamFrameInfo_t *frame_info = NULL;
  char tips[39];
  GstQcarcamSrc *qcarcamsrc = GST_QCARCAM_SRC (basesrc);
  kpi_place_marker("M - qcarcamsrc stop begin");
  GST_INFO_OBJECT(qcarcamsrc,"qcarcamsrc stop begin");
  if (qcarcamsrc->started && qcarcamsrc->hndl != QCARCAM_HNDL_INVALID) {
    ret = qcarcamqcx_stop(qcarcamsrc->hndl);
    kpi_place_marker("M - qcarcamsrc stop");
    GST_INFO_OBJECT(qcarcamsrc,"qcarcamsrc stop");
  }
  qcarcamsrc->started = FALSE;

  while (!g_queue_is_empty (&qcarcamsrc->buffers)) {
    frame_info = (QCarCamFrameInfo_t *) g_queue_pop_head (&qcarcamsrc->buffers);
    if (frame_info)
      g_free(frame_info);
  }
  g_queue_clear (&qcarcamsrc->buffers);

  kpi_place_marker("M - qcarcamsrc stop end");
  GST_INFO_OBJECT(qcarcamsrc,"qcarcamsrc stop end");
  return ret;
}
static gboolean
gst_qcarcam_src_buffer_dispose (GstBuffer * qcarcamsrcbuf)
{
  gint i, idx = -1;
  GstMemory *mem;
  GstQcarcamSrc *src;
  QCarCamRequest_t request = {};
  QCarCamStreamRequest_t* pStreamRequest = &request.streamRequests[0];
  GST_LOG_OBJECT (src, "buffer %p", qcarcamsrcbuf);
  gst_buffer_ref (qcarcamsrcbuf);
  DmaBufDesc *desc = gst_qcarcam_meta_get_desc(qcarcamsrcbuf);
  src = (GstQcarcamSrc*)(desc->ptr);
  mem = gst_buffer_get_memory (qcarcamsrcbuf, 0);
  GST_LOG_OBJECT (src, "buffer %p, mem %p", qcarcamsrcbuf, mem);
  if (G_UNLIKELY (!mem)) {
    GST_WARNING_OBJECT (src, "cannot get mem from %p",
      qcarcamsrcbuf);
    gst_buffer_unref (qcarcamsrcbuf);
    return FALSE;
  }

  for (i = 0; i < QCARCAMSRC_BUFFERS; i++) {
    if (desc->fd == src->buffer_list.pBuffers[i].planes[0].memHndl) {
      idx = i;
      break;
    }
  }

  if (G_LIKELY (idx != -1) && src->started) {
    request.requestId = src->request_id++;
    request.numStreamRequests = 1;
#ifndef _ENABLE_UMD_
    pStreamRequest->bufferlistId = 0;
#else
    pStreamRequest->bufferlistId = 1;
#endif
    pStreamRequest->bufferIdx = idx;
    qcarcamqcx_submit_request(src->hndl, &request);
  }
  gst_memory_unref (mem);
  gst_buffer_remove_memory (qcarcamsrcbuf, 0);

  return TRUE;
}

static gboolean
gst_qcarcam_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstCaps *prev_caps;
  GstVideoInfo *info;
  GstVideoFormat format;
  GstQcarcamSrc *qcarcamsrc;
  gint width, height, stride_w, stride_h;
  gint fps_n, fps_d, cur_fps;
  gboolean ret = TRUE;
  gint stride_w_ymeta, stride_h_ymeta, plane_sz_ymeta, plane_sz_y;

  qcarcamsrc = GST_QCARCAM_SRC (src);
  prev_caps = qcarcamsrc->prev_caps;
  info = &qcarcamsrc->info;

  GST_DEBUG_OBJECT (qcarcamsrc, "setting caps %" GST_PTR_FORMAT, caps);
  if (prev_caps && gst_caps_is_equal (prev_caps, caps))
    goto done;

  if (!gst_video_info_from_caps (info, caps))
    goto invalid_caps;

  qcarcamsrc->is_ubwc = _caps_has_compression_ubwc(caps);

  format = GST_VIDEO_INFO_FORMAT(info);

  /* FIXME: accroding to the format set the proper video info */
  /* update stride and offset of planes for output yuv data */
  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);
  if (format == GST_VIDEO_FORMAT_NV12) {
    if (!qcarcamsrc->is_ubwc) {
      stride_w = VENUS_Y_STRIDE(COLOR_FMT_NV12, width);
      stride_h = VENUS_Y_SCANLINES(COLOR_FMT_NV12, height);
      GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = stride_w;
      GST_VIDEO_INFO_PLANE_STRIDE (info, 1) = stride_w;
      GST_VIDEO_INFO_PLANE_OFFSET (info, 1) = stride_w * stride_h;
      GST_VIDEO_INFO_SIZE (info) = stride_w * stride_h + (stride_w * stride_h >>1);
    } else {
      GST_VIDEO_INFO_SIZE (info) = VENUS_BUFFER_SIZE_USED(COLOR_FMT_NV12_UBWC, width, height, 0);
      stride_w = VENUS_Y_STRIDE(COLOR_FMT_NV12_UBWC, width);
      stride_h = VENUS_Y_SCANLINES(COLOR_FMT_NV12_UBWC, height);
      GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = stride_w;
      GST_VIDEO_INFO_PLANE_STRIDE (info, 1) = stride_w;
      stride_w_ymeta = VENUS_Y_META_STRIDE(COLOR_FMT_NV12_UBWC, width);
      stride_h_ymeta = VENUS_Y_META_SCANLINES(COLOR_FMT_NV12_UBWC, height);
      plane_sz_ymeta = GST_ROUND_UP_N(stride_w_ymeta * stride_h_ymeta, 4096);
      plane_sz_y = GST_ROUND_UP_N(stride_w * stride_h, 4096);
      GST_VIDEO_INFO_PLANE_OFFSET (info, 1) = plane_sz_ymeta + plane_sz_y;
    }
  }

  if (prev_caps)
    gst_caps_unref (prev_caps);
  prev_caps = caps;

done:
  return ret;

/* Wrong errors */
invalid_caps:
  {
    GST_WARNING_OBJECT (qcarcamsrc, "invalid caps !!!");
    return FALSE;
  }
}

static gboolean
gst_qcarcam_src_decide_allocation (GstBaseSrc * src, GstQuery * query)
{
  GstQcarcamSrc *self = GST_QCARCAM_SRC (src);
  GstVideoInfo *info = &self->info;
  GstVideoInfo *ainfo = &self->aligned_info;
  GstBufferPool *pool = NULL;
  GstCaps *outcaps = NULL;
  GstStructure *config;
  guint min = 0, max = 0, size = 0, align_w = 0, align_h = 0, length = 0;
  gboolean update_pool;
  QCarCamBuffer_t *qcamBuf;
  gboolean ret = FALSE;
  QCarCamRequest_t request = {};
  DmaBufDesc *desc = NULL;

  GST_INFO_OBJECT (self, "%" GST_PTR_FORMAT, query);

  min = QCARCAMSRC_BUFFERS;
  self->desc = (DmaBufDesc *)calloc(min, sizeof(DmaBufDesc));
  self->buffer_list.pBuffers = (QCarCamBuffer_t *)calloc(min, sizeof(QCarCamBuffer_t));
#ifdef _ENABLE_UMD_
  self->buffer_list.id = 1;
#endif
  for (int i = 0; i < min; i++)
  {
     self->buffer_list.nBuffers = min;
     if (self->is_ubwc)
       self->buffer_list.colorFmt = QCARCAM_FMT_UBWC_NV12;
     else
       self->buffer_list.colorFmt = QCARCAM_FMT_NV12;
     self->buffer_list.flags    = QCARCAM_BUFFER_FLAG_OS_HNDL;
     qcarcam_dmabuf_alloc(&desc, info, self->is_ubwc);
     desc->ptr = self;
     memcpy(&self->desc[i], desc, sizeof(DmaBufDesc));
     *ainfo = *info;
     qcamBuf = &(self->buffer_list.pBuffers[i]);
     qcamBuf->numPlanes = desc->layout.num_planes;
     qcamBuf->planes[0].memHndl = qcarcam_dmabuf_get_fd(desc);
     qcamBuf->planes[0].width = desc->width;
     qcamBuf->planes[0].height = desc->height;
     switch (desc->layout.num_planes)
     {
       case 3:
         qcamBuf->planes[2].stride = desc->layout.planes[2].v_increment;
         qcamBuf->planes[1].stride = desc->layout.planes[1].v_increment;
         qcamBuf->planes[0].stride = desc->layout.planes[0].v_increment;
         qcamBuf->planes[2].size = desc->buffer_size_dimensions - desc->layout.planes[2].offset;
         qcamBuf->planes[1].size = desc->layout.planes[2].offset - desc->layout.planes[1].offset;
         qcamBuf->planes[0].size = desc->layout.planes[1].offset - desc->layout.planes[0].offset;
         break;
       case 2:
         qcamBuf->planes[1].stride = desc->layout.planes[1].v_increment;
         qcamBuf->planes[0].stride = desc->layout.planes[0].v_increment;
         qcamBuf->planes[1].size = desc->buffer_size_dimensions - desc->layout.planes[1].offset;
         qcamBuf->planes[0].size = desc->layout.planes[1].offset - desc->layout.planes[0].offset;
         GST_DEBUG_OBJECT(self, "desc->layout.planes[1].v_increment %d", desc->layout.planes[1].v_increment);
         GST_DEBUG_OBJECT(self, "desc->layout.planes[0].v_increment %d", desc->layout.planes[0].v_increment);
         GST_DEBUG_OBJECT(self, "desc->buffer_size_dimensions %d", desc->buffer_size_dimensions);
         GST_DEBUG_OBJECT(self, "desc->layout.planes[1].offset %d", desc->layout.planes[1].offset);
         GST_DEBUG_OBJECT(self, "desc->layout.planes[0].offset %d", desc->layout.planes[0].offset);
         break;
       case 1:
         qcamBuf->planes[0].stride = desc->layout.planes[0].v_increment;
         qcamBuf->planes[0].size = desc->buffer_size_dimensions;
         break;
       default:
         GST_ERROR_OBJECT(self, "plane number %d, should be 1/2/3", desc->layout.num_planes);
         return FALSE;
     }

     if (self->is_ubwc) {
        qcamBuf->planes[1].stride = desc->stride;
         qcamBuf->planes[0].stride = desc->stride;
         qcamBuf->planes[1].size = desc->size - GST_VIDEO_INFO_PLANE_OFFSET (ainfo, 1);
         qcamBuf->planes[0].size = GST_VIDEO_INFO_PLANE_OFFSET (ainfo, 1) - GST_VIDEO_INFO_PLANE_OFFSET (ainfo, 0);
     }

  }
  ret = qcarcamqcx_set_bufs(self->hndl, &self->buffer_list);
  if (!ret)
  {
    GST_ERROR_OBJECT (self, "qcarcamsrc set buf failed");
    return FALSE;
  }
  ret = qcarcamqcx_reserve(self->hndl);
  if (!ret)
  {
    GST_ERROR_OBJECT (self, "qcarcamsrc reserve failed");
    return FALSE;
  }
  if (!GST_BASE_SRC_CLASS (parent_class)->decide_allocation (src, query)) {
    GST_ERROR_OBJECT (self, "failed in parent decide_allocation");
    return FALSE;
  }
  ret = qcarcamqcx_start(self->hndl);
  if (!ret)
  {
    GST_ERROR_OBJECT (self, "qcarcamsrc start failed");
    return FALSE;
  }

  g_mutex_lock(&self->lock);
  for (int i = 0; i < min; i++) {
    request.requestId = self->request_id++;
    request.numStreamRequests = 1;
    QCarCamStreamRequest_t* pStreamRequest = &request.streamRequests[0];
#ifndef _ENABLE_UMD_
    pStreamRequest->bufferlistId = 0;
#else
    pStreamRequest->bufferlistId = 1;
#endif
    pStreamRequest->bufferIdx = i;
    qcarcamqcx_submit_request(self->hndl, &request);
  }
    g_mutex_unlock(&self->lock);
  return ret;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
qcarcamsrc_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_qcarcam_src_debug, "qcarcamsrc", 0,
      "qcarcamsrc debug category");

  return gst_element_register (plugin, "qcarcamsrc",
      GST_RANK_SECONDARY, GST_TYPE_QCARCAM_SRC);
}

/* gstreamer looks for this structure to register qcarcamsrc */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qcarcamsrc,
    "QTI qcarqcam source",
    qcarcamsrc_init,
    PACKAGE_VERSION "-" G_STRINGIFY(GST_VERSION_MAJOR) "/" G_STRINGIFY(GST_VERSION_MINOR) "/" G_STRINGIFY(GST_VERSION_MICRO), GST_LICENSE_UNKNOWN, PACKAGE_NAME, "-")

