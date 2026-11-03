/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dfs.h"

#include <gst/video/gstimagepool.h>
#include <gst/video/video.h>
#include <gst/video/video-utils.h>
#include <gst/utils/common-utils.h>

#define GST_CAT_DEFAULT gst_dfs_debug
GST_DEBUG_CATEGORY (gst_dfs_debug);

#define gst_dfs_parent_class parent_class
G_DEFINE_TYPE (GstDfs, gst_dfs, GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_MIN_BUFFERS        2
#define DEFAULT_MAX_BUFFERS        10
#define DEFAULT_CONFIG_PATH "/data/stereo.config"
#define DEFAULT_OUTPUT_MODE OUTPUT_MODE_VIDEO

#define DEFAULT_PROP_MODE MODE_SPEED
#define DEFAULT_PROP_RECTIFICATION FALSE
#define DEFAULT_PROP_PPLEVEL PP_BASIC
#define DEFAULT_PROP_GPU_RECT FALSE
#define DEFAULT_PROP_MIN_DISPARITY 1

#if defined(USE_QVX) || \
    (defined(RVSDK_API_VERSION) && (RVSDK_API_VERSION >= 0x202403))
#define DEFAULT_PROP_NUM_DISPARITY_LEVELS 96
#define DEFAULT_PROP_FILTER_WIDTH 16
#define DEFAULT_PROP_FILTER_HEIGHT 4
#else
#define DEFAULT_PROP_NUM_DISPARITY_LEVELS 32
#define DEFAULT_PROP_FILTER_WIDTH 11
#define DEFAULT_PROP_FILTER_HEIGHT 11
#endif // USE_QVX || RVSDK_API_VERSION >= 0x202403

#define PLY_HEADER_SIZE 128      //Point Cloud PLY Header size in bytes
#define PLY_POINT_NUM 3         //Point Cloud PLY point number per row
#define PLY_POINT_TO_STRING 8    //Point Cloud PLY double to string in bytes

enum
{
  PROP_0,
  PROP_MODE,
  PROP_MIN_DISPARITY,
  PROP_NUM_DISPARITY_LEVELS,
  PROP_FILTER_WIDTH,
  PROP_FILTER_HEIGHT,
  PROP_RECTIFICATION,
  PROP_CONFIG_PATH,
  PROP_PPLEVEL,
};

#define GST_SINK_VIDEO_FORMATS "{ NV12, NV21, GRAY8 }"

#define GST_SRC_VIDEO_FORMATS "{ RGB, BGR, RGBA, BGRA, RGBx, BGRx, GRAY8 }"

#define GST_SRC_DISPARITY_CAPS "cvp/x-disparity"
#define GST_SRC_POINT_CLOUD_CAPS "cvp/x-point-cloud"


#define GST_DFS_SRC_CAPS                              \
    "video/x-raw, "                                   \
    "format = (string) " GST_SRC_VIDEO_FORMATS "; "   \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GBM "), "  \
    "format = (string) " GST_SRC_VIDEO_FORMATS "; "   \
    GST_SRC_DISPARITY_CAPS                     "; "   \
    GST_SRC_POINT_CLOUD_CAPS


static GstStaticCaps gst_dfs_static_sink_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_SINK_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_SINK_VIDEO_FORMATS));

static GstStaticCaps gst_dfs_static_src_caps =
GST_STATIC_CAPS (GST_DFS_SRC_CAPS);

static GstCaps *
gst_dfs_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_dfs_static_sink_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_dfs_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_dfs_static_src_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_dfs_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_dfs_sink_caps ());
}

static GstPadTemplate *
gst_dfs_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_dfs_src_caps ());
}


