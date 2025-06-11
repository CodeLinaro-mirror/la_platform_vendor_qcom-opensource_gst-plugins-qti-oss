// Copyright (c) 2022, 2025 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/**
 * SECTION:element-extpoolsink
 * @title: extpoolsink
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=xxx.mp4 ! qtdemux ! h264parse !
 * qvidch264dec use-external-pool=true ! extpoolsink ! waylandsink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstextpoolsink.h"
#include "gstextpool.h"

#include <gst/video/video.h>
#include <gst/allocators/gstdmabuf.h>

GST_DEBUG_CATEGORY (gst_ext_pool_sink_debug);
#define GST_CAT_DEFAULT gst_ext_pool_sink_debug

#define EXTPOOLSINK_MIN_OUT_BUF 2
#define EXTPOOLSINK_MAX_OUT_BUF 16

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT,
};

#define SINK_FORMATS "{" \
    "NV12 "  /*  8-bit 4:2:0 */ \
    "}"

#define SINK_COMPRESSION ",compression=ubwc"

#define SRC_FORMATS "{" \
    "NV12 "  /*  8-bit 4:2:0 */ \
    "}"

#define SRC_COMPRESSION ",compression={linear,ubwc}"

#define EXTPOOL_CAPS_DMABUF(formats) \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES \
    (GST_CAPS_FEATURE_MEMORY_DMABUF, formats)

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        GST_VIDEO_CAPS_MAKE (SINK_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE (SINK_FORMATS) SINK_COMPRESSION ";"
        EXTPOOL_CAPS_DMABUF (SINK_FORMATS) ";"
        EXTPOOL_CAPS_DMABUF (SINK_FORMATS) SINK_COMPRESSION
        ";")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        GST_VIDEO_CAPS_MAKE (SRC_FORMATS) SRC_COMPRESSION ";"
        EXTPOOL_CAPS_DMABUF (SRC_FORMATS) SRC_COMPRESSION
        ";")
    );

#define gst_ext_pool_sink_parent_class parent_class
G_DEFINE_TYPE (GstExtPoolSink, gst_ext_pool_sink, GST_TYPE_VIDEO_FILTER);

static void
_print_video_info (const GstVideoInfo * info, const char *func, int line)
{
  GstVideoFormat format;
  gint width, height, stride0, stride1;
  gsize offset0, offset1, size;

  g_return_if_fail (info != NULL);

  format = GST_VIDEO_INFO_FORMAT (info);
  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);
  stride0 = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
  stride1 = GST_VIDEO_INFO_PLANE_STRIDE (info, 1);
  offset0 = GST_VIDEO_INFO_PLANE_OFFSET (info, 0);
  offset1 = GST_VIDEO_INFO_PLANE_OFFSET (info, 1);
  size = GST_VIDEO_INFO_SIZE (info);

  GST_DEBUG ("%s:%d: format=%s-%d,width=%d,height=%d,stride0=%d,stride1=%d"
      ",offset0=%" G_GSIZE_FORMAT ",offset1=%" G_GSIZE_FORMAT,
      func, line, GST_VIDEO_INFO_NAME (info), format, width, height,
      stride0, stride1, offset0, offset1);
  GST_DEBUG ("%s:%d: size=%" G_GSIZE_FORMAT, func, line, size);
}

#undef print_video_info
#define print_video_info(info) _print_video_info (info, __func__, __LINE__)

static void
_print_video_meta (const GstVideoMeta * meta, const char *func, int line)
{
  g_return_if_fail (meta != NULL);

  GST_DEBUG ("%s:%d: format=%d,width=%u,height=%u,stride0=%d,stride1=%d"
      ",offset0=%" G_GSIZE_FORMAT ",offset1=%" G_GSIZE_FORMAT,
      func, line, meta->format, meta->width, meta->height,
      meta->stride[0], meta->stride[1], meta->offset[0], meta->offset[1]);
}

#undef print_video_meta
#define print_video_meta(meta) _print_video_meta (meta, __func__, __LINE__)

/* GObject vmethod implementations */

