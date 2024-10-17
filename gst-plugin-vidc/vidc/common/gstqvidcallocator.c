/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*
*     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <gst/gst.h>
#include "gst/gstinfo.h"
#include "gstqvidcallocator.h"

GST_DEBUG_CATEGORY_EXTERN (qvidcbufferpool_debug);
#define GST_CAT_DEFAULT qvidcbufferpool_debug

/* ------------------------------------------------------------------------ */
/* --- GstQvidcDmaBufAllocator                                          --- */
/* ------------------------------------------------------------------------ */
#define GST_QVIDC_DMABUF_MEMORY_TYPE "qvidcdmabufmem"
#define gst_qvidc_dmabuf_allocator_parent_class dmabuf_parent_class
G_DEFINE_TYPE (GstQvidcDmaBufAllocator, gst_qvidc_dmabuf_allocator,
    GST_TYPE_DMABUF_ALLOCATOR);

static GstMemory *
gst_qvidc_dmabuf_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstQvidcDmaBufAllocator *alloc = GST_QVIDC_DMABUF_ALLOCATOR (allocator);
  GstMemory *mem = NULL;
  GstFdMemoryFlags flags =
      GST_FD_MEMORY_FLAG_DONT_CLOSE | GST_FD_MEMORY_FLAG_KEEP_MAPPED;
  int dmabuf_fd = -1;
  int ret = -1;

  if (alloc->mode == GST_QVIDC_DMABUF_HEAP_MODE
      || alloc->mode == GST_QVIDC_FDBUF_HEAP_MODE) {
    GST_DEBUG_OBJECT (alloc, "enter, heap_fd %d, size %d", alloc->heap_fd,
        size);
    g_return_val_if_fail (alloc->heap_fd > 0, NULL);

    ret =
        dmabufheap_alloc (alloc->heap_fd, size, O_RDWR | O_CLOEXEC, &dmabuf_fd);
    if (ret != 0 || dmabuf_fd < 0) {
      GST_DEBUG_OBJECT (alloc, "failed to alloc dmabuf %d, ret %d", dmabuf_fd,
          ret);
      goto end;
    }
  } else {
    GST_DEBUG_OBJECT (allocator, "GBM allocator not supported");
    //TODO: add GBM alloc
    goto end;
  }

  mem =
      gst_dmabuf_allocator_alloc_with_flags (allocator, dmabuf_fd, size, flags);

  if (G_UNLIKELY (!mem)) {
    GST_DEBUG_OBJECT (alloc, "failed to alloc dmabuf %d mem", dmabuf_fd);
    dmabufheap_free (dmabuf_fd);
    goto end;
  }

  GST_INFO_OBJECT (alloc,
      "Allocate dmabuf gstmemory %p with size = %d, fd = %d", mem, size,
      dmabuf_fd);

end:
  return mem;
}

static void
gst_qvidc_dmabuf_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstQvidcDmaBufAllocator *alloc = GST_QVIDC_DMABUF_ALLOCATOR (allocator);

  GST_DEBUG_OBJECT (alloc, "mem %p", mem);

  gint fd = -1;
  if (alloc->mode == GST_QVIDC_DMABUF_HEAP_MODE
      || alloc->mode == GST_QVIDC_FDBUF_HEAP_MODE) {
    fd = gst_dmabuf_memory_get_fd (mem);
    if (fd > 0) {
      GST_DEBUG_OBJECT (alloc, "dmabuf mem %p, mem_fd %d", mem, fd);
      dmabufheap_free (fd);
    } else {
      GST_ERROR_OBJECT (alloc, "dmabuf mem %p, invalid fd %d", mem, fd);
    }
  } else {
    GST_DEBUG_OBJECT (alloc, "GBM allocator not supported");
    //TODO: add GBM alloc
  }

  GST_ALLOCATOR_CLASS (dmabuf_parent_class)->free (allocator, mem);
}

static void
gst_qvidc_dmabuf_allocator_finalize (GObject * object)
{
  GstQvidcDmaBufAllocator *alloc = GST_QVIDC_DMABUF_ALLOCATOR_CAST (object);

  if (alloc->mode == GST_QVIDC_DMABUF_HEAP_MODE
      || alloc->mode == GST_QVIDC_FDBUF_HEAP_MODE) {
    GST_DEBUG_OBJECT (alloc, "heap_fd %d", alloc->heap_fd);
    if (alloc->heap_fd > 0) {
      dmabufheap_release (alloc->heap_fd);
      alloc->heap_fd = -1;
    }
  } else {
    //TODO: add GBM alloc
    GST_DEBUG_OBJECT (alloc, "GBM allocator not supported");
  }

  G_OBJECT_CLASS (dmabuf_parent_class)->finalize (object);
}

