// Copyright (c) 2022, 2025 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_GSTEXTPOOL_H__
#define __GST_GSTEXTPOOL_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS
#define GST_TYPE_EXT_POOL (gst_ext_pool_get_type())
G_DECLARE_FINAL_TYPE (GstExtPool, gst_ext_pool,
    GST, EXT_POOL, GstBufferPool)

struct _GstExtPool {
  GstBufferPool parent;

  GstVideoInfo info;
  GstVideoInfo aligned_info;

  GstAllocator *allocator;
  GstAllocationParams params;

  gboolean ubwc;
  gboolean done_align_info;
};

GstBufferPool * gst_ext_pool_new (gboolean ubwc);

/* only can get aligned info after first allocation */
static inline GstVideoInfo *
gst_ext_pool_aligned_info (const GstBufferPool * pool)
{
  GstExtPool *self = GST_EXT_POOL ((GstBufferPool *) pool);

  return &self->aligned_info;
}

gint
gst_ext_pool_buffer_get_fd (const GstBufferPool * pool,
    const GstBuffer * buffer);

gboolean
gst_ext_pool_buffer_get_ubwc (const GstBufferPool * pool,
    const GstBuffer * buffer);

G_END_DECLS
#endif /* __GST_GSTEXTPOOL_H__ */
