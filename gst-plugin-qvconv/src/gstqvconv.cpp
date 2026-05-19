// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/**
 * SECTION:element-gstqvconv
 *
 * The qvconv element does video convert/scale/blend stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v videotestsrc ! video/x-raw, format=UYVY, width=1280, height=720 ! qvconv !
 * video/x-raw, format=NV12, width=640, height=480 ! filesink location=test.yuv
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/mman.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <gst/allocators/gstfdmemory.h>
#include "gstqvconv.h"
#include "gstqvconvbufferpool.h"
#include "gstqvconvbufmeta.h"
#include <c2d2.h>

#ifdef QVCONV_DUMP_C2D_BUFFER
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#endif

GST_DEBUG_CATEGORY (gst_qvconv_debug);
#define GST_CAT_DEFAULT gst_qvconv_debug

/* the capabilities of input and output.*/
#define GST_QVCONV_SRC_TEMPLATE_CAP	GST_VIDEO_CAPS_MAKE("{UYVY, NV12, RGBA, BGR, RGB, ARGB, P010_10LE}")
#define GST_QVCONV_SINK_TEMPLATE_CAP	GST_VIDEO_CAPS_MAKE("{UYVY, NV12, RGBA, BGR, RGB, ARGB, P010_10LE}")

static GstStaticPadTemplate gst_qvconv_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_DMABUF, GST_QVCONV_SRC_TEMPLATE_CAP) ";"
                     GST_QVCONV_SRC_TEMPLATE_CAP));

static GstStaticPadTemplate gst_qvconv_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_DMABUF, GST_QVCONV_SINK_TEMPLATE_CAP) ";"
                     GST_QVCONV_SINK_TEMPLATE_CAP));

/* prototypes */
static void gst_qvconv_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_qvconv_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);

static void gst_qvconv_dispose (GObject * object);
static void gst_qvconv_finalize (GObject * object);

/* base transform vmethod */
static gboolean gst_qvconv_start (GstBaseTransform * trans);
static gboolean gst_qvconv_stop (GstBaseTransform * trans);
static GstCaps * gst_qvconv_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *
gst_qvconv_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean
    gst_qvconv_decide_allocation (GstBaseTransform * trans, GstQuery * query);

static gboolean
gst_qvconv_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);
/* video filter vmethod */
static gboolean gst_qvconv_set_info (GstVideoFilter * filter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info);
static GstFlowReturn
gst_qvconv_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf);
static gboolean
gst_qvconv_align_info (GstQvconv * qvconv, GstVideoInfo * info, const GstVideoMeta * meta, gboolean isubwc);
static gboolean
gst_qvconv_match_color_type (const GstQvconv *qvconv, GstVideoFormat format,
    ColorConvertFormat *c2d_format, gboolean ubwc);
static gboolean
gst_qvconv_do_buffer_copy (GstBaseTransform * trans, C2DBuffer * c2d_input_buffer,
    guint8 * input_ptr, const GstVideoInfo * src_info, const GstVideoMeta * gvmeta, gboolean ubwc);
static gboolean
gst_qvconv_do_convert (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf);

#define gst_qvconv_parent_class parent_class

#define QVCONV_MIN_OUTBUFFERS 2
#define QVCONV_MAX_OUTBUFFERS 32

#define QVCONV_CROP_X_POS_DEFAULT 0
#define QVCONV_CROP_Y_POS_DEFAULT 0
#define QVCONV_CROP_WIDTH_DEFAULT 0
#define QVCONV_CROP_HEIGHT_DEFAULT 0

#define QVCONV_IGNORE_DOWNSTREAM_POOL_DEFAULT TRUE

#ifdef QVCONV_DUMP_C2D_BUFFER
#define QVCONV_DUMP_OPTION_DEFAULT 0    /* default disable dump function */
#define QVCONV_DUMP_START_DEFAULT  1    /* default start from 1st frame */
#define QVCONV_DUMP_FRAMES_DEFAULT 90   /* default dump 90 frames */
#define QVCONV_DUMP_DIR_DEFAULT "/tmp/" /* default dump to /tmp/ */
#define QVCONV_DUMP_FILENAME_IN "qvconv_frames_input.raw"
#define QVCONV_DUMP_FILENAME_OUT "qvconv_frames_output.raw"
#endif

#define QVCONV_CACHE_GPU_ADDR_DEFAULT QVCONV_CACHE_GPU_ADDR_INT

#define QVCONV_INPUTCOPY_DEFAULT FALSE

enum
{
  PROP_0,
  PROP_METHOD,
  PROP_CROP_TOP_LEFT_X,
  PROP_CROP_TOP_LEFT_Y,
  PROP_CROP_WIDTH,
  PROP_CROP_HEIGHT,
  PROP_IGNORE_DOWNSTREAM_POOL,
  PROP_QUALITY,
#ifdef QVCONV_DUMP_C2D_BUFFER
  PROP_DUMP_OPTION,
  PROP_DUMP_START,
  PROP_DUMP_FRAMES,
  PROP_DUMP_DIR,
#endif
  PROP_CACHE_GPU_ADDR,
  PROP_INPUTCOPY,
};

/* class initialization */
#define QVCONV_INIT \
    GST_DEBUG_CATEGORY_INIT (gst_qvconv_debug, "qvconv", 0,\
    "debug category for qvconv element"); \
    G_ADD_PRIVATE (GstQvconv)

G_DEFINE_TYPE_WITH_CODE (GstQvconv, gst_qvconv, GST_TYPE_VIDEO_FILTER,
    QVCONV_INIT);

static GType
gst_qvconv_method_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {METHOD_NONE, "No method (default)", "none"},
      {METHOD_FLIP_H, "horizontal flip", "flip_h"},
      {METHOD_FLIP_V, "vertical flip", "flip_v"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstQvconvMethod", values);
  }
  return gtype;
}

#ifdef QVCONV_DUMP_C2D_BUFFER
static GType
gst_qvconv_dump_option_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {DUMP_OPTION_NONE, "dump none (default)", "none"},
      {DUMP_OPTION_INPUT, "dump input buffer", "input"},
      {DUMP_OPTION_OUTPUT, "dump output buffer", "output"},
      {DUMP_OPTION_BOTH, "dump both", "both"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstQvconvDumpOption", values);
  }
  return gtype;
}
#endif /* QVCONV_DUMP_C2D_BUFFER */

static GType
gst_qvconv_cache_gpu_addr_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {QVCONV_CACHE_GPU_ADDR_NONE, "disable caching", "none"},
      {QVCONV_CACHE_GPU_ADDR_INT, "cache internal buffers", "internal"},
      {QVCONV_CACHE_GPU_ADDR_EXT, "cache external buffers", "external"},
      {QVCONV_CACHE_GPU_ADDR_BOTH, "cache internal & external buffers", "both"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstQvconvCacheGpuAddr", values);
  }
  return gtype;
}

static gboolean
gst_qvconv_caps_has_feature (const GstCaps * caps, const gchar * partten)
{
  guint count = gst_caps_get_size (caps);
  gboolean ret = FALSE;

  if (count > 0) {
    for (guint i = 0; i < count; i++) {
      GstCapsFeatures *features = gst_caps_get_features (caps, i);
      if (gst_caps_features_is_any (features))
        continue;
      if (gst_caps_features_contains (features, partten))
        ret = TRUE;
    }
  }

  return ret;
}

gboolean
gst_qvconv_caps_has_compression (const GstCaps * caps, const gchar * compression)
{
  GstStructure *structure = NULL;
  const gchar *string = NULL;

  structure = gst_caps_get_structure (caps, 0);
  string = gst_structure_has_field (structure, "compression") ?
      gst_structure_get_string (structure, "compression") : NULL;

  return (g_strcmp0 (string, compression) == 0) ? TRUE : FALSE;
}

