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
  GST_DEBUG("VPP term ctx:%p", ctx);
  if (!ctx) {
    GST_ERROR("error input parameter ctx:%p", ctx);
    return;
  }

  vpp_term(ctx);

  GST_DEBUG("VPP term done ctx:%p", ctx);
}

gboolean qvratevpp_open(void *ctx)
{
  gboolean ret = FALSE;
  enum vpp_error vpp_ret = VPP_OK;

  GST_DEBUG("VPP open ctx:%p", ctx);
  if (!ctx) {
    GST_ERROR("error input parameter ctx:%p", ctx);
    return ret;
  }

  vpp_ret = vpp_open(ctx);
  if (vpp_ret == VPP_OK)
    ret = TRUE;
  else
    GST_ERROR ("vpp_open error: %d, ctx: %p", vpp_ret, ctx);

  return ret;
}

gboolean qvratevpp_close(void *ctx)
{
  gboolean ret = FALSE;
  enum vpp_error vpp_ret = VPP_OK;

  GST_DEBUG("VPP close ctx:%p", ctx);
  if (!ctx) {
    GST_ERROR("error input parameter ctx:%p", ctx);
    return ret;
  }

  vpp_ret = vpp_close(ctx);
  if (vpp_ret == VPP_OK)
    ret = TRUE;
  else
    GST_ERROR ("vpp_close error: %d, ctx: %p", vpp_ret, ctx);

  GST_DEBUG("VPP close done ctx:%p", ctx);

  return ret;
}

gboolean qvratevpp_drain(void *ctx)
{
  gboolean ret = FALSE;
  enum vpp_error vpp_ret = VPP_OK;

  GST_DEBUG("VPP drain ctx:%p", ctx);
  if (!ctx) {
    GST_ERROR("error input parameter ctx:%p", ctx);
    return ret;
  }

  vpp_ret = vpp_drain(ctx);
  if (vpp_ret == VPP_OK)
    ret = TRUE;
  else
    GST_ERROR ("vpp_drain error: %d, ctx: %p", vpp_ret, ctx);

  return ret;
}

gboolean qvratevpp_set_ctrl(void *ctx, struct hqv_control ctrl)
{
  gboolean ret = FALSE;
  enum vpp_error vpp_ret = VPP_OK;

  GST_DEBUG("VPP set ctrl ctx:%p", ctx);
  if (!ctx) {
    GST_ERROR("error input parameter ctx:%p", ctx);
    return ret;
  }

  vpp_ret = vpp_set_ctrl(ctx, ctrl);
  if (vpp_ret == VPP_OK)
    ret = TRUE;
  else
    GST_ERROR ("vpp_set_ctrl error: %d, ctx: %p", vpp_ret, ctx);

  return ret;
}

gboolean qvratevpp_set_parameter(void *ctx, enum vpp_port port, struct vpp_port_param param)
{
  gboolean ret = FALSE;
  enum vpp_error vpp_ret = VPP_OK;

  GST_DEBUG("VPP set param ctx:%p, port %d, fmt %d, width %d, height %d, stride %d, scanlines %d",
    ctx, port, param.fmt, param.width, param.height, param.stride, param.scanlines);
  if (!ctx || (port != VPP_PORT_INPUT && port != VPP_PORT_OUTPUT)) {
    GST_ERROR("error input parameter ctx:%p port:%u", ctx, port);
    return ret;
  }

  vpp_ret = vpp_set_parameter(ctx, port, param);
  if (vpp_ret == VPP_OK)
    ret = TRUE;
  else
    GST_ERROR ("vpp_set_parameter error: %d, ctx: %p", vpp_ret, ctx);

  return ret;
}

gboolean qvratevpp_get_buf_requirements(void *ctx, struct vpp_requirements *req)
{
  gboolean ret = FALSE;
  enum vpp_error vpp_ret = VPP_OK;

  GST_DEBUG("VPP get buf req ctx:%p", ctx);
  if (!ctx || !req) {
    GST_ERROR("error input parameter ctx:%p req:%p", ctx, req);
    return ret;
  }

  vpp_ret = vpp_get_buf_requirements(ctx, req);
  if (vpp_ret == VPP_OK)
    ret = TRUE;
  else
    GST_ERROR ("vpp_get_buf_requirements error: %d, ctx: %p", vpp_ret, ctx);

  return ret;
}

gboolean qvratevpp_queue_buf(void *ctx, enum vpp_port port, struct vpp_buffer *buf)
{
  gboolean ret = FALSE;
  enum vpp_error vpp_ret = VPP_OK;
  GstBuffer *gst_buf = NULL;


  if (!ctx || !buf || (port != VPP_PORT_INPUT && port != VPP_PORT_OUTPUT)) {
    GST_ERROR("error input parameter cxt:%p port:%u buf=%p", ctx, port, buf);
    return ret;
  }

  gst_buf = buf->pvGralloc;
  GST_DEBUG("VPP queue buf, port=%u, vpp buf=%p, gst buf=%p", port, buf, gst_buf);

  vpp_ret = vpp_queue_buf(ctx, port, buf);
  if (vpp_ret == VPP_OK) {
    GST_DEBUG("qvratevpp_queue_buf queue buffer success");
    ret = TRUE;
  } else
    GST_ERROR ("vpp_queue_buf error: %d, ctx: %p", vpp_ret, ctx);

  return ret;
}

gboolean qvratevpp_flush(void *ctx, enum vpp_port port)
{
  gboolean ret = FALSE;
  enum vpp_error vpp_ret = VPP_OK;

  GST_DEBUG("VPP flush port=%u ctx:%p", port, ctx);
  if (!ctx || (port != VPP_PORT_INPUT && port != VPP_PORT_OUTPUT)) {
    GST_ERROR("error input parameter ctx:%p port:%u", ctx, port);
    return ret;
  }

  vpp_ret = vpp_flush(ctx, port);
  if (vpp_ret == VPP_OK)
    ret = TRUE;
  else
    GST_ERROR ("vpp_flush error: %d, ctx: %p", vpp_ret, ctx);

  return ret;
}

gboolean qvratevpp_set_vid_prop(void *ctx, struct video_property prop)
{
  gboolean ret = FALSE;
  enum vpp_error vpp_ret = VPP_OK;

  GST_DEBUG("VPP set vid prop ctx:%p", ctx);
  if (!ctx) {
    GST_ERROR("error input parameter ctx:%p", ctx);
    return ret;
  }

  vpp_ret = vpp_set_vid_prop(ctx, prop);
  if (vpp_ret == VPP_OK)
    ret = TRUE;
  else
    GST_ERROR ("vpp_set_vid_prop error: %d, ctx: %p", vpp_ret, ctx);

  return ret;
}

