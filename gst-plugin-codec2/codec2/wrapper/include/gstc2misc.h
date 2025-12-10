/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GSTC2MISC_H__
#define __GSTC2MISC_H__

#include <glib.h>
#include <gst/gst.h>


#ifdef SLOG_TIP
#undef SLOG_TIP
#endif
#ifdef SG_ERR
#undef SG_ERR
#endif
#ifdef SG_ERR_OBJ
#undef SG_ERR_OBJ
#endif
#ifdef SG_INFO
#undef SG_INFO
#endif
#ifdef SG_INFO_OBJ
#undef SG_INFO_OBJ
#endif
#ifdef ENABLE_GSTC2_DEFAULT_SYSLOG
#undef ENABLE_GSTC2_DEFAULT_SYSLOG
#endif

#define ENABLE_GSTC2_DEFAULT_SYSLOG
#ifndef ENABLE_GSTC2_DEFAULT_SYSLOG
#define SG_ERR        GST_ERROR
#define SG_ERR_OBJ    GST_ERROR_OBJECT
#define SG_INFO       GST_INFO
#define SG_INFO_OBJ   GST_INFO_OBJECT
#else
#include <syslog.h>
#define SLOG_TIP "gst-c2:"
//if fmt contain GST specific format like GST_PTR_FORMAT/GST_SEGMENT_FORMAT or fmt is var, must use gst_info_strdup_printf() to explain those GST specific format and var
#define SG_ERR(fmt, args...)                                   \
    do {                                                       \
        gchar* __sg_str = gst_info_strdup_printf(fmt, ##args); \
        if (__sg_str) {                                        \
            syslog(LOG_ERR, SLOG_TIP "E: %s", __sg_str);       \
            g_free(__sg_str);                                  \
        }                                                      \
        GST_ERROR(fmt, ##args);                                \
    } while(0)
#define SG_ERR_OBJ(obj, fmt, args...)                          \
    do {                                                       \
        gchar* __sg_str = gst_info_strdup_printf(fmt, ##args); \
        if (__sg_str) {                                        \
            syslog(LOG_ERR, SLOG_TIP "E: %s", __sg_str);       \
            g_free(__sg_str);                                  \
        }                                                      \
        GST_ERROR_OBJECT(obj, fmt, ##args);                    \
    } while(0)
#define SG_INFO(fmt, args...)                                  \
    do {                                                       \
        gchar* __sg_str = gst_info_strdup_printf(fmt, ##args); \
        if (__sg_str) {                                        \
            syslog(LOG_NOTICE, SLOG_TIP "I: %s", __sg_str);    \
            g_free(__sg_str);                                  \
        }                                                      \
        GST_INFO(fmt, ##args);                                 \
    } while(0)
#define SG_INFO_OBJ(obj, fmt, args...)                         \
    do {                                                       \
        gchar* __sg_str = gst_info_strdup_printf(fmt, ##args); \
        if (__sg_str) {                                        \
            syslog(LOG_NOTICE, SLOG_TIP "I: %s", __sg_str);    \
            g_free(__sg_str);                                  \
        }                                                      \
        GST_INFO_OBJECT(obj, fmt, ##args);                     \
    } while(0)
#endif


#endif /* __GSTC2MISC_H__ */