static void
gst_ext_pool_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstExtPoolSink *self = GST_EXTPOOLSINK (object);

  GST_DEBUG_OBJECT (self, "prop_id %u", prop_id);

  switch (prop_id) {
    case PROP_SILENT:
      self->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ext_pool_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstExtPoolSink *self = GST_EXTPOOLSINK (object);

  GST_DEBUG_OBJECT (self, "prop_id %u", prop_id);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, self->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstBaseTransform vmethod implementations */

/* given @caps on the src or sink pad (given by @direction),
 * calculate the possible caps on the other pad.
 * refer to gst_base_transform_transform_caps() for design intent.
 *
 * extpoolsink's src and sink caps have no relation to each other,
 * so just ignore input caps and return possible caps on the other pad.
 */
static GstCaps *
gst_ext_pool_sink_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstPad *otherpad;
  GstCaps *result;

  if (GST_PAD_SRC == direction)
    otherpad = trans->sinkpad;
  else
    otherpad = trans->srcpad;

  result = gst_pad_get_pad_template_caps (otherpad);

  if (filter) {
    GstCaps *temp;
    temp = gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = temp;
  }

  GST_DEBUG_OBJECT (otherpad, "incaps %" GST_PTR_FORMAT, caps);
  GST_DEBUG_OBJECT (otherpad, "filter %" GST_PTR_FORMAT, filter);
  GST_DEBUG_OBJECT (otherpad, "result %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
_caps_has_compression_ubwc (const GstCaps * caps)
{
  gboolean ret = FALSE;

  for (gint i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    gchar *str = gst_structure_to_string (s);
    gboolean has_ubwc = g_strrstr (str, "ubwc") &&
        gst_structure_has_field (s, "compression");
    g_free (str);

    if (has_ubwc) {
      ret = TRUE;
      break;
    }
  }

  GST_DEBUG ("ret %u", ret);
  return ret;
}

static void
_fixate_caps_compression (GstPad * pad, GstCaps * result)
{
  GstCaps *peer_caps = gst_pad_peer_query_caps (pad, NULL);
  gboolean has_ubwc = _caps_has_compression_ubwc (peer_caps);

  GST_DEBUG_OBJECT (pad, "peer caps: %" GST_PTR_FORMAT, peer_caps);
  gst_caps_unref (peer_caps);

  result = gst_caps_make_writable (result);
  if (has_ubwc)
    gst_caps_set_simple (result, "compression", G_TYPE_STRING, "ubwc", NULL);
  else
    gst_caps_set_simple (result, "compression", G_TYPE_STRING, "linear", NULL);

  GST_DEBUG_OBJECT (pad, "result caps: %" GST_PTR_FORMAT, result);
}

static gboolean
_is_fixed_caps_interlaced (GstBaseTransform * trans, const GstCaps * caps)
{
  GstVideoInfo info;
  gboolean ret = FALSE;

  if (gst_video_info_from_caps (&info, caps))
    ret = GST_VIDEO_INFO_IS_INTERLACED (&info);
  else
    GST_ERROR_OBJECT (trans, "error parse caps %" GST_PTR_FORMAT, caps);

  GST_DEBUG_OBJECT (trans, "ret: %d", ret);
  return ret;
}

/* given fixed @caps, fixate @othercaps,
 * this function is called in gst_base_transform_find_transform().
 *
 * extpoolsink's in/out width/height/framerate must be same, while
 * in/out format & interlace-mode may be same or not. pass through if
 * in/out caps are all same.
 */
static GstCaps *
gst_ext_pool_sink_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  gboolean interlaced;
  GstCaps *result;
  GstPad *pad = (GST_PAD_SINK == direction) ? trans->sinkpad : trans->srcpad;

  GST_DEBUG_OBJECT (pad, "fixate othercaps %" GST_PTR_FORMAT, othercaps);
  GST_DEBUG_OBJECT (pad, "based on in caps %" GST_PTR_FORMAT, caps);

  if (gst_caps_is_fixed (othercaps)) {
    GST_DEBUG_OBJECT (pad, "othercaps is already fixed");
    result = othercaps;
    goto out;
  }

  /* Only if incaps is interlaced, do deinterlacing by GPU, or else do
   * nothing and try to pass through if possible. */
  interlaced = _is_fixed_caps_interlaced (trans, caps);

  result = gst_caps_intersect_full (caps, othercaps, GST_CAPS_INTERSECT_FIRST);
  if (gst_caps_is_empty (result)) {
    GST_DEBUG_OBJECT (pad, "intersection is empty");
    if (GST_PAD_SINK == direction) {
      if (interlaced) {
        /* For interlaced, fixate othercaps to do deinterlacing by GPU. */
        gst_caps_replace (&result, othercaps);
        GST_DEBUG_OBJECT (pad, "incaps is interlaced, do deinterlacing");
        /* If downstream supports ubwc, then output ubwc, else linear.
         * extpoolsink can output ubwc or linear when deinterlacing. */
        _fixate_caps_compression (pad, result);
      } else {
        /* For progressive, can't pass through when in caps doesn't intersect
         * with othercaps that's already the intersecton with downstream caps
         * in gst_base_transform_find_transform(). */
        GST_DEBUG_OBJECT (pad, "incaps is progressive, can't pass through");
        gst_caps_unref (othercaps);
        gst_caps_replace (&result, NULL);
        goto out;
      }
    } else {
      GST_DEBUG_OBJECT (pad, "othercaps of upstream");
      gst_caps_replace (&result, othercaps);
    }
  } else {
    GST_DEBUG_OBJECT (pad, "intersection is not empty");
    gst_caps_unref (othercaps);

    if (!interlaced) {
      GST_DEBUG_OBJECT (pad, "incaps is progressive, pass through");
      /* In case incaps is progressive, extpoolsink does nothing and just
       * pass the buffers through. Let outcaps be equal to incaps, then
       * gst_base_transform_configure_caps shall set passthrough. */
      gst_caps_replace (&result, caps);
      goto out;
    } else {
      /* extpoolsink only output progressive. */
      GST_ERROR_OBJECT (pad, "won't reach here for incaps of interlaced");
    }
  }

  GST_DEBUG_OBJECT (pad, "result %" GST_PTR_FORMAT, result);

  if (!gst_caps_is_fixed (result)) {
    /* copy width/height/framerate from caps to fixate othercaps */
    GstStructure *s0 = gst_caps_get_structure (caps, 0);
    const GValue *val;

    result = gst_caps_make_writable (result);

    val = gst_structure_get_value (s0, "width");
    if (val)
      gst_caps_set_value (result, "width", val);

    val = gst_structure_get_value (s0, "height");
    if (val)
      gst_caps_set_value (result, "height", val);

    val = gst_structure_get_value (s0, "framerate");
    if (val)
      gst_caps_set_value (result, "framerate", val);

    /* fixate remaining fields */
    result = gst_caps_fixate (result);
    GST_DEBUG_OBJECT (pad, "result %" GST_PTR_FORMAT, result);
  }

  if (GST_PAD_SINK == direction) {
    if (gst_caps_is_subset (caps, result)) {
      GST_DEBUG_OBJECT (pad, "caps is subset of result");
      gst_caps_replace (&result, caps);
    }
  }

out:
  GST_DEBUG_OBJECT (pad, "return %" GST_PTR_FORMAT, result);
  return result;
}

/* buffer's meta & size override video info's */
static gboolean
gst_ext_pool_sink_align_info_by_meta (GstVideoInfo * info,
    const GstVideoMeta * meta, gsize size)
{
  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (meta != NULL, FALSE);

  print_video_info (info);
  print_video_meta (meta);
  GST_DEBUG ("buffer size=%" G_GSIZE_FORMAT, size);

  g_return_val_if_fail (meta->format == GST_VIDEO_INFO_FORMAT (info), FALSE);
  g_return_val_if_fail (size >= GST_VIDEO_INFO_SIZE (info), FALSE);

  GST_VIDEO_INFO_WIDTH (info) = meta->width;
  GST_VIDEO_INFO_HEIGHT (info) = meta->height;
  GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = meta->stride[0];
  GST_VIDEO_INFO_PLANE_STRIDE (info, 1) = meta->stride[1];
  GST_VIDEO_INFO_PLANE_OFFSET (info, 0) = meta->offset[0];
  GST_VIDEO_INFO_PLANE_OFFSET (info, 1) = meta->offset[1];

  GST_VIDEO_INFO_SIZE (info) = size;

  return TRUE;
}

static gboolean
gst_ext_pool_sink_align_info (GstExtPoolSink * self,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM_CAST (self);
  GstVideoMeta *in_meta;
  GstVideoInfo *out_info;
  GstBufferPool *pool;

  if ((in_meta = gst_buffer_get_video_meta (inbuf)) != NULL) {
    gsize size = gst_buffer_get_size (inbuf);
    gst_ext_pool_sink_align_info_by_meta (&self->in_info, in_meta, size);
  }

  pool = gst_base_transform_get_buffer_pool (trans);
  out_info = gst_ext_pool_aligned_info (pool);
  gst_object_unref (pool);

  self->out_info = *out_info;
  print_video_info (&self->out_info);

  GST_INFO_OBJECT (self, "pool %p", pool);

  return TRUE;
}

static gboolean
_caps_compression_is_ubwc (const GstCaps * caps)
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
    case GST_VIDEO_FORMAT_NV12: {
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
    }
    default:
      GST_ERROR ("NOT support format %s", GST_VIDEO_INFO_NAME (info));
      break;
  }

  return size;
}

