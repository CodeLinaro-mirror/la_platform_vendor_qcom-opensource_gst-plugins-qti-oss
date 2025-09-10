// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqvconvbufferpool.h"
#include "gstqvconvbufmeta.h"
#include "gst/gstinfo.h"
#include <libdrm/drm_fourcc.h>

GST_DEBUG_CATEGORY_EXTERN (gst_qvconv_debug);
#define GST_CAT_DEFAULT gst_qvconv_debug

struct _GstQvconvAllocatorPrivate
{
  gboolean add_extbufmeta;
  gboolean add_metavideo;
  gboolean need_alignment;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstQvconvMemory, gst_qvconv_memory);

GQuark
gst_qvconv_c2dbuf_quark_get (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark = (gsize) g_quark_from_static_string ("GstQvconvC2dBuf");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

static G_DEFINE_QUARK (FBufModifierQuark, gst_fbuf_modifier_qdata);
static void modifier_free(gpointer p_modifier)
{
  SG_INFO_LITE("modifier_free(%p) val 0x%llx called", p_modifier, p_modifier ? *(guint64*)p_modifier : DRM_FORMAT_MOD_INVALID);
  if (p_modifier) {
    g_slice_free(guint64, p_modifier);
  }
  return;
}

bool gst_qvconv_alloc_c2d_buf (C2dConverter *c2d, C2DBuffer *c2d_buf, const GstVideoInfo *info, gboolean ubwc_flags)
{
  GstVideoFormat format;

  if (!c2d || !c2d_buf || !info)
    return false;

  format = GST_VIDEO_INFO_FORMAT(info);
  memset(c2d_buf, 0, sizeof(*c2d_buf));

  switch (gst_qvconv_get_format_from_info (info)) {
  case GST_MAKE_FOURCC ('N', 'V', '1', '2'):
    c2d_buf->gbm_format = GBM_FORMAT_NV12;
    if (ubwc_flags)
      c2d_buf->ubwc_flags = TRUE;
    break;
  case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
    c2d_buf->gbm_format = GBM_FORMAT_UYVY;
    break;
  case GST_MAKE_FOURCC ('A', 'R', 'G', 'B')://It's a workaround, gbm/gfx have no support for GBM_FORMAT_BGRA8888(=GST ARGB=ARGB8888), then, alloc/use GBM_FORMAT_ABGR8888(=GST RGBA=RGBA8888=ADRENO_PIXELFORMAT_R8G8B8A8) buffer
  case GST_MAKE_FOURCC ('R', 'G', 'B', 'A'):
    c2d_buf->gbm_format = GBM_FORMAT_ABGR8888;
    break;
  case GST_MAKE_FOURCC ('B', 'G', 'R', '\0'):
    c2d_buf->gbm_format = GBM_FORMAT_BGR888;
    break;
  case GST_MAKE_FOURCC ('R', 'G', 'B', '\0'):
    c2d_buf->gbm_format = GBM_FORMAT_RGB888;
    break;
  case GST_MAKE_FOURCC ('P', '0', '1', '0'):
    c2d_buf->gbm_format = GBM_FORMAT_P010;
    break;
  default:
    const gchar *name = GST_VIDEO_INFO_NAME(info);
    SG_ERR_LITE ("unsupported gst format for c2d, %d (%s)", format, name);
    return false;
  }

  c2d_buf->width = GST_VIDEO_INFO_WIDTH (info);
  c2d_buf->height = GST_VIDEO_INFO_HEIGHT (info);
  c2d_buf->size = GST_VIDEO_INFO_SIZE (info);

  if (!c2d->allocateBuffer (c2d_buf)) {
    SG_ERR_LITE ("failed to allocate c2d buffer");
    return false;
  }

  return true;
}

void gst_qvconv_free_c2d_buf (C2dConverter *c2d, C2DBuffer *c2d_buf)
{
  if (!c2d || !c2d_buf)
    return;

  c2d->freeBuffer (c2d_buf);
}

gpointer
gst_qvconv_memory_map (GstQvconvMemory * memory, gsize maxsize,
    GstMapFlags flags)
{
  C2DBuffer *c2d_buf = &memory->c2d_buf;
  //OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pmem_info = &memory->pmem_info;

  g_atomic_pointer_set (&memory->pointer, c2d_buf->ptr);
  return g_atomic_pointer_get (&memory->pointer);
}

void
gst_qvconv_memory_unmap (GstQvconvMemory * memory)
{
    // Nothing has to be done
    return;
}

static void
gst_qvconv_memory_free (GstQvconvMemory * memory)
{
  GstMemory *mem;
  GstQvconvAllocator *allocator;

  g_return_if_fail (GST_IS_QVCONV_MEMORY (memory));

  mem = GST_MEMORY_CAST (memory);
  allocator = GST_QVCONV_ALLOCATOR_CAST (mem->allocator);

  SG_INFO_LITE ("freeing memory with allocator %p", allocator);

  if (allocator) {
    C2dConverter *c2d = allocator->qvconv->c2d_hndl;
    C2DBuffer *c2d_buf = &memory->c2d_buf;

    gst_qvconv_free_c2d_buf (c2d, c2d_buf);
  }

  g_slice_free (GstQvconvMemory, memory);
  gst_object_unref (allocator);
}

GstMemory *
gst_qvconv_memory_new (GstQvconvAllocator * allocator,
    C2DBuffer *c2d_buf, guint mem_valid_size)
{
  GstMemory *mem;
  //OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pmem_info;
  GstQvconvMemory *memory = g_slice_new0 (GstQvconvMemory);

  gst_mini_object_init (GST_MINI_OBJECT_CAST (memory),
      GST_MINI_OBJECT_FLAG_LOCKABLE, GST_TYPE_QVCONV_MEMORY, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_qvconv_memory_free);

#if 0
  /* fill pmem info
   * FIXME: the info should be filled in allocator_alloc.
   * While the offset is not returned in c2d allocatebuffer
   * and simply the case of no allocator.
   * TODO: To merge c2d_buf and pmem_info.
   */
  pmem_info = &memory->pmem_info;
  pmem_info->pmem_fd = c2d_buf->fd;
  pmem_info->offset = 0;
  pmem_info->size = c2d_buf->size;
  pmem_info->mapped_size = c2d_buf->size;
  pmem_info->buffer = c2d_buf->ptr;
#endif

  memory->c2d_buf = *c2d_buf;

  mem = GST_MEMORY_CAST (memory);
  mem->allocator = GST_ALLOCATOR_CAST (gst_object_ref (allocator));
  g_warn_if_fail (mem_valid_size <= (guint)c2d_buf->size && "gst memory size should <= gst memory max size");
  mem->size = (guint)c2d_buf->size < mem_valid_size ? c2d_buf->size : mem_valid_size;
  mem->maxsize = (guint)c2d_buf->size > mem_valid_size ? c2d_buf->size : mem_valid_size;
  return mem;
}

/* GstQvconvAllocator class initialization */
G_DEFINE_TYPE_WITH_CODE (GstQvconvAllocator, gst_qvconv_allocator,
    GST_TYPE_ALLOCATOR, G_ADD_PRIVATE (GstQvconvAllocator));

GstAllocator *
gst_qvconv_allocator_new (GstQvconv * qvconv)
{
  GstQvconvAllocator *allocator;

  allocator =
      (GstQvconvAllocator *) g_object_new (GST_TYPE_QVCONV_ALLOCATOR, NULL);

  allocator->qvconv = qvconv;
  gst_object_ref (qvconv);

  return GST_ALLOCATOR_CAST (allocator);
}

static void
gst_qvconv_allocator_init (GstQvconvAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);
  alloc->mem_type = "QvconvAllocator";
  alloc->mem_map = (GstMemoryMapFunction) gst_qvconv_memory_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) gst_qvconv_memory_unmap;

  allocator->priv = (GstQvconvAllocatorPrivate *)
      gst_qvconv_allocator_get_instance_private (allocator);

  SG_INFO_OBJ_LITE (allocator, "allocator %p, priv-size %p-%lu, offset %d",
      allocator, allocator->priv, sizeof(*allocator->priv),
      GstQvconvAllocator_private_offset);
}