/* class initialization */
static void
gst_qvconv_class_init (GstQvconvClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = (GstElementClass *) klass;;
  GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gobject_class->set_property = gst_qvconv_set_property;
  gobject_class->get_property = gst_qvconv_get_property;
  gobject_class->dispose = gst_qvconv_dispose;
  gobject_class->finalize = gst_qvconv_finalize;

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
      "Qualcomm Technologies Inc gstreamer video converter",
      "Filter/Video",
      "video processing",
      "Jie Zhou <zhojie@codeaurora.org>");

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method",
      "Method",
      "Apply method while converting",
      gst_qvconv_method_get_type (),
      METHOD_NONE,
      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CROP_TOP_LEFT_X,
      g_param_spec_uint ("crop-x",
      "crop window x-pos",
      "X-coordinate of crop rectangle",
      0, G_MAXUINT,
      QVCONV_CROP_X_POS_DEFAULT,
      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CROP_TOP_LEFT_Y ,
      g_param_spec_uint ("crop-y",
      "crop window y-pos",
      "Y-coordinate of crop rectangle",
      0, G_MAXUINT,
      QVCONV_CROP_Y_POS_DEFAULT,
      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CROP_WIDTH,
      g_param_spec_uint ("crop-width",
      "crop window width",
      "width of crop rectangle",
      0, G_MAXUINT,
      QVCONV_CROP_WIDTH_DEFAULT,
      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CROP_HEIGHT,
      g_param_spec_uint ("crop-height",
      "crop window height",
      "height of crop rectangle",
      0, G_MAXUINT,
      QVCONV_CROP_HEIGHT_DEFAULT,
      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_IGNORE_DOWNSTREAM_POOL,
      g_param_spec_boolean ("ignore-downstream-pool",
      "ignore downstream pool",
      "ignore downstream plugin provided memory pool",
      QVCONV_IGNORE_DOWNSTREAM_POOL_DEFAULT,
      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_int ("quality",
      "quality indicator",
      "quality indicator: 0:no quality enhancement, 1:bilinear, 2:anti-aliasing, 3:bilinear+anti-aliasing",
      C2DCONV_QUALITY_NONE, C2DCONV_QUALITY_BLAA,
      C2DCONV_QUALITY_DEFAULT,
      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

#ifdef QVCONV_DUMP_C2D_BUFFER
  g_object_class_install_property (gobject_class, PROP_DUMP_OPTION,
      g_param_spec_enum ("dump-option", "dump option",
      "option to dump input/output buffer",
      gst_qvconv_dump_option_get_type (),
      DUMP_OPTION_NONE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DUMP_START,
      g_param_spec_uint ("dump-start", "dump start",
      "start frame number to dump",
      0, G_MAXUINT,
      QVCONV_DUMP_START_DEFAULT,
      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DUMP_FRAMES,
      g_param_spec_uint ("dump-frames", "dump frames",
      "number of frame to dump",
      0, G_MAXUINT,
      QVCONV_DUMP_FRAMES_DEFAULT,
      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DUMP_DIR,
      g_param_spec_string ("dump-dir", "dump directory",
      "directory to dump into, input dump file name is " QVCONV_DUMP_FILENAME_IN
      ", output dump file name is " QVCONV_DUMP_FILENAME_OUT,
      QVCONV_DUMP_DIR_DEFAULT,
      G_PARAM_READWRITE));
#endif /* QVCONV_DUMP_C2D_BUFFER */

  g_object_class_install_property (gobject_class, PROP_CACHE_GPU_ADDR,
      g_param_spec_enum ("cache-gpu-addr",
      "cache GPU address",
      "cache mapped GPU address of internal & external allocated DMA buffers",
      gst_qvconv_cache_gpu_addr_get_type (),
      QVCONV_CACHE_GPU_ADDR_DEFAULT,
      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_INPUTCOPY,
      g_param_spec_boolean ("inputcopy",
      "force do input copy",
      "force do input copy even if input gstbuf is dmabuf, only for test",
      QVCONV_INPUTCOPY_DEFAULT,
      G_PARAM_READWRITE));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_qvconv_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_qvconv_sink_template));

  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_qvconv_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_qvconv_stop);
  base_transform_class->transform_caps = GST_DEBUG_FUNCPTR (gst_qvconv_transform_caps);
  base_transform_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_qvconv_fixate_caps);
  base_transform_class->decide_allocation = GST_DEBUG_FUNCPTR (gst_qvconv_decide_allocation);
  base_transform_class->set_caps = GST_DEBUG_FUNCPTR (gst_qvconv_set_caps);
  base_transform_class->passthrough_on_same_caps = FALSE;
  base_transform_class->transform = GST_DEBUG_FUNCPTR (gst_qvconv_transform);
  base_transform_class->transform_ip = NULL;
  video_filter_class->set_info = GST_DEBUG_FUNCPTR (gst_qvconv_set_info);
}

static void
gst_qvconv_init (GstQvconv *qvconv)
{
  GstQvconvPrivate *priv;
  C2dConverter *c2d;

  SG_INFO_LITE ("qvconv info: %s", VERSION "-" G_STRINGIFY(GST_VERSION_MAJOR) "/" G_STRINGIFY(GST_VERSION_MINOR) "/" G_STRINGIFY(GST_VERSION_MICRO));

  if (!qvconv_load_libs_once ()) {
    SG_ERR_OBJ (qvconv, "failed to load libs, inst %" GST_PTR_FORMAT "@%p", qvconv, qvconv);
    return;
  }

  c2d = new C2dConverter();
  if (!c2d) {
    SG_ERR_OBJ (qvconv, "failed to instantiate c2d object, inst %" GST_PTR_FORMAT "@%p", qvconv, qvconv);
    return;
  }

  SG_INFO_OBJ (qvconv, "get c2d_conv instance %p for plugin instance %" GST_PTR_FORMAT, c2d, qvconv);

  qvconv->c2d_hndl = c2d;

  priv = (GstQvconvPrivate *)gst_qvconv_get_instance_private (qvconv);
  SG_INFO_OBJ (qvconv, "creating instance %" GST_PTR_FORMAT ", priv-size %p-%lu, offset %d",
      qvconv, priv, sizeof(*priv), GstQvconv_private_offset);
  if (priv == NULL) {
    SG_ERR_OBJ_LITE (qvconv, "Failed to get private structure of qvconv %p!", qvconv);
    return;
  }

  qvconv->priv = priv;
  priv->method = METHOD_NONE;
  priv->active = FALSE;
  g_mutex_init (&priv->lock);
  priv->crop.x = 0;
  priv->crop.y = 0;
  priv->crop.width = 0;
  priv->crop.height = 0;
  priv->pool = NULL;
  priv->ignore_downstream_pool = QVCONV_IGNORE_DOWNSTREAM_POOL_DEFAULT;
  priv->input_nondma = FALSE;
  priv->quality_indicator = C2DCONV_QUALITY_DEFAULT;

#ifdef QVCONV_DUMP_C2D_BUFFER
  priv->dump_option = QVCONV_DUMP_OPTION_DEFAULT;
  priv->dump_start  = QVCONV_DUMP_START_DEFAULT;
  priv->dump_frames = QVCONV_DUMP_FRAMES_DEFAULT;
  priv->dump_dir    = g_strdup (QVCONV_DUMP_DIR_DEFAULT);
  priv->dump_fd_src = -1;
  priv->dump_fd_dst = -1;
  priv->frame_seqno = 0;
  priv->dumped_frames = 0;
  priv->dump_error = FALSE;
#endif

  priv->cache_gpu_addr = QVCONV_CACHE_GPU_ADDR_DEFAULT;
  priv->input_buf_internal = FALSE;
  priv->output_buf_internal = FALSE;

  priv->execute_idx = 0;
  priv->idx_in_one_cycle = 0;

  priv->do_deinterlace = FALSE;

  priv->do_inputcopy = QVCONV_INPUTCOPY_DEFAULT;
}

static void
gst_qvconv_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQvconv *qvconv = GST_QVCONV (object);
  GstQvconvPrivate *priv = qvconv->priv;

  GST_DEBUG_OBJECT (qvconv, "property_id=%u", property_id);

  switch (property_id) {
  case PROP_METHOD:
    GST_OBJECT_LOCK (qvconv);
    if (GST_STATE (qvconv) <= GST_STATE_READY)
      priv->method = g_value_get_enum (value);
    else
      SG_WARN_OBJ_LITE (qvconv, "cannot set flip method at current state %d",
          GST_STATE (qvconv));
    GST_OBJECT_UNLOCK (qvconv);
    break;
  case PROP_CROP_TOP_LEFT_X:
    priv->crop.x = g_value_get_uint (value);
    break;
  case PROP_CROP_TOP_LEFT_Y:
    priv->crop.y = g_value_get_uint (value);
    break;
  case PROP_CROP_WIDTH:
    priv->crop.width = g_value_get_uint (value);
    break;
  case PROP_CROP_HEIGHT:
    priv->crop.height = g_value_get_uint (value);
    break;
  case PROP_IGNORE_DOWNSTREAM_POOL:
    priv->ignore_downstream_pool = g_value_get_boolean (value);
    break;
  case PROP_QUALITY:
    priv->quality_indicator = g_value_get_int (value);;
    break;
#ifdef QVCONV_DUMP_C2D_BUFFER
  case PROP_DUMP_OPTION:
    priv->dump_option = g_value_get_enum (value);
    break;
  case PROP_DUMP_START:
    priv->dump_start = g_value_get_uint (value);
    break;
  case PROP_DUMP_FRAMES:
    priv->dump_frames = g_value_get_uint (value);
    break;
  case PROP_DUMP_DIR:
    g_free ((gpointer) priv->dump_dir);
    priv->dump_dir = g_strdup (g_value_get_string (value));
    break;
#endif /* QVCONV_DUMP_C2D_BUFFER */
  case PROP_CACHE_GPU_ADDR:
    priv->cache_gpu_addr = g_value_get_enum (value);
    break;
  case PROP_INPUTCOPY:
    priv->do_inputcopy = g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
gst_qvconv_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstQvconv *qvconv = GST_QVCONV (object);
  GstQvconvPrivate *priv = qvconv->priv;

  GST_DEBUG_OBJECT (qvconv, "property_id=%u", property_id);

  switch (property_id) {
  case PROP_METHOD:
    g_value_set_enum (value, priv->method);
    break;
  case PROP_CROP_TOP_LEFT_X:
    g_value_set_uint (value, priv->crop.x);
    break;
  case PROP_CROP_TOP_LEFT_Y:
    g_value_set_uint (value, priv->crop.y);
    break;
  case PROP_CROP_WIDTH:
    g_value_set_uint (value, priv->crop.width);
    break;
  case PROP_CROP_HEIGHT:
    g_value_set_uint (value, priv->crop.height);
    break;
  case PROP_IGNORE_DOWNSTREAM_POOL:
    g_value_set_boolean (value, priv->ignore_downstream_pool);
    break;
  case PROP_QUALITY:
    g_value_set_int (value, priv->quality_indicator);
    break;
#ifdef QVCONV_DUMP_C2D_BUFFER
  case PROP_DUMP_OPTION:
    g_value_set_enum (value, priv->dump_option);
    break;
  case PROP_DUMP_START:
    g_value_set_uint (value, priv->dump_start);
    break;
  case PROP_DUMP_FRAMES:
    g_value_set_uint (value, priv->dump_frames);
    break;
  case PROP_DUMP_DIR:
    g_value_set_string (value, priv->dump_dir);
    break;
#endif /* QVCONV_DUMP_C2D_BUFFER */
  case PROP_CACHE_GPU_ADDR:
    g_value_set_enum (value, priv->cache_gpu_addr);
    break;
  case PROP_INPUTCOPY:
    g_value_set_boolean (value, priv->do_inputcopy);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static GstCaps *
gst_qvconv_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *result;
  GstCaps *tmp;
  GstPad *otherpad;
  GstQvconv *qvconv = GST_QVCONV (trans);
  GstQvconvPrivate *priv = qvconv->priv;

  otherpad = (direction == GST_PAD_SRC) ? trans->sinkpad : trans->srcpad;
  result = gst_pad_get_pad_template_caps(otherpad);

  /* Try to pick up NON-DMA caps with upstream's non dmabuf to avoid
   * multi-caps alternately re-negotiation.
   * Otherwise, it would cause create/destroy gbm_bo repeatedly
   * that leads to memory issue.
   */
  if (direction == GST_PAD_SRC) {
    if (priv->input_nondma) {
      GST_DEBUG_OBJECT (qvconv, "DMABuf doesn't work for input, so remove it");
      result = gst_caps_make_writable (result);
      GstCapsFeatures *features;
      features = gst_caps_get_features (result, 0);
      gst_caps_features_remove (features, GST_CAPS_FEATURE_MEMORY_DMABUF);
      priv->input_nondma = FALSE;
    }
  }

  if (filter) {
    //result should be subset of filter, refer to gst_base_transform_transform_caps()
    tmp = gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref(result);
    result = tmp;
  }

  GST_DEBUG_OBJECT (qvconv, "direction = %s, transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT " with filter %" GST_PTR_FORMAT, direction==GST_PAD_SRC?"down2up":"up2down", caps, result, filter);

  return result;
}

static void
copy_colorimetry_from_input (GstQvconv * qvconv, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstStructure *out_s = gst_caps_get_structure (out_caps, 0);
  GstStructure *in_s = gst_caps_get_structure (in_caps, 0);
  const gchar *out_str = gst_structure_get_string (out_s, "colorimetry");
  const gchar *in_str = gst_structure_get_string (in_s, "colorimetry");

  GST_DEBUG_OBJECT (qvconv, "colorimetry of in_caps:%s out_caps:%s",
      in_str ? in_str : "null", out_str ? out_str : "null");
  if (out_str) { /* preserve the out_caps colorimetry */
    SG_INFO_OBJ_LITE (qvconv, "out_caps already has colorimetry");
    return;
  }

  const GValue *in_colorimetry = gst_structure_get_value (in_s, "colorimetry");
  if (!in_str || !in_colorimetry) { /* there has no in_caps colorimetry */
    SG_INFO_OBJ_LITE (qvconv, "no in_caps colorimetry to copy");
    return;
  }

  GstVideoInfo in_info, out_info;
  if (!gst_video_info_from_caps (&in_info, in_caps)) {
    SG_ERR_OBJ (qvconv, "info_from_caps error, in_caps: %" GST_PTR_FORMAT ", inst %" GST_PTR_FORMAT "@%p", in_caps, qvconv, qvconv);
    return;
  }
  if (!gst_video_info_from_caps (&out_info, out_caps)) {
    SG_ERR_OBJ (qvconv, "info_from_caps error, out_caps: %" GST_PTR_FORMAT ", inst %" GST_PTR_FORMAT "@%p", out_caps, qvconv, qvconv);
    return;
  }

  /* YUV to YUV is okay to transfer colorimetry. */
  if ((GST_VIDEO_INFO_IS_YUV (&out_info) && GST_VIDEO_INFO_IS_YUV (&in_info))) {
    /* copy the colorimetry from the input */
    gst_structure_set_value (out_s, "colorimetry", in_colorimetry);
    SG_INFO_OBJ (qvconv, "added colorimetry %s into out_caps: %"
        GST_PTR_FORMAT, in_str, out_caps);
  }
}

static GstCaps *
gst_qvconv_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  guint i;
  gint width=0, height=0, fps_n=0, fps_d=0;
  GstCaps *result;
  GstStructure *structure;
  GstQvconv *qvconv = GST_QVCONV (trans);

  GST_DEBUG_OBJECT (qvconv, "PadDirection %d, trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, direction, othercaps, caps);

  result = gst_caps_intersect (othercaps, caps);
  if (gst_caps_is_empty (result)) {
    gst_caps_unref (result);
    result = othercaps;
  } else {
    gst_caps_unref (othercaps);
  }

  GST_DEBUG_OBJECT (qvconv, "now fixating %" GST_PTR_FORMAT, result);

  result = gst_caps_make_writable (result);
  if (!gst_caps_is_fixed(result)) {
    /* first, get width/height from input caps */
    for (i = 0; i < gst_caps_get_size (caps); ++i) {
      structure = gst_caps_get_structure (caps, i);

       if (gst_structure_has_field (structure, "width"))
         gst_structure_get_int(structure, "width", &width);

       if (gst_structure_has_field (structure, "height"))
         gst_structure_get_int(structure, "height", &height);

       if (gst_structure_has_field (structure, "framerate"))
         gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d);
    }
    /* then use input caps to fixate output caps */
    for (i = 0; i < gst_caps_get_size (result); ++i) {
      structure = gst_caps_get_structure (result, i);

       if (gst_structure_has_field (structure, "width"))
         gst_structure_fixate_field_nearest_int (structure, "width", width);

       if (gst_structure_has_field (structure, "height"))
         gst_structure_fixate_field_nearest_int (structure, "height", height);

       if (gst_structure_has_field (structure, "framerate"))
         gst_structure_fixate_field_nearest_fraction (structure, "framerate",
             fps_n, fps_d);
    }
  }

  /* fixate remaining fields */
  result = gst_caps_fixate (result);
  GST_DEBUG_OBJECT (qvconv, "fixated result: %" GST_PTR_FORMAT, result);


  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result)) {
      gst_caps_replace (&result, caps);
    } else {
      copy_colorimetry_from_input (qvconv, caps, result);
    }
  }

  return result;
}

/* Make sure src_info and dst_info are ready for C2D use before call this. */
static gboolean gst_qvconv_configure_c2d (GstQvconv * qvconv)
{
  gint method, flip;
  GstVideoFormat src_format, dst_format;
  C2dFormat src, dst;

  GstQvconvPrivate *priv = qvconv->priv;
  GstVideoInfo *src_info = &qvconv->src_info;
  GstVideoInfo *dst_info = &qvconv->dst_info;

  C2dConverter *c2d = qvconv->c2d_hndl;
  if (G_UNLIKELY (!c2d)) {
    SG_ERR_OBJ (qvconv, "no c2d instance, inst %" GST_PTR_FORMAT "@%p", qvconv, qvconv);
    return FALSE;
  }

  /* Currently no need to reconfigure C2D. Maybe it's needed in future, like multiple resolution case*/
  if (priv->active) {
    SG_ERR_OBJ (qvconv, "already configured c2d, inst %" GST_PTR_FORMAT "@%p", qvconv, qvconv);
    return TRUE;
  }

  src_format = GST_VIDEO_INFO_FORMAT (src_info);
  src.width  = GST_VIDEO_INFO_WIDTH (src_info);
  src.height = GST_VIDEO_INFO_HEIGHT (src_info);
  src.stride = GST_VIDEO_INFO_PLANE_STRIDE (src_info, 0);

  dst_format = GST_VIDEO_INFO_FORMAT (dst_info);
  dst.width  = GST_VIDEO_INFO_WIDTH (dst_info);
  dst.height = GST_VIDEO_INFO_HEIGHT (dst_info);
  dst.stride = GST_VIDEO_INFO_PLANE_STRIDE (dst_info, 0);

  if (!src.width || !src.height || !dst.width || !dst.height) {
    SG_ERR_OBJ_LITE (qvconv, "width height must not be zero, inst %p", qvconv);
    return FALSE;
  }

  if (!gst_qvconv_match_color_type (qvconv, src_format, &src.format, priv->input_buffer.ubwc_flags) ||
      !gst_qvconv_match_color_type (qvconv, dst_format, &dst.format, priv->outubwc)) {
    SG_ERR_OBJ_LITE (qvconv, "gst_qvconv_match_color_type() fail, inst %p!", qvconv);
    return FALSE;
  }

  SG_INFO_OBJ_LITE (qvconv, "allocate c2d candidate input buffer");
  if (!gst_qvconv_alloc_c2d_buf (c2d, &priv->input_buffer, src_info, priv->input_buffer.ubwc_flags)) {
    SG_ERR_OBJ_LITE (qvconv, "gst_qvconv_alloc_c2d_buf() fail, inst %p!", qvconv);
    return FALSE;
  }

  method = priv->method;
  switch (method) {
    case METHOD_NONE:
      flip = 0;
      break;
    case METHOD_FLIP_V:
      flip = C2D_MIRROR_V_BIT;
      break;
    case METHOD_FLIP_H:
      flip = C2D_MIRROR_H_BIT;
      break;
    default:
      SG_WARN_OBJ_LITE (qvconv, "no method %d, use default method", method);
      flip = 0;
      break;
  }

  {
    C2dParam param = { 0, };
    param.qualityIndicator = priv->quality_indicator;

    param.cacheGpuAddrSrcBuf = false;
    if (priv->input_buf_internal) {
      param.srcBufInternal = true;
      if (priv->cache_gpu_addr & QVCONV_CACHE_GPU_ADDR_INT)
        param.cacheGpuAddrSrcBuf = true;
    } else {
      param.srcBufInternal = false;
      if (priv->cache_gpu_addr & QVCONV_CACHE_GPU_ADDR_EXT)
        param.cacheGpuAddrSrcBuf = true;
    }

    param.cacheGpuAddrDstBuf = false;
    if (priv->output_buf_internal) {
      param.dstBufInternal = true;
      if (priv->cache_gpu_addr & QVCONV_CACHE_GPU_ADDR_INT)
        param.cacheGpuAddrDstBuf = true;
    } else {
      param.dstBufInternal = false;
      if (priv->cache_gpu_addr & QVCONV_CACHE_GPU_ADDR_EXT)
        param.cacheGpuAddrDstBuf = true;
    }

    SG_INFO_OBJ_LITE (qvconv, "c2d param: src cache %u internal %u, dst cache %u internal %u",
        param.cacheGpuAddrSrcBuf, param.srcBufInternal, param.cacheGpuAddrDstBuf, param.dstBufInternal);

    if (priv->do_deinterlace) {
      src.height >>= 1;
      SG_INFO_OBJ_LITE (qvconv, "To do c2d bob deinterlace: will configure c2d source height as %d (half of original)", src.height);
    }
    SG_INFO_OBJ_LITE (qvconv, "configure c2d converter");
    if (!c2d->configure (&src, &dst, &param)) {
      SG_ERR_OBJ_LITE (qvconv, "c2d configure error, inst %p", qvconv);
      goto free_c2d_buf;
    }
  }

  if (flip && !c2d->setFlip(flip))
    SG_ERR_OBJ_LITE (qvconv, "c2d setFlip error, inst %p", qvconv);

  c2d->setSrcCrop(priv->crop.x, priv->crop.y, priv->crop.width, priv->crop.height);

  priv->active = TRUE;

  return TRUE;

free_c2d_buf:
  gst_qvconv_free_c2d_buf(c2d, &priv->input_buffer);

  return FALSE;
}

#ifdef QVCONV_DUMP_C2D_BUFFER
static int
_open_dump_file (const gchar * dir, const gchar * filename)
{
  gchar filepath[PATH_MAX];
  size_t size = sizeof (filepath);
  int fd = -1;
  int flags = O_WRONLY | O_CREAT | O_TRUNC;
  mode_t mode = S_IWUSR | S_IRUSR;

  if (g_strlcpy (filepath, dir, size) >= size)
    goto out;
  if (g_strlcat (filepath, filename, size) >= size)
    goto out;

  GST_DEBUG ("dump file %s, max path len %ld", filepath, size);

  fd = open ((const char *)filepath, flags, mode);
  if (fd < 0) {
    int e = errno;
    SG_ERR_LITE ("%s", strerror (e));
    goto out;
  }
  GST_DEBUG ("dump fd %d", fd);

out:
  return fd;
}

static void
gst_qvconv_dump_c2d (GstQvconv * qvconv)
{
  GstQvconvPrivate *priv = qvconv->priv;
  C2dConverter *c2d = qvconv->c2d_hndl;

  if (!priv) {
    return;
  }
  priv->frame_seqno++;

  if (!c2d)
    return;

  if ((priv->dump_option & DUMP_OPTION_BOTH) == 0)
    return;

  /* don't dump any more if hit error last call */
  if (priv->dump_error)
    return;

  if (1 == priv->frame_seqno)
    GST_DEBUG ("dump option=%u start=%u frames=%u dir=%s",
        priv->dump_option, priv->dump_start,
        priv->dump_frames, priv->dump_dir);

  /* don't dump this frame if it's not the frame user wants */
  if (!(priv->frame_seqno >= priv->dump_start && priv->dumped_frames < priv->dump_frames))
    return;

  /* dump c2d input buffer */
  if (priv->dump_option & DUMP_OPTION_INPUT) {
    if (priv->dump_fd_src == -1) {
      int fd = _open_dump_file (priv->dump_dir, QVCONV_DUMP_FILENAME_IN);
      if (fd < 0) {
        priv->dump_error = TRUE;
        return;
      }
      priv->dump_fd_src = fd;
    }

    if (!c2d->dumpSurface (priv->dump_fd_src, true)) {
      SG_ERR_OBJ_LITE(qvconv, "dump input error, inst %p", qvconv);
      priv->dump_error = TRUE;
    }
  }

  /* dump c2d output buffer */
  if (priv->dump_option & DUMP_OPTION_OUTPUT) {
    if (priv->dump_fd_dst == -1) {
      int fd = _open_dump_file (priv->dump_dir, QVCONV_DUMP_FILENAME_OUT);
      if (fd < 0) {
        priv->dump_error = TRUE;
        return;
      }
      priv->dump_fd_dst = fd;
    }

    if (!c2d->dumpSurface (priv->dump_fd_dst, false)) {
      SG_ERR_OBJ_LITE(qvconv, "dump output error, inst %p", qvconv);
      priv->dump_error = TRUE;
    }
  }

  priv->dumped_frames++;
}
#endif /* QVCONV_DUMP_C2D_BUFFER */

static void
gst_qvconv_destroy_c2d (GstQvconv * qvconv)
{
  C2dConverter *c2d = qvconv->c2d_hndl;
  C2DBuffer *c2d_buf = &qvconv->priv->input_buffer;

  if (c2d) {
    gst_qvconv_free_c2d_buf (c2d, c2d_buf);
    c2d->destroy ();
  }

  qvconv->priv->active = FALSE;
}

static gboolean
gst_qvconv_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstCaps *outcaps;
  GstStructure *config;
  guint size = 0, min = 0, max = 0;
  gboolean update;
  gboolean use_peer_pool = FALSE;
  gboolean has_dmabuf_feature = FALSE;

  GstAllocationParams params = { (GstMemoryFlags) 0 };
  GstBufferPool *pool = NULL;
  GstQvconv *qvconv = GST_QVCONV (trans);
  GstQvconvPrivate *priv = qvconv->priv;

  gst_query_parse_allocation (query, &outcaps, NULL);

  SG_INFO_OBJ (qvconv, "allocation caps %" GST_PTR_FORMAT "", outcaps);

  has_dmabuf_feature = gst_qvconv_caps_has_feature (outcaps, GST_CAPS_FEATURE_MEMORY_DMABUF);

  if (gst_query_get_n_allocation_params (query) > 0)
    gst_query_parse_nth_allocation_param (query, 0, NULL, &params);

  /* check if downstrean proposed a pool , there are 3 conditions here:
   * 1. If downstream proposed DMA based buffer pool, use it for allocation and update the query.
   *    This is the case if downstream is qti plugins like c2 encoder.
   * 2. If downstrean proposed non-DMA based buffer pool, release the pool and use our own buffer
   *    for allocation, then update the query.
   * 3. If downstream does not propose any pool, use our own buffer pool for allocation, then add
   *    it as a new allocation parm into the query */
  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

    SG_INFO_OBJ_LITE (qvconv, "get query from downstream, pool: %p, min: %d, max: %d",
        pool, min, max);

    update = TRUE;
    /* if downstream can provide a DMA based buffer pool and ignore-downstream-pool==false,
     * use it for allocation. Some plugin like waylandsink, declare it could provide DMA pool,
     * actually it doesn't. Some plugin could provide DMA pool, but probably couldn't meet
     * alignment request, hence, use ignore-downstream-pool property to exclude those cases. */
    if (pool) {
      if (!priv->ignore_downstream_pool && has_dmabuf_feature) {
        SG_INFO_OBJ_LITE (qvconv, "use downstream buffer pool: %p", pool);
        use_peer_pool = TRUE;
      } else {
        SG_INFO_OBJ_LITE (qvconv, "unref and ignore peer pool due to ignore-downstream-pool:%u or peer pool dmabuf feature:%u",
            priv->ignore_downstream_pool, has_dmabuf_feature);
        gst_object_unref (pool);
        pool = NULL;
      }
    }
  } else {
    SG_INFO_OBJ_LITE (qvconv, "peer does not propose any pool. use own pool for allocation ");
    update = FALSE;
  }

  if(!use_peer_pool) {
    /* create own buffer pool */
    pool = gst_qvconv_buffer_pool_new (qvconv, has_dmabuf_feature);
    if (!pool)
      goto cleanup;

    SG_INFO_OBJ_LITE (qvconv, "min:%u max:%u size:%u", min, max, size);
    /* consider downstream proposed min/max/size, like input min=33,max=0. */
    if (0 == max) {
      max = MAX (min, QVCONV_MAX_OUTBUFFERS);
    } else { /* downstream proposed max != 0, take it for calculation. */
      max = MAX (max, QVCONV_MIN_OUTBUFFERS);
    }
    min = MAX (min, QVCONV_MIN_OUTBUFFERS);
    size = MAX (size, GST_VIDEO_INFO_SIZE (&qvconv->dst_info));

    config = gst_buffer_pool_get_config (pool);

    SG_INFO_OBJ (qvconv, "allocation: size:%u min:%u max:%u pool:%"
        GST_PTR_FORMAT, size, min, max, pool);

    /* Since we need to link to other gst elements,e.g. filesink for dump,
     * as well encoder element. The return of map may be a va rather than
     * specific structure which contains fd, etc. info. So add extended
     * option to indicate buffer pool to add extbufmeta with fd, etc. into
     * the GstBuffer acquired from own buffer pool for color conversion.
     */
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_EXT_BUFFER_META);

    gst_buffer_pool_config_set_allocator (config, NULL, &params);
    gst_buffer_pool_config_set_params (config, outcaps, size, min, max);

    SG_INFO_OBJ (qvconv, "setting own pool config to %"
        GST_PTR_FORMAT, config);

    /* configure own pool */
    if (!gst_buffer_pool_set_config (pool, config)) {
      SG_ERR_OBJ_LITE (qvconv, "configure our own buffer pool failed, inst %p", qvconv);
      goto cleanup;
    }

    /* For simplicity, simply read back the active configuration, so our base
     * class get the right information */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, NULL, &size, &min, &max);
    gst_structure_free (config);

    priv->output_buf_internal = TRUE;
  } else {
    priv->output_buf_internal = FALSE;
  }

  SG_INFO_OBJ_LITE (qvconv, "setting pool with size: %d, min: %d, max: %d",
      size, min, max);

  /* update pool info in the query */
  if (update)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans, query)) {
    SG_ERR_OBJ_LITE (qvconv, "failed in parent decide_allocation, inst %p", qvconv);
    goto cleanup;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    SG_ERR_OBJ (qvconv, "failed to start buffer pool:%"
        GST_PTR_FORMAT ", inst %" GST_PTR_FORMAT "@%p", pool, qvconv, qvconv);
    goto cleanup;
  }

  if (priv->pool) {
    if (qvconv->c2d_hndl) {
      qvconv->c2d_hndl->clearMappedGpuAddrs ();
    }
    SG_INFO_OBJ_LITE (qvconv, "unref old pool:%p", priv->pool);
    gst_object_unref (priv->pool);
  }

  SG_INFO_OBJ (qvconv, "new pool:%" GST_PTR_FORMAT ", old pool:%"
      GST_PTR_FORMAT ", handled %u/%u frames", pool, priv->pool, priv->idx_in_one_cycle, priv->execute_idx);

  priv->pool = pool;

  return TRUE;

