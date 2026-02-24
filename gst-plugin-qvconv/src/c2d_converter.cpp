// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "c2d_converter.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

/* For ADRENO_PIXELFORMAT_XXX definitions
 * used by computeFormatAlignedWidthHeight(). */
#include <msmgbm_adreno_utils.h>

#ifndef MM_C2D_UNIT_TEST
GST_DEBUG_CATEGORY_EXTERN (gst_qvconv_debug);
#define GST_CAT_DEFAULT gst_qvconv_debug
#endif

/* Dynamically load libs by dlopen. */
#define ADRENO_UTILS_LIB_NAME "libadreno_utils.so"
#define ADRENO_C2D2_LIB_NAME "libC2D2.so"
#define GBM_LIB_NAME "libgbm.so"

static const char *adreno_utils_lib_name  = ADRENO_UTILS_LIB_NAME;
static const char *adreno_c2d2_lib_name  = ADRENO_C2D2_LIB_NAME;
static const char *gbm_lib_name  = GBM_LIB_NAME;

static void *handle_utils;
static void *handle_c2d2;
static void *handle_gbm;

/* for unit test code */
void qvconv_set_lib_names (const char *utils, const char *c2d2, const char *gbm)
{
  adreno_utils_lib_name = utils;
  adreno_c2d2_lib_name = c2d2;
  gbm_lib_name = gbm;
}

/* Adreno UTILS APIs */
static void (*_compute_fmt_aligned_width_and_height) (
        int width, int height, int plane_id, int format, uint32_t num_samples,
        int tile_mode, int raster_mode, int padding_threshold,
        int *aligned_w, int *aligned_h);

/* C2D2 APIs */
static C2D_STATUS (*_c2dCreateSurface) (
        uint32           *a_surfaceId,
        uint32            surface_bits,
        C2D_SURFACE_TYPE  a_surfaceType,
        void             *a_surfaceDefinition);

static C2D_STATUS (*_c2dUpdateSurface) (
        uint32 surface_id,
        uint32 surface_bits,
        C2D_SURFACE_TYPE surface_type,
        void *surface_definition );

static C2D_STATUS (*_c2dMapAddr) (int mem_fd, void * hostptr, uint32 len,
        uint32 offset, uint32 flags, void ** gpuaddr);

static C2D_STATUS (*_c2dUnMapAddr) ( void * gpuaddr);

static C2D_STATUS (*_c2dDraw) (uint32 a_targetSurfaceId,
        uint32 a_targetConfig, C2D_RECT *target_scissor,
        uint32 target_mask_id, uint32 target_color_key,
        C2D_OBJECT *a_objectArray, uint32 a_numElems);

static C2D_STATUS (*_c2dFinish) (uint32 a_target_id);
static C2D_STATUS (*_c2dDestroySurface) (uint32 a_surfaceId);

/* GBM APIs */
static struct gbm_device *(*_gbm_create_device) (int fd);
static void (*_gbm_device_destroy) (struct gbm_device *gbm_dev);

static struct gbm_bo *(*_gbm_bo_create) (struct gbm_device *gbm_dev,
        uint32_t width, uint32_t height, uint32_t format, uint32_t usage);

static int (*_gbm_perform) (int operation, ...);
static int (*_gbm_bo_get_fd) (struct gbm_bo *bo);
uint64_t (*_gbm_bo_get_modifier) (struct gbm_bo *bo);
static void (*_gbm_bo_destroy) (struct gbm_bo *bo);

