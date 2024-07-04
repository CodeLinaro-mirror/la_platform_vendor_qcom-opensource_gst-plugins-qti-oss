// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QVAISVPP_H__
#define __GST_QVAISVPP_H__

#include <inttypes.h>
#include <vpp.h>
#include <gst/gst.h>
#include <gst/video/video.h>

struct qvaisvpp_buf_desc
{
  gint fd;
  GstVideoFormat format;
  gint width;
  gint height;
  gint stride;
  gsize size;
  gboolean ubwc;
};

typedef struct qvais_vpp_buf_desc QvaisVppBufDesc;

void *qvaisvpp_init (uint32_t flags, struct vpp_callbacks cb);

void qvaisvpp_term (void *ctx);

gboolean qvaisvpp_open (void *ctx);

gboolean qvaisvpp_close (void *ctx);

gboolean qvaisvpp_drain (void *ctx);

gboolean qvaisvpp_set_ctrl (void *ctx, struct hqv_control ctrl);

gboolean qvaisvpp_set_parameter (void *ctx, enum vpp_port port,
    struct vpp_port_param param);

gboolean qvaisvpp_get_buf_requirements (void *ctx,
    struct vpp_requirements *req);

gboolean qvaisvpp_queue_buf (void *ctx, enum vpp_port port,
    struct vpp_buffer *buf);

gboolean qvaisvpp_flush (void *ctx, enum vpp_port port);

#endif /* __GST_QVAISVPP_H__ */
