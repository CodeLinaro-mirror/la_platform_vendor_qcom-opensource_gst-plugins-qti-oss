/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
Changes from Qualcomm Innovation Center, Inc. are provided under the following license:

Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted (subject to the limitations in the
disclaimer below) provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "vidcwrapper.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <gst/gst.h>
#include "gstclient.h"
#include "vidcfactory.h"

GST_DEBUG_CATEGORY(gst_qvidc_wrapper_debug);
#define GST_CAT_DEFAULT gst_qvidc_wrapper_debug
#define INPUT_BUF_COUNT 4
#define OUTPUT_BUF_COUNT 6

using namespace QTI;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CodecCallback API handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CodecCallback : public EventCallback {
public:
    CodecCallback(const void* handle, listener_cb cb);
    ~CodecCallback();

    void onBufferAvailable(
        vidc_frame_data_type& frameData,
        InterlaceInfo& interlaceInfo) override;
    void onError(uint32_t errorCode) override;
    void onReconfig(bool started) override;

private:
    listener_cb mCallback;
    const void* mHandle;
};

CodecCallback::CodecCallback(const void* handle, listener_cb cb)
{

    LOG_MESSAGE("CodecCallback(%p) created", this);

    mCallback = cb;
    mHandle = handle;
}

CodecCallback::~CodecCallback()
{

    LOG_MESSAGE("CodecCallback(%p) destroyed", this);
}

void CodecCallback::onBufferAvailable(
    vidc_frame_data_type& frameData,
    InterlaceInfo& interlaceInfo)
{

    if (!mCallback) {
        LOG_MESSAGE("Callback not set in CodecCallback(%p)", this);
        return;
    }

    if (frameData.buf_type == VIDC_BUFFER_INPUT) {
        LOG_INFO("EBD fd:%d, data_len %u, alloc_len %u, input_tag %lu, "
            "timestamp %lld, meta_fd %d, metasize %u",
            frameData.frame_handle, frameData.data_len, frameData.alloc_len,
            frameData.input_tag, frameData.timestamp,
            frameData.metadata_handle, frameData.alloc_metadata_len);

        BufferDescriptor inBuf;
        memset(&inBuf, 0, sizeof(BufferDescriptor));

        inBuf.port_type = BUFFER_PORT_INPUT;
        inBuf.timestamp = frameData.timestamp;
        inBuf.fd = frameData.frame_handle;
        inBuf.flag = static_cast<FLAG_TYPE>(0);
        inBuf.interlaceMode = 0;
        inBuf.deinterlaced = false;
        inBuf.capacity = frameData.alloc_len;
        inBuf.size = frameData.data_len;
        inBuf.index = frameData.input_tag;
        inBuf.meta_fd = frameData.metadata_handle;
        inBuf.metasize = frameData.alloc_metadata_len;

        mCallback(mHandle, EVENT_INPUTS_DONE, &inBuf);
    } else if (frameData.buf_type == VIDC_BUFFER_OUTPUT) {
        LOG_INFO("FBD fd:%d, data_len %u, alloc_len %u, input_tag %lu, "
            "timestamp %lld, flags 0x%x, meta_fd %d, metasize %u",
            frameData.frame_handle, frameData.data_len, frameData.alloc_len,
            frameData.input_tag, frameData.timestamp, frameData.flags,
            frameData.metadata_handle, frameData.alloc_metadata_len);

        BufferDescriptor outBuf;
        memset(&outBuf, 0, sizeof(BufferDescriptor));

        outBuf.port_type = BUFFER_PORT_OUTPUT;
        outBuf.timestamp = frameData.timestamp;
        outBuf.fd = frameData.frame_handle;
        outBuf.flag = static_cast<FLAG_TYPE>(0);
        outBuf.interlaceMode = interlaceInfo.interlaceMode;
        outBuf.deinterlaced = interlaceInfo.deinterlaced;
        outBuf.capacity = frameData.alloc_len;
        outBuf.size = frameData.data_len;
        outBuf.index = frameData.input_tag;
        outBuf.meta_fd = frameData.metadata_handle;
        outBuf.metasize = frameData.alloc_metadata_len;

        if (frameData.flags & VIDC_FRAME_FLAG_READONLY) {
            outBuf.flag = FLAG_TYPE_DROP_FRAME;
            mCallback(mHandle, EVENT_DROP_FRAME, &outBuf);
        } else {
            mCallback(mHandle, EVENT_OUTPUTS_DONE, &outBuf);
        }
    }
}