static void
gst_qvconv_allocator_dispose (GObject * obj)
{
  GstQvconvAllocator *allocator = (GstQvconvAllocator *) obj;

  if (allocator->qvconv)
    gst_object_unref (allocator->qvconv);

  G_OBJECT_CLASS (gst_qvconv_allocator_parent_class)->dispose (obj);
}

static GstMemory *
gst_qvconv_allocator_alloc (GstAllocator * alloc, gsize size,
    GstAllocationParams * params)
{
  GstQvconvAllocator *allocator = (GstQvconvAllocator *) alloc;
  GstVideoInfo *info = &allocator->qvconv->dst_info;
  C2dConverter *c2d = allocator->qvconv->c2d_hndl;
  C2DBuffer c2d_buf;

  SG_INFO_OBJ_LITE (allocator, "allocating buffer size: %lu", size);
  g_warn_if_fail (GST_VIDEO_INFO_SIZE(info) == size);

  if (!gst_qvconv_alloc_c2d_buf (c2d, &c2d_buf, info, allocator->qvconv->priv->outubwc))
    return NULL;

  SG_INFO_OBJ_LITE (allocator, "allocated c2d buffer sz: %d, it's probably >= origin sz %lu", c2d_buf.size, size);

  return gst_qvconv_memory_new (allocator, &c2d_buf, size);
}