/* this function is called in gst_video_filter_set_caps() that overrides
 * gstbasetransform_class->set_caps().
 * if return TRUE, GstVideoFilter's in/out video info will be set ready.
 */
static gboolean
gst_ext_pool_sink_set_info (GstVideoFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstExtPoolSink *self = GST_EXTPOOLSINK (filter);
  const GstCapsFeatures *features;
  gboolean ret = TRUE;

  GST_INFO_OBJECT (self, "in_info=%p,  incaps: %" GST_PTR_FORMAT,
      in_info, incaps);
  GST_INFO_OBJECT (self, "out_info=%p, outcaps: %" GST_PTR_FORMAT,
      out_info, outcaps);

  self->in_ubwc = _caps_compression_is_ubwc (incaps);
  self->out_ubwc = _caps_compression_is_ubwc (outcaps);
  GST_INFO_OBJECT (self, "in_ubwc=%u, out_ubwc=%u",
      self->in_ubwc, self->out_ubwc);

  /* when 1st frame comes, align in info by video meta
   * and out info by buffer pool */
  self->in_info = *in_info;
  self->out_info = *out_info;
  /* Set valid size for _decide_allocation() to create output buffer pool
   * and allocate gstbuffer with the valid size for filesink to dump. */
  GST_VIDEO_INFO_SIZE (&self->out_info) =
      _calc_valid_size (out_info, self->out_ubwc);

  features = gst_caps_get_features (incaps, 0);
  self->in_dmabuf = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_DMABUF);

  features = gst_caps_get_features (outcaps, 0);
  self->out_dmabuf = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_DMABUF);

  GST_INFO_OBJECT (self, "in_dmabuf=%u, size %u, out_dmabuf=%u, size %u",
      self->in_dmabuf, GST_VIDEO_INFO_SIZE (&self->in_info),
      self->out_dmabuf, GST_VIDEO_INFO_SIZE (&self->out_info));

  return ret;
}

