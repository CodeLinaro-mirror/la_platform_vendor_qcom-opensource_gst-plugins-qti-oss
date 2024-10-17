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

#ifndef __GST_QVIDCALLOCATOR_H__
#define __GST_QVIDCALLOCATOR_H__

#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include "plat_dmabuf.h"

G_BEGIN_DECLS typedef struct _GstQvidcDmaBufAllocator GstQvidcDmaBufAllocator;
typedef struct _GstQvidcDmaBufAllocatorClass GstQvidcDmaBufAllocatorClass;
typedef struct _GstQvidcFdAllocator GstQvidcFdAllocator;
typedef struct _GstQvidcFdAllocatorClass GstQvidcFdAllocatorClass;

typedef enum
{
  GST_QVIDC_DMABUF_HEAP_MODE = 0, // GstDmaBufAllocator with dmabufheap-backed memory
  GST_QVIDC_DMABUF_GBM_MODE,      // GstDmaBufAllocator with gbm-backed memory
  GST_QVIDC_FDBUF_HEAP_MODE,      // GstFdAllocator with dmabufheap-backed memory
  GST_QVIDC_FDBUF_GBM_MODE        // GstFdAllocator with gbm-backed memory
} GstQvidcAllocMode;

/* ------------------------------------------------------------------------ */
/* --- GstQvidcDmaBufAllocator                                          --- */
/* ------------------------------------------------------------------------ */
#define GST_QVIDC_DMABUF_ALLOCATOR_NAME "GstQvidcDmaBufAllocator"
#define GST_TYPE_QVIDC_DMABUF_ALLOCATOR \
  (gst_qvidc_dmabuf_allocator_get_type())
#define GST_QVIDC_DMABUF_ALLOCATOR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_QVIDC_DMABUF_ALLOCATOR, GstQvidcDmaBufAllocatorClass))
#define GST_QVIDC_DMABUF_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_QVIDC_DMABUF_ALLOCATOR, GstQvidcDmaBufAllocator))
#define GST_QVIDC_DMABUF_ALLOCATOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_QVIDC_DMABUF_ALLOCATOR, GstQvidcDmaBufAllocatorClass))
#define GST_QVIDC_DMABUF_ALLOCATOR_CAST(obj) ((GstQvidcDmaBufAllocator *) (obj))

struct _GstQvidcDmaBufAllocator
{
  GstDmaBufAllocator parent;

  GstQvidcAllocMode mode;
  gboolean active;

  int heap_fd;
};

struct _GstQvidcDmaBufAllocatorClass
{
  GstDmaBufAllocatorClass parent_class;
};

GType gst_qvidc_dmabuf_allocator_get_type ();



/* ------------------------------------------------------------------------ */
/* --- GstQvidcFdAllocator                                              --- */
/* ------------------------------------------------------------------------ */
#define GST_QVIDC_FD_ALLOCATOR_NAME "GstQvidcFdAllocator"
#define GST_TYPE_QVIDC_FD_ALLOCATOR \
  (gst_qvidc_fd_allocator_get_type())
#define GST_QVIDC_FD_ALLOCATOR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_QVIDC_FD_ALLOCATOR, GstQvidcFdAllocatorClass))
#define GST_QVIDC_FD_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_QVIDC_FD_ALLOCATOR, GstQvidcFdAllocator))
#define GST_QVIDC_FD_ALLOCATOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_QVIDC_FD_ALLOCATOR, GstQvidcFdAllocatorClass))
#define GST_QVIDC_FD_ALLOCATOR_CAST(obj) ((GstQvidcFdAllocator *) (obj))

struct _GstQvidcFdAllocator
{
  GstFdAllocator parent;

  GstQvidcAllocMode mode;
  gboolean active;

  int heap_fd;
};

struct _GstQvidcFdAllocatorClass
{
  GstFdAllocatorClass parent_class;
};

GType gst_qvidc_fd_allocator_get_type ();


/* ------------------------------------------------------------------------ */
/* --- new qvidc allocator                                              --- */
/* ------------------------------------------------------------------------ */
GstAllocator *gst_qvidc_allocator_new (GstQvidcAllocMode mode);

G_END_DECLS
#endif // __GST_QVIDCALLOCATOR_H__