void CodecCallback::onError(uint32_t errorCode)
{

    if (!mCallback) {
        LOG_MESSAGE("Callback not set in CodecCallback(%p)", this);
        return;
    }

    mCallback(mHandle, EVENT_ERROR, &errorCode);
}

void CodecCallback::onReconfig(bool started)
{
    if (!mCallback) {
        LOG_MESSAGE("Callback not set in CodecCallback(%p)", this);
        return;
    }

    gboolean outputStarted = started ? TRUE : FALSE;
    mCallback(mHandle, EVENT_RECONFIG, &outputStarted);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ComponentStore API handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void* vidcStore_create()
{
    GST_DEBUG_CATEGORY_INIT(gst_qvidc_wrapper_debug,
        "qvidcwrapper", 0, "GST QTI vidc.0 wrapper");

    LOG_MESSAGE("Creating component store");
    return new VidcFactory();
}

gboolean vidcStore_createComponent(void* const comp_store,
    const gchar* name, void** const component, comp_cb* cb)
{

    LOG_MESSAGE("Creating component");

    gboolean ret = FALSE;
    ComponentIdType id = COMPONENT_DECODER;
    GstClient* client = NULL;

    if (name) {
        if (strstr(name, "decoder")) {
            id = COMPONENT_DECODER;
        } else if (strstr(name, "encoder")) {
            id = COMPONENT_ENCODER;
        } else {
            LOG_MESSAGE("error: unknown component type");
            return ret;
        }

        if (comp_store) {
            VidcFactory* factory = (VidcFactory*)comp_store;

            client = factory->create(id);
            if (client) {
                *component = client;
                ret = TRUE;
            }
        }
    }

    return ret;
}

gboolean vidcStore_isComponentSupported(void* const comp_store, gchar* name)
{
    gboolean ret = FALSE;

    return ret;
}

gboolean vidcStore_delete(void* comp_store)
{

    LOG_MESSAGE("Deleting component store");

    gboolean ret = TRUE;
    if (comp_store) {
        VidcFactory* factory = (VidcFactory*)comp_store;
        delete factory;
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Component API handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
gboolean vidc_setListener(void* const comp, void* cb_context,
    listener_cb listener, BLOCK_MODE_TYPE mayBlock)
{

    LOG_MESSAGE("Updating component listener");

    gboolean ret = FALSE;
    if (comp) {
        GstClient* client = (GstClient*)comp;

        std::unique_ptr<EventCallback> callback =
            std::make_unique<CodecCallback>(cb_context, listener);
        ret = client->setListenercallback(std::move(callback));
    }

    return ret;
}

gboolean vidc_getAllocationCountAndSize(void* const comp,
    BUFFER_PORT_TYPE type, guint* count, guint* size, guint* metasize)
{
    gboolean rc = FALSE;

    LOG_MESSAGE("Comp %p get max: %u type: %d", comp, count, type);

    if (comp) {
        GstClient* client = (GstClient*)comp;
        rc = client->getBufferRequirement(type == BUFFER_PORT_INPUT ?
            VIDC_BUFFER_INPUT : VIDC_BUFFER_OUTPUT, count, size, metasize, true);
    }

    return rc;
}

gboolean vidc_alloc(void* const comp, BufferDescriptor* buffer)
{
    gboolean rc = FALSE;
    if (!buffer) {
        LOG_ERROR("error: buffer is null");
        return rc;
    }

    LOG_MESSAGE("Comp %p allocate buffer type: %d, fd %d, size %d",
        comp, buffer->port_type, buffer->fd, buffer->size);

    if (comp) {
        GstClient* client = (GstClient*)comp;
        if (client->isLoaded()) {
            rc = client->stateIdle();
            if (!rc) {
                LOG_ERROR("set Comp %p stateIdle failed", comp);
                return rc;
            }
        }

        rc = client->useBuffer(buffer->port_type == BUFFER_PORT_INPUT ?
            VIDC_BUFFER_INPUT : VIDC_BUFFER_OUTPUT, buffer->fd, buffer->size);
    }

    return rc;
}

gboolean vidc_queue(void* const comp, BufferDescriptor* buffer)
{

    LOG_MESSAGE("Queueing work");

    gboolean rc = FALSE;

    if (!buffer) {
        LOG_MESSAGE("error: buffer is null");
        return rc;
    }

    LOG_MESSAGE("Comp %p queue buffer type: %d, fd %d, capacity %d size %d, frame_addr %p",
        comp, buffer->port_type, buffer->fd, buffer->capacity, buffer->size, buffer->data);

    if (comp) {
        GstClient* client = (GstClient*)comp;
        rc = TRUE;
        //If state is loaded, we need to put into stateIdle
        //If state is idle, we need to put int stateExecuting
        if (client->isLoaded()) {
            LOG_ERROR("Comp %p stateLoaded, invalid", comp);
            // put into stateIdle when vidc_alloc instead
            // rc = client->stateIdle();
        }

        if (rc && (client->isIdle() || client->isPaused())) {
            LOG_MESSAGE("Comp %p stateExecuting", comp);
            rc = client->stateExecuting();
        }

        if (rc) {
            vidc_frame_data_type frameData;
            memset(&frameData, 0, sizeof(vidc_frame_data_type));
            frameData.alloc_len = buffer->capacity;
            frameData.data_len = buffer->size;
            frameData.frame_handle = buffer->fd;
            frameData.input_tag = buffer->index;
            frameData.timestamp = buffer->timestamp;
            frameData.metadata_handle = buffer->meta_fd;
            frameData.alloc_metadata_len = buffer->metasize;

            if (buffer->port_type == BUFFER_PORT_INPUT) {
                LOG_MESSAGE("Comp %p emptyBuffer handle %d, input_tag %d, meta_fd %d",
                    comp, frameData.frame_handle, frameData.input_tag, frameData.metadata_handle);
                frameData.buf_type = VIDC_BUFFER_INPUT;
                if (buffer->flag == FLAG_TYPE_END_OF_STREAM) {
                    LOG_MESSAGE("Comp %p emptyBuffer handle %d, EOS",
                        comp, frameData.frame_handle);
                    frameData.flags |= VIDC_FRAME_FLAG_EOS;
                }
                rc = client->emptyBuffer(frameData);
            } else {
                LOG_MESSAGE("Comp %p fillBuffer handle %d, input_tag %d, meta_fd %d",
                    comp, frameData.frame_handle, frameData.input_tag, frameData.metadata_handle);
                frameData.buf_type = VIDC_BUFFER_OUTPUT;
                rc = client->fillBuffer(frameData);
            }
        }
    }

    LOG_MESSAGE("Comp %p rc %d", comp, rc);
    return rc;
}

gboolean vidc_start(void* const comp, BUFFER_PORT_TYPE port)
{

    LOG_MESSAGE("Starting component port %d", port);

    gboolean ret = TRUE;

    if (comp) {
        GstClient* client = (GstClient*)comp;

        vidc_buffer_type buffer_type = port == BUFFER_PORT_INPUT ?
            VIDC_BUFFER_INPUT : VIDC_BUFFER_OUTPUT;
        ret = client->start(buffer_type);
    }

    return ret;
}

gboolean vidc_stop(void* const comp, BUFFER_PORT_TYPE port)
{

    LOG_MESSAGE("Stopping component port %d", port);

    gboolean ret = FALSE;

    if (comp) {
        GstClient* client = (GstClient*)comp;
        vidc_buffer_type buffer_type = port == BUFFER_PORT_INPUT ?
            VIDC_BUFFER_INPUT : VIDC_BUFFER_OUTPUT;
        ret = client->stop(buffer_type);
    }

    return ret;
}

gboolean vidc_freeOutBuffer(void* const comp, BufferDescriptor* buffer)
{

    LOG_MESSAGE("Freeing buffer");

    gboolean ret = FALSE;

    if (comp) {
        GstClient* client = (GstClient*)comp;

        vidc_frame_data_type frameData;
        memset(&frameData, 0, sizeof(vidc_frame_data_type));
        frameData.alloc_len = buffer->capacity;
        frameData.data_len = buffer->size;
        frameData.frame_handle = buffer->fd;
        // frameData.frame_addr = buffer->data;
        frameData.buf_type = buffer->port_type == BUFFER_PORT_INPUT ?
            VIDC_BUFFER_INPUT : VIDC_BUFFER_OUTPUT;

        LOG_MESSAGE("Freeing buffer fd %d, type %d",
            frameData.frame_handle, frameData.buf_type);
        ret = client->freeBuffer(frameData);
    }

    return ret;
}

gboolean vidc_delete(void* comp)
{

    LOG_MESSAGE("Deleting component");

    gboolean ret = TRUE;
    if (comp) {
        GstClient* client = (GstClient*)comp;
        delete client;
    }

    return ret;
}

static PlaneInfo::color_format_type toPlanePixelFormat(PIXEL_FORMAT_TYPE pixel)
{
    PlaneInfo::color_format_type result = PlaneInfo::COLOR_FORMAT_UNUSED;
    switch (pixel) {
    case PIXEL_FORMAT_NV12_LINEAR: {
        result = PlaneInfo::COLOR_FORMAT_NV12;
        break;
    }
    case PIXEL_FORMAT_NV12_UBWC: {
        result = PlaneInfo::COLOR_FORMAT_NV12_UBWC;
        break;
    }
    case PIXEL_FORMAT_P010: {
        result = PlaneInfo::COLOR_FORMAT_P010;
        break;
    }
    case PIXEL_FORMAT_TP10_UBWC: {
        result = PlaneInfo::COLOR_FORMAT_TP10_UBWC;
        break;
    }
    default: {
        LOG_ERROR("unsupported pixel format!");
        break;
    }
    }

    return result;
}

void _push_to_settings(gpointer data, gpointer user_data)
{
    ConfigParams* conf_param = (ConfigParams*)data;
    GstClient::ConfigType* param = (GstClient::ConfigType*)user_data;

    LOG_DEBUG("config name:%s", conf_param->config_name);
    if (g_strcmp0(CONFIG_FUNCTION_KEY_PIXELFORMAT, conf_param->config_name) == 0) {
        param->pix_fmt = toPlanePixelFormat(conf_param->pixelFormat.fmt);
        LOG_DEBUG("config pix_fmt %d", param->pix_fmt);
    } else if (g_strcmp0(CONFIG_FUNCTION_KEY_RESOLUTION, conf_param->config_name) == 0) {
        param->width = conf_param->resolution.width;
        param->height = conf_param->resolution.height;
        LOG_DEBUG("config res %dx%d", param->width, param->height);
    } else if (g_strcmp0(CONFIG_FUNCTION_KEY_CODEC, conf_param->config_name) == 0) {
        param->session.codec = conf_param->codec;
        LOG_DEBUG("config codec 0x%08x", param->session.codec);
    } else if (g_strcmp0(CONFIG_FUNCTION_KEY_FRAMERATE, conf_param->config_name) == 0) {
        param->frameRate = conf_param->framerate;
        LOG_DEBUG("config fps %0.2f", conf_param->framerate);
    } else if (g_strcmp0(CONFIG_FUNCTION_KEY_BITRATE, conf_param->config_name) == 0) {
        param->bitRate.target_bitrate = conf_param->val.u32;
        LOG_DEBUG("config bitrate %d", param->bitRate.target_bitrate);
    } else if (g_strcmp0(CONFIG_FUNCTION_KEY_PROFILE_LEVEL, conf_param->config_name) == 0) {
        param->profile.profile = conf_param->profileAndLevel.profile;
        param->level.level = conf_param->profileAndLevel.level;
        LOG_DEBUG("config profile 0x%08x, level 0x%08x", param->profile.profile, param->level.level);
    } else if (g_strcmp0(CONFIG_FUNCTION_KEY_EXTERNAL_BUFFER, conf_param->config_name) == 0) {
        param->dyn_buffer_input = conf_param->use_external_buf;
        LOG_DEBUG("config dyn_buffer_input %d", param->dyn_buffer_input);
    } else {
        LOG_DEBUG("config name:%s unknown", conf_param->config_name);
    }
}

gboolean vidc_config(void* const comp, GPtrArray* config, BLOCK_MODE_TYPE block)
{

    LOG_MESSAGE("Applying configuration block %d", block);

    gboolean ret = FALSE;

    if (comp && config) {
        GstClient* client = (GstClient*)comp;

        GstClient::ConfigType param;
        if (client->getConfiguration(&param)) {
            LOG_MESSAGE("Applying each");
            g_ptr_array_foreach(config, _push_to_settings, &param);
            LOG_MESSAGE("Applying each done");

            if (client->setConfiguration(param)) {
                if (BLOCK_MODE_DONT_BLOCK == block) {
                    ret = client->initialize();
                } else {
                    LOG_WARNING("block applying in case of more configurations needed");
                    ret = TRUE;
                }
            }
        }

        if (!ret) {
            LOG_WARNING("Failed to apply the configuration");
        }
    }

    LOG_MESSAGE("Applying configuration ret %d", ret);
    return ret;
}

gboolean writePlane(void* const comp, uint8_t* dest, BufferDescriptor* buffer_info)
{
    gboolean ret = FALSE;

    if (!dest || !buffer_info) {
        LOG_ERROR("%s: Invalid dest(%p) or buffer_info(%p)", __func__, dest, buffer_info);
        return ret;
    }

    uint8_t* dst = dest;
    uint8_t* src = buffer_info->data;
    LOG_MESSAGE("%s dst %p src %p", __func__, dst, src);

    if (!src) {
        LOG_ERROR("%s: Invalid src", __func__);
        return ret;
    }

    uint32_t width = buffer_info->width;
    uint32_t height = buffer_info->height;
    uint32_t stride = buffer_info->stride[0];
    uint32_t stride_uv = buffer_info->stride[1];

    LOG_MESSAGE("input format %d, %ux%u, stride %u-%u, "
        "offset %" G_GSIZE_FORMAT "-%" G_GSIZE_FORMAT ".",
        buffer_info->format, width, height, stride, stride_uv,
        buffer_info->offset[0], buffer_info->offset[1]);

    // TODO: only support NV12 now, add P010
    if (comp) {
        GstClient* client = (GstClient*)comp;

        PlaneInfo::color_format_type color = client->getPortColorFormat();
        if (color == PlaneInfo::COLOR_FORMAT_NV12
            || color == PlaneInfo::COLOR_FORMAT_NV21
            || color == PlaneInfo::COLOR_FORMAT_P010) {
            uint32_t read_bytes = 0;
            uint32_t bpp = (color == PlaneInfo::COLOR_FORMAT_P010) ? 2 : 1;
            uint32_t y_stride = client->getPlaneStride(0);
            uint32_t uv_stride = client->getPlaneStride(1);
            uint32_t dest_width = client->getPlaneWidth(0);
            uint32_t dest_height = client->getPlaneHeight(0);
            LOG_MESSAGE("%s output %ux%u, y_stride %i, uv_stride %u, bpp %u, Y planeoffset %d",
                __func__, dest_width, dest_height, y_stride, uv_stride, bpp, client->getPlaneBytes(0));

            // write Y plane
            src += buffer_info->offset[0];
            if (stride == y_stride) {
                // Fast path: copy entire Y plane at once if strides match
                memcpy(dst, src, stride * height);

                dst += y_stride * height;
                src += stride * height;
                read_bytes += height * width * bpp;
            } else {
                // Slow path: copy line by line if strides differ
                for (int i = 0; i < height; i++) {
                    memcpy(dst, src, width * bpp);

                    dst += y_stride;
                    src += stride;
                    read_bytes += width * bpp;
                }
            }

            // write UV plane
            dst = dest + client->getPlaneBytes(0);
            if (buffer_info->offset[1] > 0) {
                src = buffer_info->data + buffer_info->offset[1];
            }
            if (stride_uv == uv_stride) {
                // Fast path: copy entire uv plane at once if strides match
                memcpy(dst, src, stride_uv * (height >> 1));

                read_bytes += (height >> 1) * width * bpp;
            } else {
                // Slow path: copy line by line if strides differ
                for (int i = 0; i < height / 2; i++) {
                    memcpy(dst, src, width * bpp);

                    dst += uv_stride;
                    src += stride_uv;
                    read_bytes += width * bpp;
                }
            }

            LOG_MESSAGE("%s total read %u", __func__, read_bytes);
            ret = TRUE;
        } else {
            LOG_ERROR("%s color fmt 0x%x not supported", __func__, color);
        }
    }

    return ret;
}

guint vidc_getPlaneCount(void* const comp)
{
    guint cnt = 0;
    if (comp) {
        GstClient* client = (GstClient*)comp;
        cnt = client->getPlaneCount();
    }

    return cnt;
}

guint vidc_getPlaneStride(void* const comp, const guint plane)
{
    guint stride = 0;
    if (comp) {
        GstClient* client = (GstClient*)comp;
        stride = client->getPlaneStride(plane);
    }

    return stride;
}

guint vidc_getPlaneOffset(void* const comp, const guint plane)
{
    guint offset = 0;
    if (plane > 0) {
        if (comp) {
            GstClient* client = (GstClient*)comp;
            offset = client->getPlaneBytes(plane - 1);
        }
    }

    return offset;
}

gboolean vidc_isEncoder(void* const comp)
{
    gboolean ret = FALSE;
    if (comp) {
        GstClient* client = (GstClient*)comp;
        ret = client->isEncoder();
    }

    return ret;
}

gboolean vidc_isProgressive(void* const comp)
{
    gboolean ret = TRUE;
    if (comp) {
        GstClient* client = (GstClient*)comp;
        ret = client->isProgressive();
    }

    return ret;
}