static gboolean
gst_ext_pool_sink_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstExtPoolSink *self = GST_EXTPOOLSINK (trans);
  GstBufferPool *pool = NULL;
  GstVideoInfo *info = &self->out_info;
  GstAllocator *allocator = NULL;
  GstStructure *config = NULL;
  GstCaps *outcaps = NULL;
  guint min = 0, max = 0, size = 0;

  GstBufferPool *pool_up = NULL;
  guint min_up = 0, max_up = 0, size_up = 0;
  gboolean update_pool = FALSE;

  if (!query) {
    GST_ERROR_OBJECT(self, "query is null");
    return FALSE;
  }
  gst_query_parse_allocation (query, &outcaps, NULL);

  GST_INFO_OBJECT (self, "decide_query %" GST_PTR_FORMAT ", query %" GST_PTR_FORMAT,
      decide_query, query);

  GST_INFO_OBJECT (self, "query caps %" GST_PTR_FORMAT, outcaps);

  if (decide_query && gst_query_get_n_allocation_pools (decide_query) > 0) {
    GST_INFO_OBJECT (self, "decide_query has pool");
    gst_query_parse_nth_allocation_pool (decide_query, 0, &pool, &size, &min, &max);
    GST_INFO_OBJECT (self, "downstream proposed pool %p,size %u,min %u,max %u",
        pool, size, min, max);

    if (gst_query_get_n_allocation_pools (query) > 0) {
      gst_query_parse_nth_allocation_pool (query, 0, &pool_up, &size_up, &min_up, &max_up);
      GST_INFO_OBJECT (self, "upstream proposed pool %p,size %u,min %u,max %u",
          pool_up, min_up, min_up, max_up);
      if (pool_up) {
        gst_object_unref (pool_up);
        update_pool = TRUE;
      }
    }

    // config = gst_buffer_pool_get_config (pool);
    // gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
    // gst_buffer_pool_config_get_allocator (config, &allocator, NULL);
    // gst_buffer_pool_config_set_allocator (config, allocator, NULL);
    // gst_buffer_pool_set_config (pool, config);

    // GST_INFO_OBJECT (self, "pool %p, allocator %p", pool, allocator);

    if (update_pool) {
      GST_INFO_OBJECT (self, "from decide query update pool");
      gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
    }
    else {
      GST_INFO_OBJECT (self, "from decide query add pool");
      gst_query_add_allocation_pool (query, pool, size, min, max);
    }

    if (pool)
      gst_object_unref (pool);

    return TRUE;
  } else {
    GST_INFO_OBJECT (self, "query pool");
    if (gst_query_get_n_allocation_pools (query) > 0) {
      GST_INFO_OBJECT (self, "query has pool");
      gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
      GST_INFO_OBJECT (self, "upstream proposed pool %p,size %u,min %u,max %u",
          pool, min_up, min_up, max_up);
      if (pool) {
        gst_object_unref (pool);
        pool = NULL;
        update_pool = TRUE;
      }
    }

    if (pool)
      gst_object_unref (pool);

    GST_INFO_OBJECT (self, "new pool");
    /* always use its own pool at this time */
    pool = gst_ext_pool_new (self->out_ubwc);
    if (!pool) {
      GST_ERROR_OBJECT (self, "pool new error");
      return FALSE;
    }
    //self->pool = pool;

    /* only support dmabuf allocator at this time */
    allocator = gst_dmabuf_allocator_new ();
    if (!allocator) {
      GST_ERROR_OBJECT (self, "allocator new error");
      gst_clear_object (&pool);
      return FALSE;
    }


    GST_INFO_OBJECT (self, "size %u, info size %u", size, (guint) info->size);
    size = MAX (size, info->size);
    min = MAX (min, EXTPOOLSINK_MIN_OUT_BUF);
    max = MAX (MAX (min, max), EXTPOOLSINK_MAX_OUT_BUF);

    GST_INFO_OBJECT (self, "ext pool %p, allocator %p, size %u, min %d, max %d",
        pool, allocator, size, min, max);

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
    gst_buffer_pool_config_set_allocator (config, allocator, NULL);
    gst_buffer_pool_set_config (pool, config);

    if (update_pool) {
      GST_INFO_OBJECT (self, "from query update pool");
      gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
    }
    else {
      GST_INFO_OBJECT (self, "from query add pool");
      gst_query_add_allocation_pool (query, pool, size, min, max);
    }
  }

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
    decide_query, query)) {
    return FALSE;
  }

  GST_INFO_OBJECT (self, "exit");
  return TRUE;
}

