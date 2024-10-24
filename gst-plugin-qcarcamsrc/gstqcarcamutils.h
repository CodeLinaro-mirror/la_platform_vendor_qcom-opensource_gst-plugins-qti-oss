// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QCARCAMUTILS_H__
#define __GST_QCARCAMUTILS_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/gstinfo.h>
#include <gst/gstmeta.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/gstbuffer.h>

#include <drm/drm_fourcc.h>
#include "gstqcarcamdmabuf.h"

typedef struct
{
  GstMeta meta;

  const DmaBufDesc *desc;
} GstQcarcamMeta;

GstQcarcamMeta *
_add_qcarcam_meta (GstBuffer * buffer, DmaBufDesc * desc);
void
_modifier_attach (GstBuffer * buffer, DmaBufDesc * desc);

DmaBufDesc *
gst_qcarcam_meta_get_desc (GstBuffer * buffer);

#endif /* __GST_QCARCAMUTILS_H__ */
