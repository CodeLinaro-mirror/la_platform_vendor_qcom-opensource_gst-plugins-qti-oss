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
#include "gstqvidcbufferpool.h"
#include "vidcwrapper.h"

GST_DEBUG_CATEGORY_EXTERN (qvidcbufferpool_debug);
#define GST_CAT_DEFAULT qvidcbufferpool_debug

G_DEFINE_TYPE (GstQvidcBufferPool, gst_qvidc_buffer_pool, GST_TYPE_BUFFER_POOL);

#define parent_class gst_qvidc_buffer_pool_parent_class

/* Function will be named qvidcbufferpool_qdata_quark() */
static G_DEFINE_QUARK (QvidcBufferPoolQuark, qvidcbufferpool_qdata);

static GstFlowReturn
_buffer_pool_acquire_buffer_wrap (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params);
static void
_buffer_pool_release_buffer_wrap (GstBufferPool * bpool, GstBuffer * buffer);

static GstFlowReturn
_buffer_pool_add_buffer_to_table (GstBufferPool * bpool,
    GstBuffer * buffer, GstMemory * mem);

GType
gst_video_vidcbuf_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { GST_META_TAG_VIDEO_STR, NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstVideoVIDCBufMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static gboolean
gst_video_vidcbuf_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstVideoVIDCBufMeta *vidcbuf_meta = (GstVideoVIDCBufMeta *) meta;
  vidcbuf_meta->vidc_buf = NULL;

  return TRUE;
}

const GstMetaInfo *
gst_video_vidcbuf_meta_get_info (void)
{
  static const GstMetaInfo *video_vidcbuf_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & video_vidcbuf_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_VIDEO_VIDCBUF_META_API_TYPE,
        "GstVideoVIDCBufMeta",
        sizeof (GstVideoVIDCBufMeta),
        (GstMetaInitFunction) gst_video_vidcbuf_meta_init, NULL, NULL);
    g_once_init_leave ((GstMetaInfo **) & video_vidcbuf_meta_info,
        (GstMetaInfo *) meta);
  }
  return video_vidcbuf_meta_info;
}

static void
print_gst_buf (gpointer key, gpointer value, gpointer data)
{
  GST_LOG ("key:0x%lx value:%p", *(gint64 *) key, value);
}

static void
gst_qvidc_buffer_pool_init (GstQvidcBufferPool * pool)
{
}

static const gchar **
gst_qvidc_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_VIDCBUF_META,
    NULL
  };

  return options;
}

static gboolean
gst_qvidc_buffer_pool_set_config (GstBufferPool * bpool, GstStructure * config)
{
  GstCaps *caps;
  GstVideoInfo info;
  guint size, min, max;
  GstQvidcBufferPool *pool = GST_QVIDC_BUFFER_POOL_CAST (bpool);

  if (NULL == config) {
    GST_ERROR_OBJECT (pool, "null config");
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min, &max)) {
    GST_ERROR_OBJECT (pool, "invalid config");
    return FALSE;
  }

  if (NULL == caps) {
    GST_WARNING_OBJECT (pool, "no caps in config, ignore this config");
    return FALSE;
  }

  GST_INFO_OBJECT (pool, "pool:%p, caps %" GST_PTR_FORMAT
      " min:%u max:%u size:%u,", pool, caps, min, max, size);

  if (gst_video_info_from_caps (&info, caps)) {
    GST_DEBUG_OBJECT (pool, "size %u, info.size %u, %ux%u",
        size, info.size, info.width, info.height);
    g_return_val_if_fail (size >= info.size, FALSE);
    info.size = MAX (size, info.size);
    pool->param.info = info;
  } else {
    GST_ERROR_OBJECT (pool, "caps error");
    return FALSE;
  }

  // gst_buffer_pool_config_set_params (config, caps, info.size, min, max);
  if (pool->allocator) {
    GST_ERROR_OBJECT (pool, "config_set_allocator");
    gst_buffer_pool_config_set_allocator (config, pool->allocator, NULL);
  }

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (bpool, config);
}