cleanup:
  if (pool) {
    gst_object_unref (pool);
    pool = NULL;
  }
  if (priv->pool) {
    gst_object_unref (priv->pool);
    priv->pool = NULL;
  }

  return FALSE;
}

void
gst_qvconv_dispose (GObject * object)
{
  GstQvconv *qvconv = GST_QVCONV (object);

  SG_INFO_OBJ_LITE (qvconv, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_qvconv_parent_class)->dispose (object);
}

void
gst_qvconv_finalize (GObject * object)
{
  GstQvconv *qvconv = GST_QVCONV (object);
  GstQvconvPrivate *priv = qvconv->priv;

  SG_INFO_OBJ (qvconv, "finalizing instance %" GST_PTR_FORMAT "@%p", qvconv, qvconv);

  if (qvconv->c2d_hndl != NULL) {
    delete (qvconv->c2d_hndl);
    qvconv->c2d_hndl = NULL;
  }

  if (priv == NULL) {
    SG_ERR_OBJ_LITE (qvconv, "priv is NULL in finalize(), inst %p!", qvconv);
    goto PARENT_FINALIZE;
  }

  SG_INFO_OBJ_LITE (qvconv, "%s: handled %u frames", __func__, priv->execute_idx);

#ifdef QVCONV_DUMP_C2D_BUFFER
  if (priv->dump_fd_src >= 0) {
    close (priv->dump_fd_src);
    priv->dump_fd_src = -1;
  }
  if (priv->dump_fd_dst >= 0) {
    close (priv->dump_fd_dst);
    priv->dump_fd_dst = -1;
  }
  if (priv->dump_dir) {
    g_free ((gpointer) priv->dump_dir);
    priv->dump_dir = NULL;
  }
#endif

  g_mutex_clear (&priv->lock);

  /* clean up object here */
PARENT_FINALIZE:
  G_OBJECT_CLASS (gst_qvconv_parent_class)->finalize (object);
}