static void
gst_qvconv_allocator_class_init (GstQvconvAllocatorClass * klass)
{
  GObjectClass *gobj_class = (GObjectClass *) klass;
  GstAllocatorClass *alloc_class = (GstAllocatorClass *) klass;

  gobj_class->dispose = gst_qvconv_allocator_dispose;
  alloc_class->alloc = gst_qvconv_allocator_alloc;
}


#define gst_qvconv_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstQvconvBufferPool, gst_qvconv_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static const char **
gst_qvconv_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT, NULL};

  return options;
}

static gboolean
gst_qvconv_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config)
{
  GstCaps *caps;
  GstVideoInfo *info;
  GstQvconvBufferPool *self_pool = GST_QVCONV_BUFFER_POOL_CAST (pool);
  GstAllocator *allocator = self_pool->allocator;
  GstQvconvAllocatorPrivate *priv;
  if(self_pool->dmabuf)
     priv = GST_QVCONV_DMABUF_ALLOCATOR_CAST(allocator)->priv;
  else
     priv = GST_QVCONV_ALLOCATOR_CAST(allocator)->priv;
  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL)) {
    SG_WARN_OBJ_LITE (pool, "invalid config");
    return FALSE;
  }

  if (NULL == caps) {
    SG_WARN_OBJ_LITE (pool, "no caps in config");
    return FALSE;
  }

  /* TODO:
   * For simplicity and the allocator knows the alignement
   * via c2d openned in parent, we just get info from parent
   * instead of calc based on the caps and alignment info.
   * While those info should be calced when we use util
   * allocator to allocate memory.
   */
  info = &self_pool->qvconv->dst_info;
  SG_INFO_OBJ (pool, "%dx%d, caps %" GST_PTR_FORMAT ", format = %s",
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info), caps,
      gst_video_format_to_string (info->finfo->format));

  /* enable metadata based on config of the pool */
  //priv->add_metavideo = gst_buffer_pool_config_has_option (config,
  //    GST_BUFFER_POOL_OPTION_VIDEO_META);
  /* always add GstVideoMeta even for UBWC case as we use GstVideoMeta to transmit fd/meta_fd information */
  priv->add_metavideo = TRUE;

  /* parse extra alignment info */
  priv->need_alignment =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  if (priv->need_alignment)
    priv->add_metavideo = TRUE;

  /* check if has qvconv extbuf meta */
  priv->add_extbufmeta = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_EXT_BUFFER_META);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static GstFlowReturn
