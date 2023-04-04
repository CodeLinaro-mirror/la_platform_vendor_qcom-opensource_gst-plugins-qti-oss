// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QCODEC2_UTILS_H__
#define __GST_QCODEC2_UTILS_H__

#include <gst/gst.h>

typedef struct _GstC2Comp GstC2Comp;

struct _GstC2Comp
{
  GstMiniObject parent_instance;

  void *comp;
};

GType gst_c2_comp_get_type (void);


GstC2Comp *gst_c2_comp_create (void *comp);
void *get_c2_comp (GstC2Comp * gst_c2_comp);
GstC2Comp *gst_c2_comp_ref (GstC2Comp * comp);
void gst_c2_comp_unref (GstC2Comp * comp);

#endif /* __GST_QCODEC2_UTILS_H__ */
