// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QCARCAM_SRC_H__
#define __GST_QCARCAM_SRC_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>

#include "gstqcarcamdmabuf.h"
#include "gstqcarcamqcx.h"
#include "gstqcarcamutils.h"

#define GST_TYPE_QCARCAM_SRC (gst_qcarcam_src_get_type())
#define GST_QCARCAM_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QCARCAM_SRC,GstQcarcamSrc))
#define GST_QCARCAM_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QCARCAM_SRC,GstQcarcamSrcClass))
#define GST_IS_QCARCAM_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QCARCAM_SRC))
#define GST_IS_QCARCAM_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QCARCAM_SRC))

typedef struct _GstQcarcamSrc GstQcarcamSrc;
typedef struct _GstQcarcamSrcClass GstQcarcamSrcClass;

typedef struct {
  int counter;
  int period;
  struct timespec last_t;
  int last_t_valid;
  int period_upper;
  int period_lower;
}LOG_HEARTBEAT_CTX;

struct _GstQcarcamSrc
{
  GstPushSrc parent;

  QCarCamHndl_t hndl;
  gboolean is_ubwc;
  gboolean started;
  QCarCamEventCallback_t callback;
  uint32_t input_id;
  uint32_t mode_id;
  LOG_HEARTBEAT_CTX logbeat;
  QCarCamInput_t input;
  unsigned int queryfilled;
  QCarCamInputSrc_t source;
  QCarCamIspUsecaseConfig_t isp_config;
  GstAllocator *allocator;
  GQueue buffers;
  GstVideoInfo info;
  GstVideoInfo aligned_info;
  QCarCamBufferList_t buffer_list;
  GstCaps *prev_caps;
  DmaBufDesc *desc;
  uint32_t request_id;
  GMutex lock;
  GCond buf_cond;
  GMutex buf_lock;
};

struct _GstQcarcamSrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_qcarcam_src_get_type (void);

#endif /* __GST_QCARCAM_SRC_H__ */
