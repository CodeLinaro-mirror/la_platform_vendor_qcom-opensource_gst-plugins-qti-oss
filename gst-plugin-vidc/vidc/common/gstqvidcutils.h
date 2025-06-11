// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QVIDC_UTILS_H__
#define __GST_QVIDC_UTILS_H__

#include <gst/gst.h>
#include "vidcwrapper.h"

#define QVIDC_MIN_OUTBUFFERS 6
#define QVIDC_MAX_OUTBUFFERS 32
#define COMMON_FRAMERATE 30

typedef struct _GstVIDCComp GstVIDCComp;

struct _GstVIDCComp
{
  GstMiniObject parent_instance;

  void *comp;
};

GType gst_vidc_comp_get_type (void);


GstVIDCComp *gst_vidc_comp_create (void *comp);
GstVIDCComp *gst_vidc_comp_ref (GstVIDCComp * comp);
void gst_vidc_comp_unref (GstVIDCComp * comp);

ConfigParams make_framerate_param (gfloat framerate, gboolean is_input);

#endif /* __GST_QVIDC_UTILS_H__ */