static gboolean
gst_ext_pool_sink_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstExtPoolSink *self = GST_EXTPOOLSINK (trans);
  GstVideoInfo *info = &self->out_info;
  GstBufferPool *pool = NULL;
  GstCaps *outcaps = NULL;
  GstAllocator *allocator;
  GstStructure *config;
  guint min = 0, max = 0, size = 0;
  gboolean update_pool;

  GST_INFO_OBJECT (self, "%" GST_PTR_FORMAT, query);

  /* Consider downstream proposed min/max/size if provided. */
  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    GST_INFO_OBJECT (self, "downstream proposed pool %p,size %u,min %u,max %u",
        pool, size, min, max);

    update_pool = TRUE;
  } else {
    GST_INFO_OBJECT (self, "downstream not propose pool");
    update_pool = FALSE;
  }

  gst_query_parse_allocation (query, &outcaps, NULL);

  GST_INFO_OBJECT (self, "size %u, info size %u", size, (guint) info->size);
  size = MAX (size, info->size);
  min = MAX (min, EXTPOOLSINK_MIN_OUT_BUF);
  max = MAX (MAX (min, max), EXTPOOLSINK_MAX_OUT_BUF);

  GST_INFO_OBJECT (self, "pool size %u, min %u, max %u", size, min, max);

  if (pool)
    gst_object_unref (pool);

  /* always use its own pool at this time */
  pool = gst_ext_pool_new (self->out_ubwc);
  if (!pool) {
    GST_ERROR_OBJECT (self, "pool new error");
    return FALSE;
  }
  //self->pool = pool;

  /* only support dmabuf allocator at this time */
  allocator = gst_dmabuf_allocator_new ();
  if (!allocator) {
    GST_ERROR_OBJECT (self, "allocator new error");
    gst_clear_object (&pool);
    return FALSE;
  }

  GST_INFO_OBJECT (self, "ext pool %p, allocator %p", pool, allocator);

  config = gst_buffer_pool_get_config (pool);
  // always add video meta in its own pool
  //gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_set_config (pool, config);

  if (update_pool) {
    GST_INFO_OBJECT (self, "update pool");
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  }
  else {
    GST_INFO_OBJECT (self, "add pool");
    gst_query_add_allocation_pool (query, pool, size, min, max);
  }

  gst_object_unref (pool);

  /* GstBaseTransform manages buffer pool created by subclass's _decide_allocation(),
   * so no need to set pool active or inactive in subclass implementation.
   * gstbasetransform.c:1657:default_prepare_output_buffer:<extpoolsink0> setting pool 0x7f9d600180a0 active */

