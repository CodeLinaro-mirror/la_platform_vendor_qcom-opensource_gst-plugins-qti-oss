// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QVIDC_AV1_DEC_H__
#define __GST_QVIDC_AV1_DEC_H__

#include <gst/gst.h>
#include "gstqvidcvdec.h"

G_BEGIN_DECLS
#define GST_TYPE_QVIDC_AV1_DEC \
  (gst_qvidc_av1_dec_get_type())
#define GST_QVIDC_AV1_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QVIDC_AV1_DEC,GstQvidcAV1Dec))
#define GST_QVIDC_AV1_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QVIDC_AV1_DEC,GstQvidcAV1DecClass))
#define GST_QVIDC_AV1_DEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_QVIDC_AV1_DEC,GstQvidcAV1DecClass))
#define GST_IS_QVIDC_AV1_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QVIDC_AV1_DEC))
#define GST_IS_QVIDC_AV1_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QVIDC_AV1_DEC))
typedef struct _GstQvidcAV1Dec GstQvidcAV1Dec;
typedef struct _GstQvidcAV1DecClass GstQvidcAV1DecClass;

struct _GstQvidcAV1Dec
{
  GstQvidcVdec parent;
};

struct _GstQvidcAV1DecClass
{
  GstQvidcVdecClass parent_class;
};

GType gst_qvidc_av1_dec_get_type (void);

G_END_DECLS
#endif /* __GST_QVIDC_AV1_DEC_H__ */