static void
gst_qvidc_buffer_pool_finalize (GObject * obj)
{
  GstQvidcBufferPool *pool = GST_QVIDC_BUFFER_POOL_CAST (obj);
  GHashTable *buffer_table = pool->buffer_table;
  GstVIDCComp *gst_vidc_comp = pool->param.gst_vidc_comp;

  GST_DEBUG_OBJECT (pool, "finalize buffer pool:%p", pool);

  if (buffer_table) {
    g_hash_table_foreach (buffer_table, print_gst_buf, NULL);
    g_hash_table_destroy (buffer_table);
  }

  g_queue_clear (&pool->pending_buffers);

  if (pool->allocator) {
    GST_DEBUG_OBJECT (pool, "finalize allocator:%p ref cnt:%d", pool->allocator,
        GST_OBJECT_REFCOUNT (pool->allocator));
    gst_object_unref (pool->allocator);
    pool->allocator = NULL;
  }

  g_mutex_clear (&pool->buflock);

  if (gst_vidc_comp) {
    gst_vidc_comp_unref (gst_vidc_comp);
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
destroy_gst_buffer (gpointer data)
{
  GstBuffer *gst_buf = (GstBuffer *) data;
  if (gst_buf) {
    GST_LOG ("destroy gst buffer:%p ref_cnt:%d", gst_buf,
        GST_OBJECT_REFCOUNT (gst_buf));
    gst_buffer_unref (gst_buf);
  }
}

static gboolean
mark_meta_data_pooled (GstBuffer * buffer, GstMeta ** meta, gpointer user_data)
{
  GST_META_FLAG_SET (*meta, GST_META_FLAG_POOLED);
  GST_META_FLAG_SET (*meta, GST_META_FLAG_LOCKED);

  return TRUE;
}

GstFlowReturn
gst_qvidc_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstQvidcBufferPool *pool = GST_QVIDC_BUFFER_POOL_CAST (bpool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstMemory *mem = NULL;

  GST_DEBUG_OBJECT (pool, "enter, size %d", pool->param.info.size);

  if (params) {
    GST_DEBUG_OBJECT (pool, "format %u, start %d, stop %d", params->format,
        params->start, params->stop);
  }

  if (pool->param.is_ext_pool) {
    GstBufferPoolAcquireParamsExt params_ext;
    ret = gst_buffer_pool_acquire_buffer (pool->param.ext_pool, buffer, NULL);
    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (pool, "acquire ext buffer failed from pool %p, ret %d",
          pool->param.ext_pool, ret);
    } else {
      mem = gst_buffer_peek_memory (*buffer, 0);
      if (mem == NULL) {
        GST_WARNING_OBJECT (pool, "Failed to acquire ext memory block!");
        gst_buffer_unref (*buffer);
      } else {
        ret = _buffer_pool_add_buffer_to_table (bpool, *buffer, mem);
        if (ret == GST_FLOW_OK) {
          gint fd;
          if (gst_is_dmabuf_memory (mem)) {
            fd = gst_dmabuf_memory_get_fd (mem);
          } else {
            fd = gst_fd_memory_get_fd (mem);
          }
          GST_DEBUG_OBJECT (pool,
              "Acquired ext buffer fd: %d in buffer: %p from pool: %p", fd,
              *buffer, pool->param.ext_pool);
        } else {
          gst_buffer_unref (*buffer);
        }
      }
    }
  } else {
    *buffer = gst_buffer_new ();
    mem = gst_allocator_alloc (pool->allocator, pool->param.info.size, NULL);
    if (mem == NULL) {
      GST_WARNING_OBJECT (pool, "Failed to allocate memory block!");
      gst_buffer_unref (*buffer);
    } else {
      ret = _buffer_pool_add_buffer_to_table (bpool, *buffer, mem);
      if (ret == GST_FLOW_OK) {
        gst_buffer_append_memory (*buffer, mem);
        gint fd;
        if (gst_is_dmabuf_memory (mem)) {
          fd = gst_dmabuf_memory_get_fd (mem);
        } else {
          fd = gst_fd_memory_get_fd (mem);
        }
        GST_DEBUG_OBJECT (pool, "append mem %p to buf %p, fd %d", mem, *buffer,
            fd);
      } else {
        gst_buffer_unref (*buffer);
      }
    }
  }

  return ret;
}

static GstFlowReturn
gst_qvidc_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstQvidcBufferPool *pool = GST_QVIDC_BUFFER_POOL_CAST (bpool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);

  GST_DEBUG_OBJECT (pool, "enter");
  return _buffer_pool_acquire_buffer_wrap (bpool, buffer, params);
}

