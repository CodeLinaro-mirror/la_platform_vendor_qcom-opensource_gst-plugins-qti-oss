// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QVAISPOOL_H__
#define __GST_QVAISPOOL_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_QVAIS_POOL (gst_qvais_pool_get_type())
G_DECLARE_FINAL_TYPE (GstQvaisPool, gst_qvais_pool,
    GST, QVAIS_POOL, GstBufferPool)

struct _GstQvaisPool
{
  GstBufferPool parent;

  GstVideoInfo info;
  GstVideoInfo aligned_info;
  GstAllocator *allocator;
  GstAllocationParams params;
  gboolean ubwc;
  gboolean align_info_done;
};

GstBufferPool *gst_qvais_pool_new (gboolean ubwc);

G_END_DECLS
#endif /* __GST_QVAISPOOL_H__ */
