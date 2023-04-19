// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqvratevpp.h"

#include <gst/gstinfo.h>

GST_DEBUG_CATEGORY_EXTERN (gst_qvrate_debug);
#define GST_CAT_DEFAULT gst_qvrate_debug

void *qvratevpp_init(uint32_t flags, struct vpp_callbacks cb)
{
  GST_DEBUG("VPP init flags:0x%x", flags);

  return vpp_init (flags, cb);
}

void qvratevpp_term(void *ctx)
{
  if (!ctx) {
    GST_ERROR("error input parameter ctx:%p", ctx);
    return;
  }

  vpp_term(ctx);
}

gboolean qvratevpp_open(void *ctx)
{
  gboolean ret = FALSE;

  if (!ctx) {
    GST_ERROR("error input parameter ctx:%p", ctx);
    return ret;
  }

  if (vpp_open(ctx) == VPP_OK)
    ret = TRUE;

  return ret;
}

gboolean qvratevpp_close(void *ctx)
{
  gboolean ret = FALSE;

  if (!ctx) {
    GST_ERROR("error input parameter ctx:%p", ctx);
    return ret;
  }

  if (vpp_close(ctx) == VPP_OK)
    ret = TRUE;

  return ret;
}

gboolean qvratevpp_set_ctrl(void *ctx, struct hqv_control ctrl)
{
  gboolean ret = FALSE;

  if (!ctx) {
    GST_ERROR("error input parameter ctx:%p", ctx);
    return ret;
  }

  if (vpp_set_ctrl(ctx, ctrl) == VPP_OK)
    ret = TRUE;

  return ret;
}

gboolean qvratevpp_set_parameter(void *ctx, enum vpp_port port, struct vpp_port_param param)
{
  gboolean ret = FALSE;

  if (!ctx || (port != VPP_PORT_INPUT && port != VPP_PORT_OUTPUT)) {
    GST_ERROR("error input parameter ctx:%p port:%u", ctx, port);
    return ret;
  }

  if (vpp_set_parameter(ctx, port, param) == VPP_OK)
    ret = TRUE;

  return ret;
}

gboolean qvratevpp_get_buf_requirements(void *ctx, struct vpp_requirements *req)
{
  gboolean ret = FALSE;

  if (!ctx || !req) {
    GST_ERROR("error input parameter ctx:%p req:%p", ctx, req);
    return ret;
  }

  if (vpp_get_buf_requirements(ctx, req) == VPP_OK)
    ret = TRUE;

  return ret;
}

gboolean qvratevpp_queue_buf(void *ctx, enum vpp_port port, struct vpp_buffer *buf)
{
  gboolean ret = FALSE;

  GST_DEBUG("port=%u, buf=%p", port, buf);

  if (!ctx || !buf || (port != VPP_PORT_INPUT && port != VPP_PORT_OUTPUT)) {
    GST_ERROR("error input parameter cxt:%p port:%u buf=%p", ctx, port, buf);
    return ret;
  }

  if (vpp_queue_buf(ctx, port, buf) == VPP_OK) {
    GST_DEBUG("qvratevpp_queue_buf queue buffer success");
    ret = TRUE;
  }
  GST_DEBUG("qvratevpp_queue_buf queue buffer ret %d", ret);

  return ret;
}

gboolean qvratevpp_reconfigure(void *ctx,
                         struct vpp_port_param input_param,
                         struct vpp_port_param output_param)
{
  gboolean ret = FALSE;

  if (!ctx) {
    GST_ERROR("error input parameter ctx:%p", ctx);
    return ret;
  }

  if (vpp_reconfigure(ctx, input_param, output_param) == VPP_OK)
    ret = TRUE;

  return ret;
}

gboolean qvratevpp_flush(void *ctx, enum vpp_port port)
{
  gboolean ret = FALSE;

  if (!ctx || (port != VPP_PORT_INPUT && port != VPP_PORT_OUTPUT)) {
    GST_ERROR("error input parameter ctx:%p port:%u", ctx, port);
    return ret;
  }

  if (vpp_flush(ctx, port) == VPP_OK)
    ret = TRUE;

  return ret;
}

gboolean qvratevpp_set_vid_prop(void *ctx, struct video_property prop)
{
  gboolean ret = FALSE;

  if (!ctx) {
    GST_ERROR("error input parameter ctx:%p", ctx);
    return ret;
  }

  if (vpp_set_vid_prop(ctx, prop) == VPP_OK)
    ret = TRUE;

  return ret;
}