static void
gst_qvidc_dmabuf_allocator_class_init (GstQvidcDmaBufAllocatorClass * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  GST_DEBUG_OBJECT (allocator_class, "enter");
  obj_class->finalize = GST_DEBUG_FUNCPTR (gst_qvidc_dmabuf_allocator_finalize);
  allocator_class->alloc = GST_DEBUG_FUNCPTR (gst_qvidc_dmabuf_allocator_alloc);
  allocator_class->free = GST_DEBUG_FUNCPTR (gst_qvidc_dmabuf_allocator_free);
}

static void
gst_qvidc_dmabuf_allocator_init (GstQvidcDmaBufAllocator * allocator)
{
  GST_DEBUG_OBJECT (allocator, "enter");
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_QVIDC_DMABUF_MEMORY_TYPE;
}

GstAllocator *
gst_qvidc_dmabuf_allocator_new (GstQvidcAllocMode mode)
{
  GstQvidcDmaBufAllocator *allocator = NULL;
  allocator =
      (GstQvidcDmaBufAllocator *) g_object_new (GST_TYPE_QVIDC_DMABUF_ALLOCATOR,
      NULL);
  if (NULL == allocator) {
    GST_ERROR ("Failed to create qvidc allocator");
    return NULL;
  }

  allocator->mode = mode;
  GST_INFO_OBJECT (allocator, "Create vidc dmabuf allocator %p with mode %d",
      allocator, allocator->mode);

  if (mode == GST_QVIDC_DMABUF_HEAP_MODE || mode == GST_QVIDC_FDBUF_HEAP_MODE) {
    allocator->heap_fd = dmabufheap_init (ID_DMA_BUF_HEAP_CACHED);
    GST_DEBUG_OBJECT (allocator, "dmabufheap fd %d", allocator->heap_fd);
    if (allocator->heap_fd < 0) {
      GST_ERROR_OBJECT (allocator, "dmabufheap init failed");
      gst_object_unref (allocator);
      return NULL;
    }
  } else {
    //TODO: add GBM alloc
    GST_DEBUG_OBJECT (allocator, "GBM allocator not supported");
    gst_object_unref (allocator);
    return NULL;
  }

  return GST_ALLOCATOR_CAST (allocator);
}

/* ------------------------------------------------------------------------ */
/* --- GstQvidcFdAllocator                                              --- */
/* ------------------------------------------------------------------------ */
#define GST_QVIDC_FD_MEMORY_TYPE "qvidcfdmem"
#define gst_qvidc_fd_allocator_parent_class fd_parent_class
G_DEFINE_TYPE (GstQvidcFdAllocator, gst_qvidc_fd_allocator,
    GST_TYPE_FD_ALLOCATOR);

static GstMemory *
gst_qvidc_fd_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstQvidcFdAllocator *alloc = GST_QVIDC_FD_ALLOCATOR (allocator);
  GstMemory *mem = NULL;
  GstFdMemoryFlags flags =
      GST_FD_MEMORY_FLAG_DONT_CLOSE | GST_FD_MEMORY_FLAG_KEEP_MAPPED;
  int dmabuf_fd = -1;
  int ret = -1;

  if (alloc->mode == GST_QVIDC_DMABUF_HEAP_MODE
      || alloc->mode == GST_QVIDC_FDBUF_HEAP_MODE) {
    GST_DEBUG_OBJECT (alloc, "enter, heap_fd %d, size %d", alloc->heap_fd,
        size);
    g_return_val_if_fail (alloc->heap_fd > 0, NULL);

    ret =
        dmabufheap_alloc (alloc->heap_fd, size, O_RDWR | O_CLOEXEC, &dmabuf_fd);
    if (ret != 0 || dmabuf_fd < 0) {
      GST_DEBUG_OBJECT (alloc, "failed to alloc dmabuf %d, ret %d", dmabuf_fd,
          ret);
      goto end;
    }
  } else {
    GST_DEBUG_OBJECT (allocator, "GBM allocator not supported");
    //TODO: add GBM allocator
    goto end;
  }

  mem = gst_fd_allocator_alloc (allocator, dmabuf_fd, size, flags);

  if (G_UNLIKELY (!mem)) {
    GST_DEBUG_OBJECT (alloc, "failed to alloc fd %d mem", dmabuf_fd);
    dmabufheap_free (dmabuf_fd);
    goto end;
  }

  GST_INFO_OBJECT (alloc,
      "Allocate fd gstmemory %p with size = %d, fd = %d", mem, size, dmabuf_fd);