GType
gst_dfs_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue mode[] = {
    {MODE_CVP,
        "CVP hardware mode", "cvp"},
    {MODE_COVERAGE,
        "CPU solution, speed mode", "coverage"},
    {MODE_SPEED,
        "OpenCL solution, speed mode", "speed"},
    {MODE_BALANCE,
        "CPU solution, accuracy mode", "balance"},
    {MODE_ACCURACY,
        "CPU solution, accuracy mode", "accuracy"},
    {0, NULL, NULL},
  };
  if (!type)
    type = g_enum_register_static ("DFSMode", mode);

  return type;
}

GType
gst_dfs_pplevel_get_type (void)
{
  static GType type = 0;
  static const GEnumValue pplevel[] = {
    {PP_BASIC,
        "basic mode", "basic"},
    {PP_MEDIUM,
        "advanced mode", "medium"},
    {PP_STRONG,
        "strong mode, need specific customer code", "strong"},
    {PP_SUPREME,
        "supreme mode, need specific customer code", "supreme"},
    {0, NULL, NULL},
  };
  if (!type)
    type = g_enum_register_static ("PPlevel", pplevel);

  return type;
}

static GstBufferPool *
gst_dfs_create_pool (GstDfs * dfs, GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstBufferPool *pool = NULL;
  GstAllocator *allocator = NULL;
  guint size = 0;
  GstVideoInfo info;

  if (gst_structure_has_name (structure, "video/x-raw")) {
    if (!gst_video_info_from_caps (&info, caps)) {
      GST_ERROR_OBJECT (dfs, "Invalid caps %" GST_PTR_FORMAT, caps);
      return NULL;
    }

    if ((pool = gst_image_buffer_pool_new ()) == NULL) {
      GST_ERROR_OBJECT (dfs, "Failed to create image pool!");
      return NULL;
    }

    // If downstream allocation query supports GBM, allocate gbm memory.
    if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
      allocator = gst_fd_allocator_new ();
      GST_INFO_OBJECT (dfs, "Buffer pool uses GBM memory");
    } else {
      allocator = gst_qti_allocator_new (GST_FD_MEMORY_FLAG_KEEP_MAPPED);
      GST_INFO_OBJECT (dfs, "Buffer pool uses DMA memory");
    }

    if (allocator == NULL) {
      GST_ERROR_OBJECT (dfs, "Failed to create allocator");
      gst_clear_object (&pool);
      return NULL;
    }

    size = GST_VIDEO_INFO_SIZE (&info);
  } else if (gst_structure_has_name (structure, "cvp/x-disparity")) {
    GST_INFO_OBJECT (dfs, "Uses SYSTEM memory");
    pool = gst_buffer_pool_new ();
    guint width, height;
    gst_dfs_engine_get_eye_dims (GST_VIDEO_INFO_WIDTH (dfs->ininfo),
        GST_VIDEO_INFO_HEIGHT (dfs->ininfo), &width, &height);
    size = width * height * sizeof (float);

  } else if (gst_structure_has_name (structure, "cvp/x-point-cloud")) {
    GST_INFO_OBJECT (dfs, "Uses SYSTEM memory");
    pool = gst_buffer_pool_new ();
    guint width, height;
    gst_dfs_engine_get_eye_dims (GST_VIDEO_INFO_WIDTH (dfs->ininfo),
        GST_VIDEO_INFO_HEIGHT (dfs->ininfo), &width, &height);
    // Setting initial size of buffer to worst case size.
    // since point cloud is not known ahead of time and is not constant.
    size = (width * height) * PLY_POINT_NUM * PLY_POINT_TO_STRING + PLY_HEADER_SIZE;
  }

  structure = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (structure, caps, size,
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  if (allocator != NULL) {
    gst_buffer_pool_config_set_allocator (structure, allocator, NULL);
    g_object_unref (allocator);

    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_add_option (structure,
        GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);
  }

  if (!gst_buffer_pool_set_config (pool, structure)) {
    GST_WARNING_OBJECT (dfs, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }

  return pool;
}

static gboolean
gst_dfs_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstDfs *dfs = GST_DFS_CAST (trans);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (dfs, "Failed to parse the decide_allocation caps!");
    return FALSE;
  }
  // Invalidate the cached pool if there is an allocation_query.
  if (dfs->outpool) {
    gst_buffer_pool_set_active (dfs->outpool, FALSE);
    gst_object_unref (dfs->outpool);
  }
  // Create a new buffer pool.
  pool = gst_dfs_create_pool (dfs, caps);
  dfs->outpool = pool;

  // Get the configured pool properties in order to set in query.
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, &caps, &size, &minbuffers,
      &maxbuffers);

  if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    gst_query_add_allocation_param (query, allocator, &params);

  gst_structure_free (config);

  // Check whether the query has pool.
  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, minbuffers,
        maxbuffers);
  else
    gst_query_add_allocation_pool (query, pool, size, minbuffers, maxbuffers);

  return TRUE;
}

