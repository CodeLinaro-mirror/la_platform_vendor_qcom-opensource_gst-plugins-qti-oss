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
    GstBuffer * buffer, gint64 key);


static gboolean
steal_gst_buf (gpointer key, gpointer value, gpointer data)
{
  gint64 vkey = *(gint64 *) key;
  gint fd = (vkey >> 32) & 0xFFFFFFFF;
  gint meta_fd = vkey & 0xFFFFFFFF;
  GST_LOG ("key pointer:%p, key:0x%lx value:%p, fd %d, meta_fd %d",
      key, vkey, value, fd, meta_fd);

  if (meta_fd == 0) {
    GST_LOG ("ext pool duplicated buffer key %p, 0x%lx, steal from table",
        key, vkey);
    g_free (key);
    return TRUE;
  }

  return FALSE;
}

static gboolean
release_gst_buf (gint64 * key, GstBuffer * buffer, GstBufferPool * pool)
{
  gint64 vkey = *key;
  gint fd = (vkey >> 32) & 0xFFFFFFFF;
  gint meta_fd = vkey & 0xFFFFFFFF;
  GST_DEBUG_OBJECT (pool, "key:0x%lx buffer:%p, fd %d, meta_fd %d",
      vkey, buffer, fd, meta_fd);

  if (pool && buffer) {
    gst_buffer_pool_release_buffer (pool, buffer);
    GST_DEBUG_OBJECT (pool, "release_buffer %p from pool %p", buffer, pool);
  }

  return FALSE;
}