static gboolean
gst_qvconv_start (GstBaseTransform * trans)
{
  GstQvconv *qvconv = GST_QVCONV (trans);
  GstQvconvPrivate *priv = qvconv->priv;

  priv->idx_in_one_cycle = 0;

  SG_INFO_OBJ (qvconv, "start instance %" GST_PTR_FORMAT "@%p, handled %u/%u frames, c2d inst=%p",
      qvconv, qvconv, priv->idx_in_one_cycle, priv->execute_idx, qvconv->c2d_hndl);

  return TRUE;
}

static gboolean
gst_qvconv_stop (GstBaseTransform * trans)
{
  GstQvconv *qvconv = GST_QVCONV (trans);
  GstQvconvPrivate *priv = qvconv->priv;
  C2dConverter *c2d = qvconv->c2d_hndl;

  gst_qvconv_destroy_c2d (qvconv);

  if (priv->pool) {
    GST_DEBUG_OBJECT (qvconv, "deactivating pool: %p", priv->pool);
    gst_buffer_pool_set_active (priv->pool, FALSE);
    gst_object_unref (priv->pool);
    priv->pool = NULL;
  }

  SG_INFO_OBJ (qvconv, "stop instance %" GST_PTR_FORMAT "@%p, handled %u/%u frames, c2d inst=%p",
      qvconv, qvconv, priv->idx_in_one_cycle, priv->execute_idx, c2d);

  return TRUE;
}

