// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqcarcamdmabuf.h"

#include <gst/gstinfo.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dlfcn.h>
#include <drm/drm_fourcc.h>
#include <gbm.h>
#include <gbm_priv.h>



GST_DEBUG_CATEGORY_EXTERN (gst_qcarcam_src_debug);
#define GST_CAT_DEFAULT gst_qcarcam_src_debug

static int dev_fd = -1;

#define GBM_RENDER_DEVICE_NAME "/dev/dri/renderD128"
static struct gbm_device *gbm_dev = NULL;

/* Dynamically load libgbm by dlopen. */
#define GBM_LIB_NAME "libgbm.so"

static const char *gbm_lib_name  = GBM_LIB_NAME;
static void *handle_gbm;

/* GBM API Pointers */
static struct gbm_device *(*_gbm_create_device) (int fd);
static void (*_gbm_device_destroy) (struct gbm_device *gbm_dev);
static struct gbm_bo *(*_gbm_bo_create) (struct gbm_device *gbm_dev,
        uint32_t width, uint32_t height, uint32_t format, uint32_t usage);
static int (*_gbm_perform) (int operation, ...);
static int (*_gbm_bo_get_fd) (struct gbm_bo *bo);
static uint32_t (*_gbm_bo_get_width) (struct gbm_bo *bo);
static uint32_t (*_gbm_bo_get_height) (struct gbm_bo *bo);
static uint32_t (*_gbm_bo_get_stride) (struct gbm_bo *bo);
static uint32_t (*_gbm_bo_get_offset) (struct gbm_bo *bo, int plane);
static uint64_t (*_gbm_bo_get_modifier) (struct gbm_bo *bo);
static void (*_gbm_bo_destroy) (struct gbm_bo *bo);

#define LOAD_SYMBOL(lib, sym) do {                        \
      dlerror (); /* clear any existing error */          \
      *(void **) & (_ ## sym) = dlsym (lib, #sym);        \
      const char *dlerr = dlerror ();                     \
      if (NULL != dlerr) {                                \
        GST_ERROR ("dlsym error: %s", dlerr);             \
        goto error;                                       \
      }                                                   \
      GST_DEBUG ("loaded symbol %s", #sym);               \
    } while (0)

static void _do_dlclose_libs (void)
{
  if (handle_gbm) {
    dlclose (handle_gbm);
    handle_gbm = NULL;
  }
}

static gpointer _do_load_lib_symbols (gpointer data)
{
  gpointer ret = NULL;

  GST_INFO ("data %p", data);

  handle_gbm = dlopen (gbm_lib_name, RTLD_NOW);
  if (NULL == handle_gbm) {
    const char *dlerr = dlerror();
    if (NULL == dlerr)
        dlerr = "NULL";
    GST_ERROR ("dlopen %s error: %s", gbm_lib_name, dlerr);
    goto error;
  }

  LOAD_SYMBOL (handle_gbm, gbm_create_device);
  LOAD_SYMBOL (handle_gbm, gbm_device_destroy);
  LOAD_SYMBOL (handle_gbm, gbm_bo_create);
  LOAD_SYMBOL (handle_gbm, gbm_perform);
  LOAD_SYMBOL (handle_gbm, gbm_bo_get_fd);
  LOAD_SYMBOL (handle_gbm, gbm_bo_get_width);
  LOAD_SYMBOL (handle_gbm, gbm_bo_get_height);
  LOAD_SYMBOL (handle_gbm, gbm_bo_get_stride);
  LOAD_SYMBOL (handle_gbm, gbm_bo_get_offset);
  LOAD_SYMBOL (handle_gbm, gbm_bo_get_modifier);
  LOAD_SYMBOL (handle_gbm, gbm_bo_destroy);

  ret = (gpointer) -1; /* load all okay */

error:
  GST_INFO ("ret %p", ret);
  atexit (_do_dlclose_libs);

  return ret;
}

/* Load libs only once in multi-threaded usage. */
gboolean qcarcam_dmabuf_load_libs_once (void)
{
  static GOnce once = G_ONCE_INIT;

  g_once (&once, _do_load_lib_symbols, NULL);
  GST_INFO ("GOnce retval %p status %d", once.retval, once.status);

  return once.retval != NULL ? TRUE : FALSE;
}

#define gbm_create_device _gbm_create_device
#define gbm_device_destroy _gbm_device_destroy
#define gbm_bo_create _gbm_bo_create
#define gbm_perform _gbm_perform
#define gbm_bo_get_fd _gbm_bo_get_fd
#define gbm_bo_get_width _gbm_bo_get_width
#define gbm_bo_get_height _gbm_bo_get_height
#define gbm_bo_get_stride _gbm_bo_get_stride
#define gbm_bo_get_offset _gbm_bo_get_offset
#define gbm_bo_get_modifier _gbm_bo_get_modifier
#define gbm_bo_destroy _gbm_bo_destroy

static gboolean
do_dmabuf_device_open (const char *dev_name)
{
  int fd;

  GST_DEBUG ("dev_name %s", dev_name);

  if (dev_fd != -1) {
    GST_DEBUG ("already opened dev_fd %d", dev_fd);
    return TRUE;
  }

  if ((fd = open (dev_name, O_RDONLY | O_CLOEXEC)) < 0) {
    GST_ERROR ("open %s error %s", dev_name, strerror (errno));
    return FALSE;
  } else {
    dev_fd = fd;
    GST_DEBUG ("dev_fd %d", dev_fd);
  }

  return TRUE;
}

static void
do_dmabuf_device_close (void)
{
  GST_DEBUG ("dev_fd %d", dev_fd);
  if (dev_fd < 0)
    return;

  if (close (dev_fd))
    GST_ERROR ("close error %s", strerror (errno));

  dev_fd = -1;
}

static inline gboolean
gbm_dmabuf_fill_desc (DmaBufDesc * desc,
    const GstVideoInfo * info, gboolean ubwc)
{
  GstVideoFormat format;

  if (!desc || !info)
    return FALSE;

  format = GST_VIDEO_INFO_FORMAT (info);
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      desc->format = GBM_FORMAT_NV12;
      break;

    default:
      GST_ERROR ("NOT support format %s-%d", GST_VIDEO_INFO_NAME (info),
          format);
      return FALSE;
  }

  desc->width = GST_VIDEO_INFO_WIDTH (info);
  desc->height = GST_VIDEO_INFO_HEIGHT (info);
  desc->size = GST_VIDEO_INFO_SIZE (info);
  desc->ubwc = ubwc;

  return TRUE;
}