static void
gst_qvidc_buffer_pool_init (GstQvidcBufferPool * pool)
{
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
    GstVideoInfoDmaDrm drm_info;
    if (gst_video_info_dma_drm_from_caps (&drm_info, caps)) {
      info = drm_info.vinfo;
      GST_DEBUG_OBJECT (pool, "DMA_DRM caps: size %u, info.size %u, %ux%u",
          size, info.size, info.width, info.height);
      if (size < info.size)
        info.size = size;
      else
        info.size = MAX (size, info.size);
      pool->param.info = info;
    } else {
      GST_ERROR_OBJECT (pool, "caps error");
      return FALSE;
    }
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
    g_hash_table_foreach_steal (buffer_table, steal_gst_buf, NULL);
    g_hash_table_destroy (buffer_table);
  }

  g_queue_clear (&pool->pending_buffers);

  if (pool->meta_allocator) {
    GST_DEBUG_OBJECT (pool, "finalize meta allocator:%p ref cnt:%d", pool->meta_allocator,
        GST_OBJECT_REFCOUNT (pool->meta_allocator));
    gst_object_unref (pool->meta_allocator);
    pool->meta_allocator = NULL;
  }

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
        GST_MINI_OBJECT_REFCOUNT_VALUE (GST_MINI_OBJECT_CAST (gst_buf)));
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
  GstMemory *meta_mem = NULL;

  GST_DEBUG_OBJECT (pool, "enter, size %d", pool->param.info.size);

  if (params) {
    GST_DEBUG_OBJECT (pool, "format %u, start %d, stop %d", params->format,
        params->start, params->stop);
  }

  if (pool->param.is_ext_pool && pool->param.ext_pool) {
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
        ret = GST_FLOW_ERROR;
      }
    }
  } else {
    *buffer = gst_buffer_new ();
    mem = gst_allocator_alloc (pool->allocator, pool->param.info.size, NULL);
    if (mem == NULL) {
      GST_WARNING_OBJECT (pool, "Failed to allocate memory block!");
      gst_buffer_unref (*buffer);
      ret = GST_FLOW_ERROR;
    } else {
      gst_buffer_append_memory (*buffer, mem);
      GST_DEBUG_OBJECT (pool, "append mem %p to buf %p", mem, *buffer);
      ret = GST_FLOW_OK;
    }
  }

  if (ret == GST_FLOW_OK) {
    meta_mem =
        gst_allocator_alloc (pool->meta_allocator, pool->param.metasize, NULL);
    if (meta_mem == NULL) {
      GST_WARNING_OBJECT (pool, "Failed to allocate meta memory block!");
      gst_buffer_unref (*buffer);
      ret = GST_FLOW_ERROR;
    } else {
      gint fd = -1;
      gint meta_fd = -1;

      if (gst_is_dmabuf_memory (mem)) {
        fd = gst_dmabuf_memory_get_fd (mem);
      } else {
        fd = gst_fd_memory_get_fd (mem);
      }

      if (gst_is_dmabuf_memory (meta_mem)) {
        meta_fd = gst_dmabuf_memory_get_fd (meta_mem);
      } else {
        meta_fd = gst_fd_memory_get_fd (meta_mem);
      }

      /* Attach QTI video meta */
      GstCustomMeta *qvd_meta = NULL;
      GstVIDCComp *gst_vidc_comp = pool->param.gst_vidc_comp;
      if (vidc_isEncoder (gst_vidc_comp->comp)) {
        qvd_meta = gst_buffer_add_custom_meta (*buffer, "GstQVIDCEMeta");
        GST_DEBUG_OBJECT (pool, "add custom GstQVIDCEMeta meta-mem %p to buf %p, meta_fd %d",
              meta_mem, *buffer, meta_fd);
      } else {
        qvd_meta = gst_buffer_add_custom_meta (*buffer, "GstQVIDCDMeta");
        GST_DEBUG_OBJECT (pool, "add custom GstQVIDCDMeta meta-mem %p to buf %p, meta_fd %d",
              meta_mem, *buffer, meta_fd);
      }
      if (qvd_meta) {
        GstStructure *structure = gst_custom_meta_get_structure (qvd_meta);
        if (structure) {
          gst_structure_set (structure, "meta-mem", GST_TYPE_MEMORY, meta_mem, NULL);
          GST_DEBUG_OBJECT (pool, "add custom meta-mem %p to buf %p, meta_fd %d",
              meta_mem, *buffer, meta_fd);

          gint64 key = ((gint64) fd << 32) | ((gint64) meta_fd);
          ret = _buffer_pool_add_buffer_to_table (bpool, *buffer, key);
          if (ret != GST_FLOW_OK) {
            gst_buffer_unref (*buffer);
          }
        } else {
          GST_WARNING_OBJECT (pool, "Failed to get custom meta structure");
          gst_buffer_unref (*buffer);
          ret = GST_FLOW_ERROR;
        }
      } else {
        GST_WARNING_OBJECT (pool, "Failed to add custom meta");
        gst_buffer_unref (*buffer);
        ret = GST_FLOW_ERROR;
      }

      gst_memory_unref (meta_mem);
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
gst_qvidc_buffer_pool_release_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstBufferPoolClass *bp_class = GST_BUFFER_POOL_CLASS (parent_class);
  GstQvidcBufferPool *pool = GST_QVIDC_BUFFER_POOL_CAST (bpool);

  _buffer_pool_release_buffer_wrap (bpool, buffer);
}

static void
gst_qvidc_buffer_pool_flush_start (GstBufferPool * bpool)
{
  GstBufferPoolClass *bp_class = GST_BUFFER_POOL_CLASS (parent_class);
  GstQvidcBufferPool *pool = GST_QVIDC_BUFFER_POOL_CAST (bpool);
  GST_DEBUG_OBJECT (pool, "flush_start %p", pool);

  if (pool->param.is_outport) {
    GST_DEBUG_OBJECT (pool, "no flush for output %p", pool);
    return;
  }

  GHashTable *buffer_table = pool->buffer_table;
  if (buffer_table) {
    g_hash_table_foreach_steal (buffer_table, release_gst_buf, bpool);
  }
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
    key = ((gint64) param_ext->fd << 32) | ((gint64) param_ext->meta_fd);
  if (key > 0) {
    GST_DEBUG_OBJECT (pool, "lookup buffer from table key 0x%lx, fd %d", key,
        (key >> 32) & 0xFFFFFFFF);

    gst_buf = (GstBuffer *) g_hash_table_lookup (buffer_table, &key);
    if (gst_buf) {
      GST_DEBUG_OBJECT (pool,
          "found a gst buf:%p fd:%d meta_fd:%d ref_cnt:%d", gst_buf,
          (key >> 32) & 0xFFFFFFFF, (key & 0xFFFFFFFF),
          GST_MINI_OBJECT_REFCOUNT_VALUE (GST_MINI_OBJECT_CAST (gst_buf)));
    } else {
      GST_DEBUG_OBJECT (pool, "no buffer find in table");
    }
  }

  if (!gst_buf) {
    gst_buf = g_queue_pop_head (&pool->pending_buffers);
    if (!gst_buf) {
      GST_WARNING_OBJECT (pool,
          "acquire_buffer failed from pending_buffers, try lookup from table");
      ret = GST_FLOW_ERROR;
    } else {
      GST_DEBUG_OBJECT (pool, "acquire_buffer %p from pending_buffers",
          gst_buf);
      mem = gst_buffer_peek_memory (gst_buf, 0);
      if (G_UNLIKELY (!mem)) {
        GST_ERROR_OBJECT (pool, "failed to allocate gst memory");
        gst_buffer_unref (gst_buf);
        gst_buf = NULL;
        ret = GST_FLOW_ERROR;
      } else {
        gint fd = -1;
        if (gst_is_dmabuf_memory (mem)) {
          fd = gst_dmabuf_memory_get_fd (mem);
          GST_DEBUG_OBJECT (pool, "pending_buffers dma fd %d", fd);
        } else if (gst_is_fd_memory (mem)) {
          fd = gst_fd_memory_get_fd (mem);
          GST_DEBUG_OBJECT (pool, "pending_buffers fd %d", fd);
        }

        if (fd > 0) {
          key = ((gint64) fd << 32);
          GST_DEBUG_OBJECT (pool, "pending_buffers entry key 0x%lx, fd %d", key,
              (key >> 32) & 0xFFFFFFFF);

          if (pool->param.is_ext_pool && !pool->param.ext_pool) {
            /* FIXME: to avoid unnecessary internal buffer allocation with external
             * buffer mode.
             * Because input pool is not provided by upstream like qcarcamsrc,
             * so metadata buffer allocation along with internal buffer are bounded.
             * These internal buffers are overheads and not necessary if using external
             * buffer. Need to refine implementation not to allocate internal buffer.
             */
            if (param_ext) {
              key = ((gint64) param_ext->fd << 32);

              GST_DEBUG_OBJECT (pool, "no ext pool, add buffer to table key 0x%lx, fd %d",
                  key, (key >> 32) & 0xFFFFFFFF);

              ret = _buffer_pool_add_buffer_to_table (bpool, gst_buf, key);
              if (ret != GST_FLOW_OK) {
                gst_buffer_unref (gst_buf);
                gst_buf = NULL;
              }
            } else {
              GST_ERROR_OBJECT (pool, "param_ext is NULL in external pool mode without ext_pool");
              gst_buffer_unref (gst_buf);
              gst_buf = NULL;
              ret = GST_FLOW_ERROR;
            }
          }
        } else {
          GST_ERROR_OBJECT (pool, "failed to get buffer fd %d", fd);
          gst_buffer_unref (gst_buf);
          gst_buf = NULL;
          ret = GST_FLOW_ERROR;
        }
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

  GST_DEBUG_OBJECT (pool, "enter buf %p, is_outport %d", buffer, pool->param.is_outport);
  g_mutex_lock (&pool->buflock);
  if (buffer) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, 0);
    if (G_UNLIKELY (!mem)) {
      GST_ERROR_OBJECT (pool, "failed to get gst memory");
    } else {
      GstVIDCComp *gst_vidc_comp = pool->param.gst_vidc_comp;
      if (gst_vidc_comp) {
        if (pool->param.is_outport) {
          gint fd;
          if (gst_is_dmabuf_memory (mem)) {
            fd = gst_dmabuf_memory_get_fd (mem);
          } else {
            fd = gst_fd_memory_get_fd (mem);
          }

          gint meta_fd = -1;
          guint metasize = 0;
          GstCustomMeta *meta = NULL;
          if (vidc_isEncoder (gst_vidc_comp->comp)) {
            gst_vidc_buffer_get_custom_meta (buffer, "GstQVIDCEMeta", &meta_fd, &metasize);
          } else {
            gst_vidc_buffer_get_custom_meta (buffer, "GstQVIDCDMeta", &meta_fd, &metasize);
          }

          GST_DEBUG_OBJECT (pool,
              "queue buf to driver fd %d, capacity %d, size %d, meta_fd %d", fd,
              pool->param.info.size, gst_buffer_get_size (buffer), meta_fd);

          BufferDescriptor vidcbuf;
          memset (&vidcbuf, 0, sizeof (BufferDescriptor));
          vidcbuf.fd = fd;
          vidcbuf.port_type = BUFFER_PORT_OUTPUT;
          vidcbuf.capacity = pool->param.info.size;
          vidcbuf.size = 0;
          vidcbuf.meta_fd = meta_fd;
          vidcbuf.metasize = metasize;

          if (!vidc_queue (gst_vidc_comp->comp, &vidcbuf)) {
            GST_ERROR_OBJECT (pool, "failed to queue buf fd %d", fd);
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
    GstBuffer * buffer, gint64 key)
{
  GstBufferPoolClass *bp_class = GST_BUFFER_POOL_CLASS (parent_class);
  GstQvidcBufferPool *pool = GST_QVIDC_BUFFER_POOL_CAST (bpool);
  GstFlowReturn ret = GST_FLOW_OK;
  GHashTable *buffer_table = pool->buffer_table;
  GstVideoInfo *vinfo = &pool->param.info;

  GST_DEBUG_OBJECT (pool, "enter buf %p, key 0x%lx", buffer, key);

  if (G_UNLIKELY (!buffer)) {
    GST_ERROR_OBJECT (pool, "invalid gst buffer");
    ret = GST_FLOW_ERROR;
  }

  if (ret == GST_FLOW_OK) {
    gint64 *buf_key = g_malloc (sizeof (gint64));
    if (buf_key) {
      *buf_key = key;
      g_hash_table_insert (buffer_table, buf_key, buffer);
      GST_DEBUG_OBJECT (pool,
          "add a gst buf:%p fd:%d meta_fd:%d ref_cnt:%d, key pointer:%p, key:0x%lx",
          buffer, (*buf_key >> 32) & 0xFFFFFFFF, (*buf_key & 0xFFFFFFFF),
          GST_MINI_OBJECT_REFCOUNT_VALUE (GST_MINI_OBJECT_CAST (buffer)),
          buf_key, *buf_key);
    } else {
      GST_ERROR_OBJECT (pool, "fail to alloc buf key");
      ret = GST_FLOW_ERROR;
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

  bp_class->set_config = gst_qvidc_buffer_pool_set_config;
  bp_class->alloc_buffer = gst_qvidc_buffer_pool_alloc_buffer;
  bp_class->acquire_buffer = gst_qvidc_buffer_pool_acquire_buffer;
  bp_class->release_buffer = gst_qvidc_buffer_pool_release_buffer;
  bp_class->flush_start = gst_qvidc_buffer_pool_flush_start;
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

  if (pool->param.is_ext_pool && pool->param.ext_pool) {
    GST_DEBUG_OBJECT (pool, "using ext pool %p", pool->param.ext_pool);
  } else {
    GST_DEBUG_OBJECT (pool, "Create qvic allocator");
    pool->allocator = gst_qvidc_allocator_new (param->mode);
    g_return_val_if_fail (pool->allocator != NULL, NULL);
  }

  pool->meta_allocator = gst_qvidc_allocator_new (param->mode);
  g_return_val_if_fail (pool->meta_allocator != NULL, NULL);

  buffer_table =
      g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free,
      destroy_gst_buffer);
  pool->buffer_table = buffer_table;

  g_queue_init (&pool->pending_buffers);

  GST_INFO_OBJECT (pool,
      "new buffer pool:%p allocator:%p table %p ubwc:%d, meta_allocator %p", pool,
      pool->allocator, buffer_table, param->is_ubwc, pool->meta_allocator);

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