static GstFlowReturn
gst_dfs_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstDfs *dfs = GST_DFS_CAST (trans);
  GstBufferPool *pool = dfs->outpool;
  GstFlowReturn ret = GST_FLOW_OK;

  if (gst_base_transform_is_passthrough (trans)) {
    GST_LOG_OBJECT (dfs, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (dfs, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  ret = gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (dfs, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }
  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS),
      0, -1);

  return GST_FLOW_OK;
}

static gboolean
gst_dfs_accept_caps (GstBaseTransform * trans, GstPadDirection direction,
    GstCaps * caps)
{
  GstDfs *dfs = GST_DFS_CAST (trans);
  GstCaps *template_caps = NULL;
  GstCaps *stripped = NULL;
  gboolean ret = FALSE;
  guint i, n;

  if (direction == GST_PAD_SRC) {
    template_caps = gst_pad_get_pad_template_caps (
        GST_BASE_TRANSFORM_SRC_PAD (trans));
    ret = gst_caps_can_intersect (caps, template_caps);
    gst_caps_unref (template_caps);
    return ret;
  }

  /*
   * Sink pad: qmmfsrc adds multiview-mode, pixel-aspect-ratio and
   * interlace-mode to its output caps. Strip them before testing
   * against the sink template so they do not cause a false rejection.
   */
  template_caps = gst_pad_get_pad_template_caps (
      GST_BASE_TRANSFORM_SINK_PAD (trans));

  stripped = gst_caps_copy (caps);
  n = gst_caps_get_size (stripped);
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (stripped, i);
    gst_structure_remove_fields (s,
        "multiview-mode", "multiview-flags",
        "pixel-aspect-ratio", "interlace-mode", NULL);
  }

  ret = gst_caps_can_intersect (stripped, template_caps);

  GST_DEBUG_OBJECT (dfs, "accept_caps (sink): %s for %" GST_PTR_FORMAT,
      ret ? "yes" : "no", caps);

  gst_caps_unref (stripped);
  gst_caps_unref (template_caps);
  return ret;
}