static inline gboolean
gbm_dmabuf_open (void)
{
  const char *render_name = GBM_RENDER_DEVICE_NAME;

  if (!do_dmabuf_device_open (render_name)) {
    GST_ERROR ("open device error");
    return FALSE;
  }

  gbm_dev = gbm_create_device (dev_fd);
  GST_DEBUG ("gbm_dev %p", gbm_dev);
  if (NULL == gbm_dev) {
    GST_ERROR ("create gbm device error");
    do_dmabuf_device_close ();
    return FALSE;
  }

  return TRUE;
}

static inline void
gbm_dmabuf_close (void)
{
  GST_DEBUG ("gbm_dev %p", gbm_dev);

  if (gbm_dev) {
    gbm_device_destroy (gbm_dev);
    gbm_dev = NULL;
  }

  do_dmabuf_device_close ();
}

static gboolean
gbm_dmabuf_alloc (DmaBufDesc * desc)
{
  uint32_t flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
  struct gbm_bo *bo;
  uint32_t width, height, align_w, align_h;
  generic_buf_layout_t layout = {};
  struct gbm_buf_info buf_info;

  GST_DEBUG ("create gbm bo for format 0x%x, width %d, height %d",
      desc->format, desc->width, desc->height);

  desc->fd = desc->meta_fd = -1;

  if (desc->ubwc)
    flags |= GBM_BO_USAGE_UBWC_ALIGNED_QTI;

  bo = gbm_bo_create (gbm_dev, desc->width, desc->height, desc->format, flags);
  if (NULL == bo) {
    GST_ERROR ("gbm alloc error %s-%d", strerror (errno), errno);
    return FALSE;
  }

  desc->bo = bo;
  desc->fd = gbm_bo_get_fd (bo);
  width = gbm_bo_get_width (bo);
  height = gbm_bo_get_height (bo);
  desc->stride = gbm_bo_get_stride (bo);
  desc->modifier = gbm_bo_get_modifier (bo);

  GST_DEBUG ("created gbm bo %p, fd %d, width %u, height %u, "
      "stride %u, modifier %lx",
      bo, desc->fd, width, height, desc->stride, desc->modifier);

  {
    size_t size = 0;

    gbm_perform (GBM_PERFORM_GET_METADATA_ION_FD, bo, &(desc->meta_fd));

    gbm_perform (GBM_PERFORM_GET_BO_SIZE, bo, &size);
    if ((gsize) size < desc->size)
      GST_WARNING ("gbm bo size %lu should >= requested size", size);

    desc->size = (gsize) size;

    gbm_perform(GBM_PERFORM_GET_YUV_PLANE_INFO, bo, &layout);
    memcpy(&(desc->layout), &layout, sizeof(layout));
    //desc->layout = layout;
    buf_info.width = width;
    buf_info.height = height;
    buf_info.format = desc->format;
    gbm_perform(GBM_PERFORM_GET_BUFFER_SIZE_DIMENSIONS, &buf_info, 0, &align_w, &align_h, &desc->buffer_size_dimensions);
    GST_DEBUG ("created gbm bo meta_fd %d, size %lu, buffer_size_dimensions %d", desc->meta_fd, size, desc->buffer_size_dimensions);
  }

  return TRUE;
}