/*the info refer to input or output of c2d, not plugin's input*/
static gboolean
gst_qvconv_align_info (GstQvconv * qvconv, GstVideoInfo * info, const GstVideoMeta * meta, gboolean isubwc)
{
  GstVideoFormat format;
  gint width, height, stride0, stride1;
  gsize offset0, offset1;

  g_return_val_if_fail (qvconv != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  format  = GST_VIDEO_INFO_FORMAT (info);
  width   = GST_VIDEO_INFO_WIDTH (info);
  height  = GST_VIDEO_INFO_HEIGHT (info);
  stride0 = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
  stride1 = GST_VIDEO_INFO_PLANE_STRIDE (info, 1);
  offset0 = GST_VIDEO_INFO_PLANE_OFFSET (info, 0);
  offset1 = GST_VIDEO_INFO_PLANE_OFFSET (info, 1);

  SG_INFO_OBJ_LITE (qvconv, "GstVideoInfo: format %s-%d,width %d,height %d,"
      "stride0 %d,stride1 %d,offset0 %lu,offset1 %lu,size %lu, qvconv %p",
      GST_VIDEO_INFO_NAME (info), format, width, height,
      stride0, stride1, offset0, offset1, GST_VIDEO_INFO_SIZE (info), qvconv);

  /* GstVideoMeta overrides GstVideoInfo. */
  if (meta) {
    g_return_val_if_fail (meta->format == format, FALSE);

    width = (gint)meta->width;
    height = (gint)meta->height;
    stride0 = meta->stride[0];
    stride1 = meta->stride[1];
    offset0 = meta->offset[0];
    offset1 = meta->offset[1];

    SG_INFO_OBJ_LITE (qvconv, "GstVideoMeta: format %d,width %u,height %u,"
        "stride0 %d,stride1 %d,offset0 %lu,offset1 %lu",
        meta->format, width, height, stride0, stride1, offset0, offset1);
  }

  /*Currently C2dConverter ONLY supports offset0 == 0 && stride0 == stride1. */
  g_return_val_if_fail (0 == offset0, FALSE);
  if (GST_VIDEO_FORMAT_NV12 == format)
    g_return_val_if_fail (stride0 == stride1, FALSE);

  //input/output info is for c2d, should use c2d's method to calculate stride/offset information.
  //Those stride info. will be used when copying data from plugin's input buf to c2d's input buf, also be used when
  //attaching Video Meta on output gst buffer. The GST_VIDEO_INFO_SIZE will be used to set output gst buf's size,
  //it only contain valid pixel data, not contain padding data which is used to meet 4k alignment.
  switch (format) {
    case GST_VIDEO_FORMAT_UYVY:
      if (!meta) {
        stride0 = ALIGN (width * 2, ALIGN64);
        SG_INFO_OBJ_LITE (qvconv, "UYVY stride0=%d", stride0);
      }
      GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = stride0;
      // only consider valid data, not including padding data for 4k alignment
      GST_VIDEO_INFO_SIZE (info) = stride0 * height;
      SG_INFO_OBJ_LITE (qvconv, "UYVY stride0=%d,size=%lu", stride0, GST_VIDEO_INFO_SIZE (info));
      break;
    case GST_VIDEO_FORMAT_NV12:
      if (!isubwc) {
        if (!meta) {
          gint stride_w, stride_h;

          stride_w = VENUS_Y_STRIDE(COLOR_FMT_NV12, width);
          stride_h = VENUS_Y_SCANLINES(COLOR_FMT_NV12, height);
          GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = stride_w;
          GST_VIDEO_INFO_PLANE_STRIDE (info, 1) = stride_w;
          GST_VIDEO_INFO_PLANE_OFFSET (info, 1) = stride_w * stride_h;
          GST_VIDEO_INFO_SIZE (info) = stride_w * stride_h + (stride_w * stride_h >>1);
          SG_INFO_OBJ_LITE (qvconv, "NV12 stride_w=%d,stride_h=%d", stride_w, stride_h);
        } else {
          gint uv_h;

          /* Use stride0, stride1, height and offset1 from meta. */
          GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = stride0;
          GST_VIDEO_INFO_PLANE_STRIDE (info, 1) = stride1;
          GST_VIDEO_INFO_PLANE_OFFSET (info, 1) = offset1;
          /* Refer to gst fill_planes() for size calculation. */
          uv_h = GST_ROUND_UP_2 (height) >> 1;
          GST_VIDEO_INFO_SIZE (info) = offset1 + stride1 * uv_h;
        }
        SG_INFO_OBJ_LITE (qvconv, "NV12 stride0=%d,offset1=%lu,size=%lu",
            GST_VIDEO_INFO_PLANE_STRIDE (info, 0),
            GST_VIDEO_INFO_PLANE_OFFSET (info, 1), GST_VIDEO_INFO_SIZE (info));
      } else {
        //Currently, decoder output frame only dump valid data range of NV12 ubwc buffer through filesink.
        //Then, just consider VENUS_BUFFER_SIZE_USED data here. Stride for UBWC fmt is meaningless.
        GST_VIDEO_INFO_SIZE (info) = VENUS_BUFFER_SIZE_USED(COLOR_FMT_NV12_UBWC, width, height, GST_VIDEO_INFO_IS_INTERLACED(info)?1:0);
        if (!meta) {
          gint stride_w, stride_h;

          stride_w = VENUS_Y_STRIDE(COLOR_FMT_NV12_UBWC, width);
          stride_h = VENUS_Y_SCANLINES(COLOR_FMT_NV12_UBWC, height);
          GST_VIDEO_INFO_PLANE_STRIDE(info, 0) = stride_w;
          GST_VIDEO_INFO_PLANE_STRIDE(info, 1) = stride_w;
          if (GST_VIDEO_INFO_IS_INTERLACED(info)) {
            GST_VIDEO_INFO_PLANE_OFFSET (info, 1) = VENUS_BUFFER_SIZE(COLOR_FMT_NV12_UBWC, width, height) / 2;
          }else{
            gint stride_w_ymeta, stride_h_ymeta, plane_sz_ymeta, plane_sz_y;
            stride_w_ymeta = VENUS_Y_META_STRIDE(COLOR_FMT_NV12_UBWC, width);
            stride_h_ymeta = VENUS_Y_META_SCANLINES(COLOR_FMT_NV12_UBWC, height);
            plane_sz_ymeta = GST_ROUND_UP_N(stride_w_ymeta * stride_h_ymeta, 4096);
            plane_sz_y = GST_ROUND_UP_N(stride_w * stride_h, 4096);
            GST_VIDEO_INFO_PLANE_OFFSET (info, 1) = plane_sz_ymeta + plane_sz_y;
          }
          SG_INFO_OBJ_LITE (qvconv, "NV12 ubwc stride_w=%d,stride_h=%d,offset[1]=%d", stride_w, stride_h, GST_VIDEO_INFO_PLANE_OFFSET (info, 1));
        } else {
          /* Use stride0, stride1 and offset1 from meta. */
          GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = stride0;
          GST_VIDEO_INFO_PLANE_STRIDE (info, 1) = stride1;
          GST_VIDEO_INFO_PLANE_OFFSET (info, 1) = offset1;
        }
        SG_INFO_OBJ_LITE (qvconv, "NV12 ubwc stride0=%d,offset1=%lu,size=%lu",
            GST_VIDEO_INFO_PLANE_STRIDE (info, 0),
            GST_VIDEO_INFO_PLANE_OFFSET (info, 1), GST_VIDEO_INFO_SIZE (info));
      }
      break;
    case GST_VIDEO_FORMAT_ARGB://It's a workaround, gbm/gfx have no support for GBM_FORMAT_BGRA8888(=GST ARGB=ARGB8888), then, alloc/use GBM_FORMAT_ABGR8888(=GST RGBA=RGBA8888=ADRENO_PIXELFORMAT_R8G8B8A8) buffer
      GST_DEBUG_OBJECT (qvconv, "GST_VIDEO_FORMAT_ARGB case, reuse GST_VIDEO_FORMAT_RGBA handling");
    case GST_VIDEO_FORMAT_RGBA:
      if (!meta) {
        gint stride_w, stride_h;
        int c2d_format = GST_VIDEO_FORMAT_RGBA == format ? RGBA8888 : ARGB8888;

        computeFormatAlignedWidthHeight(width, height,
            c2d_format, &stride_w, &stride_h);
        SG_INFO_OBJ_LITE (qvconv, "RGBA/ARGB stride_w=%d,stride_h=%d", stride_w, stride_h);

        stride_w = stride_w * 4;
        GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = stride_w;
        GST_VIDEO_INFO_SIZE (info) = stride_w * stride_h;
      } else {
        /* Use stride0 and height from meta. */
        GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = stride0;
        GST_VIDEO_INFO_SIZE (info) = stride0 * height;
      }
      SG_INFO_OBJ_LITE (qvconv, "RGBA/ARGB stride0=%d,size=%lu",
          GST_VIDEO_INFO_PLANE_STRIDE (info, 0), GST_VIDEO_INFO_SIZE (info));
      break;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB:
      if (!meta) {
        gint stride_w, stride_h;
        int c2d_format = GST_VIDEO_FORMAT_RGB == format ? RGB888 : BGR888;

        computeFormatAlignedWidthHeight(width, height,
            c2d_format, &stride_w, &stride_h);
        SG_INFO_OBJ_LITE (qvconv, "RGB/BGR stride_w=%d,stride_h=%d", stride_w, stride_h);

        stride_w = stride_w * 3;
        GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = stride_w;
        GST_VIDEO_INFO_SIZE (info) = stride_w * stride_h;
      } else {
        /* Use stride0 and height from meta. */
        GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = stride0;
        GST_VIDEO_INFO_SIZE (info) = stride0 * height;
      }
      SG_INFO_OBJ_LITE (qvconv, "RGB/BGR stride0=%d,size=%lu",
          GST_VIDEO_INFO_PLANE_STRIDE (info, 0), GST_VIDEO_INFO_SIZE (info));
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
        if (!meta) {
          gint stride_w, stride_h;

          stride_w = VENUS_Y_STRIDE(COLOR_FMT_P010, width);
          stride_h = VENUS_Y_SCANLINES(COLOR_FMT_P010, height);
          GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = stride_w;
          GST_VIDEO_INFO_PLANE_STRIDE (info, 1) = stride_w;
          GST_VIDEO_INFO_PLANE_OFFSET (info, 1) = stride_w * stride_h;
          GST_VIDEO_INFO_SIZE (info) = stride_w * stride_h + (stride_w * stride_h >>1);
          SG_INFO_OBJ_LITE (qvconv, "P010 stride_w=%d,stride_h=%d", stride_w, stride_h);
        } else {
          gint uv_h;

          /* Use stride0, stride1, height and offset1 from meta. */
          GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = stride0;
          GST_VIDEO_INFO_PLANE_STRIDE (info, 1) = stride1;
          GST_VIDEO_INFO_PLANE_OFFSET (info, 1) = offset1;
          /* Refer to gst fill_planes() for size calculation. */
          uv_h = GST_ROUND_UP_2 (height) >> 1;
          GST_VIDEO_INFO_SIZE (info) = offset1 + stride1 * uv_h;
        }
        SG_INFO_OBJ_LITE (qvconv, "P010 stride0=%d,offset1=%lu,size=%lu",
            GST_VIDEO_INFO_PLANE_STRIDE (info, 0),
            GST_VIDEO_INFO_PLANE_OFFSET (info, 1), GST_VIDEO_INFO_SIZE (info));
    break;
    default:
      SG_ERR_OBJ_LITE (qvconv, "unsupport format when calculate gst video info layout %d(%s), inst %p", format, GST_VIDEO_INFO_NAME(info), qvconv);
      return FALSE;
  }

  SG_INFO_OBJ_LITE (qvconv, "c2d in/gst out: format %s,width %d,height %d,"
      "stride0 %d,offset1 %lu,size %lu",
      GST_VIDEO_INFO_NAME (info), width, height,
      GST_VIDEO_INFO_PLANE_STRIDE (info, 0),
      GST_VIDEO_INFO_PLANE_OFFSET (info, 1), GST_VIDEO_INFO_SIZE (info));

  return TRUE;
}

static gboolean
gst_qvconv_match_color_type (const GstQvconv *qvconv, GstVideoFormat format,
    ColorConvertFormat *c2d_format, gboolean ubwc)
{
  switch (format) {
    case GST_VIDEO_FORMAT_UYVY:
        *c2d_format = CbYCrY;
        break;
    case GST_VIDEO_FORMAT_NV12:
         if (ubwc)
           *c2d_format = NV12_UBWC;
         else
           *c2d_format = NV12_128m;
        break;
    case GST_VIDEO_FORMAT_RGBA:
         *c2d_format = RGBA8888;
        break;
    case GST_VIDEO_FORMAT_ARGB:
         *c2d_format = ARGB8888;
        break;
    case GST_VIDEO_FORMAT_BGR:
        *c2d_format = BGR888;
        break;
    case GST_VIDEO_FORMAT_RGB:
        *c2d_format = RGB888;
        break;
    case GST_VIDEO_FORMAT_P010_10LE:
        *c2d_format = VENUS_P010;
        break;
    default:
        SG_ERR_OBJ_LITE (qvconv, "cannot find match c2d color type \
            for %s, inst %p", gst_video_format_to_string (format), qvconv);
        return FALSE;
  }

  return TRUE;
}

static gboolean
gst_qvconv_do_buffer_copy (GstBaseTransform * trans, C2DBuffer * c2d_input_buffer,
    guint8 * input_ptr, const GstVideoInfo * src_info, const GstVideoMeta * gvmeta, gboolean ubwc)
{
  gint width, height, c2d_in_stride, c2d_in_offset, h, plugin_in_stride, plugin_in_offset;
  GstVideoFormat input_format;

  GstQvconv *qvconv = GST_QVCONV (trans);

  /*src_info here refer to input of c2d, not plugin's input, it's dst of copy*/
  input_format = GST_VIDEO_INFO_FORMAT(src_info);
  width = GST_VIDEO_INFO_WIDTH (src_info);
  height = GST_VIDEO_INFO_HEIGHT (src_info);
  c2d_in_stride = GST_VIDEO_INFO_PLANE_STRIDE (src_info, 0);
  c2d_in_offset = GST_VIDEO_INFO_PLANE_OFFSET (src_info, 1);  //only meaningful for multiple plane format, like NV12

  //Update copy src stride/offset from GstVideoMeta on plugin sinkpad's gstbuffer at first. If no GstVideoMeta, let them be -1 and will calculate according to GST default rule later.
  plugin_in_stride = gvmeta != NULL ? gvmeta->stride[0] : -1;  //only consider RGB space and NV12/P010, for NV12/P010, uv's stride is equal to y's stride
  plugin_in_offset = gvmeta != NULL ? gvmeta->offset[1] : -1;  //only meaningful for multiple plane format, like NV12/P010
  GST_DEBUG_OBJECT (qvconv, "src_info format %s,width %d,height %d,copy dst stride %d,copy dst uv offset %d,copy src stride %d,copy src uv offset %d",
            GST_VIDEO_INFO_NAME(src_info), width, height, c2d_in_stride, c2d_in_offset, plugin_in_stride, plugin_in_offset);

  switch (input_format) {
    case GST_VIDEO_FORMAT_UYVY:
      if (plugin_in_stride == -1) {
        plugin_in_stride = width << 1;
      }
      for (h = 0; h < height; h++) {
        memcpy ((guint8 *) c2d_input_buffer->ptr + h * c2d_in_stride,
            input_ptr + h * plugin_in_stride, 2 * width);
      }
      break;
    case GST_VIDEO_FORMAT_NV12:
      if (!ubwc) {
        if (plugin_in_stride == -1) {
          plugin_in_stride = GST_ROUND_UP_4(width);//for sw gst plugin, NV12's stride = GST_ROUND_UP_4(width), refer to video-info.c:fill_planes()
          plugin_in_offset = plugin_in_stride * height;
        }
        for (h = 0; h < height; h++) {
          memcpy ((guint8 *) c2d_input_buffer->ptr + h * c2d_in_stride,
              input_ptr + h * plugin_in_stride, width);
        }
        for (h = 0; h < height/2; h++) {
          memcpy ((guint8 *) c2d_input_buffer->ptr + c2d_in_offset + h * c2d_in_stride,
              input_ptr + plugin_in_offset + h * plugin_in_stride,
              width);
        }
      } else {
        memcpy((guint8 *)c2d_input_buffer->ptr, input_ptr, GST_VIDEO_INFO_SIZE(src_info));
      }
      break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_ARGB:
      if (plugin_in_stride == -1) {
        plugin_in_stride = width << 2;
      }
      for (h = 0; h < height; h++) {
        memcpy ((guint8 *) c2d_input_buffer->ptr + h * c2d_in_stride,
            input_ptr + h * plugin_in_stride, 4 * width);
      }
      break;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB:
      if (plugin_in_stride == -1) {
        /*stride in plane info is GST_ROUND_UP_4 (width * 3);*/
        plugin_in_stride = GST_ROUND_UP_4(3 * width);
      }
      for (h = 0; h < height; h++) {
        memcpy ((guint8 *) c2d_input_buffer->ptr + h * c2d_in_stride,
            input_ptr + h * plugin_in_stride, 3 * width);
      }
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      if (plugin_in_stride == -1) {
        plugin_in_stride = GST_ROUND_UP_4(width*2);
        plugin_in_offset = plugin_in_stride * height;
      }
      for (h = 0; h < height; h++) {
        memcpy ((guint8 *) c2d_input_buffer->ptr + h * c2d_in_stride,
            input_ptr + h * plugin_in_stride, width<<1);
      }
      for (h = 0; h < height/2; h++) {
        memcpy ((guint8 *) c2d_input_buffer->ptr + c2d_in_offset + h * c2d_in_stride,
            input_ptr + plugin_in_offset + h * plugin_in_stride,
            width<<1);
        }
      break;
    default:
      SG_ERR_OBJ_LITE (qvconv, "unsupport format %s when copy plugin's input gst buf to c2d input buf, inst %p",
          gst_video_format_to_string (input_format), qvconv);
      return FALSE;
  }

  return TRUE;
}

static gboolean
_validate_crop_setting (const GstQvconvCrop * crop,
    const GstVideoInfo * info, gboolean * need_crop)
{
  gboolean valid_x, valid_y;
  guint width  = (guint)GST_VIDEO_INFO_WIDTH (info);
  guint height = (guint)GST_VIDEO_INFO_HEIGHT (info);

  *need_crop = FALSE;

  GST_DEBUG ("crop x:w:y:h: %u:%u:%u:%u, source w:h: %u:%u",
      crop->x, crop->width, crop->y, crop->height, width, height);

  /* default all zero means not to crop, and it's valid */
  if (!crop->width && !crop->height && !crop->x && !crop->y)
    return TRUE;

  /* crop wxh same as source wxh, notify user not to set as this */
  if (crop->width == width && crop->height == height) {
    SG_ERR_LITE ("invalid crop wxh same as source wxh!");
    return FALSE;
  }

  valid_x = crop->width > 0  && crop->x + crop->width  <= width;
  valid_y = crop->height > 0 && crop->y + crop->height <= height;

  GST_DEBUG ("crop valid x:y: %u:%u", valid_x, valid_y);

  if (valid_x && valid_y) {
    *need_crop = TRUE;
    return TRUE;
  } else {
    return FALSE;
  }
}

static gboolean
gst_qvconv_set_caps (GstBaseTransform * trans, GstCaps * incaps,
        GstCaps * outcaps)
{
  GstVideoFilterClass *fclass;
  GstVideoInfo in_info, out_info;
  gboolean res, pass_through, need_crop;

  GstVideoFilter *filter = GST_VIDEO_FILTER_CAST (trans);
  GstQvconv *qvconv = GST_QVCONV (trans);
  GstQvconvPrivate *priv = qvconv->priv;

  if (!gst_video_info_from_caps (&in_info, incaps))
    goto invalid_caps;

  if (!gst_video_info_from_caps (&out_info, outcaps))
    goto invalid_caps;

  if (!_validate_crop_setting (&priv->crop, &in_info, &need_crop))
    goto invalid_crop;

  /* if the caps are equal and no flip or crop set, then pass through */
  pass_through = gst_caps_is_equal (incaps, outcaps) &&
      priv->method == METHOD_NONE && !need_crop && priv->ignore_downstream_pool;
  gst_base_transform_set_passthrough (trans, pass_through);

  SG_INFO_OBJ_LITE (qvconv, "pass_through: %u, flip: %d, need_crop: %u, ignore_downstream_pool: %u",
      pass_through, priv->method, need_crop, priv->ignore_downstream_pool);

  fclass = GST_VIDEO_FILTER_GET_CLASS (filter);
  if (fclass->set_info)
    res = fclass->set_info (filter, incaps, &in_info, outcaps, &out_info);
  else
    res = TRUE;

  return res;

  /* ERRORS */
invalid_caps:
  {
    SG_ERR_OBJ_LITE (qvconv, "invalid caps, inst %p", qvconv);
    g_warn_if_fail (FALSE && "invalid caps");
    return FALSE;
  }
invalid_crop:
  {
    SG_ERR_OBJ_LITE (qvconv, "invalid crop, inst %p", qvconv);
    g_warn_if_fail (FALSE && "invalid crop");
    return FALSE;
  }
}

static gboolean
gst_qvconv_set_info (GstVideoFilter * filter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstQvconv *qvconv = GST_QVCONV (filter);
  gboolean is_input_ubwc = FALSE;
  GstQvconvPrivate *priv = qvconv->priv;

  GST_DEBUG_OBJECT (qvconv, "set_info");

  g_warn_if_fail(!GST_VIDEO_INFO_IS_INTERLACED(out_info) && "plugin OUT info/cap have interlace feature, have not verified such case!");

  if (in_info->interlace_mode != out_info->interlace_mode) {
    if (GST_VIDEO_INFO_IS_INTERLACED(in_info) && !GST_VIDEO_INFO_IS_INTERLACED(out_info) &&
      gst_qvconv_caps_has_compression(incaps, "ubwc") && GST_VIDEO_INFO_FORMAT(in_info)==GST_VIDEO_FORMAT_NV12) {
      is_input_ubwc = TRUE;
      priv->do_deinterlace = TRUE;
      SG_INFO_OBJ_LITE (qvconv, "input is interlace(mode %d) nv12 ubwc, output isn't interlace(mode %d), will do deinterlace!", in_info->interlace_mode, out_info->interlace_mode);
      if (priv->crop.x | priv->crop.y | priv->crop.width | priv->crop.height) {
        g_warn_if_fail(FALSE && "deinterlace couldn't work with crop !!!");
        SG_ERR_OBJ_LITE (qvconv, "Not support deinterlace with crop, inst %p!", qvconv);
        return FALSE;
      }
    }else{
      /* if present, these must match too */
      goto format_mismatch;
    }
  }

  GST_DEBUG ("reconfigured %d %d", GST_VIDEO_INFO_FORMAT (in_info),
      GST_VIDEO_INFO_FORMAT (out_info));

  SG_INFO_OBJ (qvconv, "gst_qvconv_set_info(%p) in caps %" GST_PTR_FORMAT "", qvconv, incaps);
  SG_INFO_OBJ (qvconv, "gst_qvconv_set_info(%p) out caps %" GST_PTR_FORMAT "", qvconv, outcaps);
  if (is_input_ubwc || gst_qvconv_caps_has_compression (incaps, "ubwc")) {
    priv->input_buffer.ubwc_flags = TRUE;
  }
  if (gst_qvconv_caps_has_compression (outcaps, "ubwc")) {
    priv->outubwc = TRUE;
  }

  /* get alignment require for c2d */
  if (!gst_qvconv_align_info (qvconv, in_info, NULL, priv->input_buffer.ubwc_flags))
    return FALSE;
  if (!gst_qvconv_align_info (qvconv, out_info, NULL, priv->outubwc))
    return FALSE;

  qvconv->src_info = *in_info;
  qvconv->dst_info = *out_info;

  return TRUE;

  /* ERRORS */
format_mismatch:
  {
    SG_ERR_OBJ_LITE (qvconv, "input and output formats do not match, inst %p", qvconv);
    return FALSE;
  }
}

static gboolean
gst_qvconv_do_convert (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  C2dConverter *c2d;
  C2DBuffer c2d_input_buffer;
  C2DBuffer c2d_output_buffer;
  GstVideoMeta* QGVMeta = NULL;
  GstQvconvExtBufMeta *meta;
  gint input_ion_fd;
  guint input_ion_offset = 0, input_ion_size = 0, fd_memory_size = 0;
  GstMemory *out_mem;
  C2DBuffer *c2d_buffer;
  GstQvconvMemory *memory;
  gint output_data_fd = -1, output_meta_fd = -1;
  gsize output_data_size = 0;

  void *input_ion_ptr = NULL;
  GstMemory *in_mem = NULL;
  GstMapInfo map_info;
  gboolean need_copy = FALSE;
  gboolean ret = FALSE;
  GstMemory *mem;

  GstQvconv *qvconv = GST_QVCONV (trans);
  GstQvconvPrivate *priv = qvconv->priv;
  GstVideoInfo *src_info = &qvconv->src_info;

  c2d = qvconv->c2d_hndl;

  /* For the input buffer source, currently consider two cases here
   * 1. From decoder or camera or other HW, no memcpy, C2D reuse upstream
   *    plugin pushed buffer as input.
   * 2. If we judge upstream plugin is SW plugin , i.e. videotestsrc or
   *    filesrc, we do a buffer copy and then convert.
   */
  QGVMeta = gst_buffer_get_video_meta (inbuf);
#define SIG_OF_QVMETA(vmeta)	(unsigned int)((vmeta)->offset[2])
#define DATASZ_OF_QVMETA(vmeta)	(unsigned int)((vmeta)->offset[3])
#define FD_OF_QVMETA(vmeta)		(int)((vmeta)->stride[2])
#define METAFD_OF_QVMETA(vmeta)	(int)((vmeta)->stride[3])
  if (gst_buffer_n_memory (inbuf) && gst_is_dmabuf_memory (gst_buffer_peek_memory (inbuf, 0))) {
    if (!priv->do_inputcopy) {
      in_mem = gst_buffer_get_memory (inbuf, 0);
      gst_memory_map (in_mem, &map_info, GST_MAP_READ);
      c2d_input_buffer.fd = gst_dmabuf_memory_get_fd (in_mem);
      c2d_input_buffer.ptr = map_info.data;
      input_ion_size = map_info.size;
      input_ion_offset = 0;
      GST_DEBUG_OBJECT (qvconv, "qvconv input dmabuf fd memory %d %p %u, GstMemory %p, no need memcpy", c2d_input_buffer.fd, c2d_input_buffer.ptr, input_ion_size, in_mem);
    } else {
      need_copy = TRUE;
      GST_DEBUG_OBJECT (qvconv, "qvconv input is dmabuf, as prop. inputcopy is true, still do memcpy");
    }
   } else if (QGVMeta && QGVMeta->n_planes <= 2 && SIG_OF_QVMETA(QGVMeta) == GST_MAKE_FOURCC('Q','a','U','T')) {
    if (!priv->do_inputcopy) {
      input_ion_fd = FD_OF_QVMETA(QGVMeta);
      input_ion_size = DATASZ_OF_QVMETA(QGVMeta);
      input_ion_offset = 0;
      input_ion_ptr = mmap(NULL, input_ion_size, PROT_READ|PROT_WRITE, MAP_SHARED,
          input_ion_fd, input_ion_offset);
      c2d_input_buffer.fd = input_ion_fd;
      c2d_input_buffer.ptr = input_ion_ptr;
      GST_DEBUG_OBJECT (qvconv, "qvconv input gstbuf has special meta %d %p %u, no need memcpy", input_ion_fd, input_ion_ptr, input_ion_size);
    } else {
      need_copy = TRUE;
      GST_DEBUG_OBJECT (qvconv, "qvconv input gstbuf has special meta, as prop. inputcopy is true, still need memcpy");
    }
  } else {
    GST_DEBUG_OBJECT(qvconv, "Not DMA buffer, do memcpy");
    need_copy = TRUE;
    priv->input_nondma = TRUE;
  }

  if (!priv->active) {
    SG_INFO_OBJ (qvconv, "first buffer(%p) comes, configure c2d inst=%p, meta=%p, instance %"
        GST_PTR_FORMAT "@%p, input need copy %d, in_mem %p, force inputcopy %d, PTS: %" GST_TIME_FORMAT, inbuf, c2d, QGVMeta, qvconv, qvconv, (int)need_copy, in_mem, (int)priv->do_inputcopy, GST_TIME_ARGS(GST_BUFFER_PTS(inbuf)));
    /* GstVideoMeta overrides GstVideoInfo for v4l2src->qvconv case.
     * v4l2src gets stride from QC camera driver, and stride can be configured
     * as no padding or as GBM-aligned with padding.
     * For copy case, upstream plugin pushed memory and meta are only for SW,
     * those stride&alignment probably are not suitable for c2d HW input buffer.
     * Then, needn't update src_info for copy case.*/
    if (QGVMeta && !need_copy)
      if (!gst_qvconv_align_info (qvconv, src_info, QGVMeta, priv->input_buffer.ubwc_flags)) {
        SG_ERR_OBJ_LITE (qvconv, "align src info and meta error, inst %p", qvconv);
        goto exit;
      }

    if (need_copy)
      priv->input_buf_internal = TRUE;

    if (!gst_qvconv_configure_c2d (qvconv)) {
      SG_ERR_OBJ_LITE (qvconv, "configure c2d error, inst %p", qvconv);
      goto exit;
    }
  } else {
    /* Input buffer type MUST NOT change. */
    if (need_copy != priv->input_buf_internal) {
      SG_ERR_OBJ (qvconv, "Fatal error: Can NOT handle input buffer changed from %s, at idx %u, inst %" GST_PTR_FORMAT "@%p",
          need_copy ? "dmabuf to non-dmabuf" : "non-dmabuf to dmabuf", priv->idx_in_one_cycle, qvconv, qvconv);
      goto exit;
    }
  }

  if(need_copy) {
    /* allocated in gst_qvconv_configure_c2d(). */
    c2d_input_buffer = priv->input_buffer;
    gst_buffer_map (inbuf, &map_info, GST_MAP_READ);
    if (!gst_qvconv_do_buffer_copy (trans, &c2d_input_buffer, map_info.data,
        src_info, QGVMeta, priv->input_buffer.ubwc_flags)) {
      gst_buffer_unmap (inbuf, &map_info);
      goto exit;
    } else {
      gst_buffer_unmap (inbuf, &map_info);
    }
  }

  meta = gst_buffer_get_qvconv_extbuf_meta (outbuf);
  if (!meta) {
    GST_DEBUG_OBJECT (qvconv, "No ext buf meta in buf %p", outbuf);

    out_mem = gst_buffer_get_memory (outbuf, 0);
    GST_LOG_OBJECT (qvconv, "Gst outbuf %p, GstMemory %p when outbuf from downstream", outbuf, out_mem);
    if (!out_mem) {
      SG_ERR_OBJ_LITE (qvconv, "No gst memory in buffer: %p", outbuf);
      goto exit;
    }

    if(gst_is_fd_memory(out_mem)) {
      gsize offset;
      gsize maxsize;

      c2d_output_buffer.fd = gst_fd_memory_get_fd (out_mem);

      fd_memory_size = gst_memory_get_sizes (out_mem, &offset, &maxsize);

      if (fd_memory_size > 0) {
        c2d_output_buffer.ptr = mmap(NULL, fd_memory_size, PROT_READ|PROT_WRITE, MAP_SHARED,
            c2d_output_buffer.fd, 0);
        c2d_output_buffer.size = fd_memory_size;

        GST_DEBUG_OBJECT (qvconv, "get gstfdmemory, fd: %d, size: %d, va: %p",
          c2d_output_buffer.fd, fd_memory_size, c2d_output_buffer.ptr);
      } else {
        SG_ERR_OBJ_LITE (qvconv, "Incorrect memory size for buffer: %p", outbuf);
        gst_memory_unref (out_mem);
        goto exit;
      }
    } else {
      SG_ERR_OBJ_LITE (qvconv, "Cannot find fd in memory %p !", out_mem);
      gst_memory_unref (out_mem);
      goto exit;
    }
    gst_memory_unref (out_mem);
  } else {
    c2d_output_buffer.fd = meta->fd;
    c2d_output_buffer.ptr = meta->ptr;
  }

#define HEARTBEAT_LOG_PERIOD      256  //must be 2^n
#define HEARTBEAT_LOG_STARTRANGE  3
  if ((priv->idx_in_one_cycle & (HEARTBEAT_LOG_PERIOD-1)) == 0 || priv->idx_in_one_cycle < HEARTBEAT_LOG_STARTRANGE) {
    SG_INFO_OBJ (qvconv, "do converting, inbuf=%p, input fd: %d, input ptr: %p, input offset: %d "
        "outbuf=%p, output fd: %d, output ptr: %p, inst %" GST_PTR_FORMAT "@%p, idx %u/%u, c2d inst=%p, PTS: %" GST_TIME_FORMAT, inbuf, c2d_input_buffer.fd, c2d_input_buffer.ptr, input_ion_offset,
        outbuf, c2d_output_buffer.fd, c2d_output_buffer.ptr, qvconv, qvconv, priv->idx_in_one_cycle, priv->execute_idx, c2d, GST_TIME_ARGS(GST_BUFFER_PTS(inbuf)));
  } else {
    GST_DEBUG_OBJECT (qvconv, "do converting, inbuf=%p, input fd: %d, input ptr: %p, input offset: %d "
        "outbuf=%p, output fd: %d, output ptr: %p, inst %" GST_PTR_FORMAT "@%p, idx %u/%u, c2d inst=%p, PTS: %" GST_TIME_FORMAT, inbuf, c2d_input_buffer.fd, c2d_input_buffer.ptr, input_ion_offset,
        outbuf, c2d_output_buffer.fd, c2d_output_buffer.ptr, qvconv, qvconv, priv->idx_in_one_cycle, priv->execute_idx, c2d, GST_TIME_ARGS(GST_BUFFER_PTS(inbuf)));
  }

  if (!c2d->convert (c2d_input_buffer.fd, c2d_input_buffer.ptr,
      (void *)((guint8 *)c2d_input_buffer.ptr + input_ion_offset),
      c2d_output_buffer.fd, c2d_output_buffer.ptr, c2d_output_buffer.ptr)) {
    SG_ERR_OBJ_LITE (qvconv, "conversion failed");
    goto exit;
  }

  if ((priv->idx_in_one_cycle & (HEARTBEAT_LOG_PERIOD-1)) == 0 || priv->idx_in_one_cycle < HEARTBEAT_LOG_STARTRANGE) {
    SG_INFO_OBJ (qvconv, "do converting finish, inbuf=%p, input fd: %d, input ptr: %p, input offset: %d "
        "outbuf=%p, output fd: %d, output ptr: %p, inst %" GST_PTR_FORMAT "@%p, idx %u/%u, c2d inst=%p, PTS: %" GST_TIME_FORMAT, inbuf, c2d_input_buffer.fd, c2d_input_buffer.ptr, input_ion_offset,
        outbuf, c2d_output_buffer.fd, c2d_output_buffer.ptr, qvconv, qvconv, priv->idx_in_one_cycle, priv->execute_idx, c2d, GST_TIME_ARGS(GST_BUFFER_PTS(inbuf)));
  }

#ifdef QVCONV_DUMP_C2D_BUFFER
  if (priv->dump_option & (DUMP_OPTION_INPUT | DUMP_OPTION_OUTPUT))
    gst_qvconv_dump_c2d (qvconv);
#endif

  /* add fd info. in GstVideoMeta in order waylandsink can recognize the buffer */
  if (meta) {
    mem = gst_buffer_get_memory (outbuf, 0);
    GST_LOG_OBJECT (qvconv, "Gst outbuf %p, GstMemory %p when outbuf from qvconv", outbuf, mem);
    if (GST_QVCONV_BUFFER_POOL_CAST(priv->pool)->dmabuf) {
       memory =(GstQvconvMemory *)
           gst_mini_object_get_qdata (GST_MINI_OBJECT (mem),
        GST_QVCONV_PRIVATE_DATA);
       gst_memory_unref (GST_MEMORY_CAST (mem));
    } else {
      memory = GST_QVCONV_MEMORY_CAST (mem);
    }
    if(!memory) {
      SG_ERR_OBJ_LITE (qvconv, "get memory failed");
      goto exit;
    }
    c2d_buffer = &memory->c2d_buf;
    output_data_fd = c2d_buffer->fd;//c2d target buffer is from this qvconv's own pool
    output_meta_fd = c2d_buffer->meta_fd;
    output_data_size = c2d_buffer->size;

    if (memory && !(GST_QVCONV_BUFFER_POOL_CAST(priv->pool)->dmabuf))
      gst_memory_unref (GST_MEMORY_CAST (memory));
  }else{
    output_data_fd = c2d_output_buffer.fd;//c2d target buffer is from downstream plugin's pool
    output_data_size = c2d_output_buffer.size;
  }
  QGVMeta = gst_buffer_get_video_meta(outbuf);
  if (QGVMeta == NULL) {
    GstVideoInfo *info = &qvconv->dst_info;
    QGVMeta = gst_buffer_add_video_meta_full (outbuf, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info), info->offset, info->stride);
    SG_INFO_OBJ_LITE(qvconv, "c2d target buffer is from downstream plugin pool, still try to add GstVideoMeta, ret GstVideoMeta %p", QGVMeta);
  }
  if (QGVMeta) {
    //Disable below tricky meta setting on output buffer. To implement buffer sharing with downstream plugin, just support gst dmabuf method.
    //Keeping input buffer tricky meta method, is to support some gen3 platform. Later, will remove it too.
    //QGVMeta->offset[2] = GST_MAKE_FOURCC('Q', 'a','U','T');
    //QGVMeta->offset[3] = output_data_size;
    //QGVMeta->stride[2] = output_data_fd;
    //QGVMeta->stride[3] = output_meta_fd;
    GST_DEBUG_OBJECT (qvconv, "Attach GstVideoMeta %p on output GstBuffer %p", QGVMeta, outbuf);
  }else{
    SG_ERR_OBJ_LITE (qvconv, "No GstVideoMeta attach on output GstBuffer %p, it's not expected!", outbuf);
  }

  if (priv->do_deinterlace) {
    //clear interlace flag in output gstbuf
    GST_BUFFER_FLAG_UNSET(outbuf, GST_VIDEO_BUFFER_FLAG_INTERLACED | GST_VIDEO_BUFFER_FLAG_TFF | GST_VIDEO_BUFFER_FLAG_RFF | GST_VIDEO_BUFFER_FLAG_ONEFIELD);
    GST_LOG_OBJECT (qvconv, "clear interlace flags in output gstbuf %p", outbuf);
  }

  ret = TRUE;

exit:
  if (fd_memory_size > 0 && c2d_output_buffer.ptr)
    munmap (c2d_output_buffer.ptr, fd_memory_size);

  if (input_ion_ptr)
    munmap (input_ion_ptr, input_ion_size);

  if (in_mem) {
    GST_LOG_OBJECT (qvconv,"unmap in_mem %p", in_mem);
    gst_memory_unmap (in_mem, &map_info);
    gst_memory_unref (in_mem);
  }

  return ret;
}