gst_qvconv_buffer_pool_alloc (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstQvconvAllocatorPrivate *priv;
  GstQvconvBufferPool *self_pool;
  GstVideoInfo *info;
  GstBuffer *buf;
  GstMemory *mem;
  C2DBuffer *c2d_buf=NULL;
  guint64* p_modifier = g_slice_new (guint64);
  if (!p_modifier) {
    SG_ERR_OBJ_LITE(pool, "g_slice_new() for gbm modifier failed!");
    goto no_buf;
  }


  self_pool = GST_QVCONV_BUFFER_POOL_CAST (pool);
  if(self_pool->dmabuf)
     priv = GST_QVCONV_DMABUF_ALLOCATOR_CAST(self_pool->allocator)->priv;
  else
     priv = GST_QVCONV_ALLOCATOR_CAST(self_pool->allocator)->priv;
  info = &self_pool->qvconv->dst_info;

  SG_INFO_OBJ_LITE (pool, "alloc out buffer");
  buf = gst_buffer_new_allocate (GST_ALLOCATOR_CAST (self_pool->allocator),
      info->size, NULL);
  SG_INFO_OBJ_LITE (pool, "alloc-ed out buffer gstbuf %p", buf);
  if (buf == NULL)
    goto no_buf;

  if (priv->add_metavideo) {
    SG_INFO_OBJ_LITE (pool, "adding GstVideoMeta");
    gst_buffer_add_video_meta_full (buf,
        GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info),
        GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info),
        GST_VIDEO_INFO_N_PLANES (info),
        info->offset,
        info->stride);
  }

  //add_extbufmeta is equivalent to use qvconv plugin allocated output buffer pool, instead of downstream plugin's pool
  if (priv->add_extbufmeta) {
    GstQvconvMemory *memory;
    mem = gst_buffer_get_memory (buf, 0);
    SG_INFO_OBJ_LITE (pool, "adding qvconv extbuf metadata");
    if(self_pool->dmabuf) {
        memory =(GstQvconvMemory *)
           gst_mini_object_get_qdata (GST_MINI_OBJECT (mem),
        GST_QVCONV_PRIVATE_DATA);
        gst_memory_unref (GST_MEMORY_CAST (mem));
    }
    else
        memory  = GST_QVCONV_MEMORY_CAST (mem);
    if(!memory)
      goto no_buf;
    c2d_buf = &memory->c2d_buf;
    if(!c2d_buf->fd)
      goto no_buf;
    gst_buffer_add_qvconv_extbuf_meta (buf, c2d_buf->fd,
        c2d_buf->handle, c2d_buf->ptr, c2d_buf->size);
   if(!self_pool->dmabuf)
     gst_memory_unref (GST_MEMORY_CAST (memory));
  }

  if(c2d_buf && c2d_buf->gbm_bo) {
    extern uint64_t (*_gbm_bo_get_modifier) (struct gbm_bo *bo);
    //Only when using qvconv allocated buffer, need to attach modifier. If buffer is from downstream plugin's pool, downstream plugin should attach the modifier.
    *p_modifier = _gbm_bo_get_modifier (c2d_buf->gbm_bo);
    SG_INFO_OBJ_LITE (pool, "Attaching modifier quark %p, value:0x%lx on gstbuf %p", p_modifier, *p_modifier, buf);
    gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (buf), gst_fbuf_modifier_qdata_quark(), p_modifier, modifier_free);
  }else{
    SG_ERR_OBJ_LITE (pool, "c2d_buf(%p) not correct or gbm_bo is NULL, code should not reach here!", c2d_buf);
    goto no_buf;
  }

  *buffer = buf;
  return GST_FLOW_OK;

no_buf:
  {
    SG_WARN_OBJ_LITE (pool, "alloc out buffer failed!");
    *buffer = NULL;
    if(p_modifier) {
      g_slice_free (guint64, p_modifier);
    }
    return GST_FLOW_ERROR;
  }
}

static void
gst_qvconv_buffer_pool_init (GstQvconvBufferPool * pool)
{
}

