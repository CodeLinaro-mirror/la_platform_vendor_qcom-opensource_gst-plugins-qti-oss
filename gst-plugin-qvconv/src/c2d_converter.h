// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __C2D_CONVERTER_H__
#define __C2D_CONVERTER_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#include <sys/types.h>
#include <dlfcn.h>

#include <map>
#include <c2d2.h>

#ifndef _ENABLE_UMD_
#include <linux/msm_kgsl.h> //just to include KGSL_USER_MEM_TYPE_ION
#else
#ifdef KGSL_USER_MEM_TYPE_ION
#undef KGSL_USER_MEM_TYPE_ION
#endif
#define KGSL_USER_MEM_TYPE_ION 0x3
#endif

#ifndef MM_C2D_UNIT_TEST
#include <gst/gstinfo.h>

bool qvconv_load_libs_once (void);
#else
#include <stdio.h>

enum {
    C2D_TEST_ERROR = 0x1,
    C2D_TEST_WARN = 0x2,
    C2D_TEST_INFO = 0x4,
    C2D_TEST_DEBUG = 0x8,
};

extern int c2d_test_debug_level;

typedef void* gpointer;

#define c2d_print(level, fmt, args...)   \
    do {                                 \
        if (level & c2d_test_debug_level)     \
            printf("[%s:%d] " fmt "\n", __func__, __LINE__, ##args); \
    } while(0)

#define c2d_error(fmt, args...) c2d_print(C2D_TEST_ERROR, fmt, ##args)
#define c2d_warn(fmt, args...)  c2d_print(C2D_TEST_WARN, fmt, ##args)
#define c2d_info(fmt, args...)  c2d_print(C2D_TEST_INFO, fmt, ##args)
#define c2d_debug(fmt, args...) c2d_print(C2D_TEST_DEBUG, fmt, ##args)

#define GST_ERROR c2d_error
#define GST_WARNING c2d_warn
#define GST_INFO  c2d_info
#define GST_DEBUG c2d_debug
#define GST_LOG c2d_debug
#define g_warn_if_fail(e)
#define gst_printerrln c2d_error

bool qvconv_load_libs (void);
#endif /* MM_C2D_UNIT_TEST */


//As no environment var to decide qvconv syslog log level, just add default syslog. LOG_NOTICE is usually syslog default enabled level.
//In qvconv, for GST log <= info, should use SG_XXX(_LITE) log instead of GST_XXX log
#define ENABLE_QVCONV_DEFAULT_SYSLOG
#ifdef ENABLE_QVCONV_DEFAULT_SYSLOG
#include <syslog.h>
#ifndef MM_C2D_UNIT_TEST
#define SLOG_TIP "qvconv:"
#else
#define SLOG_TIP "c2dtest:"
#endif
//SG mean syslog + GST
#define SG_ERR_LITE(fmt, args...)			\
    do {						\
        syslog(LOG_ERR, SLOG_TIP "E: " fmt, ##args);	\
        GST_ERROR(fmt, ##args);				\
    } while(0)

#define SG_WARN_LITE(fmt, args...)			\
    do {						\
        syslog(LOG_WARNING, SLOG_TIP "W: " fmt, ##args);\
        GST_WARNING(fmt, ##args);			\
    } while(0)

#define SG_INFO_LITE(fmt, args...)			\
    do {						\
        syslog(LOG_NOTICE, SLOG_TIP "I: " fmt, ##args);	\
        GST_INFO(fmt, ##args);				\
    } while(0)

//if fmt contain GST specific format like GST_PTR_FORMAT/GST_SEGMENT_FORMAT or fmt is var, must use below macro instead of their _LITE version
#define SG_ERR(fmt, args...)				\
    do {						\
        gchar* s = gst_info_strdup_printf(fmt, ##args);	\
        syslog(LOG_ERR, SLOG_TIP "E: %s", s);		\
        g_free(s);					\
        GST_ERROR(fmt, ##args);				\
    } while(0)

#define SG_WARN(fmt, args...)				\
    do {						\
        gchar* s = gst_info_strdup_printf(fmt, ##args);	\
        syslog(LOG_WARNING, SLOG_TIP "W: %s", s);	\
        g_free(s);					\
        GST_WARNING(fmt, ##args);			\
    } while(0)

#define SG_INFO(fmt, args...)				\
    do {						\
        gchar* s = gst_info_strdup_printf(fmt, ##args);	\
        syslog(LOG_NOTICE, SLOG_TIP "I: %s", s);	\
        g_free(s);					\
        GST_INFO(fmt, ##args);				\
    } while(0)

#ifndef MM_C2D_UNIT_TEST  //In c2d unit test source code: c2d_converter.cpp, no GST_XXX_OBJECT, then, no SG_XXX_OBJ.
#define SG_ERR_OBJ_LITE(obj, fmt, args...)		\
    do {						\
        syslog(LOG_ERR, SLOG_TIP "E: " fmt, ##args);	\
        GST_ERROR_OBJECT(obj, fmt, ##args);		\
    } while(0)

#define SG_WARN_OBJ_LITE(obj, fmt, args...)		\
    do {						\
        syslog(LOG_WARNING, SLOG_TIP "W: " fmt, ##args);\
        GST_WARNING_OBJECT(obj, fmt, ##args);		\
    } while(0)

#define SG_INFO_OBJ_LITE(obj, fmt, args...)		\
    do {						\
        syslog(LOG_NOTICE, SLOG_TIP "I: " fmt, ##args);	\
        GST_INFO_OBJECT(obj, fmt, ##args);		\
    } while(0)

#define SG_ERR_OBJ(obj, fmt, args...)			\
    do {						\
        gchar* s = gst_info_strdup_printf(fmt, ##args);	\
        syslog(LOG_ERR, SLOG_TIP "E: %s", s);		\
        g_free(s);					\
        GST_ERROR_OBJECT(obj, fmt, ##args);		\
    } while(0)

#define SG_WARN_OBJ(obj, fmt, args...)			\
    do {						\
        gchar* s = gst_info_strdup_printf(fmt, ##args);	\
        syslog(LOG_WARNING, SLOG_TIP "W: %s", s);	\
        g_free(s);					\
        GST_WARNING_OBJECT(obj, fmt, ##args);		\
    } while(0)

#define SG_INFO_OBJ(obj, fmt, args...)			\
    do {						\
        gchar* s = gst_info_strdup_printf(fmt, ##args);	\
        syslog(LOG_NOTICE, SLOG_TIP "I: %s", s);	\
        g_free(s);					\
        GST_INFO_OBJECT(obj, fmt, ##args);		\
    } while(0)
#endif //end of #ifndef MM_C2D_UNIT_TEST
#else  //ENABLE_QVCONV_DEFAULT_SYSLOG
#define SG_ERR_LITE		GST_ERROR
#define SG_WARN_LITE		GST_WARNING
#define SG_INFO_LITE		GST_INFO
#define SG_ERR			GST_ERROR
#define SG_WARN			GST_WARN
#define SG_INFO			GST_INFO
#ifndef MM_C2D_UNIT_TEST  //In c2d unit test source code: c2d_converter.cpp, no GST_XXX_OBJECT, then, no SG_XXX_OBJ.
#define SG_ERR_OBJ		GST_ERROR_OBJECT
#define SG_WARN_OBJ		GST_WARNING_OBJECT
#define SG_INFO_OBJ		GST_INFO_OBJECT
#define SG_ERR_OBJ_LITE		GST_ERROR_OBJECT
#define SG_WARN_OBJ_LITE	GST_WARNING_OBJECT
#define SG_INFO_OBJ_LITE	GST_INFO_OBJECT
#endif
#endif  //end of #ifdef ENABLE_QVCONV_DEFAULT_SYSLOG

#include <gbm.h>
#include <gbm_priv.h>

#ifdef QVCONV_USE_MMM_COLOR_FMT
#include <media/mmm_color_fmt.h>

enum color_fmts {
        COLOR_FMT_NV12 = MMM_COLOR_FMT_NV12,
        COLOR_FMT_NV21 = MMM_COLOR_FMT_NV21,
        COLOR_FMT_NV12_UBWC = MMM_COLOR_FMT_NV12_UBWC,
        COLOR_FMT_NV12_BPP10_UBWC = MMM_COLOR_FMT_NV12_BPP10_UBWC,
        COLOR_FMT_RGBA8888 = MMM_COLOR_FMT_RGBA8888,
        COLOR_FMT_RGBA8888_UBWC = MMM_COLOR_FMT_RGBA8888_UBWC,
        COLOR_FMT_RGBA1010102_UBWC = MMM_COLOR_FMT_RGBA1010102_UBWC,
        COLOR_FMT_RGB565_UBWC = MMM_COLOR_FMT_RGB565_UBWC,
        COLOR_FMT_P010_UBWC = MMM_COLOR_FMT_P010_UBWC,
        COLOR_FMT_P010 = MMM_COLOR_FMT_P010,
        COLOR_FMT_NV12_512 = MMM_COLOR_FMT_NV12_512,
};

#define VENUS_Y_STRIDE MMM_COLOR_FMT_Y_STRIDE
#define VENUS_UV_STRIDE MMM_COLOR_FMT_UV_STRIDE
#define VENUS_Y_SCANLINES MMM_COLOR_FMT_Y_SCANLINES
#define VENUS_UV_SCANLINES MMM_COLOR_FMT_UV_SCANLINES
#define VENUS_Y_META_STRIDE MMM_COLOR_FMT_Y_META_STRIDE
#define VENUS_UV_META_STRIDE MMM_COLOR_FMT_UV_META_STRIDE
#define VENUS_Y_META_SCANLINES MMM_COLOR_FMT_Y_META_SCANLINES
#define VENUS_UV_META_SCANLINES MMM_COLOR_FMT_UV_META_SCANLINES
#define VENUS_BUFFER_SIZE MMM_COLOR_FMT_BUFFER_SIZE
#define VENUS_BUFFER_SIZE_USED MMM_COLOR_FMT_BUFFER_SIZE_USED

#else
#include <vidc/media/msm_media_info.h>
#endif

#define C2DCONV_QUALITY_NONE 0 //no any quality enhancement
#define C2DCONV_QUALITY_BL   1 //bilinear enhancement
#define C2DCONV_QUALITY_AA   2 //anti-aliasing enhancement
#define C2DCONV_QUALITY_BLAA 3 //bilinear + anti-aliasing
#define C2DCONV_QUALITY_DEFAULT C2DCONV_QUALITY_NONE

#define ALIGN8K 8192
#define ALIGN4K 4096
#define ALIGN2K 2048
#define ALIGN512 512
#define ALIGN256 256
#define ALIGN128 128
#define ALIGN64 64
#define ALIGN32 32
#define ALIGN16 16
#define ALIGN( num, to ) (((num) + (to-1)) & (~(to-1)))

enum ColorConvertFormat {
    RGB565 = 1,
    YCbCr420Tile,
    YCbCr420SP,
    YCbCr420P,
    YCrCb420P,
    RGBA8888,
    RGBA8888_UBWC,
    NV12_2K,
    NV12_128m,
    NV12_UBWC,
    TP10_UBWC,
    YCbCr420_VENUS_P010,
    P010,
    VENUS_P010,
    CbYCrY,
    BGR888,
    RGB888,
    ARGB8888,
    NO_COLOR_FORMAT
};

typedef struct C2DBuffer {
    int fd;
    int handle;
    void *ptr;
    int size;
    int gbm_format;
    int width;
    int height;
    int meta_fd;
    bool ubwc_flags;
    struct gbm_bo *gbm_bo;
} C2DBuffer;

typedef struct C2dFormat {
    ColorConvertFormat format;
    int width;
    int height;
    int stride;
} C2dFormat;

typedef struct C2dParam {
    int  qualityIndicator;
    bool cacheGpuAddrSrcBuf;
    bool srcBufInternal;
    bool cacheGpuAddrDstBuf;
    bool dstBufInternal;
} C2dParam;

class C2dConverter
{
public:
    C2dConverter();
    ~C2dConverter();
    bool configure(const C2dFormat *src, const C2dFormat *dst, const C2dParam *param);
    void destroy();
    /* Call it after configure() and before convert()*/
    bool setSrcCrop(int x, int y, int w, int h);
    /* Call it after configure() and before convert()*/
    bool setFlip(int flip);
    //void setRotation(int rotation); // not needed currently
    bool convert(int srcFd, void *srcBase, void *srcData, int dstFd, void *dstBase, void *dstData);
    bool allocateBuffer(struct C2DBuffer *buffer);
    void freeBuffer(struct C2DBuffer *buffer);
    bool dumpSurface(int fd, bool source);
    void clearMappedGpuAddrs(void);

private:
    bool isYUVSurface(ColorConvertFormat format);
    bool createSurface(ColorConvertFormat format, size_t width, size_t height, bool isSource);
    C2D_STATUS updateYuvSurface(void *gpuAddr, void *base, void * data, bool isSource);
    C2D_STATUS updateRgbSurface(void *gpuAddr, void * data, bool isSource);
    void destroySurfaces();
    uint32_t getC2DFormat(ColorConvertFormat format, bool isSource);
    size_t calcYSize(ColorConvertFormat format, size_t width, size_t height);
    size_t calcSize(ColorConvertFormat format, size_t width, size_t height, bool isSource);
    void *mapGpuAddress(int fd, void *buf, size_t len);
    bool unmapGpuAddress(void *gpuAddr);
    bool openGbmDevice();
    void closeGbmDevice();

private:
    pthread_mutex_t mMutex;

    bool mConfigured;

    C2D_OBJECT mBlit;
    uint32_t mSrcSurface;
    uint32_t mDstSurface;
    void *mSrcSurfaceDef;
    void *mDstSurfaceDef;

    ColorConvertFormat mSrcFormat;
    ColorConvertFormat mDstFormat;
    uint32_t mFlags;

    size_t mSrcWidth;
    size_t mSrcHeight;
    size_t mSrcStride;
    size_t mSrcSize;
    size_t mSrcYSize;

    size_t mDstWidth;
    size_t mDstHeight;
    size_t mDstStride;
    size_t mDstSize;
    size_t mDstYSize;

    int mGbmDevFd;
    struct gbm_device *mGbmDevice;

    C2dParam mParam;

private:
    /* dup fd of external buffer to keep it valid while in using. */
    struct ExtBufInfo {
        int dupFd;
        void *gpuAddr;
    };
    /* Cache mapped GPU addresses of internal/external DMA buffers. */
    /* inode number as key for external buffer */
    std::map<ino_t, ExtBufInfo> mMappedGpuAddrsInode;
    /* fd number as key for internal buffer */
    std::map<int, void*> mMappedGpuAddrsFd;

    void *getMappedGpuAddrByInode(int fd, ino_t inode, void *va, size_t size);
    void *getMappedGpuAddrByFd(int fd, void *va, size_t size);

    bool acquireMappedGpuAddr(int srcFd, void *srcData, void **srcGpuAddr,
        int dstFd, void *dstData, void **dstGpuAddr);
    void releaseMappedGpuAddr(void *srcGpuAddr, void *dstGpuAddr);
};

extern void computeFormatAlignedWidthHeight (int width, int height,
    int format, int *aligned_w, int *aligned_h);

extern uint64_t (*_gbm_bo_get_modifier) (struct gbm_bo *bo);

#endif /* __C2D_CONVERTER_H__ */