static GstFlowReturn
gst_qvconv_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstQvconv *qvconv = GST_QVCONV (trans);
  GstQvconvPrivate *priv = qvconv->priv;

  GST_LOG_OBJECT (qvconv, "converting inbuf: %p, %" GST_TIME_FORMAT
      " to outbuf: %p, %" GST_TIME_FORMAT ", inst %p, index %u/%u",  inbuf, GST_TIME_ARGS (GST_BUFFER_PTS (inbuf)),
      outbuf, GST_TIME_ARGS (GST_BUFFER_PTS (outbuf)), qvconv, priv->idx_in_one_cycle, priv->execute_idx);

  g_mutex_lock (&priv->lock);
  if (!gst_qvconv_do_convert(trans, inbuf, outbuf))
  {
    g_mutex_unlock (&priv->lock);
    priv->execute_idx++;
    priv->idx_in_one_cycle++;
    SG_ERR_OBJ_LITE (qvconv, "converting finished, failed!!!");
    return GST_FLOW_ERROR;
  }
  g_mutex_unlock (&priv->lock);
  priv->execute_idx++;
  priv->idx_in_one_cycle++;
  GST_LOG_OBJECT (qvconv, "converting finished, succeed.");

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qvconv", GST_RANK_NONE,
      GST_TYPE_QVCONV);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    qvconv, "Qualcomm Technologies Inc video converter",
    plugin_init, VERSION "-" G_STRINGIFY(GST_VERSION_MAJOR) "/" G_STRINGIFY(GST_VERSION_MINOR) "/" G_STRINGIFY(GST_VERSION_MICRO), "Proprietary", "Qualcomm Technologies Inc Qvconv",
    "http://www.qualcomm.com")