static void
gst_qvconv_buffer_pool_finalize (GObject * obj)
{
  GstQvconvBufferPool *pool = GST_QVCONV_BUFFER_POOL_CAST (obj);

  SG_INFO_OBJ_LITE (pool, "finalize buffer pool");

  if (pool->qvconv)
    gst_object_unref (pool->qvconv);

  if (pool->allocator) {
    gst_object_unref (pool->allocator);
    pool->allocator = NULL;
  }

  G_OBJECT_CLASS (gst_qvconv_buffer_pool_parent_class)->finalize (obj);
}

static void
gst_qvconv_buffer_pool_class_init (GstQvconvBufferPoolClass * klass)
{
  GObjectClass *gobj_class = (GObjectClass *) klass;
  GstBufferPoolClass *bp_class = (GstBufferPoolClass *) klass;

  gobj_class->finalize = gst_qvconv_buffer_pool_finalize;

  bp_class->get_options = gst_qvconv_buffer_pool_get_options;
  bp_class->set_config = gst_qvconv_buffer_pool_set_config;
  bp_class->alloc_buffer = gst_qvconv_buffer_pool_alloc;
}

GstBufferPool *
gst_qvconv_buffer_pool_new (GstQvconv * qvconv, gboolean use_dmabuf)
{
  GstQvconvBufferPool *pool;

  g_return_val_if_fail (GST_IS_QVCONV (qvconv), NULL);
  pool = (GstQvconvBufferPool *)
      g_object_new (GST_TYPE_QVCONV_BUFFER_POOL, NULL);
  pool->qvconv = (GstQvconv *) gst_object_ref (qvconv);
  pool->dmabuf = use_dmabuf;
  //allocator for pool
  if(pool->dmabuf)
     pool->allocator = gst_qvconv_dmabuf_allocator_new (qvconv);
  else
     pool->allocator = gst_qvconv_allocator_new (qvconv);

  SG_INFO_OBJ_LITE (pool, "new qvconv %s buffer pool %p", pool->dmabuf ? "dma":"normal", pool);
  return GST_BUFFER_POOL (pool);
}

/* GstQvconvDmaBufAllocator class initialization */
G_DEFINE_TYPE_WITH_CODE (GstQvconvDmaBufAllocator, gst_qvconv_dmabuf_allocator,
    GST_TYPE_DMABUF_ALLOCATOR, G_ADD_PRIVATE (GstQvconvDmaBufAllocator));

static void
gst_qvconv_dmabuf_memory_free (GstQvconvMemory *memory)
{
  GstAllocator *alloc;
  GstQvconvDmaBufAllocator *allocator;

  g_return_if_fail(memory != NULL);
  alloc = memory->memory.allocator;
  allocator = GST_QVCONV_DMABUF_ALLOCATOR_CAST (alloc);

  if (allocator) {
    C2dConverter *c2d = allocator->qvconv->c2d_hndl;
    C2DBuffer *c2d_buf = &memory->c2d_buf;

    gst_qvconv_free_c2d_buf(c2d, c2d_buf);
  }

  g_slice_free (GstQvconvMemory, memory);
  gst_object_unref (allocator);
}

GstMemory *
gst_qvconv_dmabuf_memory_new (GstQvconvDmaBufAllocator * allocator,
    C2DBuffer *c2d_buf, guint mem_valid_size)
{
  GstMemory *mem;
  //OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pmem_info;
  GstQvconvMemory *memory = g_slice_new0 (GstQvconvMemory);

  mem = gst_dmabuf_allocator_alloc_with_flags (GST_ALLOCATOR_CAST(allocator), c2d_buf->fd, c2d_buf->size,
      static_cast<GstFdMemoryFlags>(GST_FD_MEMORY_FLAG_KEEP_MAPPED | GST_FD_MEMORY_FLAG_DONT_CLOSE));
  if (G_UNLIKELY (!mem)) {
    SG_ERR_LITE ("GstFdMemory allocation failed");
    return NULL;
  }

  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (mem),
      GST_QVCONV_PRIVATE_DATA, memory, (GDestroyNotify)gst_qvconv_dmabuf_memory_free);

