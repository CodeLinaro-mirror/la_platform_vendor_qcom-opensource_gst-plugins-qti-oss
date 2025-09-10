// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QVCONV_BUFFER_POOL__
#define __GST_QVCONV_BUFFER_POOL__

#include "gstqvconv.h"
//#include "OMX_QCOMExtns.h"
#include "c2d_converter.h"
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS
typedef struct _GstQvconvMemory GstQvconvMemory;

#define GST_BUFFER_POOL_OPTION_EXT_BUFFER_META "GstBufferPoolOptionExtBufferMeta"

#define GST_TYPE_QVCONV_MEMORY (gst_qvconv_memory_get_type())
#define GST_IS_QVCONV_MEMORY(obj) (GST_IS_MINI_OBJECT_TYPE(obj, GST_TYPE_QVCONV_MEMORY))
#define GST_QVCONV_MEMORY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QVCONV_MEMORY, GstQvconvMemory))
#define GST_QVCONV_MEMORY_CAST(obj) ((GstQvconvMemory *)(obj))

struct _GstQvconvMemory
{
  GstMemory memory;

  C2DBuffer c2d_buf;

  /* pmem info struct for downstream element */
  //OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO pmem_info;

  gpointer pointer;
};

GType gst_qvconv_memory_get_type (void);

typedef struct _GstQvconvAllocator GstQvconvAllocator;
typedef struct _GstQvconvAllocatorClass GstQvconvAllocatorClass;
typedef struct _GstQvconvAllocatorPrivate GstQvconvAllocatorPrivate;
#define GST_TYPE_QVCONV_ALLOCATOR (gst_qvconv_allocator_get_type())
#define GST_QVCONV_ALLOCATOR(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QVCONV_ALLOCATOR, GstQvconvAllocator))
#define GST_QVCONV_ALLOCATOR_CAST(obj) ((GstQvconvAllocator*)(obj))
GType gst_qvconv_allocator_get_type (void);

GstMemory * gst_qvconv_memory_new (GstQvconvAllocator * allocator, C2DBuffer *c2d_buf, guint mem_valid_size);

bool gst_qvconv_alloc_c2d_buf (C2dConverter *c2d, C2DBuffer *c2d_buf, const GstVideoInfo *info, gboolean ubwc);
void gst_qvconv_free_c2d_buf (C2dConverter *c2d, C2DBuffer *c2d_buf);

struct _GstQvconvAllocator
{
  GstAllocator allocator;
  GstQvconv *qvconv;
  GstQvconvAllocatorPrivate *priv;
};

struct _GstQvconvAllocatorClass
{
    GstAllocatorClass parent_class;
};

gpointer gst_qvconv_memory_map (GstQvconvMemory * memory,
    gsize maxsize, GstMapFlags flags);
void gst_qvconv_memory_unmap (GstQvconvMemory * memory);

GstAllocator *gst_qvconv_allocator_new (GstQvconv * qvconv);
////////////////////////////
#define GST_QVCONV_PRIVATE_DATA gst_qvconv_c2dbuf_quark_get ()
typedef struct _GstQvconvDmaBufAllocator GstQvconvDmaBufAllocator;
typedef struct _GstQvconvDmaBufAllocatorClass GstQvconvDmaBufAllocatorClass;
typedef struct _GstQvconvAllocatorPrivate GstQvconvDmaBufAllocatorPrivate;
#define GST_TYPE_QVCONV_DMABUF_ALLOCATOR (gst_qvconv_dmabuf_allocator_get_type())
#define GST_QVCONV_DMABUF_ALLOCATOR(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QVCONV_DMABUF_ALLOCATOR, GstQvconvDmaBufAllocator))
#define GST_QVCONV_DMABUF_ALLOCATOR_CAST(obj) ((GstQvconvDmaBufAllocator*)(obj))
GType gst_qvconv_dmabuf_allocator_get_type (void);

GstMemory * gst_qvconv_dmabuf_memory_new (GstQvconvDmaBufAllocator * allocator, C2DBuffer *c2d_buf, guint mem_valid_size);

struct _GstQvconvDmaBufAllocator
{
  GstDmaBufAllocator allocator;
  GstQvconv *qvconv;
  GstQvconvDmaBufAllocatorPrivate *priv;
};

struct _GstQvconvDmaBufAllocatorClass
{
    GstDmaBufAllocatorClass parent_class;
};
GQuark gst_qvconv_c2dbuf_quark_get (void);

GstAllocator *gst_qvconv_dmabuf_allocator_new (GstQvconv * qvconv);
////////////////////////////

typedef struct _GstQvconvBufferPool GstQvconvBufferPool;
typedef struct _GstQvconvBufferPoolClass GstQvconvBufferPoolClass;

/* buffer pool functions */
#define GST_TYPE_QVCONV_BUFFER_POOL      (gst_qvconv_buffer_pool_get_type())
#define GST_IS_QVCONV_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QVCONV_BUFFER_POOL))
#define GST_QVCONV_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QVCONV_BUFFER_POOL, GstQvconvBufferPool))
#define GST_QVCONV_BUFFER_POOL_CAST(obj) ((GstQvconvBufferPool*)(obj))
#define gst_qvconv_get_format_from_info(info) (GST_STR_FOURCC (gst_video_format_to_string (info->finfo->format)))

struct _GstQvconvBufferPool
{
  GstBufferPool bufferpool;
  GstAllocator *allocator;
  GstQvconv *qvconv;
  gboolean dmabuf;
};

struct _GstQvconvBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType gst_qvconv_buffer_pool_get_type (void);
GstBufferPool *gst_qvconv_buffer_pool_new (GstQvconv * qvconv, gboolean use_dmabuf);

G_END_DECLS
#endif /* __GST_Qvconv_BUFFER_POOL__ */