static void
gst_qvidc_buffer_pool_free_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstBufferPoolClass *bp_class = GST_BUFFER_POOL_CLASS (parent_class);
  GstQvidcBufferPool *pool = GST_QVIDC_BUFFER_POOL_CAST (bpool);
  GstMemory *mem = NULL;

  GST_DEBUG_OBJECT (pool, "enter");

  gst_buffer_unref (buffer);
}

static void
gst_qvidc_buffer_pool_release_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstBufferPoolClass *bp_class = GST_BUFFER_POOL_CLASS (parent_class);
  GstQvidcBufferPool *pool = GST_QVIDC_BUFFER_POOL_CAST (bpool);

  _buffer_pool_release_buffer_wrap (bpool, buffer);
}

static GstFlowReturn
_buffer_pool_acquire_buffer_wrap (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBufferPoolClass *bp_class = GST_BUFFER_POOL_CLASS (parent_class);
  GstQvidcBufferPool *pool = GST_QVIDC_BUFFER_POOL_CAST (bpool);
  GstMemory *mem = NULL;
  GstBuffer *gst_buf = NULL;
  gint64 key = -1;

  GstBufferPoolAcquireParamsExt *param_ext =
      (GstBufferPoolAcquireParamsExt *) params;

  GstVideoInfo *vinfo = &pool->param.info;
  GHashTable *buffer_table = pool->buffer_table;

  g_mutex_lock (&pool->buflock);
  GST_ERROR_OBJECT (pool, "acquire_buffer %p", params);

  if (param_ext)
    key = ((gint64) param_ext->fd << 32) | param_ext->meta_fd;
  if (key > 0) {
    GST_DEBUG_OBJECT (pool, "lookup buffer from table key 0x%lx, fd %d", key,
        (key >> 32) & 0xFFFFFFFF);

    gst_buf = (GstBuffer *) g_hash_table_lookup (buffer_table, &key);
    if (gst_buf) {
      GST_DEBUG_OBJECT (pool,
          "found a gst buf:%p fd:%d meta_fd:%d ref_cnt:%d", gst_buf,
          (key >> 32) & 0xFFFFFFFF, (key & 0xFFFFFFFF),
          GST_OBJECT_REFCOUNT (gst_buf));
    } else {
      GST_DEBUG_OBJECT (pool, "no buffer find in table");
      ret = GST_FLOW_ERROR;
    }
  } else {
    gst_buf = g_queue_pop_head (&pool->pending_buffers);
    if (!gst_buf) {
      GST_WARNING_OBJECT (pool,
          "acquire_buffer failed from pending_buffers, try lookup from table");
      ret = GST_FLOW_ERROR;
    } else {
      GST_WARNING_OBJECT (pool, "acquire_buffer %p from pending_buffers",
          gst_buf);
      mem = gst_buffer_peek_memory (gst_buf, 0);
      if (G_UNLIKELY (!mem)) {
        GST_ERROR_OBJECT (pool, "failed to allocate gst memory");
        gst_buffer_unref (gst_buf);
        gst_buf = NULL;
        ret = GST_FLOW_ERROR;
      } else {
        gint fd;
        if (gst_is_dmabuf_memory (mem)) {
          fd = gst_dmabuf_memory_get_fd (mem);
          GST_DEBUG_OBJECT (pool, "pending_buffers dma fd %d", fd);
        } else if (gst_is_fd_memory (mem)) {
          fd = gst_fd_memory_get_fd (mem);
          GST_DEBUG_OBJECT (pool, "pending_buffers fd %d", fd);
        }
        key = ((gint64) fd << 32);
        GST_DEBUG_OBJECT (pool, "pending_buffers entry key 0x%lx, fd %d", key,
            (key >> 32) & 0xFFFFFFFF);
      }
    }
  }

  *buffer = gst_buf;
  g_mutex_unlock (&pool->buflock);

  return ret;
}

