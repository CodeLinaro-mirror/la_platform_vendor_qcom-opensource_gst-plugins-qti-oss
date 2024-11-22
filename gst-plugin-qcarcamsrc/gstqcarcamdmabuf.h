// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QCARCAM_DMABUF_H__
#define __GST_QCARCAM_DMABUF_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gbm_priv.h"
typedef struct
{
  gint fd;
  gint meta_fd;                 /* GBM meta fd */
  struct gbm_bo *bo;
  void *data;
  guint64 modifier;
  gsize size;
  gint stride;

  gint format;                  /* GBM format */
  gint width;
  gint height;
  gboolean ubwc;
  generic_buf_layout_t layout;
  guint buffer_size_dimensions;
  gpointer ptr;
} DmaBufDesc;


gboolean qcarcam_dmabuf_load_libs_once (void);

gboolean qcarcam_dmabuf_alloc (DmaBufDesc ** desc,
    const GstVideoInfo *info, gboolean ubwc);

gint qcarcam_dmabuf_get_fd (const DmaBufDesc * desc);

gsize qcarcam_dmabuf_get_size (const DmaBufDesc * desc);

guint64 qcarcam_dmabuf_get_modifier (const DmaBufDesc * desc);

void qcarcam_dmabuf_align_info (const DmaBufDesc * desc, GstVideoInfo *info);

void qcarcam_dmabuf_free (DmaBufDesc * desc);

#endif /* __GST_QCARCAM_DMABUF_H__ */