end:
  return mem;
}

static void
gst_qvidc_fd_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstQvidcFdAllocator *alloc = GST_QVIDC_FD_ALLOCATOR (allocator);

  gint fd = -1;
  if (alloc->mode == GST_QVIDC_DMABUF_HEAP_MODE
      || alloc->mode == GST_QVIDC_FDBUF_HEAP_MODE) {
    fd = gst_fd_memory_get_fd (mem);
    if (fd > 0) {
      GST_DEBUG_OBJECT (alloc, "dmabuf mem %p, mem_fd %d", mem, fd);
      dmabufheap_free (fd);
    } else {
      GST_ERROR_OBJECT (alloc, "dmabuf mem %p, invalid fd %d", mem, fd);
    }
  } else {
    GST_DEBUG_OBJECT (alloc, "GBM allocator not supported");
    //TODO: add GBM allocator
  }

  GST_ALLOCATOR_CLASS (fd_parent_class)->free (allocator, mem);
}

static void
gst_qvidc_fd_allocator_finalize (GObject * object)
{
  GstQvidcFdAllocator *alloc = GST_QVIDC_FD_ALLOCATOR_CAST (object);

  if (alloc->mode == GST_QVIDC_DMABUF_HEAP_MODE
      || alloc->mode == GST_QVIDC_FDBUF_HEAP_MODE) {
    GST_DEBUG_OBJECT (alloc, "heap_fd %d", alloc->heap_fd);
    if (alloc->heap_fd > 0) {
      dmabufheap_release (alloc->heap_fd);
      alloc->heap_fd = -1;
    }
  } else {
    //TODO: add gbm based
    GST_DEBUG_OBJECT (alloc, "GBM allocator not supported");
  }

  G_OBJECT_CLASS (fd_parent_class)->finalize (object);
}

static void
gst_qvidc_fd_allocator_class_init (GstQvidcFdAllocatorClass * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  GST_DEBUG_OBJECT (allocator_class, "enter");
  obj_class->finalize = GST_DEBUG_FUNCPTR (gst_qvidc_fd_allocator_finalize);
  allocator_class->alloc = GST_DEBUG_FUNCPTR (gst_qvidc_fd_allocator_alloc);
  allocator_class->free = GST_DEBUG_FUNCPTR (gst_qvidc_fd_allocator_free);
}

static void
gst_qvidc_fd_allocator_init (GstQvidcFdAllocator * allocator)
{
  GST_DEBUG_OBJECT (allocator, "enter");
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_QVIDC_FD_MEMORY_TYPE;
}

GstAllocator *
gst_qvidc_fd_allocator_new (GstQvidcAllocMode mode)
{
  GstQvidcFdAllocator *allocator = NULL;
  allocator =
      (GstQvidcFdAllocator *) g_object_new (GST_TYPE_QVIDC_FD_ALLOCATOR, NULL);
  if (NULL == allocator) {
    GST_ERROR ("Failed to create qvidc allocator");
    return NULL;
  }

  allocator->mode = mode;
  GST_INFO_OBJECT (allocator, "Create vidc fd allocator %p with mode %d",
      allocator, allocator->mode);

  if (mode == GST_QVIDC_DMABUF_HEAP_MODE || mode == GST_QVIDC_FDBUF_HEAP_MODE) {
    allocator->heap_fd = dmabufheap_init (ID_DMA_BUF_HEAP_CACHED);
    GST_DEBUG_OBJECT (allocator, "dmabufheap fd %d", allocator->heap_fd);
    if (allocator->heap_fd < 0) {
      GST_ERROR_OBJECT (allocator, "dmabufheap init failed");
      gst_object_unref (allocator);
      return NULL;
    }
  } else {
    //TODO: add gbm based alloc
    GST_DEBUG_OBJECT (allocator, "GBM allocator not supported");
    gst_object_unref (allocator);
    return NULL;
  }

  return GST_ALLOCATOR_CAST (allocator);
}

GstAllocator *
gst_qvidc_allocator_new (GstQvidcAllocMode mode)
{
  GST_INFO_OBJECT (NULL, "Create vidc allocator with mode %d", mode);

  if (mode == GST_QVIDC_DMABUF_HEAP_MODE || mode == GST_QVIDC_DMABUF_GBM_MODE) {
    return gst_qvidc_dmabuf_allocator_new (mode);
  } else {
    return gst_qvidc_fd_allocator_new (mode);
  }
}