static void
gbm_dmabuf_free (DmaBufDesc * desc)
{
  if (!desc) {
    GST_ERROR ("NULL desc");
    return;
  }

  GST_DEBUG ("free gbm bo %p, fd %d, meta_fd %d",
      desc->bo, desc->fd, desc->meta_fd);

  if (desc->bo) {
    close (desc->fd);
    gbm_bo_destroy (desc->bo);
    desc->bo = NULL;
    desc->fd = -1;
    desc->meta_fd = -1;
  }
}

/* Better cache performance putting the 2 variables in a same bss segment. */
static gint dmabuf_ref_count;
static GMutex dmabuf_ref_mutex;

static gboolean
_qcarcam_dmabuf_open (void)
/* open dmabuf only when ref count is zero. */
{
  gboolean ret = TRUE;

  g_mutex_lock (&dmabuf_ref_mutex);

  if (0 == dmabuf_ref_count)
    ret = gbm_dmabuf_open ();

  if (ret)
    dmabuf_ref_count++;

  GST_DEBUG ("ref count %d", dmabuf_ref_count);

  g_mutex_unlock (&dmabuf_ref_mutex);

  return ret;
}

/* close dmabuf only when ref count gets to zero. */
static void
_qcarcam_dmabuf_close (void)
{
  g_mutex_lock (&dmabuf_ref_mutex);

  if (dmabuf_ref_count > 0)
    dmabuf_ref_count--;

  if (0 == dmabuf_ref_count)
    gbm_dmabuf_close ();

  GST_DEBUG ("ref count %d", dmabuf_ref_count);

  g_mutex_unlock (&dmabuf_ref_mutex);
}

/* Below are external interfaces. */

gboolean
qcarcam_dmabuf_alloc (DmaBufDesc ** desc,
    const GstVideoInfo * info, gboolean ubwc)
{
  GST_INFO ("ubwc %u, size %" G_GSIZE_FORMAT,
      ubwc, GST_VIDEO_INFO_SIZE (info));

  *desc = g_new0 (DmaBufDesc, 1);
  if (NULL == *desc) {
    GST_ERROR ("no memory");
    return FALSE;
  }

  if (!gbm_dmabuf_fill_desc (*desc, info, ubwc))
    goto desc_free;

  if (!_qcarcam_dmabuf_open ()) {
    GST_ERROR ("open error");
    goto desc_free;
  }

  if (!gbm_dmabuf_alloc (*desc)) {
    GST_ERROR ("alloc error");
    goto dmabuf_close;
  }

  GST_DEBUG ("desc %p, size %" G_GSIZE_FORMAT,
      *desc, qcarcam_dmabuf_get_size (*desc));

  return TRUE;

dmabuf_close:
  _qcarcam_dmabuf_close ();

desc_free:
  g_free (*desc);
  *desc = NULL;

  return FALSE;
}

gint
qcarcam_dmabuf_get_fd (const DmaBufDesc * desc)
{
  gint fd = -1;

  if (desc)
    fd = desc->fd;

  GST_DEBUG ("desc %p, fd=%d", desc, fd);

  return fd;
}

gsize
qcarcam_dmabuf_get_size (const DmaBufDesc * desc)
{
  gsize size = 0;

  if (desc)
    size = desc->size;

  GST_DEBUG ("desc %p, size %" G_GSIZE_FORMAT, desc, size);

  return size;
}

guint64
qcarcam_dmabuf_get_modifier (const DmaBufDesc * desc)
{
  uint64_t modifier = DRM_FORMAT_MOD_INVALID;

  if (desc) {
    if (desc->bo)
      modifier = gbm_bo_get_modifier (desc->bo);
  }

  GST_DEBUG ("desc %p, modifier 0x%lx", desc, modifier);

  return (guint64) modifier;
}

/* align info by allocated desc */
void
qcarcam_dmabuf_align_info (const DmaBufDesc * desc, GstVideoInfo * info)
{
  GST_DEBUG ("desc %p, info=%p", desc, info);
  if (!desc || !info)
    return;

  GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = desc->stride;
  GST_VIDEO_INFO_PLANE_STRIDE (info, 1) = desc->stride;
  GST_VIDEO_INFO_PLANE_OFFSET (info, 0) = gbm_bo_get_offset (desc->bo, 0);
  GST_VIDEO_INFO_PLANE_OFFSET (info, 1) = gbm_bo_get_offset (desc->bo, 1);
  GST_VIDEO_INFO_SIZE (info) = desc->size;

  GST_DEBUG ("aligned info stride %u, offset0 %u, offset1 %u, size %lu", desc->stride,
      gbm_bo_get_offset (desc->bo, 0), gbm_bo_get_offset (desc->bo, 1), desc->size);
}

void
qcarcam_dmabuf_free (DmaBufDesc * desc)
{
  GST_DEBUG ("desc %p", desc);

  gbm_dmabuf_free (desc);
  g_free (desc);

  _qcarcam_dmabuf_close ();
}
