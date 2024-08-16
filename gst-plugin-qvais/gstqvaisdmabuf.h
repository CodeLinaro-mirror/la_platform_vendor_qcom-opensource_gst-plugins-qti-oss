// Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QVAIS_DMABUF_H__
#define __GST_QVAIS_DMABUF_H__

#include <gst/gst.h>
#include <gst/video/video.h>

/* DmaBufDesc is opaque to client */
typedef struct gbm_buf_desc DmaBufDesc;

gboolean qvais_dmabuf_load_libs_once (void);

gboolean qvais_dmabuf_alloc (DmaBufDesc ** desc,
    const GstVideoInfo * info, gboolean ubwc);

gint qvais_dmabuf_get_fd (const DmaBufDesc * desc);

gsize qvais_dmabuf_get_size (const DmaBufDesc * desc);

guint64 qvais_dmabuf_get_modifier (const DmaBufDesc * desc);

void qvais_dmabuf_align_info (const DmaBufDesc * desc, GstVideoInfo * info);

void qvais_dmabuf_free (DmaBufDesc * desc);

#endif /* __GST_QVAIS_DMABUF_H__ */