#if 0
  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
#else
  /* No need to call parent_class decide_allocation? yes for qvconv of cases waylandsink, filesink & omxh264enc */
  return TRUE;
#endif
}

static void
gst_ext_pool_sink_reference_buffer_hold (GstExtPoolSink * self,
    GstBuffer * buffer)
{
  GST_LOG_OBJECT (self, "ref_buf_held %p, buffer %p",
      self->ref_buf_held, buffer);

  if (self->ref_buf_held)
    gst_buffer_unref (self->ref_buf_held);

  self->ref_buf_held = gst_buffer_ref (buffer);
}

static void
gst_ext_pool_sink_reference_buffer_free (GstExtPoolSink * self)
{
  GST_LOG_OBJECT (self, "ref_buf_held %p", self->ref_buf_held);

  if (self->ref_buf_held) {
    gst_buffer_unref (self->ref_buf_held);
    self->ref_buf_held = NULL;
  }
}

/* This function is just for testing without GPU deinterlace. */
static inline gboolean
gst_ext_pool_sink_do_frame_copy (GstExtPoolSink * self,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstVideoFrame src, dst;

  GST_LOG_OBJECT (self, "just copy without format conversion");

  if (!gst_video_frame_map (&src, &self->in_info, inbuf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "map inbuf error");
    goto invalid_buffer;
  }

  /* Since gst_fd_mem_map() only return address if mapping flags are a subset
   * of the previous flags, here map it as read and write, thereafter, mapping
   * it again as read in filesink shall succeed, otherwise, mapping late may
   * fail if GstMapFlags is not the subset of the previous flags. */
  if (!gst_video_frame_map (&dst, &self->out_info, outbuf,
          GST_MAP_READ | GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "map outbuf error");
    gst_video_frame_unmap (&src);
    goto invalid_buffer;
  }

  /* need to remove format check predicate in gst_video_frame_copy(),
   * RGBx is pushed directly to ximagesink as BGRx for display */
  if (!gst_video_frame_copy (&dst, &src))
    GST_ERROR_OBJECT (self, "copy buffer error");

  gst_video_frame_unmap (&dst);
  gst_video_frame_unmap (&src);

  return TRUE;

invalid_buffer:
  return FALSE;
}

static gboolean
gst_ext_pool_sink_do_transform (GstExtPoolSink * self,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  return gst_ext_pool_sink_do_frame_copy (self, inbuf, outbuf);
}

static GstFlowReturn
gst_ext_pool_sink_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstExtPoolSink *self = GST_EXTPOOLSINK (trans);
  GstMemory *memory = NULL;

  GST_INFO_OBJECT (self, "enter %p", buf);
  memory = gst_buffer_peek_memory (buf, 0);
  if (memory) {
    gint fd = gst_dmabuf_memory_get_fd (memory);
    GST_DEBUG_OBJECT (self, "buffer fd: %d in buffer: %p", fd, buf);
  } else {
    GST_WARNING_OBJECT (self, "failed to get buf memory from %p", buf);
  }

  return ret;
}

/* this function is called in default_generate_output() by
 * gst_base_transform_chain() in case of non-passthrough.
 */
static GstFlowReturn
gst_ext_pool_sink_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstExtPoolSink *self = GST_EXTPOOLSINK (trans);

  if (self->silent == FALSE)
    GST_LOG_OBJECT (self, "inbuf=%p, outbuf=%p", inbuf, outbuf);

  if (!gst_ext_pool_sink_do_transform (self, inbuf, outbuf))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