#define LOAD_SYMBOL(lib, sym) do {                        \
      dlerror (); /* clear any existing error */          \
      *(void **) & (_ ## sym) = dlsym (lib, #sym);        \
      const char *dlerr = dlerror ();                     \
      if (NULL != dlerr) {                                \
        SG_ERR_LITE ("dlsym error: %s", dlerr);           \
        goto error;                                       \
      }                                                   \
      GST_DEBUG ("loaded symbol %s", #sym);               \
    } while (0)

static void *_do_dlopen_lib (const char *lib, int flags)
{
  void *handle = dlopen (lib, flags);
  if (nullptr == handle) {
    const char *dlerr = dlerror();
    if (NULL == dlerr)
        dlerr = "NULL";
    SG_ERR_LITE ("dlopen error %s: %s", lib, dlerr);
  }

  return handle;
}

static void _do_dlclose_libs (void)
{
  //printf ("qvconv %s: %p, %p, %p\n", __func__,
  //        handle_utils, handle_c2d2, handle_gbm);

  if (handle_utils) {
    dlclose (handle_utils);
    handle_utils = nullptr;
  }

  if (handle_c2d2) {
    dlclose (handle_c2d2);
    handle_c2d2 = nullptr;
  }

  if (handle_gbm) {
    dlclose (handle_gbm);
    handle_gbm = nullptr;
  }
}

static gpointer _do_qvconv_load_libs (gpointer data)
{
  gpointer ret = NULL;

  SG_INFO_LITE ("data %p", data);

  handle_utils = _do_dlopen_lib (adreno_utils_lib_name, RTLD_NOW);
  if (nullptr == handle_utils)
    goto error;

  handle_c2d2 = _do_dlopen_lib (adreno_c2d2_lib_name, RTLD_NOW);
  if (nullptr == handle_c2d2)
    goto error;

  handle_gbm = _do_dlopen_lib (gbm_lib_name, RTLD_NOW);
  if (nullptr == handle_gbm)
    goto error;

  LOAD_SYMBOL (handle_utils, compute_fmt_aligned_width_and_height);

  LOAD_SYMBOL (handle_c2d2, c2dCreateSurface);
  LOAD_SYMBOL (handle_c2d2, c2dUpdateSurface);
  LOAD_SYMBOL (handle_c2d2, c2dMapAddr);
  LOAD_SYMBOL (handle_c2d2, c2dUnMapAddr);
  LOAD_SYMBOL (handle_c2d2, c2dDraw);
  LOAD_SYMBOL (handle_c2d2, c2dFinish);
  LOAD_SYMBOL (handle_c2d2, c2dDestroySurface);

  LOAD_SYMBOL (handle_gbm, gbm_create_device);
  LOAD_SYMBOL (handle_gbm, gbm_device_destroy);
  LOAD_SYMBOL (handle_gbm, gbm_bo_create);
  LOAD_SYMBOL (handle_gbm, gbm_perform);
  LOAD_SYMBOL (handle_gbm, gbm_bo_get_fd);
  LOAD_SYMBOL (handle_gbm, gbm_bo_get_modifier);
  LOAD_SYMBOL (handle_gbm, gbm_bo_destroy);

  ret = (gpointer) -1; /* load all okay */

error:
  SG_INFO_LITE ("ret %p", ret);
  atexit (_do_dlclose_libs);
  return ret;
}

#ifndef MM_C2D_UNIT_TEST
/* Load libs only once in multi-threaded usage. */
bool qvconv_load_libs_once (void)
{
  static GOnce once = G_ONCE_INIT;

  g_once (&once, _do_qvconv_load_libs, NULL);
  SG_INFO_LITE ("GOnce retval %p status %d", once.retval, once.status);

  return once.retval != NULL ? true : false;
}

#else

/* Load libs in single-threaded usage. */
bool qvconv_load_libs (void)
{
  bool ret = true;

  if (_do_qvconv_load_libs (NULL) == NULL)
    ret = false;

  SG_INFO_LITE ("ret %d", ret);

  return ret;
}
#endif /* MM_C2D_UNIT_TEST */

#define compute_fmt_aligned_width_and_height _compute_fmt_aligned_width_and_height

#define c2dCreateSurface _c2dCreateSurface
#define c2dUpdateSurface _c2dUpdateSurface
#define c2dMapAddr _c2dMapAddr
#define c2dUnMapAddr _c2dUnMapAddr
#define c2dDraw _c2dDraw
#define c2dFinish _c2dFinish
#define c2dDestroySurface _c2dDestroySurface

#define gbm_create_device _gbm_create_device
#define gbm_device_destroy _gbm_device_destroy
#define gbm_bo_create _gbm_bo_create
#define gbm_perform _gbm_perform
#define gbm_bo_get_fd _gbm_bo_get_fd
#define gbm_bo_get_modifier _gbm_bo_get_modifier
#define gbm_bo_destroy _gbm_bo_destroy

void computeFormatAlignedWidthHeight (int width, int height,
    int format, int *aligned_w, int *aligned_h)
{
  int32_t tile_mode = 0;
  int32_t raster_mode = 0;
  int32_t padding_threshold = 512; /* hard code for RGB formats */
  int adreno_format = 0;

  *aligned_w = 0;
  *aligned_h = 0;

  switch (format) {
  case ARGB8888://It's a workaround, gbm/gfx have no support for GBM_FORMAT_BGRA8888(=GST ARGB=ARGB8888), then, alloc/use GBM_FORMAT_ABGR8888(=GST RGBA=RGBA8888=ADRENO_PIXELFORMAT_R8G8B8A8) buffer
  case RGBA8888:
    adreno_format = msm_gbm::ADRENO_PIXELFORMAT_R8G8B8A8;
    break;
  case BGR888:
  case RGB888:
    // there is no msm_gbm::ADRENO_PIXELFORMAT_B8G8R8 in GBM.
    adreno_format = msm_gbm::ADRENO_PIXELFORMAT_R8G8B8;
    break;
  default:
    SG_ERR_LITE("Format not supported %d", format);
    return;
  }

  compute_fmt_aligned_width_and_height (width, height, 0, adreno_format, 1,
      tile_mode, raster_mode, padding_threshold, aligned_w, aligned_h);
}

C2dConverter::C2dConverter() :
    mConfigured(false), mSrcSurface(0), mDstSurface(0),
    mSrcSurfaceDef(nullptr), mDstSurfaceDef(nullptr),
    mSrcFormat(NO_COLOR_FORMAT), mDstFormat(NO_COLOR_FORMAT), mFlags(0),
    mSrcWidth(0), mSrcHeight(0), mSrcStride(0), mSrcSize(0), mSrcYSize(0),
    mDstWidth(0), mDstHeight(0), mDstStride(0), mDstSize(0), mDstYSize(0)
{
    pthread_mutex_init(&mMutex, NULL);
    memset(&mBlit,0,sizeof(C2D_OBJECT));
    memset(&mParam, 0, sizeof(C2dParam));
    openGbmDevice();
}

C2dConverter::~C2dConverter()
{
    closeGbmDevice();
    pthread_mutex_destroy(&mMutex);
}

bool C2dConverter::configure(const C2dFormat *src, const C2dFormat *dst,
    const C2dParam *param)
{
    bool ret = false;
    int  quality = param->qualityIndicator;

    SG_INFO_LITE ("c2d inst=%p: src: format=%d,width=%d,height=%d,stride=%d, "
        "dst: format=%d,width=%d,height=%d,stride=%d, quality=%d", this,
        src->format, src->width, src->height, src->stride,
        dst->format, dst->width, dst->height, dst->stride, quality);

    pthread_mutex_lock(&mMutex);
    /* New a C2dConverter and can configure it only once. */
    if (mConfigured) {
        SG_ERR_LITE ("c2d inst=%p: Already configured", this);
        goto out;
    }

    mParam = *param;

    mSrcFormat = src->format;
    mSrcWidth = src->width;
    mSrcHeight = src->height;
    mSrcStride = src->stride;

    mDstFormat = dst->format;
    mDstWidth = dst->width;
    mDstHeight = dst->height;
    mDstStride = dst->stride;

    mSrcSize = calcSize(mSrcFormat, mSrcWidth, mSrcHeight, true);
    mDstSize = calcSize(mDstFormat, mDstWidth, mDstHeight, false);
    if (!mSrcSize || !mDstSize)
        goto out;

    mSrcYSize = calcYSize(mSrcFormat, mSrcWidth, mSrcHeight);
    mDstYSize = calcYSize(mDstFormat, mDstWidth, mDstHeight);
    ///mFlags = flags;

    ret = createSurface(mSrcFormat, mSrcWidth, mSrcHeight, true);
    if (!ret)
        goto out;

    ret = createSurface(mDstFormat, mDstWidth, mDstHeight, false);
    if (!ret)
        goto out;

    mBlit.source_rect.x = 0 << 16;
    mBlit.source_rect.y = 0 << 16;
    mBlit.source_rect.width = mSrcWidth << 16;
    mBlit.source_rect.height = mSrcHeight << 16;
    mBlit.target_rect.x = 0 << 16;
    mBlit.target_rect.y = 0 << 16;
    mBlit.target_rect.width = mDstWidth << 16;
    mBlit.target_rect.height = mDstHeight << 16;
    mBlit.config_mask = C2D_ALPHA_BLEND_NONE | C2D_TARGET_RECT_BIT;
    if (quality == C2DCONV_QUALITY_NONE) {
        mBlit.config_mask |= C2D_NO_BILINEAR_BIT | C2D_NO_ANTIALIASING_BIT;
    }else if (quality == C2DCONV_QUALITY_BL) {
        mBlit.config_mask |= C2D_NO_ANTIALIASING_BIT;
    }else if (quality == C2DCONV_QUALITY_AA) {
        mBlit.config_mask |= C2D_NO_BILINEAR_BIT;
    }
    mBlit.surface_id = mSrcSurface;

    mConfigured = true;

out:
    pthread_mutex_unlock(&mMutex);
    return ret;
}

void C2dConverter::destroy()
{
    pthread_mutex_lock(&mMutex);

    SG_INFO_LITE ("c2d inst=%p: mConfigured=%d,srcFormat=%d,srcWidth=%lu,srcHeight=%lu,dstFormat=%d,dstWidth=%lu,dstHeight=%lu", this,
              mConfigured, mSrcFormat, mSrcWidth, mSrcHeight, mDstFormat, mDstWidth, mDstHeight);

    if (mConfigured) {
        clearMappedGpuAddrs();
        destroySurfaces();
        mConfigured = false;
    }

    pthread_mutex_unlock(&mMutex);
}

bool C2dConverter::setSrcCrop(int x, int y, int w, int h)
{
    if (!(x | y | w | h))
        return false;

    pthread_mutex_lock(&mMutex);

    mBlit.source_rect.x = x << 16;
    mBlit.source_rect.y = y << 16;
    mBlit.source_rect.width = w << 16;
    mBlit.source_rect.height = h << 16;
    mBlit.config_mask |= C2D_SOURCE_RECT_BIT;

    SG_INFO_LITE ("c2d inst=%p: x=%d,y=%d,w=%d,h=%d,mask=%x", this, x, y, w, h, mBlit.config_mask);

    pthread_mutex_unlock(&mMutex);

    return true;
}

bool C2dConverter::setFlip(int flip)
{
    if (flip && C2D_MIRROR_V_BIT != flip && C2D_MIRROR_H_BIT != flip) {
        SG_ERR_LITE ("c2d inst=%p: Invald flip=0x%x", this, flip);
        return false;
    }

    pthread_mutex_lock(&mMutex);

    if (flip)
        mBlit.config_mask |= flip;
    else
        mBlit.config_mask &= ~(C2D_MIRROR_V_BIT | C2D_MIRROR_H_BIT);

    SG_INFO_LITE ("c2d inst=%p: flip=%x,mask=%x", this, flip, mBlit.config_mask);

    pthread_mutex_unlock(&mMutex);

    return true;
}

bool C2dConverter::convert(int srcFd, void *srcBase, void *srcData, int dstFd, void *dstBase, void *dstData)
{
    C2D_STATUS ret, ret2 = C2D_STATUS_OK;
    void *srcMappedGpuAddr = NULL;
    void *dstMappedGpuAddr = NULL;
    bool status = false;

    GST_DEBUG ("c2d inst=%p: C2D conv begin: srcFd=%d,srcBase=%p,srcData=%p,dstFd=%d,dstBase=%p,dstData=%p", this,
               srcFd, srcBase, srcData, dstFd, dstBase, dstData);
    if (srcFd < 0 || !srcBase || !srcData || dstFd < 0 || !dstBase || !dstData) {
        SG_ERR_LITE ("c2d inst=%p: Invalid parameter(s)", this);
        return false;
    }

    pthread_mutex_lock(&mMutex);

    if (!mConfigured) {
        SG_ERR_LITE ("c2d inst=%p: Configure c2d firstly", this);
        goto out;
    }

    status = acquireMappedGpuAddr(srcFd, srcData, &srcMappedGpuAddr, dstFd, dstData, &dstMappedGpuAddr);
    if (!status)
        goto out;

    //In below update operation, gpu addr is mapped from dupfd by c2d, and cpu addr is mapped from primary fd, however, c2d works well with such calling.
    if (isYUVSurface(mSrcFormat)) {
        ret = updateYuvSurface(srcMappedGpuAddr, srcBase, srcData, true);
    } else {
        ret = updateRgbSurface(srcMappedGpuAddr, srcData, true);
    }
    if (ret != C2D_STATUS_OK) {
        SG_ERR_LITE ("c2d inst=%p: Update src surface def failed (%d)", this, ret);
        goto out;
    }

    if (isYUVSurface(mDstFormat)) {
      ret = updateYuvSurface(dstMappedGpuAddr, dstBase, dstData, false);
    } else {
      ret = updateRgbSurface(dstMappedGpuAddr, dstData, false);
    }
    if (ret != C2D_STATUS_OK) {
        SG_ERR_LITE ("c2d inst=%p: Update dst surface def failed (%d)", this, ret);
        goto out;
    }

    mBlit.surface_id = mSrcSurface;
    GST_LOG ("c2d inst=%p: Will call c2dDraw()", this);
    ret = c2dDraw(mDstSurface, C2D_TARGET_ROTATE_0, 0, 0, 0, &mBlit, 1);
    if (ret == C2D_STATUS_OK && (ret2 = c2dFinish(mDstSurface)) == C2D_STATUS_OK) {
        GST_LOG ("c2d inst=%p: c2dDraw and c2dFinish completed ok!", this);
        status = true;
    } else {
        SG_ERR_LITE ("c2d inst=%p: c2dDraw failed (%d) or c2dFinish failed (%d)", this, ret, ret2);
    }

out:
    releaseMappedGpuAddr(srcMappedGpuAddr, dstMappedGpuAddr);
    pthread_mutex_unlock(&mMutex);
    GST_DEBUG ("c2d inst=%p: C2D conv end", this);
    return status;
}

bool C2dConverter::isYUVSurface(ColorConvertFormat format)
{
    switch (format) {
    case NV12_128m:
    case NV12_UBWC:
    case CbYCrY:
    case VENUS_P010:
        return true;
    default:
        return false;
    }
}

bool
C2dConverter::createSurface(ColorConvertFormat format, size_t width, size_t height, bool isSource)
{
    void *surfaceDef = NULL;
    C2D_SURFACE_TYPE surfaceType;
    C2D_STATUS ret;
    size_t stride = isSource ? mSrcStride : mDstStride;

    if (isYUVSurface(format)) {
        C2D_YUV_SURFACE_DEF **surfaceYUVDef = (C2D_YUV_SURFACE_DEF **)
                                (isSource ? &mSrcSurfaceDef : &mDstSurfaceDef);
        *surfaceYUVDef = (C2D_YUV_SURFACE_DEF *)calloc(1, sizeof(C2D_YUV_SURFACE_DEF));
        if (*surfaceYUVDef == NULL) {
            SG_ERR_LITE ("c2d inst=%p: surfaceYUVDef allocation failed", this);
            return false;
        }

        (*surfaceYUVDef)->format = getC2DFormat(format, isSource);
        (*surfaceYUVDef)->width = width;
        (*surfaceYUVDef)->height = height;
        (*surfaceYUVDef)->plane0 = (void *)0xaaaaaaaa;
        (*surfaceYUVDef)->phys0 = (void *)0xaaaaaaaa;
        (*surfaceYUVDef)->stride0 = stride;
        (*surfaceYUVDef)->plane1 = (void *)0xaaaaaaaa;
        (*surfaceYUVDef)->phys1 = (void *)0xaaaaaaaa;
        (*surfaceYUVDef)->stride1 = stride;
        (*surfaceYUVDef)->stride2 = stride;
        (*surfaceYUVDef)->phys2 = NULL;
        (*surfaceYUVDef)->plane2 = NULL;

        surfaceDef = *surfaceYUVDef;
        surfaceType = C2D_SURFACE_YUV_HOST;
    } else {
        C2D_RGB_SURFACE_DEF **surfaceRGBDef = (C2D_RGB_SURFACE_DEF **)
                                (isSource ? &mSrcSurfaceDef : &mDstSurfaceDef);
        *surfaceRGBDef = (C2D_RGB_SURFACE_DEF *)calloc(1, sizeof(C2D_RGB_SURFACE_DEF));
        if (*surfaceRGBDef == NULL) {
            SG_ERR_LITE ("c2d inst=%p: surfaceRGBDef allocation failed", this);
            return false;
        }

        (*surfaceRGBDef)->format = getC2DFormat(format, isSource);
        (*surfaceRGBDef)->width = width;
        (*surfaceRGBDef)->height = height;
        (*surfaceRGBDef)->buffer = (void *)0xaaaaaaaa;
        (*surfaceRGBDef)->phys = (void *)0xaaaaaaaa;
        (*surfaceRGBDef)->stride = stride;

        surfaceDef = *surfaceRGBDef;
        surfaceType = C2D_SURFACE_RGB_HOST;
    }

    //TODO: 2024.09, on LRH/HGY-LVPVM, c2d not support C2D_SURFACE_WITH_PHYS, then, c2d api has memcpy internally
    surfaceType = (C2D_SURFACE_TYPE)(surfaceType | C2D_SURFACE_WITH_PHYS | C2D_SURFACE_WITH_PHYS_DUMMY);
    ret = c2dCreateSurface(isSource ? &mSrcSurface : &mDstSurface,
              isSource ? C2D_SOURCE : C2D_TARGET, surfaceType, surfaceDef);
    if (C2D_STATUS_OK != ret) {
        SG_ERR_LITE ("c2d inst=%p: c2dCreateSurface failed, ret=%d", this, ret);
        if (isSource) {
            free(mSrcSurfaceDef);
            mSrcSurfaceDef = NULL;
        } else {
            free(mDstSurfaceDef);
            mDstSurfaceDef = NULL;
        }
    }

    return ret == C2D_STATUS_OK ? true : false;
}

C2D_STATUS C2dConverter::updateYuvSurface(void *gpuAddr, void *base,
                                                void *data, bool isSource)
{
    ptrdiff_t offset = (uint8_t *)data - (uint8_t *)base;
    //TODO: 2024.09, on LRH/HGY-LVPVM, c2d not support C2D_SURFACE_WITH_PHYS, then, c2d api has memcpy internally
    C2D_SURFACE_TYPE surfaceType = (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS);

    if (isSource) {
        C2D_YUV_SURFACE_DEF * srcSurfaceDef = (C2D_YUV_SURFACE_DEF *)mSrcSurfaceDef;
        srcSurfaceDef->plane0 = data;
        srcSurfaceDef->phys0  = (uint8_t *)gpuAddr + offset;
        srcSurfaceDef->plane1 = (uint8_t *)data + mSrcYSize;
        srcSurfaceDef->phys1  = (uint8_t *)srcSurfaceDef->phys0 + mSrcYSize;
        return c2dUpdateSurface(mSrcSurface, C2D_SOURCE, surfaceType, srcSurfaceDef);
    } else {
        C2D_YUV_SURFACE_DEF * dstSurfaceDef = (C2D_YUV_SURFACE_DEF *)mDstSurfaceDef;
        dstSurfaceDef->plane0 = data;
        dstSurfaceDef->phys0  = (uint8_t *)gpuAddr + offset;
        dstSurfaceDef->plane1 = (uint8_t *)data + mDstYSize;
        dstSurfaceDef->phys1  = (uint8_t *)dstSurfaceDef->phys0 + mDstYSize;
        return c2dUpdateSurface(mDstSurface, C2D_TARGET, surfaceType, dstSurfaceDef);
    }
}

C2D_STATUS C2dConverter::updateRgbSurface(void *gpuAddr, void * data, bool isSource)
{
    C2D_SURFACE_TYPE surfaceType = (C2D_SURFACE_TYPE)(C2D_SURFACE_RGB_HOST | C2D_SURFACE_WITH_PHYS);

    if (isSource) {
        C2D_RGB_SURFACE_DEF * srcSurfaceDef = (C2D_RGB_SURFACE_DEF *)mSrcSurfaceDef;
        srcSurfaceDef->buffer = data;
        srcSurfaceDef->phys = gpuAddr;
        return  c2dUpdateSurface(mSrcSurface, C2D_SOURCE, surfaceType, srcSurfaceDef);
    } else {
        C2D_RGB_SURFACE_DEF * dstSurfaceDef = (C2D_RGB_SURFACE_DEF *)mDstSurfaceDef;
        dstSurfaceDef->buffer = data;
        dstSurfaceDef->phys = gpuAddr;
        return c2dUpdateSurface(mDstSurface, C2D_TARGET, surfaceType, dstSurfaceDef);
    }
}

void C2dConverter::destroySurfaces()
{
    if (mSrcSurface) {
        C2D_STATUS ret = c2dDestroySurface(mSrcSurface);
        SG_INFO_LITE ("c2d inst=%p: c2dDestroySurface ret=%d", this, ret);
        mSrcSurface = 0;
    }

    if (mSrcSurfaceDef) {
        free(mSrcSurfaceDef);
        mSrcSurfaceDef = NULL;
    }

    if (mDstSurface) {
        C2D_STATUS ret = c2dDestroySurface(mDstSurface);
        SG_INFO_LITE ("c2d inst=%p: c2dDestroySurface ret=%d", this, ret);
        mDstSurface = 0;
    }

    if (mDstSurfaceDef) {
        free(mDstSurfaceDef);
        mDstSurfaceDef = NULL;
    }
}

/* Refined from media/libc2dcolorconvert/C2DColorConverter.cpp */
uint32_t C2dConverter::getC2DFormat(ColorConvertFormat format, bool isSource)
{
    uint32_t c2dFormat;
    switch (format) {
    case RGBA8888:
        c2dFormat = C2D_COLOR_FORMAT_8888_RGBA | C2D_FORMAT_SWAP_ENDIANNESS;
        if (isSource)
            c2dFormat |= C2D_FORMAT_PREMULTIPLIED;
        return c2dFormat;
    case ARGB8888:
        c2dFormat = C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_SWAP_ENDIANNESS;
        if (isSource)
            c2dFormat |= C2D_FORMAT_PREMULTIPLIED;
        return c2dFormat;
    case NV12_128m:
        return C2D_COLOR_FORMAT_420_NV12;
    case NV12_UBWC:
        return C2D_COLOR_FORMAT_420_NV12 | C2D_FORMAT_UBWC_COMPRESSED;
    case CbYCrY:
        return C2D_COLOR_FORMAT_422_UYVY;
    case BGR888:
        return C2D_COLOR_FORMAT_888_RGB;
    case RGB888:
        return C2D_COLOR_FORMAT_888_RGB | C2D_FORMAT_SWAP_ENDIANNESS;
    case VENUS_P010:
        return C2D_COLOR_FORMAT_420_P010;
    default:
        SG_ERR_LITE ("c2d inst=%p: Format not supported, %d", this, format);
        return -1;
    }
}

/* Refined from media/libc2dcolorconvert/C2DColorConverter.cpp */
size_t C2dConverter::calcYSize(ColorConvertFormat format, size_t width, size_t height)
{
    switch (format) {
    case NV12_128m: {
        int32_t stride_alignment = VENUS_Y_STRIDE(COLOR_FMT_NV12, 1);
        int32_t scanline_alignment = VENUS_Y_SCANLINES(COLOR_FMT_NV12, 1);
        return ALIGN(width, stride_alignment) * ALIGN(height, scanline_alignment);
    }
    case NV12_UBWC:
        return ALIGN( VENUS_Y_STRIDE(COLOR_FMT_NV12_UBWC, width) *
               VENUS_Y_SCANLINES(COLOR_FMT_NV12_UBWC, height), ALIGN4K) +
               ALIGN( VENUS_Y_META_STRIDE(COLOR_FMT_NV12_UBWC, width) *
               VENUS_Y_META_SCANLINES(COLOR_FMT_NV12_UBWC, height), ALIGN4K);
    case VENUS_P010: {
        return (VENUS_Y_STRIDE(COLOR_FMT_P010, width) *
               VENUS_Y_SCANLINES(COLOR_FMT_P010, height));
    }
    default:
        GST_DEBUG ("c2d inst=%p: Format %d is not needed to handle", this, format);
        return 0;
    }
}

/* Refined from media/libc2dcolorconvert/C2DColorConverter.cpp */
size_t C2dConverter::calcSize(ColorConvertFormat format, size_t width, size_t height, bool isSource)
{
    int alignedw = 0;
    int alignedh = 0;
    int32_t size = 0;

    SG_INFO_LITE ("c2d inst=%p: format=%d,width=%lu,height=%lu, isSource %d", this, format, width, height, (int)isSource);

    switch (format) {
    case ARGB8888:  //It's a workaround, gbm/gfx have no support for GBM_FORMAT_BGRA8888(=GST ARGB=ARGB8888), then, alloc/use GBM_FORMAT_ABGR8888(=GST RGBA=RGBA8888=ADRENO_PIXELFORMAT_R8G8B8A8) buffer
        GST_DEBUG ("c2d inst=%p: ARGB8888 case, reuse RGBA8888 handling", this);
    case RGBA8888:
        computeFormatAlignedWidthHeight(width, height, format,
                                        &alignedw, &alignedh);
        size = alignedw * alignedh * 4;
        SG_INFO_LITE ("c2d inst=%p: %s alignedw %d,alignedh %d,size %d, ignore mSrcStride %zu", this, (ARGB8888==format)? "ARGB8888":"RGBA8888", alignedw, alignedh, size, mSrcStride);
        break;
    case NV12_128m:
        alignedw = VENUS_Y_STRIDE(COLOR_FMT_NV12, width);
        alignedh = VENUS_Y_SCANLINES(COLOR_FMT_NV12, height);
        size = ALIGN(alignedw * alignedh + (alignedw * ALIGN((height+1)/2, VENUS_Y_SCANLINES(COLOR_FMT_NV12, 1)/2)), ALIGN4K);//equal to VENUS_BUFFER_SIZE(COLOR_FMT_NV12, width, height)
        break;
    case NV12_UBWC:
        size = VENUS_BUFFER_SIZE(COLOR_FMT_NV12_UBWC, width, height);
        break;
    case CbYCrY:
        size = ALIGN(ALIGN(width * 2, ALIGN64) * height, ALIGN4K);//equal to gbm's UYVY size = align4k(align32(width) * height * 2)
        break;
    case BGR888:
    case RGB888:
        computeFormatAlignedWidthHeight(width, height, format,
                                        &alignedw, &alignedh);
        size = alignedw * alignedh * 3;
        SG_INFO_LITE ("c2d inst=%p: %s alignedw %d alignedh %d,size=%d, ignore mSrcStride %zu", this, (BGR888 == format) ? "BGR888" : "RGB888", alignedw, alignedh, size, mSrcStride);
        break;
    case VENUS_P010:
        size = VENUS_BUFFER_SIZE(COLOR_FMT_P010, width, height);
        break;
    default:
        SG_ERR_LITE ("c2d inst=%p: Format not supported , %d", this, format);
        break;
    }

    return size;
}

#if 0 // debug code to check if fd is valid, the fd may be closed wrongly
static inline void _validate_fd(int fd)
{
    struct stat sb;

    if (fstat(fd, &sb)) {
        SG_ERR_LITE("invalid fd %d, error: %s", fd, strerror(errno));
        //perror("fstat");
    } else {
        SG_INFO_LITE("fd %d, ino:%lx, size:%lu", fd, sb.st_ino, sb.st_size);
    }
}
#endif

/*
* Tells GPU to map given buffer and returns a physical address of mapped buffer
*/
void *C2dConverter::mapGpuAddress(int fd, void *buf, size_t len)
{
    C2D_STATUS status;
    void *gpuaddr = NULL;

    if (fd < 0 || !buf) {
        SG_ERR_LITE("c2d inst=%p: Invalid arg(s), fd %d, buf %p", this, fd, buf);
        return NULL;
    }
    //_validate_fd(fd);

    status = c2dMapAddr(fd, buf, len, 0, KGSL_USER_MEM_TYPE_ION, &gpuaddr);
    if (status != C2D_STATUS_OK) {
        SG_ERR_LITE ("c2d inst=%p: c2dMapAddr failed: status %d fd %d ptr %p len %lu flags %x", this,
              status, fd, buf, len, KGSL_USER_MEM_TYPE_ION);
        return NULL;
    }
    GST_LOG ("c2d inst=%p: c2d mapping created: gpuaddr %p fd %d ptr %p len %lu", this, gpuaddr, fd, buf, len);

    return gpuaddr;
}

bool C2dConverter::unmapGpuAddress(void *gpuAddr)
{
    GST_DEBUG ("c2d inst=%p: c2d unmap gpuaddr %p", this, gpuAddr);

    C2D_STATUS status = c2dUnMapAddr(gpuAddr);
    if (status != C2D_STATUS_OK) {
        SG_ERR_LITE ("c2d inst=%p: c2dUnMapAddr failed: status %d gpuaddr %p", this, status, gpuAddr);
    }else{
        GST_LOG ("c2d inst=%p: c2d unmap gpuaddr %p succeed", this, gpuAddr);
    }

    return (status == C2D_STATUS_OK);
}

bool C2dConverter::openGbmDevice()
{
    mGbmDevFd = -1;
    mGbmDevice = NULL;
#ifdef _ENABLE_UMD_
    SG_INFO_LITE ("c2d inst=%p: No need to open GBM device node", this);
#else
#define GBMDEV_DEVICE_NODE "/dev/dri/renderD128"
    SG_INFO_LITE ("c2d inst=%p: open %s is calling...", this, GBMDEV_DEVICE_NODE);
    mGbmDevFd = open (GBMDEV_DEVICE_NODE, O_RDWR | O_CLOEXEC);
    SG_INFO_LITE ("c2d inst=%p: open %s ret %d", this, GBMDEV_DEVICE_NODE, mGbmDevFd);
    if (mGbmDevFd < 0) {
        int e = errno;
        SG_ERR_LITE ("c2d inst=%p: failed to open gbm device %s node, errno %d(%s)", this, GBMDEV_DEVICE_NODE, e, strerror(e));
        return false;
    }
#endif
    SG_INFO_LITE ("c2d inst=%p: gbm_create_device(%d) is calling...", this, mGbmDevFd);
    mGbmDevice = gbm_create_device (mGbmDevFd);
    SG_INFO_LITE ("c2d inst=%p: gbm_create_device(%d) ret %p", this, mGbmDevFd, mGbmDevice);
    if (NULL == mGbmDevice) {
        SG_ERR_LITE ("c2d inst=%p: failed to create gbm_device with fd %d", this, mGbmDevFd);
        if (mGbmDevFd > 0) {
            close (mGbmDevFd);
        }
        mGbmDevFd = -1;
        return false;
    }

    return true;
}

void C2dConverter::closeGbmDevice()
{
    if (mGbmDevice) {
        SG_INFO_LITE ("c2d inst=%p: gbm_device_destroy(%p) is calling...", this, mGbmDevice);
        gbm_device_destroy(mGbmDevice);
        SG_INFO_LITE ("c2d inst=%p: gbm_device_destroy(%p) completed", this, mGbmDevice);
    }
    mGbmDevice = NULL;
    if (mGbmDevFd > 0) {
        SG_INFO_LITE ("c2d inst=%p: close(%d) is calling...", this, mGbmDevFd);
        close (mGbmDevFd);
        SG_INFO_LITE ("c2d inst=%p: close(%d) completed", this, mGbmDevFd);
    }
    mGbmDevFd = -1;
}

bool C2dConverter::allocateBuffer(struct C2DBuffer *buffer)
{
    uint32_t flags = GBM_BO_USE_SCANOUT;
    void *va = NULL;

    if (!buffer) {
        SG_ERR_LITE("c2d inst=%p: Buffer param pointer is NULL", this);
        return false;
    }

    if (!buffer->width || !buffer->height) {
        SG_ERR_LITE("c2d inst=%p: Buffer param width/height is NULL", this);
        return false;
    }
    if (!buffer->gbm_format) {
        SG_ERR_LITE("c2d inst=%p: Buffer format is NULL", this);
        return false;
    }

    if (buffer->ubwc_flags == true)
        flags |= GBM_BO_USAGE_UBWC_ALIGNED_QTI;

    SG_INFO_LITE ("c2d inst=%p: create a gbm_bo(wid:%d, hei:%d, flags:0x%x) for format %d !", this,
               buffer->width, buffer->height, flags, buffer->gbm_format);

    buffer->gbm_bo = gbm_bo_create (mGbmDevice, buffer->width,
                     buffer->height, buffer->gbm_format, flags);
    SG_INFO_LITE ("c2d inst=%p: gbm_bo_create() ret %p", this, buffer->gbm_bo);
    if (NULL == buffer->gbm_bo) {
        SG_ERR_LITE ("c2d inst=%p: failed to create a bo", this);
        return false;
    }

    SG_INFO_LITE ("c2d inst=%p: gbm_bo_get_fd() calling...", this);
    buffer->fd = gbm_bo_get_fd (buffer->gbm_bo);
    SG_INFO_LITE ("c2d inst=%p: gbm_bo_get_fd() ret %d", this, buffer->fd);
    buffer->meta_fd = -1;
    SG_INFO_LITE ("c2d inst=%p: gbm_perform(get meta fd) calling...", this);
    gbm_perform (GBM_PERFORM_GET_METADATA_ION_FD, buffer->gbm_bo, &buffer->meta_fd);
    SG_INFO_LITE ("c2d inst=%p: gbm_perform() ret meta_fd %d", this, buffer->meta_fd);
    if (buffer->fd < 0 || buffer->meta_fd < 0) {
        SG_ERR_LITE ("c2d inst=%p: bo_fd:%d, meta_fd:%d are invalid", this, buffer->fd, buffer->meta_fd);
        goto fail;
    }

    if ((int)buffer->gbm_bo->size < (int)buffer->size) {
        SG_WARN_LITE ("c2d inst=%p: gbm buffer size should >= the value in gst_qvconv_align_info()!", this);
    }

    buffer->size = buffer->gbm_bo->size;
    SG_INFO_LITE ("c2d inst=%p: mmap(fd %d) size %u calling...", this, buffer->fd, buffer->size);
    va = mmap(NULL, buffer->size, PROT_READ|PROT_WRITE, MAP_SHARED, buffer->fd, 0);
    if (MAP_FAILED == va) {
        int e = errno;
        SG_ERR_LITE ("c2d inst=%p: failed to map buffer of size = %u, fd = 0x%x, errno %d(%s)", this, buffer->size, buffer->fd, e, strerror(e));
        goto fail;
    }

    SG_INFO_LITE ("c2d inst=%p: created gbm_bo %p(%u x %u, fmt %u, size %u, stride %u), exported fd %d, exported meta_fd %d, mmap va %p", this,
            buffer->gbm_bo, buffer->gbm_bo->width, buffer->gbm_bo->height, buffer->gbm_bo->format,
            buffer->gbm_bo->size, buffer->gbm_bo->stride, buffer->fd, buffer->meta_fd, va);

    buffer->ptr = va;
    return true;

fail:
    if (buffer->fd >= 0)
        close (buffer->fd);
    gbm_bo_destroy (buffer->gbm_bo);
    buffer->gbm_bo = NULL;
    buffer->fd = -1;
    buffer->meta_fd = -1;
    return false;
}

void C2dConverter::freeBuffer(struct C2DBuffer *buffer)
{
    if (!buffer) {
        SG_ERR_LITE ("c2d inst=%p: Buffer param pointer is NULL", this);
        return;
    }

    if (buffer->ptr) {
        munmap (buffer->ptr, buffer->size);
        buffer->ptr = NULL;
    }

    if (buffer->gbm_bo) {
        SG_INFO_LITE ("c2d inst=%p: destroy gbm_bo %p(%u x %u, fmt %u, size %u, stride %u), exported fd %d, exported meta_fd %d", this,
                buffer->gbm_bo, buffer->gbm_bo->width, buffer->gbm_bo->height, buffer->gbm_bo->format,
                buffer->gbm_bo->size, buffer->gbm_bo->stride, buffer->fd, buffer->meta_fd);

        if (buffer->fd >=0)
            close (buffer->fd);

        gbm_bo_destroy (buffer->gbm_bo);
        buffer->gbm_bo = NULL;
        buffer->fd = -1;
        buffer->meta_fd = -1;
    }
}

bool C2dConverter::dumpSurface(int fd, bool source)
{
    size_t stride, sliceHeight;
    ssize_t nbytes = 0;
    bool ret = true;
    ColorConvertFormat format = mDstFormat;
    size_t width = mDstWidth, height = mDstHeight;
    void *mSurfaceDef = mDstSurfaceDef;

    if (fd < 0)
        return false;

    if (source) {
        format = mSrcFormat;
        width  = mSrcWidth;
        height = mSrcHeight;
        mSurfaceDef = mSrcSurfaceDef;
    }

    if (isYUVSurface(format)) {
        C2D_YUV_SURFACE_DEF * surfaceDef = (C2D_YUV_SURFACE_DEF *)mSurfaceDef;
        uint8_t *base = (uint8_t *)surfaceDef->plane0;
        stride = surfaceDef->stride0;
        sliceHeight = surfaceDef->height;

        if (NV12_128m == format) {
            /* dump luma */
            for (size_t i = 0; i < sliceHeight; i++) {
                nbytes = write(fd, base, width); // work only for NV12
                if (nbytes != (ssize_t)width) {
                    ret = false;
                    goto out;
                }
                base += stride;
            }
            /* dump chroma */
            base = (uint8_t *)surfaceDef->plane1;
            stride = surfaceDef->stride1;
            for (size_t i = 0; i < (sliceHeight+1)/2; i++) { // work only for NV12
                nbytes = write(fd, base, width);
                if (nbytes != (ssize_t)width) {
                    ret = false;
                    goto out;
                }
                base += stride;
            }
        } else if (CbYCrY == format) {
            for (size_t i = 0; i < sliceHeight; i++) {
                nbytes = write(fd, base, width*2);
                if (nbytes != (ssize_t)(width*2)) {
                    ret = false;
                    goto out;
                }
                base += stride;
            }
        } else if (NV12_UBWC == format) {
            size_t size = (size_t)VENUS_BUFFER_SIZE_USED(COLOR_FMT_NV12_UBWC, width, height, 0);
            nbytes = write(fd, base, size);
            if (nbytes != (ssize_t)size) {
                ret = false;
                goto out;
            }
        } else if (VENUS_P010 == format) {
            /* dump luma */
            for (size_t i = 0; i < sliceHeight; i++) {
                nbytes = write(fd, base, width*2);
                if (nbytes != (ssize_t)width*2) {
                    ret = false;
                    goto out;
                }
                base += stride;
            }
            /* dump chroma */
            base = (uint8_t *)surfaceDef->plane1;
            stride = surfaceDef->stride1;
            for (size_t i = 0; i < (sliceHeight+1)/2; i++) {
                nbytes = write(fd, base, width*2);
                if (nbytes != (ssize_t)width*2) {
                    ret = false;
                    goto out;
                }
                base += stride;
            }
        } else {
            SG_ERR_LITE ("c2d inst=%p: Not support dump format: %d", this, format);
            return false;
        }
    } else {
        C2D_RGB_SURFACE_DEF * surfaceDef = (C2D_RGB_SURFACE_DEF *)mSurfaceDef;
        uint8_t *base = (uint8_t *)surfaceDef->buffer;
        stride = surfaceDef->stride;
        sliceHeight = surfaceDef->height;

        int bpp = 1; // bytes per pixel
        switch (format) {
        case RGBA8888:
        case ARGB8888:
            bpp = 4; break;
        case BGR888:
        case RGB888:
            bpp = 3; break;
        default:
            SG_ERR_LITE ("c2d inst=%p: Not support dump format: %d", this, format);
            return false;
        }

        for (size_t i = 0; i < sliceHeight; i++) {
            nbytes = write(fd, base, width * bpp);
            if (nbytes != (ssize_t)(width * bpp)) {
                ret = false;
                goto out;
            }
            base += stride;
        }
    }

out:
    if (nbytes < 0 || false == ret) {
        int e = errno;
        SG_ERR_LITE ("c2d inst=%p: file write error: %s, errno %d, nbytes %zd", this, strerror(e), e, nbytes);
    }
    return ret;
}

static inline int getInode(int fd, ino_t *i, off_t *sz)
{
    struct stat sb;
    int ret = fstat(fd, &sb);
    if (0 == ret) {
        *i = sb.st_ino;
        *sz = sb.st_size;
        GST_LOG("dev 0x%lx, inode 0x%lx, rdev 0x%lx, sz %lld",
            sb.st_dev, sb.st_ino, sb.st_rdev, (long long)sb.st_size);
    } else {
        int e = errno;
        SG_ERR_LITE("fstat error: %s(%d), ret %d", strerror(e), e, ret);
    }

    return ret;
}

void* C2dConverter::getMappedGpuAddrByInode(int fd, ino_t inode, void *va, size_t size)
{
#define MAX_NUM_BUF_CACHED 32

    auto i = mMappedGpuAddrsInode.find(inode);
    if (i != mMappedGpuAddrsInode.end()) {
        GST_LOG("c2d inst=%p: found gpu addr %p, inode 0x%lx, fd %d, va %p, dup fd %d, sz %zu", this,
            i->second.gpuAddr, inode, fd, va, i->second.dupFd, size);
        return i->second.gpuAddr;
    }

    void *gpuAddr = NULL;
    int dupFd = dup(fd);
    if (dupFd == -1) {
        int e = errno;
        SG_ERR_LITE("c2d inst=%p: dup fd error: %s, fd %d, va %p, sz %zu", this, strerror(e), fd, va, size);
    } else {
        gpuAddr = mapGpuAddress(dupFd, va, size);
        if (NULL != gpuAddr) {
                mMappedGpuAddrsInode.insert({inode, {dupFd, gpuAddr}});
                SG_INFO_LITE("c2d inst=%p: insert gpu addr %p, inode 0x%lx, fd %d, va %p, dup fd %d, sz %zu", this,
                    gpuAddr, inode, fd, va, dupFd, size);
        } else {
            SG_ERR_LITE("c2d inst=%p: mapGpuAddress error, fd %d(dup %d), va %p, sz %zu", this, fd, dupFd, va, size);
        }
    }

    /* If upstream has no pool, and always allocates new buffer for each frame
     * to push to qvconv, then map size would increase unlimitedly since inode
     * numbers of newly allocated DMA buffers are always different. See kernel
     * function get_next_ino() that allocates inode number. In this case, have
     * to disable caching external buffers by setting property cache-gpu-addr. */
    size_t mapSize = mMappedGpuAddrsInode.size();
    g_warn_if_fail(mapSize <= MAX_NUM_BUF_CACHED && "cached number of gpu address is too large!");
    if (mapSize > MAX_NUM_BUF_CACHED)
        SG_WARN_LITE("c2d inst=%p: cached %lu gpu address is too large!", this, mapSize);

    return gpuAddr;
}

void* C2dConverter::getMappedGpuAddrByFd(int fd, void *va, size_t size)
{
    auto i = mMappedGpuAddrsFd.find(fd);
    if (i != mMappedGpuAddrsFd.end()) {
        GST_LOG("c2d inst=%p: found gpu addr %p, fd %d, va %p, sz %zu", this, i->second, fd, va, size);
        return i->second;
    }

    void *gpuAddr = mapGpuAddress(fd, va, size);
    if (NULL != gpuAddr) {
        mMappedGpuAddrsFd.insert({fd, gpuAddr});
        SG_INFO_LITE("c2d inst=%p: insert gpu addr %p, fd %d, va %p, sz %zu", this, gpuAddr, fd, va, size);
    } else {
        SG_ERR_LITE("c2d inst=%p: mapGpuAddress error, fd %d, va %p, sz %zu", this, fd, va, size);
    }

    return gpuAddr;
}

inline bool C2dConverter::acquireMappedGpuAddr(int srcFd, void *srcData, void **srcGpuAddr,
    int dstFd, void *dstData, void **dstGpuAddr)
{
    bool ret = false;
    void *src = NULL, *dst = NULL;
    bool cacheSrc = mParam.cacheGpuAddrSrcBuf;
    bool srcInt   = mParam.srcBufInternal;
    bool cacheDst = mParam.cacheGpuAddrDstBuf;
    bool dstInt   = mParam.dstBufInternal;
    ino_t srcInode = 0, dstInode = 0;
    off_t srcSz = 0, dstSz = 0;

    GST_LOG("c2d inst=%p: cache gpu addr src %u:%u, dst %u:%u, fd (%d,%d)", this, cacheSrc, srcInt, cacheDst, dstInt, srcFd, dstFd);

    if (!srcInt) {
        if (0 != getInode(srcFd, &srcInode, &srcSz)) {
            SG_ERR_LITE("c2d inst=%p: src ext buf getInode(fd %d) error!", this, srcFd);
            goto out;
        }
        if (mSrcSize > (size_t)srcSz) {
            SG_ERR_LITE("c2d inst=%p: src ext buf (fd %d, inode 0x%lx) c2d map sz %zu > fstat sz %lld, it's error!", this, srcFd, srcInode, mSrcSize, (long long)srcSz);
            gst_printerrln("src ext buf (fd %d, inode 0x%lx) c2d map sz %zu > fstat sz %lld, error!", srcFd, srcInode, mSrcSize, (long long)srcSz);
            g_warn_if_fail(mSrcSize <= (size_t)srcSz && "src ext buf c2d map sz should <= fstat sz");
            goto out;
        }
    }
    if (!dstInt) {
        if (0 != getInode(dstFd, &dstInode, &dstSz)) {
            SG_ERR_LITE("c2d inst=%p: dst ext buf getInode(fd %d) error!", this, dstFd);
            goto out;
        }
        if (mDstSize > (size_t)dstSz) {
            SG_ERR_LITE("c2d inst=%p: dst ext buf (fd %d, inode 0x%lx) c2d map sz %zu > fstat sz %lld, it's error!", this, dstFd, dstInode, mDstSize, (long long)dstSz);
            gst_printerrln("dst ext buf (fd %d, inode 0x%lx) c2d map sz %zu > fstat sz %lld, error!", dstFd, dstInode, mDstSize, (long long)dstSz);
            g_warn_if_fail(mDstSize <= (size_t)dstSz && "dst ext buf c2d map sz should <= fstat sz");
            goto out;
        }
    }

    if (cacheSrc) {
        if (srcInt)
            src = getMappedGpuAddrByFd(srcFd, srcData, mSrcSize);
        else
            src = getMappedGpuAddrByInode(srcFd, srcInode, srcData, mSrcSize);
    } else {
        src = mapGpuAddress(srcFd, srcData, mSrcSize);
    }

    if (NULL == src) {
        SG_ERR_LITE("c2d inst=%p: error: src gpu address NULL", this);
        goto out;
    }

    if (cacheDst) {
        if (dstInt)
            dst = getMappedGpuAddrByFd(dstFd, dstData, mDstSize);
        else
            dst = getMappedGpuAddrByInode(dstFd, dstInode, dstData, mDstSize);
    } else {
        dst = mapGpuAddress(dstFd, dstData, mDstSize);
    }

    if (NULL == dst) {
        SG_ERR_LITE("c2d inst=%p: error: dst gpu address NULL", this);
        if (!cacheSrc) {
            unmapGpuAddress(src);
            src = NULL;
        }
    } else {
        ret = true;
    }

out:
    *srcGpuAddr = src;
    *dstGpuAddr = dst;
    GST_LOG("c2d inst=%p: srcGpuAddr %p dstGpuAddr %p ret %u", this, *srcGpuAddr, *dstGpuAddr, ret);
    return ret;
}

inline void C2dConverter::releaseMappedGpuAddr(void *srcGpuAddr, void *dstGpuAddr)
{
    if (!mParam.cacheGpuAddrSrcBuf)
        if (srcGpuAddr)
            unmapGpuAddress(srcGpuAddr);

    if (!mParam.cacheGpuAddrDstBuf)
        if (dstGpuAddr)
            unmapGpuAddress(dstGpuAddr);
}

void C2dConverter::clearMappedGpuAddrs(void)
{
    SG_INFO_LITE("c2d inst=%p: cached %lu gpu address by inode", this, mMappedGpuAddrsInode.size());
    SG_INFO_LITE("c2d inst=%p: cached %lu gpu address by fd", this, mMappedGpuAddrsFd.size());

    if (mMappedGpuAddrsInode.size() > 0) {
        for (const auto &i : mMappedGpuAddrsInode) {
            SG_INFO_LITE("c2d inst=%p: clear gpu addr %p, inode 0x%lx, dup fd %d", this,
                i.second.gpuAddr, i.first, i.second.dupFd);
            unmapGpuAddress(i.second.gpuAddr);
            close(i.second.dupFd);
        }
        mMappedGpuAddrsInode.clear();
    }

    if (mMappedGpuAddrsFd.size() > 0) {
        for (const auto &i : mMappedGpuAddrsFd) {
            SG_INFO_LITE("c2d inst=%p: clear gpu addr %p, fd %d", this, i.second, i.first);
            unmapGpuAddress(i.second);
        }
        mMappedGpuAddrsFd.clear();
    }
}