static GstCaps *
gst_dfs_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstDfs *dfs = GST_DFS_CAST (trans);
  GstCaps *result = NULL;
  guint i, n;

  GST_DEBUG_OBJECT (dfs, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (dfs, "Filter caps: %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SINK) {
    /* Given sink caps, return what we can produce on the src pad. */
    GstPad *pad = GST_BASE_TRANSFORM_SRC_PAD (trans);
    result = gst_pad_get_pad_template_caps (pad);
  } else {
    /*
     * Given src caps, return what we accept on the sink pad.
     *
     * The upstream camera source (qmmfsrc) sets multiview-mode=mono,
     * pixel-aspect-ratio and interlace-mode on its output caps for
     * over-under stereo frames. GstBaseTransform verifies the
     * transformation by intersecting the proposed incaps against what
     * transform_caps returns for the SRC direction. The sink template
     * does not carry these fields, so strip them from the template
     * before returning so the intersection succeeds.
     */
    GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD (trans);
    result = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
    n = gst_caps_get_size (result);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (result, i);
      gst_structure_remove_fields (s,
          "multiview-mode", "multiview-flags",
          "pixel-aspect-ratio", "interlace-mode", NULL);
    }
  }

  if (filter != NULL) {
    GstCaps *intersection =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (dfs, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstCaps *
gst_dfs_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstDfs *dfs = GST_DFS_CAST (trans);
  GstStructure *output = NULL;
  const GValue *value = NULL;

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  output = gst_caps_get_structure (outcaps, 0);

  if (gst_structure_has_name (output, "cvp/x-point-cloud")) {
    outcaps = gst_caps_fixate (outcaps);
  } else {

    if (gst_structure_has_name (output, "video/x-raw")) {
      // Fixate the output format.
      value = gst_structure_get_value (output, "format");

      if (!gst_value_is_fixed (value)) {
        gst_structure_fixate_field (output, "format");
        value = gst_structure_get_value (output, "format");
      }

      GST_DEBUG_OBJECT (dfs, "Output format fixed to: %s",
          g_value_get_string (value));
    }

    gint width = 0, height = 0;
    GstVideoInfo ininfo;

    if (!gst_video_info_from_caps (&ininfo, incaps))
      GST_ERROR_OBJECT (dfs, "Failed to get input video info from caps!");

    guint eye_width, eye_height;
    gst_dfs_engine_get_eye_dims (GST_VIDEO_INFO_WIDTH (&ininfo),
        GST_VIDEO_INFO_HEIGHT (&ininfo), &eye_width, &eye_height);
    width  = (gint) eye_width;
    height = (gint) eye_height;

    value = gst_structure_get_value (output, "width");
    if ((NULL == value) || !gst_value_is_fixed (value)) {
      gst_structure_set (output, "width", G_TYPE_INT, width, NULL);
      value = gst_structure_get_value (output, "width");
    }
    width = g_value_get_int (value);

    value = gst_structure_get_value (output, "height");
    if ((NULL == value) || !gst_value_is_fixed (value)) {
      gst_structure_set (output, "height", G_TYPE_INT, height, NULL);
      value = gst_structure_get_value (output, "height");
    }
    height = g_value_get_int (value);

    GST_DEBUG_OBJECT (dfs, "Output width and height fixated to: %dx%d",
        width, height);

    /*
     * Propagate framerate from incaps. The src template exposes framerate
     * as a range [ 0/1, 2147483647/1 ]. GstBaseTransform requires the
     * result of fixate_caps to be fully fixed (gst_caps_is_fixed() == TRUE)
     * before it will accept the negotiated caps. Copy the upstream-negotiated
     * framerate so the output caps become fixed.
     */
    value = gst_structure_get_value (
        gst_caps_get_structure (incaps, 0), "framerate");
    if (value != NULL && gst_value_is_fixed (value)) {
      gst_structure_set_value (output, "framerate", value);
    } else {
      gst_structure_fixate_field (output, "framerate");
    }

  }

  GST_DEBUG_OBJECT (dfs, "Fixated caps to %" GST_PTR_FORMAT, outcaps);

  return outcaps;
}

static GstFlowReturn
gst_dfs_transform (GstBaseTransform * trans, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstDfs *dfs = GST_DFS_CAST (trans);
  GstVideoMeta *vmeta = NULL;
  DfsInitSettings settings;
  GstVideoFrame inframe;
  GstMapInfo out_info0;
  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_STIME_NONE;

  vmeta = gst_buffer_get_video_meta (inbuffer);

  /* Ensure caps have been negotiated before the first buffer arrives. */
  if (dfs->ininfo == NULL) {
    GST_ERROR_OBJECT (dfs, "Input video info not set, caps not negotiated!");
    return GST_FLOW_ERROR;
  }

  //Check if Engine has been init already
  if (dfs->engine == NULL) {
    guint frame_width, frame_height;
    gint  frame_stride;

    /*
     * Prefer GstVideoMeta when present (attached by pool-aware sources).
     * Fall back to the negotiated GstVideoInfo from set_caps() otherwise.
     */
    if (vmeta != NULL) {
      frame_width  = vmeta->width;
      frame_height = vmeta->height;
      frame_stride = vmeta->stride[0];
    } else {
      GST_DEBUG_OBJECT (dfs, "No GstVideoMeta on input buffer, "
          "falling back to negotiated video info");
      frame_width  = GST_VIDEO_INFO_WIDTH (dfs->ininfo);
      frame_height = GST_VIDEO_INFO_HEIGHT (dfs->ininfo);
      frame_stride = GST_VIDEO_INFO_PLANE_STRIDE (dfs->ininfo, 0);
    }

    settings.mode = dfs->output_mode;
    settings.format = dfs->format;
    settings.stereo_frame_width  = frame_width;
    settings.stereo_frame_height = frame_height;
    settings.stride = frame_stride; //Only need Y-plane
    settings.dfs_mode = dfs->dfs_mode;
    settings.min_disparity = dfs->min_disparity;
    settings.num_disparity_levels = dfs->num_disparity_levels;
    settings.filter_width = dfs->filter_width;
    settings.filter_height = dfs->filter_height;
    settings.rectification = dfs->rectification;
    settings.pplevel = dfs->pplevel;
    settings.gpu_rect = dfs->gpu_rect;
    settings.frames_per_second = (GST_VIDEO_INFO_FPS_D (dfs->ininfo) > 0)
        ? (guint) (GST_VIDEO_INFO_FPS_N (dfs->ininfo)
            / GST_VIDEO_INFO_FPS_D (dfs->ininfo))
        : 30;
    settings.config_path = dfs->config_location;

    ts_begin = gst_util_get_timestamp ();

    dfs->engine = gst_dfs_engine_new (&settings);
    if (dfs->engine == NULL) {
      GST_ERROR_OBJECT (dfs, "Failed to create DFS engine!");
      return GST_FLOW_ERROR;
    }

    ts_end = gst_util_get_timestamp ();
    tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

    GST_INFO ("DFS init time: %" G_GINT64_FORMAT ".%03"
        G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
        (GST_TIME_AS_USECONDS (tsdelta) % 1000));
  }

  if (!gst_video_frame_map (&inframe, dfs->ininfo, inbuffer,
          (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    GST_ERROR_OBJECT (dfs, "Failed to map input buffer!");
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map_range (outbuffer, 0, 1, &out_info0, GST_MAP_READWRITE)) {
    gst_video_frame_unmap (&inframe);
    GST_ERROR_OBJECT (dfs, "Failed to map output buffer!");
    return GST_FLOW_ERROR;
  }

  ts_begin = gst_util_get_timestamp ();
  if (!gst_dfs_engine_execute (dfs->engine, &inframe, out_info0.data, out_info0.size)) {
    GST_ERROR_OBJECT (dfs, "Failed to execute engine");;
  }

  ts_end = gst_util_get_timestamp ();
  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  GST_INFO ("DFS time: %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  gst_buffer_unmap (outbuffer, &out_info0);
  gst_video_frame_unmap (&inframe);

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_dfs_change_state (GstElement * element, GstStateChange transition)
{
  GstDfs *dfs = GST_DFS_CAST (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (dfs, "Failure");
    return ret;
  }

  return ret;
}


static void
gst_dfs_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDfs *dfs = GST_DFS (object);

  GST_OBJECT_LOCK (dfs);
  switch (property_id) {
    case PROP_MODE:
      dfs->dfs_mode = g_value_get_enum (value);
      break;
    case PROP_MIN_DISPARITY:
      dfs->min_disparity = g_value_get_int (value);
      break;
    case PROP_NUM_DISPARITY_LEVELS:
      dfs->num_disparity_levels = g_value_get_int (value);
      break;
    case PROP_FILTER_WIDTH:
      dfs->filter_width = g_value_get_int (value);
      break;
    case PROP_FILTER_HEIGHT:
      dfs->filter_height = g_value_get_int (value);
      break;
    case PROP_RECTIFICATION:
      dfs->rectification = g_value_get_boolean (value);
      break;
    case PROP_CONFIG_PATH:
      dfs->config_location = g_strdup(g_value_get_string (value));
      break;
    case PROP_PPLEVEL:
      dfs->pplevel = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (dfs);
}

static void
gst_dfs_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstDfs *dfs = GST_DFS (object);

  GST_OBJECT_LOCK (dfs);
  switch (property_id) {
    case PROP_MODE:
      g_value_set_enum (value, dfs->dfs_mode);
      break;
    case PROP_MIN_DISPARITY:
      g_value_set_int (value, dfs->min_disparity);
      break;
    case PROP_NUM_DISPARITY_LEVELS:
      g_value_set_int (value, dfs->num_disparity_levels);
      break;
    case PROP_FILTER_WIDTH:
      g_value_set_int (value, dfs->filter_width);
      break;
    case PROP_FILTER_HEIGHT:
      g_value_set_int (value, dfs->filter_height);
      break;
    case PROP_RECTIFICATION:
      g_value_set_boolean (value, dfs->rectification);
      break;
    case PROP_CONFIG_PATH:
      g_value_set_string (value, dfs->config_location);
      break;
    case PROP_PPLEVEL:
      g_value_set_enum (value, dfs->pplevel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (dfs);
}

static gboolean
gst_dfs_set_caps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps)
{
  GstDfs *dfs = GST_DFS_CAST (trans);
  GstVideoInfo ininfo;
  GstStructure *structure = NULL;

  if (!gst_video_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (dfs, "Failed to get input video info from caps!");
    return FALSE;
  }
  // Get the output caps structure in order to determine the mode.
  structure = gst_caps_get_structure (outcaps, 0);

  if (gst_structure_has_name (structure, "video/x-raw")) {
    dfs->output_mode = OUTPUT_MODE_VIDEO;
    dfs->format =
        gst_video_format_from_string (gst_structure_get_string (structure,
            "format"));
  } else if (gst_structure_has_name (structure, "cvp/x-disparity")) {
    dfs->output_mode = OUTPUT_MODE_DISPARITY;
  } else if (gst_structure_has_name (structure, "cvp/x-point-cloud")) {
    dfs->output_mode = OUTPUT_MODE_POINT_CLOUD;
  }

  if (dfs->rectification == TRUE && dfs->dfs_mode == MODE_SPEED)
    dfs->gpu_rect = TRUE;

  dfs->ininfo = gst_video_info_copy (&ininfo);

  gst_base_transform_set_passthrough (trans, FALSE);

  return TRUE;
}

static void
gst_dfs_finalize (GObject * object)
{
  GstDfs *dfs = GST_DFS (object);

  if (dfs->outpool != NULL)
    gst_object_unref (dfs->outpool);

  if (dfs->ininfo != NULL)
    gst_video_info_free (dfs->ininfo);

  if (dfs->engine != NULL)
    gst_dfs_engine_free (dfs->engine);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (dfs));
}

static void
gst_dfs_class_init (GstDfsClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_dfs_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_dfs_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_dfs_finalize);


  g_object_class_install_property (gobject, PROP_MODE,
      g_param_spec_enum ("mode", "mode",
          "Select DFS mode. 0-cvp: hardware, 1-cpu: coverage mode. "
          "2-opencl: speed mode. 3-opencl: balance mode, only for "
          "equal or after RVSDK202403 version. 4-cpu: accuracy mode,"
          "only for equal or before RVSDK202307 version.",
          GST_TYPE_DFS_MODE, DEFAULT_PROP_MODE,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_MIN_DISPARITY,
      g_param_spec_int ("min-disparity", "min-disparity",
          "Set min disparity", 0, 240, DEFAULT_PROP_MIN_DISPARITY,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_NUM_DISPARITY_LEVELS,
      g_param_spec_int ("num-disparity-level", "num-disparity-level",
          "Set disparirty level. Distinct disparity levels between"
          "neighboring pixels. Multiples of 16",
          16, 256, DEFAULT_PROP_NUM_DISPARITY_LEVELS,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_FILTER_WIDTH,
      g_param_spec_int ("filter-width", "filter-width",
          "Set filter width. Controls window size for guided filter"
          "used in DFS implementation. Must be odd number. Max value"
          "should be < image width", 1, INT_MAX, DEFAULT_PROP_FILTER_WIDTH,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_FILTER_HEIGHT,
      g_param_spec_int ("filter-height", "filter-height",
          "Set filter height. Controls window size for guided filter"
          "used in DFS implementation. Must be odd number. Max value"
          "should be < image height", 1, INT_MAX, DEFAULT_PROP_FILTER_HEIGHT,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_RECTIFICATION,
      g_param_spec_boolean ("rectification", "rectification",
          "Perform rectification on input frames.", DEFAULT_PROP_RECTIFICATION,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_CONFIG_PATH,
      g_param_spec_string("config", "Path to stereo config file",
          "Path to config file. Eg.: /data/stereo.config", DEFAULT_CONFIG_PATH,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_PPLEVEL,
      g_param_spec_enum ("pplevel", "pplevel",
          "Select postprocessing level", GST_TYPE_DFS_PPLEVEL, DEFAULT_PROP_PPLEVEL,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element, "Depth From Stereo",
      "Runs Depth From Stereo (DFS) algorithm",
      "Calculates disparity map a pair of stereo images", "QTI");

  gst_element_class_add_pad_template (element, gst_dfs_sink_template ());
  gst_element_class_add_pad_template (element, gst_dfs_src_template ());

  element->change_state = GST_DEBUG_FUNCPTR (gst_dfs_change_state);

  trans->decide_allocation = GST_DEBUG_FUNCPTR (gst_dfs_decide_allocation);
  trans->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_dfs_prepare_output_buffer);

  trans->accept_caps = GST_DEBUG_FUNCPTR (gst_dfs_accept_caps);
  trans->transform_caps = GST_DEBUG_FUNCPTR (gst_dfs_transform_caps);
  trans->fixate_caps = GST_DEBUG_FUNCPTR (gst_dfs_fixate_caps);
  trans->set_caps = GST_DEBUG_FUNCPTR (gst_dfs_set_caps);
  trans->transform = GST_DEBUG_FUNCPTR (gst_dfs_transform);
}

static void
gst_dfs_init (GstDfs * dfs)
{
  dfs->ininfo = NULL;
  dfs->outpool = NULL;
  dfs->engine = NULL;
  dfs->config_location = DEFAULT_CONFIG_PATH;
  dfs->output_mode = DEFAULT_OUTPUT_MODE;

  dfs->dfs_mode = DEFAULT_PROP_MODE;
  dfs->min_disparity = DEFAULT_PROP_MIN_DISPARITY;
  dfs->num_disparity_levels = DEFAULT_PROP_NUM_DISPARITY_LEVELS;
  dfs->filter_width = DEFAULT_PROP_FILTER_WIDTH;
  dfs->filter_height = DEFAULT_PROP_FILTER_HEIGHT;
  dfs->rectification = DEFAULT_PROP_RECTIFICATION;
  dfs->gpu_rect = DEFAULT_PROP_GPU_RECT;

  GST_DEBUG_CATEGORY_INIT (gst_dfs_debug, "qtidfs", 0, "DFS");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtidfs", GST_RANK_PRIMARY,
      GST_TYPE_DFS);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtidfs,
    "DFS",
    plugin_init,
    PACKAGE_VERSION, PACKAGE_LICENSE, PACKAGE_SUMMARY, PACKAGE_ORIGIN)