static void
_buffer_pool_release_buffer_wrap (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstBufferPoolClass *bp_class = GST_BUFFER_POOL_CLASS (parent_class);
  GstQvidcBufferPool *pool = GST_QVIDC_BUFFER_POOL_CAST (bpool);

  GST_DEBUG_OBJECT (pool, "enter buf %p", buffer);
  g_mutex_lock (&pool->buflock);
  if (buffer) {

    GstMemory *mem = gst_buffer_peek_memory (buffer, 0);
    if (G_UNLIKELY (!mem)) {
      GST_ERROR_OBJECT (pool, "failed to get gst memory");
    } else {
      GstVIDCComp *gst_vidc_comp = pool->param.gst_vidc_comp;
      if (gst_vidc_comp) {
        if (!vidc_isEncoder (gst_vidc_comp->comp)) {
          if (pool->param.is_outport) {
            gint fd;
            if (gst_is_dmabuf_memory (mem)) {
              fd = gst_dmabuf_memory_get_fd (mem);
            } else {
              fd = gst_fd_memory_get_fd (mem);
            }
            GST_DEBUG_OBJECT (pool,
                "queue buf to driver fd %d, capacity %d, size %d", fd,
                pool->param.info.size, gst_buffer_get_size (buffer));

            BufferDescriptor vidcbuf;
            memset (&vidcbuf, 0, sizeof (BufferDescriptor));
            vidcbuf.fd = fd;
            vidcbuf.port_type = BUFFER_PORT_OUTPUT;
            vidcbuf.capacity = pool->param.info.size;
            vidcbuf.size = 0;

            if (!vidc_queue (gst_vidc_comp->comp, &vidcbuf)) {
              GST_ERROR_OBJECT (pool, "failed to queue buf fd %d", fd);
            }
          }
        }
      }
    }

    GST_DEBUG_OBJECT (pool, "push pending_buffers buf %p", buffer);
    g_queue_push_tail (&pool->pending_buffers, buffer);
  }

  g_mutex_unlock (&pool->buflock);
}