#if 0
  /* fill pmem info
   * FIXME: the info should be filled in allocator_alloc.
   * While the offset is not returned in c2d allocatebuffer
   * and simply the case of no allocator.
   * TODO: To merge c2d_buf and pmem_info.
   */
  pmem_info = &memory->pmem_info;
  pmem_info->pmem_fd = c2d_buf->fd;
  pmem_info->offset = 0;
  pmem_info->size = c2d_buf->size;
  pmem_info->mapped_size = c2d_buf->size;
  pmem_info->buffer = c2d_buf->ptr;
#endif

  memory->c2d_buf = *c2d_buf;
  memory->memory.allocator = GST_ALLOCATOR_CAST(gst_object_ref (allocator));

  g_warn_if_fail (mem_valid_size <= (guint)c2d_buf->size && "gst memory size should <= gst memory max size");
  mem->size = (guint)c2d_buf->size < mem_valid_size ? c2d_buf->size : mem_valid_size;
  mem->maxsize = (guint)c2d_buf->size > mem_valid_size ? c2d_buf->size : mem_valid_size;
  return mem;
}

GstAllocator *
gst_qvconv_dmabuf_allocator_new (GstQvconv * qvconv)
{
  GstQvconvDmaBufAllocator *allocator;

  allocator =
      (GstQvconvDmaBufAllocator *) g_object_new (GST_TYPE_QVCONV_DMABUF_ALLOCATOR, NULL);

  allocator->qvconv = qvconv;
  gst_object_ref (qvconv);

  return GST_ALLOCATOR_CAST (allocator);;
}

static void
gst_qvconv_dmabuf_allocator_init (GstQvconvDmaBufAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);
  alloc->mem_type = "GST_QVCONV_MEMORY_TYPE_DMABUF";

  allocator->priv = (GstQvconvDmaBufAllocatorPrivate *)
      gst_qvconv_dmabuf_allocator_get_instance_private (allocator);

  SG_INFO_OBJ_LITE (allocator, "allocator %p, priv-size %p-%lu, offset %d",
      allocator, allocator->priv, sizeof(*allocator->priv),
      GstQvconvDmaBufAllocator_private_offset);
}

static void
gst_qvconv_dmabuf_allocator_dispose (GObject * obj)
{
  GstQvconvDmaBufAllocator *allocator = (GstQvconvDmaBufAllocator *) obj;

  if (allocator->qvconv)
    gst_object_unref (allocator->qvconv);

  G_OBJECT_CLASS (gst_qvconv_dmabuf_allocator_parent_class)->dispose (obj);
}

static GstMemory *
gst_qvconv_dmabuf_allocator_alloc (GstAllocator * alloc, gsize size,
    GstAllocationParams * params)
{
  C2dConverter *c2d;
  C2DBuffer c2d_buf;
  GstQvconvDmaBufAllocator *allocator;
  GstVideoInfo *info;

  allocator = (GstQvconvDmaBufAllocator *) alloc;
  c2d = allocator->qvconv->c2d_hndl;
  info = &allocator->qvconv->dst_info;

  SG_INFO_OBJ_LITE (allocator, "allocating out buffer size: %lu", size);
  if (!gst_qvconv_alloc_c2d_buf (c2d, &c2d_buf, info, allocator->qvconv->priv->outubwc)) {
      SG_ERR_LITE ("failed to allocate c2d buf for output buffer");
      return NULL;
  }
  SG_INFO_OBJ_LITE (allocator, "allocated out buffer sz: %d, it's probably >= origin sz %lu", c2d_buf.size, size);

  return gst_qvconv_dmabuf_memory_new (allocator, &c2d_buf, size);
}

static void
gst_qvconv_dmabuf_allocator_class_init (GstQvconvDmaBufAllocatorClass * klass)
{
  GObjectClass *gobj_class = (GObjectClass *) klass;
  GstAllocatorClass *alloc_class = (GstAllocatorClass *) klass;

  gobj_class->dispose = gst_qvconv_dmabuf_allocator_dispose;
  alloc_class->alloc = gst_qvconv_dmabuf_allocator_alloc;
}
