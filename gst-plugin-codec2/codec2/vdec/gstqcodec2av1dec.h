// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QCODEC2_AV1_DEC_H__
#define __GST_QCODEC2_AV1_DEC_H__

#include <gst/gst.h>
#include "gstqcodec2vdec.h"

G_BEGIN_DECLS
#define GST_TYPE_QCODEC2_AV1_DEC \
  (gst_qcodec2_av1_dec_get_type())
#define GST_QCODEC2_AV1_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QCODEC2_AV1_DEC,GstQcodec2AV1Dec))
#define GST_QCODEC2_AV1_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QCODEC2_AV1_DEC,GstQcodec2AV1DecClass))
#define GST_QCODEC2_AV1_DEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_QCODEC2_AV1_DEC,GstQcodec2AV1DecClass))
#define GST_IS_QCODEC2_AV1_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QCODEC2_AV1_DEC))
#define GST_IS_QCODEC2_AV1_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QCODEC2_AV1_DEC))
typedef struct _GstQcodec2AV1Dec GstQcodec2AV1Dec;
typedef struct _GstQcodec2AV1DecClass GstQcodec2AV1DecClass;

struct _GstQcodec2AV1Dec
{
  GstQcodec2Vdec parent;
};

struct _GstQcodec2AV1DecClass
{
  GstQcodec2VdecClass parent_class;
};

GType gst_qcodec2_av1_dec_get_type (void);

G_END_DECLS
#endif /* __GST_QCODEC2_AV1_DEC_H__ */
