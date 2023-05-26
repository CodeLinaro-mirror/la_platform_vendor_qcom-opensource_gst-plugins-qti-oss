// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QVRATEPOOL_H__
#define __GST_QVRATEPOOL_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_QVRATE_POOL (gst_qvrate_pool_get_type())
G_DECLARE_FINAL_TYPE (GstQvratePool, gst_qvrate_pool,
GST, QVRATE_POOL, GstBufferPool)

struct _GstQvratePool {
  GstBufferPool parent;

  GstVideoInfo info;
  GstVideoInfo aligned_info;
  GstAllocator *allocator;
  GstAllocationParams params;
  gboolean ubwc;
  gboolean done_align_info;
};

GstBufferPool * gst_qvrate_pool_new (gboolean ubwc);

G_END_DECLS

#endif /* __GST_QVRATEPOOL_H__ */
