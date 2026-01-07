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

//As no environment var to decide qcarcamsrc syslog log level, just add default syslog. LOG_NOTICE is usually syslog default enabled level.
//In qcarcamsrc, for GST log <= info, should use SG_XXX(_LITE) log instead of GST_XXX log
#define ENABLE_QCARCAMSRC_DEFAULT_SYSLOG
#ifdef ENABLE_QCARCAMSRC_DEFAULT_SYSLOG
#include <syslog.h>
#define SLOG_TIP "qcarcamsrc:"
//SG mean syslog + GST
#define SG_ERR_LITE(fmt, args...)            \
    do {                        \
        syslog(LOG_ERR, SLOG_TIP "E: " fmt, ##args);    \
        GST_ERROR(fmt, ##args);                \
    } while(0)

#define SG_WARN_LITE(fmt, args...)            \
    do {                        \
        syslog(LOG_WARNING, SLOG_TIP "W: " fmt, ##args);    \
        GST_WARNING(fmt, ##args);            \
    } while(0)

#define SG_INFO_LITE(fmt, args...)            \
    do {                        \
        syslog(LOG_NOTICE, SLOG_TIP "I: " fmt, ##args);    \
        GST_INFO(fmt, ##args);                \
    } while(0)

//if fmt contain GST specific format like GST_PTR_FORMAT/GST_SEGMENT_FORMAT or fmt is var, must use below macro instead of their _LITE version
#define SG_ERR(fmt, args...)                \
    do {                        \
        gchar* s = gst_info_strdup_printf(fmt, ##args);    \
        syslog(LOG_ERR, SLOG_TIP "E: %s", s);        \
        g_free(s);                    \
        GST_ERROR(fmt, ##args);                \
    } while(0)

#define SG_WARN(fmt, args...)                \
    do {                        \
        gchar* s = gst_info_strdup_printf(fmt, ##args);    \
        syslog(LOG_WARNING, SLOG_TIP "W: %s", s);    \
        g_free(s);                    \
        GST_WARNING(fmt, ##args);            \
    } while(0)

#define SG_INFO(fmt, args...)                \
    do {                        \
        gchar* s = gst_info_strdup_printf(fmt, ##args);    \
        syslog(LOG_NOTICE, SLOG_TIP "I: %s", s);    \
        g_free(s);                    \
        GST_INFO(fmt, ##args);                \
    } while(0)

#define SG_ERR_OBJ_LITE(obj, fmt, args...)        \
    do {                        \
        syslog(LOG_ERR, SLOG_TIP "E: " fmt, ##args);    \
        GST_ERROR_OBJECT(obj, fmt, ##args);        \
    } while(0)

#define SG_WARN_OBJ_LITE(obj, fmt, args...)        \
    do {                        \
        syslog(LOG_WARNING, SLOG_TIP "W: " fmt, ##args);    \
        GST_WARNING_OBJECT(obj, fmt, ##args);        \
    } while(0)

#define SG_INFO_OBJ_LITE(obj, fmt, args...)        \
    do {                        \
        syslog(LOG_NOTICE, SLOG_TIP "I: " fmt, ##args);    \
        GST_INFO_OBJECT(obj, fmt, ##args);        \
    } while(0)

#define SG_ERR_OBJ(obj, fmt, args...)            \
    do {                        \
        gchar* s = gst_info_strdup_printf(fmt, ##args);    \
        syslog(LOG_ERR, SLOG_TIP "E: %s", s);        \
        g_free(s);                    \
        GST_ERROR_OBJECT(obj, fmt, ##args);        \
    } while(0)

#define SG_WARN_OBJ(obj, fmt, args...)            \
    do {                        \
        gchar* s = gst_info_strdup_printf(fmt, ##args);    \
        syslog(LOG_WARNING, SLOG_TIP "W: %s", s);    \
        g_free(s);                    \
        GST_WARNING_OBJECT(obj, fmt, ##args);        \
    } while(0)

#define SG_INFO_OBJ(obj, fmt, args...)            \
    do {                        \
        gchar* s = gst_info_strdup_printf(fmt, ##args);    \
        syslog(LOG_NOTICE, SLOG_TIP "I: %s", s);    \
        g_free(s);                    \
        GST_INFO_OBJECT(obj, fmt, ##args);        \
    } while(0)
#else  //ENABLE_QCARCAMSRC_DEFAULT_SYSLOG
#define SG_ERR_LITE         GST_ERROR
#define SG_WARN_LITE        GST_WARNING
#define SG_INFO_LITE        GST_INFO
#define SG_ERR              GST_ERROR
#define SG_WARN             GST_WARN
#define SG_INFO             GST_INFO
#define SG_ERR_OBJ          GST_ERROR_OBJECT
#define SG_WARN_OBJ         GST_WARNING_OBJECT
#define SG_INFO_OBJ         GST_INFO_OBJECT
#define SG_ERR_OBJ_LITE     GST_ERROR_OBJECT
#define SG_WARN_OBJ_LITE    GST_WARNING_OBJECT
#define SG_INFO_OBJ_LITE    GST_INFO_OBJECT
#endif  //end of #ifdef ENABLE_QCARCAMSRC_DEFAULT_SYSLOG

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