static GstFlowReturn
_buffer_pool_add_buffer_to_table (GstBufferPool * bpool,
    GstBuffer * buffer, GstMemory * mem)
{
  GstBufferPoolClass *bp_class = GST_BUFFER_POOL_CLASS (parent_class);
  GstQvidcBufferPool *pool = GST_QVIDC_BUFFER_POOL_CAST (bpool);
  GstFlowReturn ret = GST_FLOW_OK;
  GHashTable *buffer_table = pool->buffer_table;
  GstVideoInfo *vinfo = &pool->param.info;

  GST_DEBUG_OBJECT (pool, "enter buf %p", buffer);

  if (G_UNLIKELY (!buffer)) {
    GST_ERROR_OBJECT (pool, "invalid gst buffer");
    ret = GST_FLOW_ERROR;
  }

  if (G_UNLIKELY (!mem)) {
    GST_ERROR_OBJECT (pool, "invalid gst memory");
    ret = GST_FLOW_ERROR;
  }

  if (ret == GST_FLOW_OK) {
    gint fd;
    if (gst_is_dmabuf_memory (mem)) {
      fd = gst_dmabuf_memory_get_fd (mem);
    } else {
      fd = gst_fd_memory_get_fd (mem);
    }
    if (fd < 0) {
      GST_ERROR_OBJECT (pool, "failed to get buffer fd");
      ret = GST_FLOW_ERROR;
    } else {
      guint64 *buf_key = g_malloc (sizeof (guint64));
      *buf_key = (guint64) fd << 32;
      g_hash_table_insert (buffer_table, buf_key, buffer);
      GST_DEBUG_OBJECT (pool,
          "add a gst buf:%p fd:%d meta_fd:%d ref_cnt:%d, key:0x%lx", buffer,
          (*buf_key >> 32) & 0xFFFFFFFF, (*buf_key & 0xFFFFFFFF),
          GST_OBJECT_REFCOUNT (buffer), *buf_key);
    }
  }

  return ret;
}

static void
gst_qvidc_buffer_pool_class_init (GstQvidcBufferPoolClass * klass)
{
  GObjectClass *gobj_class = (GObjectClass *) klass;
  GstBufferPoolClass *bp_class = (GstBufferPoolClass *) klass;

  gobj_class->finalize = gst_qvidc_buffer_pool_finalize;

  bp_class->get_options = gst_qvidc_buffer_pool_get_options;
  bp_class->set_config = gst_qvidc_buffer_pool_set_config;
  bp_class->alloc_buffer = gst_qvidc_buffer_pool_alloc_buffer;
  bp_class->free_buffer = gst_qvidc_buffer_pool_free_buffer;
  bp_class->acquire_buffer = gst_qvidc_buffer_pool_acquire_buffer;
  bp_class->release_buffer = gst_qvidc_buffer_pool_release_buffer;
}

GstBufferPool *
gst_qvidc_buffer_pool_new (GstBufferPoolInitParam * param)
{
  GstQvidcBufferPool *pool = NULL;
  GHashTable *buffer_table = NULL;

  if (!param) {
    GST_ERROR ("invalid input parameter");
    return NULL;
  }

  pool = (GstQvidcBufferPool *)
      g_object_new (GST_TYPE_QVIDC_BUFFER_POOL, NULL);
  if (!pool) {
    GST_ERROR ("failed to create buffer pool");
    return NULL;
  }

  g_mutex_init (&pool->buflock);

  pool->param = *param;

  GST_DEBUG_OBJECT (pool, "pool mode:%d ubwc:%d", param->mode, param->is_ubwc);

  if (pool->param.is_ext_pool) {
    GST_DEBUG_OBJECT (pool, "using ext pool %p", pool->param.ext_pool);
  } else {
    GST_DEBUG_OBJECT (pool, "Create qvic allocator");
    pool->allocator = gst_qvidc_allocator_new (param->mode);
    g_return_val_if_fail (pool->allocator != NULL, NULL);
  }

  buffer_table =
      g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free,
      destroy_gst_buffer);
  pool->buffer_table = buffer_table;

  g_queue_init (&pool->pending_buffers);

  GST_INFO_OBJECT (pool,
      "new output buffer pool:%p allocator:%p table %p ubwc:%d", pool,
      pool->allocator, buffer_table, param->is_ubwc);

  return GST_BUFFER_POOL (pool);
}

GstBuffer *
gst_qvidc_buffer_pool_find_buffer (GstBufferPool * bpool, gint64 key)
{
  GstQvidcBufferPool *pool = GST_QVIDC_BUFFER_POOL_CAST (bpool);
  GST_DEBUG_OBJECT (pool, "enter key 0x%lx", key);

  GstBuffer *gst_buf =
      (GstBuffer *) g_hash_table_lookup (pool->buffer_table, &key);
  return gst_buf;
}
