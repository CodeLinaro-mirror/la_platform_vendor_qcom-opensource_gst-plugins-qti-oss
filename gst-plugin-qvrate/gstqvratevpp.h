// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QVRATEVPP_H__
#define __GST_QVRATEVPP_H__

#include <inttypes.h>
#include <vpp.h>
#include <gst/gst.h>
#include <gst/video/video.h>

struct qvratevpp_buf_desc
{
  gint fd;
  GstVideoFormat format;
  gint width;
  gint height;
  gint stride;
  gsize size;
  gboolean ubwc;
};

typedef struct qvrate_vpp_buf_desc QvrateVppBufDesc;

void *qvratevpp_init(uint32_t flags, struct vpp_callbacks cb);

void qvratevpp_term(void *ctx);

gboolean qvratevpp_open(void *ctx);

gboolean qvratevpp_close(void *ctx);

gboolean qvratevpp_drain(void *ctx);

gboolean qvratevpp_set_ctrl(void *ctx, struct hqv_control ctrl);

gboolean qvratevpp_set_parameter(void *ctx, enum vpp_port port, struct vpp_port_param param);

gboolean qvratevpp_get_buf_requirements(void *ctx, struct vpp_requirements *req);

gboolean qvratevpp_queue_buf(void *ctx, enum vpp_port port, struct vpp_buffer *buf);

gboolean qvratevpp_flush(void *ctx, enum vpp_port port);

gboolean qvratevpp_set_vid_prop(void *ctx, struct video_property prop);

#endif /* __GST_QVRATEVPP_H__ */