/* Override GstBaseTransformClass's default_copy_metadata() not to copy
 * buffer flags like interlaced flags. Instead, extpoolsink should set
 * buffer flags itself to reflect the reality. */
static gboolean
gst_ext_pool_sink_copy_metadata (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstExtPoolSink *self = GST_EXTPOOLSINK (trans);

  GST_LOG_OBJECT (self, "copy timestamps");

  /* when we get here, the outbuf should be writable */
  GST_BUFFER_PTS (outbuf) = GST_BUFFER_PTS (inbuf);
  GST_BUFFER_DTS (outbuf) = GST_BUFFER_DTS (inbuf);
  GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET (inbuf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);
  GST_BUFFER_OFFSET_END (outbuf) = GST_BUFFER_OFFSET_END (inbuf);

  //gst_buffer_copy_into (outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return TRUE;
}

static gboolean
gst_ext_pool_sink_stop (GstBaseTransform * trans)
{
  GstExtPoolSink *self = GST_EXTPOOLSINK (trans);

  /* gstbasetransform manages lifecycle of buffer pool totally */

  gst_ext_pool_sink_reference_buffer_free (self);

  GST_DEBUG_OBJECT (self, "done");

  return TRUE;
}

static void
gst_ext_pool_sink_finalize (GObject * obj)
{
  GstExtPoolSink *self = GST_EXTPOOLSINK (obj);

  GST_INFO_OBJECT (self, "done");
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/* initialize the extpoolsink's class */
static void
gst_ext_pool_sink_class_init (GstExtPoolSinkClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;
  GstVideoFilterClass *filter_class = (GstVideoFilterClass *) klass;

  GST_INFO ("start");

  gobject_class->set_property = gst_ext_pool_sink_set_property;
  gobject_class->get_property = gst_ext_pool_sink_get_property;
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_ext_pool_sink_finalize);

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  gst_element_class_set_static_metadata (gstelement_class,
      "Video Extpoolsink",
      "Extpoolsink/Video",
      "Sink video frame to external pool", "QTI");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ext_pool_sink_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_ext_pool_sink_fixate_caps);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_ext_pool_sink_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ext_pool_sink_decide_allocation);
  trans_class->copy_metadata =
      GST_DEBUG_FUNCPTR (gst_ext_pool_sink_copy_metadata);
  // trans_class->transform = GST_DEBUG_FUNCPTR (gst_ext_pool_sink_transform);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_ext_pool_sink_transform_ip);
  trans_class->passthrough_on_same_caps = TRUE;
  filter_class->set_info = GST_DEBUG_FUNCPTR (gst_ext_pool_sink_set_info);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_ext_pool_sink_stop);
}

static gboolean
gst_ext_pool_sink_load_libs (void)
{
  extern gboolean ext_dmabuf_load_libs_once (void);
  gboolean ret = TRUE;

  if (!ext_dmabuf_load_libs_once ()) {
    GST_ERROR ("failed to load libs");
    ret = FALSE;
  }

  return ret;
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_ext_pool_sink_init (GstExtPoolSink * self)
{
  if (!gst_ext_pool_sink_load_libs ())
    return;

  gst_video_info_init (&self->in_info);
  gst_video_info_init (&self->out_info);
  self->ref_buf_held = NULL;
  self->active = FALSE;
  self->silent = FALSE;
  self->in_dmabuf = FALSE;
  self->out_dmabuf = FALSE;
  self->in_ubwc = FALSE;
  self->out_ubwc = FALSE;

  GST_INFO_OBJECT (self, "done");
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
extpoolsink_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_ext_pool_sink_debug, "extpoolsink", 0,
      "extpoolsink debug category");

  return gst_element_register (plugin, "extpoolsink",
      GST_RANK_SECONDARY, GST_TYPE_EXTPOOLSINK);
}

/* gstreamer looks for this structure to register extpoolsink */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    extpoolsink,
    "video extpoolsink",
    extpoolsink_init, PACKAGE_VERSION "-" G_STRINGIFY(GST_VERSION_MAJOR) "/" G_STRINGIFY(GST_VERSION_MINOR) "/" G_STRINGIFY(GST_VERSION_MICRO), GST_LICENSE_UNKNOWN, PACKAGE_NAME, "-")
