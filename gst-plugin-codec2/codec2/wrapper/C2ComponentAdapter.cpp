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
*
* Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
* Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "C2ComponentAdapter.h"

#include "C2WrapperUtils.h"

#include <chrono>
#include <C2PlatformSupport.h>
#include <gst/gst.h>
#include <C2AllocatorGBM.h>
#include <C2BlockInternal.h>
#ifdef USE_DMAHEAP
#include <C2DmaLinearAllocator.h>
#else
#include <C2AllocatorIon.h>
#include <C2HandleIonInternal.h>
#endif

GST_DEBUG_CATEGORY_EXTERN(gst_qcodec2_wrapper_debug);
#define GST_CAT_DEFAULT gst_qcodec2_wrapper_debug

/* Currently, size of input queue is 6 in video driver.
 * If count of pending works are more than 6, it causes queue overflow issue.
 */
#define MAX_PENDING_WORK 6
/* max external buffer count extension */
#define MAX_EXT_BUF_CNT_EXTENSION 5

using namespace std::chrono_literals;

std::shared_ptr<C2Buffer> createLinearBuffer(const std::shared_ptr<C2LinearBlock>& block)
{
    return C2Buffer::CreateLinearBuffer(block->share(block->offset(), block->size(), ::C2Fence()));
}

std::shared_ptr<C2Buffer> createGraphicBuffer(const std::shared_ptr<C2GraphicBlock>& block)
{
    return C2Buffer::CreateGraphicBuffer(block->share(C2Rect(block->width(), block->height()), ::C2Fence()));
}

namespace QTI {

static void s_releaseExtBuf (void *comp, int32_t extFd)
{
    reinterpret_cast<C2ComponentAdapter*>(comp)->releaseExtBuf(extFd);
}

C2ComponentAdapter::C2ComponentAdapter(std::shared_ptr<C2Component> comp)
{

    LOG_MESSAGE("Component(%p) created", this);

    mComp = std::move(comp);
    mIntf = nullptr;
    mListener = nullptr;
    mCallback = nullptr;
    mLinearPool = nullptr;
    mGraphicPool = nullptr;
    mNumPendingWorks = 0;
    mDataCopyFunc = nullptr;
    mDataCopyFuncParam = nullptr;
    mC2AllocatorGBM = nullptr;
    mC2LinearAllocator = nullptr;
    mPendingSignaled = false;

#ifdef USE_AGL_C2SERVICE
    mIC2AllocatorGBM = nullptr;
    mUseAglC2Service = true;
#endif
}

C2ComponentAdapter::~C2ComponentAdapter()
{

    LOG_MESSAGE("Component(%p) destroyed", this);

    mComp = nullptr;
    mIntf = nullptr;
    mListener = nullptr;
    mCallback = nullptr;
    mInPendingBuffer.clear();
    mOutPendingBuffer.clear();
    mTrackBuffers.clear();
    mLinearPool = nullptr;
    mGraphicPool = nullptr;
    mC2AllocatorGBM = nullptr;
#ifdef USE_AGL_C2SERVICE
    mIC2AllocatorGBM = nullptr;
#endif
    mC2LinearAllocator = nullptr;
}

c2_status_t C2ComponentAdapter::setListenercallback(std::unique_ptr<EventCallback> callback,
    c2_blocking_t mayBlock)
{

    LOG_MESSAGE("Component(%p) listener set", this);

    c2_status_t result = C2_NO_INIT;

    if (callback != NULL) {
        mListener = std::shared_ptr<C2Component::Listener>(new C2ComponentListenerAdapter(this));
        result = mComp->setListener_vb(mListener, mayBlock);
    }

    if (result == C2_OK) {
        mCallback = std::move(callback);
    }

    return result;
}

c2_status_t C2ComponentAdapter::setDataCopyFunc(void* func, void* param)
{
    c2_status_t result = C2_OK;
    mDataCopyFunc = reinterpret_cast<fnDataCopy>(func);
    mDataCopyFuncParam = param;

    return result;
}

c2_status_t C2ComponentAdapter::writePlane(uint8_t* dest, BufferDescriptor* buffer_info)
{
    c2_status_t result = C2_OK;
    uint8_t* dst = dest;
    uint8_t* src = buffer_info->data;

    if (!dst || !src) {
        LOG_ERROR("Inavlid buffer in writePlane(%p)", this);
        return C2_BAD_VALUE;
    }

    uint32_t width = buffer_info->width;
    uint32_t height = buffer_info->height;
    uint32_t stride = buffer_info->stride[0];

    LOG_MESSAGE("format %d, %ux%u, stride %u, "
                "offset %" G_GSIZE_FORMAT "-%" G_GSIZE_FORMAT ".",
        buffer_info->format, width, height, stride,
        buffer_info->offset[0], buffer_info->offset[1]);

    /*TODO: add support for other color formats */
    if (buffer_info->format == GST_VIDEO_FORMAT_NV12) {
        if (buffer_info->ubwc_flag) {
            memcpy(dst, src, buffer_info->size);
        } else {
            uint32_t y_stride = VENUS_Y_STRIDE(COLOR_FMT_NV12, width);
            uint32_t uv_stride = VENUS_UV_STRIDE(COLOR_FMT_NV12, width);
            uint32_t y_scanlines = VENUS_Y_SCANLINES(COLOR_FMT_NV12, height);
            uint32_t offset = y_stride * y_scanlines;

            if (buffer_info->heic_flag) {
                y_stride = VENUS_Y_STRIDE(COLOR_FMT_NV12_512, width);
                uv_stride = VENUS_UV_STRIDE(COLOR_FMT_NV12_512, width);
                y_scanlines = VENUS_Y_SCANLINES(COLOR_FMT_NV12_512, height);
            }

            src += buffer_info->offset[0];
            if (stride == y_stride && stride == uv_stride) {
                if (buffer_info->offset[1] - buffer_info->offset[0] == offset) {
                    memcpy(dst, src, offset + stride * (height >> 1));
                } else {
                    memcpy(dst, src, stride * height);
                    dst = dest + offset;
                    if (buffer_info->offset[1] > 0) {
                        src = buffer_info->data + buffer_info->offset[1];
                    } else {
                        src += stride * height;
                    }
                    memcpy(dst, src, stride * (height >> 1));
                }
            } else {
                for (int i = 0; i < height; i++) {
                    memcpy(dst, src, width);
                    dst += y_stride;
                    src += stride;
                }

                dst = dest + offset;
                if (buffer_info->offset[1] > 0) {
                    src = buffer_info->data + buffer_info->offset[1];
                }

                for (int i = 0; i < height / 2; i++) {
                    memcpy(dst, src, width);
                    dst += uv_stride;
                    src += stride;
                }
            }
        }
    } else if (buffer_info->format == GST_VIDEO_FORMAT_P010_10LE) {
        uint32_t y_stride = VENUS_Y_STRIDE(COLOR_FMT_P010, width);
        uint32_t uv_stride = VENUS_UV_STRIDE(COLOR_FMT_P010, width);
        uint32_t y_scanlines = VENUS_Y_SCANLINES(COLOR_FMT_P010, height);
        uint32_t offset = y_stride * y_scanlines;

        src += buffer_info->offset[0];
        if (stride == y_stride && stride == uv_stride) {
            if (buffer_info->offset[1] - buffer_info->offset[0] == offset) {
                memcpy(dst, src, offset + stride * (height >> 1));
            } else {
                memcpy(dst, src, stride * height);
                dst = dest + offset;
                if (buffer_info->offset[1] > 0) {
                    src = buffer_info->data + buffer_info->offset[1];
                } else {
                    src += stride * height;
                }
                memcpy(dst, src, stride * (height >> 1));
            }
        } else {
            for (int i = 0; i < height; i++) {
                memcpy(dst, src, width<<1);
                dst += y_stride;
                src += stride;
            }

            dst = dest + offset;
            if (buffer_info->offset[1] > 0) {
                src = buffer_info->data + buffer_info->offset[1];
            }

            for (int i = 0; i < height / 2; i++) {
                memcpy(dst, src, width<<1);
                dst += uv_stride;
                src += stride;
            }
        }
    } else if (buffer_info->format == GST_VIDEO_FORMAT_NV12_10LE32) {
        if (buffer_info->ubwc_flag) {
            memcpy(dst, src, buffer_info->size);
        } else {
            LOG_ERROR("Non UBWC NV12_10LE32 not supported yet");
            result = C2_BAD_VALUE;
        }
    } else {
        result = C2_BAD_VALUE;
    }

    return result;
}

c2_status_t C2ComponentAdapter::prepareC2Buffer(std::shared_ptr<C2Buffer>* c2Buf, BufferDescriptor* buffer)
{
    uint8_t* rawBuffer = buffer->data;
    uint8_t* destBuffer = nullptr;
    uint32_t frameSize = buffer->size;
    c2_status_t result = C2_OK;
    uint32_t allocSize = 0;
    uint32_t dim_x = buffer->width;
    uint32_t dim_y = buffer->height;

    if (!rawBuffer) {
        LOG_ERROR("Inavlid buffer in prepareC2Buffer(%p)", this);
        result = C2_BAD_VALUE;
    } else {
        std::shared_ptr<C2LinearBlock> linear_block;
        std::shared_ptr<C2GraphicBlock> graphic_block;

        std::shared_ptr<C2Buffer> buf;
        c2_status_t err = C2_OK;
        C2MemoryUsage c2Usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };
        if (buffer->secure) {
            c2Usage = { C2MemoryUsage::READ_PROTECTED, 0 };
        }

        if (buffer->pool_type == BUFFER_POOL_BASIC_LINEAR) {
            /* With linear buffer recycling feature added, the 1MB allocation
             * size alignment could get nearly optimal balance of dec input
             * buffer count 7~9 and total buffer size in buffer pool. */
            allocSize = GST_ROUND_UP_N(frameSize, 1024 * 1024);
            err = mLinearPool->fetchLinearBlock(allocSize, c2Usage, &linear_block);
            if (err != C2_OK || !linear_block) {
                LOG_ERROR("Linear pool failed to allocate input buffer of size : (%d)", frameSize);
                return C2_NO_MEMORY;
            }

            if (mDataCopyFunc) {
                if (linear_block->handle()) {
                    const C2Handle* handle = linear_block->handle();
                    if (!handle) {
                        LOG_ERROR("invalid C2 handle");
                        return C2_CORRUPTED;
                    }
                    uint32_t dest_fd = handle->data[0];
                    /* That data length is from upstream gst plugin pushed down gstbuffer.
                     * In the DataCopyFunc callback function, it may reduce the data length
                     * to its actual length accordingly, but couldn’t increase the length
                     * as the dst buffer is already allocated according to that data length.
                     * Hence, pass the data length pointer as parameter to DataCopyFunc
                     * so as to get the actual data length in return.
                     */
                    int ret = mDataCopyFunc(dest_fd, rawBuffer, &frameSize, mDataCopyFuncParam);
                    if (ret) {
                        LOG_ERROR("data copy failed");
                        return C2_CORRUPTED;
                    }

                    if (frameSize > buffer->size) {
                        LOG_ERROR("frameSize exceeds, previous: %u current: %u",
                            buffer->size, frameSize);
                        return C2_CORRUPTED;
                    }
                } else {
                    LOG_ERROR("invalid handle of linear block");
                    return C2_CORRUPTED;
                }
            } else {
                if (!buffer->secure) {
                    C2WriteView view = linear_block->map().get();
                    if (view.error() != C2_OK) {
                        LOG_ERROR("C2LinearBlock::map() failed : %d", view.error());
                        return C2_NO_MEMORY;
                    }
                    destBuffer = view.base();
                    memcpy(destBuffer, rawBuffer, frameSize);
                } else {
                    LOG_ERROR("should not be here for secure mode");
                    return C2_CORRUPTED;
                }
            }
            linear_block->mSize = frameSize;
            buf = createLinearBuffer(linear_block);
        } else if (buffer->pool_type == BUFFER_POOL_BASIC_GRAPHIC) {
            uint32_t gbmUsage = 0;
            if (mGraphicPool) {
                if (buffer->format == GST_VIDEO_FORMAT_NV12) {
                    if (buffer->ubwc_flag) {
                        LOG_MESSAGE("NV12: usage add UBWC");
                        gbmUsage = GBM_BO_USAGE_UBWC_ALIGNED_QTI;
                    } else if (buffer->heic_flag) {
                        LOG_MESSAGE("NV12: usage add NV12 512 QTI");
                        gbmUsage = GBM_BO_PRIVATE_USAGE_NV12_512_QTI;

                        /* In HEIC encode, align width & height to multiples of 512
                         * because in codec2, VENUS_NV12_512 is deprecated. if this
                         * format is enabled for HEIC, C2D will be invoked and used
                         * to create a buffer of width & height which are mutliples
                         * of 512 and then copy buffer.
                         * Now, As VENUS_NV12_512 is deprecated, allocate buffer
                         * with 512 aligned width & height here itself and copy frame
                         * data from gst buffer to C2 graphic buffer.
                         * TODO: In buffer non-copy mode, HEIC encode still fails,
                         * need to fix.
                         */
                        dim_x = static_cast<uint32_t>(GST_ROUND_UP_N(dim_x, 512));
                        dim_y = static_cast<uint32_t>(GST_ROUND_UP_N(dim_y, 512));
                    }
                }
                C2MemoryUsageGBM c2GbmUsage(c2Usage, gbmUsage);

                err = mGraphicPool->fetchGraphicBlock(dim_x, dim_y,
                    gst_to_c2_gbmformat(buffer->format), c2GbmUsage, &graphic_block);
                if (C2_OK != err || !graphic_block) {
                    LOG_ERROR("fetchGraphicBlock failed: %d", err);
                    return C2_NO_MEMORY;
                }

                C2GraphicView view(graphic_block->map().get());
                if (view.error() != C2_OK) {
                    LOG_ERROR("C2GraphicBlock::map failed: %d", view.error());
                    return C2_NO_MEMORY;
                }

                destBuffer = (guint8*)view.data()[0];

                if (C2_OK != writePlane(destBuffer, buffer)) {
                    LOG_ERROR("failed to write planes for graphic buffer");
                    return C2_NO_MEMORY;
                }

                buf = createGraphicBuffer(graphic_block);
            }
        }

        if (!buf) {
            LOG_ERROR("failed to allocate input C2Buffer");
            return C2_NO_MEMORY;
        }
        *c2Buf = buf;
    }

    return result;
}

c2_status_t C2ComponentAdapter::waitForProgressOrStateChange(
    uint32_t maxPendingWorks, uint32_t timeoutMs)
{
    std::unique_lock<std::mutex> ul(mLock);
    std::chrono::milliseconds timeout(timeoutMs);
    std::chrono::steady_clock::time_point endTime = std::chrono::steady_clock::now() + timeout;
    c2_status_t ret = C2_OK;

    LOG_MESSAGE("work pending:%u, max:%u", mNumPendingWorks, maxPendingWorks);

    if (mNumPendingWorks >= maxPendingWorks) {
        auto pendingSignaled = [this]{ return mPendingSignaled; };
        if (timeoutMs > 0) {
            if (!mPendingWorkCond.wait_until(ul, endTime, pendingSignaled)) {
                LOG_ERROR("Timed-Out waiting for work / state-transition (pending=%u)",
                    mNumPendingWorks);
                ret = C2_TIMED_OUT;
            }
        } else if (timeoutMs == 0) {
            mPendingWorkCond.wait(ul, pendingSignaled);
        }

        mPendingSignaled = false;
        LOG_MESSAGE("wake up");
    }

    return ret;
}

void C2ComponentAdapter::registerTrackBuffer(const C2FrameData& input)
{
    uint64_t frameIndex = input.ordinal.frameIndex.peeku();

    for (size_t i = 0; i < input.buffers.size(); ++i) {
        TrackBuffer* trackbuf = new TrackBuffer(this, frameIndex, input.buffers[i]);
        if (trackbuf) {
            c2_status_t status = input.buffers[i]->registerOnDestroyNotify(
                onDestroyNotify, trackbuf);

            if (status != C2_OK) {
                LOG_ERROR("TrackBuffer registerOnDestroyNotify failed, buf idx:%zu", trackbuf->frameIndex);
                delete trackbuf;
            } else {
                LOG_MESSAGE("emplace buf idx:%zu TrackBuffer %p to mTrackBuffers", trackbuf->frameIndex, trackbuf);
                std::unique_lock<std::mutex> ul(mLock);
                mTrackBuffers.emplace(trackbuf);
            }
        }
    }

    if (!input.buffers.empty()) {
        std::unique_lock<std::mutex> ul(mLock);
        mNumPendingWorks++;
    }
}

void C2ComponentAdapter::unregisterTrackBuffer(
    std::list<std::unique_ptr<C2Work> >& workItems)
{
    // Unregister input buffers onDestroyNotify
    for (const std::unique_ptr<C2Work>& work : workItems) {
        if (work) {

            uint64_t frameIndex = work->input.ordinal.frameIndex.peeku();

            {
                std::unique_lock<std::mutex> ul(mLock);
                for (auto it = mTrackBuffers.begin();
                     it != mTrackBuffers.end(); ++it) {
                    if ((*it)->frameIndex == frameIndex) {
                        if (auto buffer = (*it)->buffer.lock()) {
                            buffer->unregisterOnDestroyNotify(
                                onDestroyNotify, *it);
                        }

                        LOG_MESSAGE("erase buf idx:%zu, TrackBuffer %p",
                            frameIndex, (*it));
                        mTrackBuffers.erase(it);
                        delete (*it);
                    }
                }
            }
        }
    }
}

void C2ComponentAdapter::unregisterTrackBufferAll()
{
    LOG_MESSAGE("unregister all track buffers");

    std::unique_lock<std::mutex> ul(mLock);

    for (auto it = mTrackBuffers.begin(); it != mTrackBuffers.end(); ++it) {
        if (auto buf = (*it)->buffer.lock()) {
            LOG_MESSAGE("erase buf idx:%zu TrackBuffer %p", (*it)->frameIndex, (*it));
            buf->unregisterOnDestroyNotify(onDestroyNotify, *it);
        }
        delete (*it);
    }

    mTrackBuffers.clear();
}

void C2ComponentAdapter::onDestroyNotify(const C2Buffer* buf, void* arg)
{
    if (!buf || !arg) {
        LOG_MESSAGE("no buf");
        return;
    }

    TrackBuffer* trackbuf = (TrackBuffer*)arg;
    if (trackbuf->adapter) {
        trackbuf->adapter->onBufferDestroyed(buf, arg);
    }
}

void C2ComponentAdapter::onBufferDestroyed(const C2Buffer* buf, void* arg)
{
    std::unique_lock<std::mutex> ul(mLock);

    LOG_MESSAGE("%s mNumPendingWorks %d", __func__, mNumPendingWorks);

    TrackBuffer* trackbuf = (TrackBuffer*)arg;
    if (!mTrackBuffers.empty()) {

        auto buf = mTrackBuffers.find(trackbuf);
        if (buf != mTrackBuffers.end()) {
            LOG_MESSAGE("erase buf idx:%zu TrackBuffer %p", trackbuf->frameIndex, trackbuf);
            releaseInputBuf(trackbuf->frameIndex);
            mTrackBuffers.erase(buf);
            delete trackbuf;
        }

        if (mNumPendingWorks > 0) {
            mNumPendingWorks--;
        }

        mPendingSignaled = true;
        ul.unlock();
        mPendingWorkCond.notify_one();
    }
}

c2_status_t C2ComponentAdapter::setMaxAllocationCount(uint32_t max, BUFFER_POOL_TYPE type)
{
    c2_status_t status = C2_BAD_VALUE;

    if (BUFFER_POOL_BASIC_GRAPHIC == type) {
#ifdef USE_AGL_C2SERVICE
        if(mIC2AllocatorGBM) {
            status = mIC2AllocatorGBM->setMaxAllocationCount(max);
        } else
#endif
        if (mC2AllocatorGBM) {
            status = mC2AllocatorGBM->setMaxAllocationCount(max);
        }
    } else {
        LOG_ERROR("Unsupported pool type: %d", type);
    }

    return status;
}

uint32_t C2ComponentAdapter::getMaxAllocationCount(BUFFER_POOL_TYPE type)
{
    uint32_t count = 0;

    if (BUFFER_POOL_BASIC_GRAPHIC == type) {
#ifdef USE_AGL_C2SERVICE
        if(mIC2AllocatorGBM) {
            count = mIC2AllocatorGBM->getMaxAllocationCount();
        } else
#endif
        if (mC2AllocatorGBM) {
            count = mC2AllocatorGBM->getMaxAllocationCount();
        }
    } else {
        LOG_ERROR("Unsupported pool type: %d", type);
    }

    return count;
}

std::shared_ptr<C2Buffer> C2ComponentAdapter::alloc(BufferDescriptor* buffer)
{
    c2_status_t ret = C2_OK;
    std::shared_ptr<C2Buffer> buf = nullptr;
    gint32 fd = -1;
    guint32 size = 0;
    uint32_t dim_x = buffer->width;
    uint32_t dim_y = buffer->height;

    if (buffer->pool_type == BUFFER_POOL_BASIC_GRAPHIC) {
        std::shared_ptr<C2GraphicBlock> graphicBlock = nullptr;
        C2MemoryUsage c2Usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };
        uint32_t gbmUsage = 0;

        if (mGraphicPool) {
            if (buffer->ubwc_flag) {
                gbmUsage = GBM_BO_USAGE_UBWC_ALIGNED_QTI;
                LOG_MESSAGE("NV12: usage add UBWC ALIGNED QTI");
            } else if (buffer->heic_flag) {
                gbmUsage = GBM_BO_PRIVATE_USAGE_NV12_512_QTI;
                LOG_MESSAGE("NV12: usage add NV12 512 QTI");

                /* In HEIC encode, align width & height to multiples of 512
                 * because in codec2, VENUS_NV12_512 is deprecated. if this
                 * format is enabled for HEIC, C2D will be invoked and used
                * to create a buffer of width & height which are mutliples
                * of 512 and then copy buffer.
                * Now, As VENUS_NV12_512 is deprecated, allocate buffer
                * with 512 aligned width & height here itself and copy frame
                * data from gst buffer to C2 graphic buffer.
                * TODO: In buffer non-copy mode, HEIC encode still fails,
                * need to fix.
                */
                dim_x = static_cast<uint32_t>(GST_ROUND_UP_N(dim_x, 512));
                dim_y = static_cast<uint32_t>(GST_ROUND_UP_N(dim_y, 512));
            }

            C2MemoryUsageGBM c2GbmUsage(c2Usage, gbmUsage);

            ret = mGraphicPool->fetchGraphicBlock(dim_x, dim_y,
                gst_to_c2_gbmformat(buffer->format), c2GbmUsage, &graphicBlock);

            if (ret != C2_OK || !graphicBlock) {
                LOG_ERROR("Graphic pool failed to allocate input buffer");
                ret = C2_NO_MEMORY;
            } else {
                const C2Handle* handle = graphicBlock->handle();
                if (!handle) {
                    LOG_ERROR("C2GraphicBlock's C2 handle is invalid");
                    ret = C2_CORRUPTED;
                } else {
                    buf = createGraphicBuffer(graphicBlock);
                    if (isUseExternalBuffer(BUFFER_POOL_BASIC_GRAPHIC)) {
                        fd = handle->data[2]; // external fd
                    } else {
                        fd = handle->data[0];
                    }
                    /* ref the buffer and store it. When the fd is queued,
                     * we can find the graphic block with the input fd */
                    mInPendingBuffer[fd] = graphicBlock;
                    buffer->fd = fd;

                    guint32 stride = 0;
                    guint32 width = 0;
                    guint32 height = 0;
                    guint32 format = 0;
                    guint64 usage = 0;

                    _UnwrapNativeCodec2GBMMetadata(handle, &width,
                        &height, &format, &usage, &stride, &size, nullptr);
                    buffer->capacity = size;
                    setBufLayout(buffer, format, usage, width, height, INTERLACE_MODE_PROGRESSIVE);

                    LOG_MESSAGE("allocated C2Buffer, fd: %d capacity: %d, ubwc: %d, stride %u, offset %" G_GSIZE_FORMAT,
                        fd, buffer->capacity, buffer->ubwc_flag, stride, buffer->offset[1]);
                }
            }
        } else {
            LOG_ERROR("Graphic pool is not created");
            ret = C2_NO_INIT;
        }
    } else {
        /* TODO: support linear buffer */
        LOG_ERROR("Unsupported pool type: %u", buffer->pool_type);
        ret = C2_OMITTED;
    }

    return buf;
}

c2_status_t C2ComponentAdapter::queue(BufferDescriptor* buffer)
{
    uint8_t* inputBuffer = buffer->data;
    gint32 fd = buffer->fd;
    C2FrameData::flags_t inputFrameFlag = toC2Flag(buffer->flag);
    uint64_t frame_index = buffer->index;
    uint64_t timestamp = buffer->timestamp;

    LOG_MESSAGE("Component(%p) work queued, Frame index : %lu, Timestamp : %lu",
        this, frame_index, timestamp);

    c2_status_t result = C2_OK;
    std::list<std::unique_ptr<C2Work> > workList;
    std::unique_ptr<C2Work> work = std::make_unique<C2Work>();
    std::shared_ptr<C2Buffer> c2Buffer;

    work->input.flags = inputFrameFlag;
    work->input.ordinal.timestamp = timestamp;
    work->input.ordinal.frameIndex = frame_index;
    bool isEOSFrame = inputFrameFlag & C2FrameData::FLAG_END_OF_STREAM;

    work->input.buffers.clear();

    /* check if input buffer contains fd/va and decide if we need to
     * allocate a new C2 buffer or not */
    if (buffer->c2Buffer) {
        /* Disable delete function for this shared_ptr to avoid double free issue
         * since it is created from raw pointer got from another shared_ptr. That
         * shared_ptr takes responsibility to call delete function.*/
        std::shared_ptr<C2Buffer> c2Buffer(static_cast<C2Buffer*>(buffer->c2Buffer), [](C2Buffer*) {});
        work->input.buffers.emplace_back(c2Buffer);
    } else if (fd > 0) {
        if (buffer->pool_type == BUFFER_POOL_BASIC_LINEAR) {
            /* If the buffer fd is positive, we assume it is a valid external
             * dma buffer, then will try to import the external buffer by fd */
            std::shared_ptr<C2Buffer> clientBuf = nullptr;
            result = importExternalBuf(clientBuf, fd, buffer->size);
            if (result == C2_OK) {
                work->input.buffers.emplace_back(clientBuf);
            } else {
                LOG_ERROR("Failed(%d) to import buffer", result);
            }
        } else if (buffer->pool_type == BUFFER_POOL_BASIC_GRAPHIC) {
            std::map<uint64_t, std::shared_ptr<C2GraphicBlock> >::iterator it;
            std::shared_ptr<C2Buffer> buf = nullptr;
            std::shared_ptr<C2GraphicBlock> graphicBlock = nullptr;

            /* Find the buffer with fd */
            it = mInPendingBuffer.find(fd);
            if (it != mInPendingBuffer.end()) {
                graphicBlock = it->second;
                if (graphicBlock) {
                    buf = createGraphicBuffer(graphicBlock);
                    work->input.buffers.emplace_back(buf);
                } else {
                    LOG_ERROR("invalid graphic block");
                    result = C2_NO_MEMORY;
                }
            } else {
                /* If the buffer is not found, we assume it is a valid external buffer.
                 * When using external buffer, first attach the fd to C2AllocatorGBM,
                 * then when calling alloc(), it will try to import the external
                 * buffer by fd instead of allocating a new one. */
                if (!isUseExternalBuffer(BUFFER_POOL_BASIC_GRAPHIC)) {
                    setUseExternalBuffer(BUFFER_POOL_BASIC_GRAPHIC, TRUE);
                    LOG_MESSAGE("Set to use external buffer for C2AllocatorGBM");
                }
                result = attachExternalFd(BUFFER_POOL_BASIC_GRAPHIC, fd);
                if (result == C2_OK) {
                    buf = alloc(buffer);
                    if (buf) {
                        work->input.buffers.emplace_back(buf);
                        LOG_MESSAGE("Successfully import and queue the external "
                                    "buffer, fd=%d",
                            fd);
                    } else {
                        LOG_ERROR("Failed to import external fd: %d", fd);
                        result = C2_CORRUPTED;
                    }
                } else {
                    LOG_ERROR("Failed(%d) to attach external fd: %d", result, fd);
                }
            }
        } else {
            LOG_ERROR("Invalid buffer pool type %d", buffer->pool_type);
        }
    } else if (inputBuffer) {
        std::shared_ptr<C2Buffer> clientBuf;

        result = prepareC2Buffer(&clientBuf, buffer);
        if (result == C2_OK) {
            work->input.buffers.emplace_back(clientBuf);
        } else {
            LOG_ERROR("Failed(%d) to allocate buffer", result);
            result = C2_NO_MEMORY;
        }
    } else if (isEOSFrame) {
        LOG_MESSAGE("queue EOS frame");
    } else {
        LOG_ERROR("invalid buffer descriptor");
        result = C2_BAD_VALUE;
    }

    if (result == C2_OK) {
        registerTrackBuffer(work->input);

        work->worklets.clear();
        work->worklets.emplace_back(new C2Worklet);
        workList.push_back(std::move(work));

        if (!isEOSFrame) {
            waitForProgressOrStateChange(MAX_PENDING_WORK, 0);
        } else {
            LOG_MESSAGE("queue empty C2 work with EOS");
        }

        result = mComp->queue_nb(&workList);
        if (result != C2_OK) {
            LOG_ERROR("Failed to queue work");
        }
    }

    return result;
}

c2_status_t C2ComponentAdapter::flush(C2Component::flush_mode_t mode)
{
    c2_status_t result = C2_OK;
    std::list<std::unique_ptr<C2Work> > flushedWork;

    result = mComp->flush_sm(mode, &flushedWork);
    if (result == C2_OK) {
        LOG_MESSAGE("Component(%p) flushed work num:%zu", this, flushedWork.size());
        unregisterTrackBuffer(flushedWork);
    } else {
        LOG_ERROR("Failed to flush work");
    }

    return result;
}

c2_status_t C2ComponentAdapter::drain(C2Component::drain_mode_t mode)
{

    LOG_MESSAGE("Component(%p) drain", this);

    c2_status_t result = C2_OK;
    UNUSED(mode);

    return result;
}

c2_status_t C2ComponentAdapter::start()
{

    LOG_MESSAGE("Component(%p) start", this);

    auto ret = mComp->start();
#ifdef USE_AGL_C2SERVICE
    if(!mIC2AllocatorGBM && intf()) {
        C2String name = intf()->getName();
        auto isDecoder = name.find("decoder") != std::string::npos;
        if (isDecoder) {
            LOG_MESSAGE("Component(%p) try to get GBM Allocator", this);
            mIC2AllocatorGBM = aglqc2::QC2Client::GBMAllocator::get(mComp);
            auto acquireFunc = std::bind(&C2ComponentAdapter::acquireExtBuf, this,
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
            auto releaseFunc = std::bind(&C2ComponentAdapter::releaseExtBuf, this, std::placeholders::_1);
            if (mIC2AllocatorGBM) {
                LOG_MESSAGE("Component(%p) try to set GBM callbacks", this);
                mIC2AllocatorGBM->setAcquireExtBufCb(acquireFunc);
                mIC2AllocatorGBM->setReleaseExtBufCb(releaseFunc);
                mIC2AllocatorGBM->passReleaseExtBufCb((uintptr_t)s_releaseExtBuf, (uintptr_t)this);
            } else {
                LOG_MESSAGE("Component(%p) mIC2AllocatorGBM is null", this);
            }
        }
    }
#endif
    return ret;
}

c2_status_t C2ComponentAdapter::stop()
{

    LOG_MESSAGE("Component(%p) stop", this);

    c2_status_t result = mComp->stop();

    unregisterTrackBufferAll();

    return result;
}

c2_status_t C2ComponentAdapter::reset()
{

    LOG_MESSAGE("Component(%p) reset", this);

    c2_status_t result = mComp->reset();

    unregisterTrackBufferAll();

    return result;
}

c2_status_t C2ComponentAdapter::release()
{

    LOG_MESSAGE("Component(%p) release", this);

    c2_status_t result = mComp->release();

    unregisterTrackBufferAll();

    return result;
}

C2ComponentInterfaceAdapter* C2ComponentAdapter::intf()
{
    if (mIntf) {
        LOG_MESSAGE("Component(%p) interface already created %p", this, mIntf.get());
    } else if (mComp) {
        std::shared_ptr<C2ComponentInterface> compIntf = nullptr;

        compIntf = mComp->intf();
        mIntf = std::shared_ptr<C2ComponentInterfaceAdapter>(new C2ComponentInterfaceAdapter(compIntf));
        LOG_MESSAGE("Component(%p) interface created %p", this, mIntf.get());
    }

    return mIntf ? mIntf.get() : NULL;
}

c2_status_t C2ComponentAdapter::createBlockpool(C2BlockPool::local_id_t poolType)
{

    LOG_MESSAGE("Component(%p) block pool (%lu) allocated", this, poolType);

    std::shared_ptr<C2BlockPool> pool;
    std::shared_ptr<C2Allocator> allocator;
    c2_status_t ret = C2_OK;

    if (poolType == C2BlockPool::BASIC_LINEAR) {
        ret = android::CreateCodec2BlockPool(C2AllocatorStore::DEFAULT_LINEAR, mComp, &mLinearPool);
        if (ret != C2_OK || !mLinearPool) {
            return ret;
        }
        uint64_t local_id = mLinearPool->getLocalId();
        android::GetCodec2BlockPoolWithAllocator(local_id, mComp, &pool, &allocator);
        if (!allocator) {
            LOG_ERROR("Failed to get allocator");
            ret = C2_NOT_FOUND;
        } else {
            mC2LinearAllocator = allocator;
        }
    } else if (poolType == C2BlockPool::BASIC_GRAPHIC) {
        ret = android::CreateCodec2BlockPool(C2AllocatorStore::DEFAULT_GRAPHIC, mComp, &mGraphicPool);
        if (ret != C2_OK || !mGraphicPool) {
            return ret;
        }
        uint64_t local_id = mGraphicPool->getLocalId();
        android::GetCodec2BlockPoolWithAllocator(local_id, mComp, &pool, &allocator);
        if (!allocator) {
            LOG_ERROR("Failed to get allocator");
            ret = C2_NOT_FOUND;
        } else if (intf()) {
            C2String name = intf()->getName();
            auto isDecoder = name.find("decoder") != std::string::npos;
            if (!(isDecoder && mUseAglC2Service)) {
                mC2AllocatorGBM = std::dynamic_pointer_cast<android::C2AllocatorGBM>(allocator);
                auto acquireFunc = std::bind(&C2ComponentAdapter::acquireExtBuf, this,
                    std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
                auto releaseFunc = std::bind(&C2ComponentAdapter::releaseExtBuf, this, std::placeholders::_1);
                if (mC2AllocatorGBM) {
                    mC2AllocatorGBM->setAcquireExtBufCb(acquireFunc);
                    mC2AllocatorGBM->setReleaseExtBufCb(releaseFunc);
                }
            }
        }
    }

    if (ret != C2_OK) {
        LOG_ERROR("Failed (%d) to create block pool (%lu)", ret, poolType);
    }

    return ret;
}

c2_status_t C2ComponentAdapter::configBlockPool(C2BlockPool::local_id_t poolType)
{
    C2BlockPool::local_id_t local_id;
    c2_status_t ret = C2_OK;

    LOG_MESSAGE("Component(%p) config block pool (%lu)", this, poolType);

    local_id = (poolType == C2BlockPool::BASIC_GRAPHIC) ? mGraphicPool->getLocalId() : mLinearPool->getLocalId();
    LOG_MESSAGE("Get pool local id:%lu", local_id);
    std::vector<C2Param*> params;
    std::unique_ptr<C2PortBlockPoolsTuning::output> pool = C2PortBlockPoolsTuning::output::AllocUnique({ local_id });
    params.push_back(pool.get());
    ret = mIntf->config(params, C2_DONT_BLOCK);
    if (ret != C2_OK) {
        LOG_ERROR("Failed (%d) to config block pool (%lu)", ret, poolType);
    }

    return ret;
}

uint32_t C2ComponentAdapter::getInterlaceMode(std::vector<std::unique_ptr<C2Param> >& configUpdate, bool& deinterlaced)
{
    uint32_t interlace = INTERLACE_MODE_PROGRESSIVE;
    uint32_t is_deinterlaced = 0;
    android::ReflectedParamUpdater::Dict paramsMap;
    android::ReflectedParamUpdater::Value paramVal;
    C2Value c2Value;

    paramsMap = mIntf->getParams(configUpdate);
    if (paramsMap.find("vendor.qti-ext-dec-info-interlace.format") != paramsMap.end()) {
        paramVal = paramsMap["vendor.qti-ext-dec-info-interlace.format"];
        if (paramVal.find(&c2Value)) {
            if (c2Value.get(&interlace)) {
                LOG_DEBUG("interlace type:%u", interlace);
            }
        }
    }
    if (paramsMap.find("vendor.qti-ext-dec-info-interlace.deinterlaced") != paramsMap.end()) {
        paramVal = paramsMap["vendor.qti-ext-dec-info-interlace.deinterlaced"];
        if (paramVal.find(&c2Value)) {
            if (c2Value.get(&is_deinterlaced)) {
                deinterlaced = (is_deinterlaced != 0);
                LOG_DEBUG("deinterlace is %s", deinterlaced ? "enabled" : "disabled");
            }
        }
    }

    return interlace;
}

uint32_t C2ComponentAdapter::getAvgFrameQP(std::vector<std::unique_ptr<C2Param> >& configUpdate)
{
    uint32_t frameQP = 0;

#if GST_REPORT_FRAME_QP_OPTION == 0
    android::ReflectedParamUpdater::Dict paramsMap;
    android::ReflectedParamUpdater::Value paramVal;
    C2Value c2Value;

    paramsMap = mIntf->getParams(configUpdate);
    if (paramsMap.find("vendor.qti-ext-enc-info-coded_avgqp.frameQP") != paramsMap.end()) {
        paramVal = paramsMap["vendor.qti-ext-enc-info-coded_avgqp.frameQP"];
        if (paramVal.find(&c2Value)) {
            if (c2Value.get(&frameQP)) {
                LOG_DEBUG("get average frame QP: %u", frameQP);
            }
        }
    }
#elif GST_REPORT_FRAME_QP_OPTION == 1
    for (auto& param : configUpdate) {
        if (param->coreIndex().typeIndex() == kParamIndexAverageBlockQuantization) {
            frameQP = ((C2AndroidStreamAverageBlockQuantizationInfo::output*)param.get())->value;
            LOG_DEBUG("get average frame QP: %u", frameQP);
        }
    }
#endif

    return frameQP;
}

void C2ComponentAdapter::printHDRStaticInfo(const C2StreamHdrStaticInfo::output& hsi)
{
    LOG_MESSAGE("SEI(float style) R(%0.5f, %0.5f) G(%0.5f, %0.5f) B(%0.5f, %0.5f) WP(%0.5f, %0.5f) L(max %f, min %f)",
        hsi.mastering.red.x,
        hsi.mastering.red.y,
        hsi.mastering.green.x,
        hsi.mastering.green.y,
        hsi.mastering.blue.x,
        hsi.mastering.blue.y,
        hsi.mastering.white.x,
        hsi.mastering.white.y,
        hsi.mastering.maxLuminance,
        hsi.mastering.minLuminance);

    LOG_MESSAGE("SEI(float style) CLL %f, %f", hsi.maxCll, hsi.maxFall);
}

void C2ComponentAdapter::paramHelper(const std::shared_ptr<C2Buffer>& buffer, uint64_t index)
{
    if (!buffer) {
        return;
    }

    // get HDR static info from first buffer
    if (index == 0 && buffer->hasInfo(C2StreamHdrStaticInfo::output::PARAM_TYPE)) {
        std::shared_ptr<const C2Info> info = buffer->getInfo(C2StreamHdrStaticInfo::output::PARAM_TYPE);
        auto hdrStaticInfo = (C2StreamHdrStaticInfo::output*)(info.get());
        if (hdrStaticInfo) {
            printHDRStaticInfo(*hdrStaticInfo);
        }
    }
}

void C2ComponentAdapter::handleWorkDone(
    std::weak_ptr<C2Component> component,
    std::list<std::unique_ptr<C2Work> > workItems)
{

    LOG_MESSAGE("Component(%p) work done", this);

    while (!workItems.empty()) {
        std::unique_ptr<C2Work> work = std::move(workItems.front());

        workItems.pop_front();
        if (!work) {
            continue;
        }

        if (work->worklets.empty()) {
            LOG_DEBUG("Component(%p) worklet empty", this);
            continue;
        }

        if (work->result != C2_OK) {
            LOG_DEBUG("No output for component(%p), ret:%d", this, work->result);
            continue;
        }

        const std::unique_ptr<C2Worklet>& worklet = work->worklets.front();
        std::shared_ptr<C2Buffer> buffer = nullptr;
        uint64_t bufferIdx = 0;
        C2FrameData::flags_t outputFrameFlag = worklet->output.flags;
        uint64_t timestamp = worklet->output.ordinal.timestamp.peeku();
        bool deinterlaced = false;
        uint32_t interlaceMode = getInterlaceMode(worklet->output.configUpdate, deinterlaced);
        InterlaceInfo interlaceInfo = { interlaceMode, deinterlaced };
        uint32_t frameQp = getAvgFrameQP(worklet->output.configUpdate);

        while (!worklet->output.configUpdate.empty()) {
            std::unique_ptr<C2Param> param;
            worklet->output.configUpdate.back().swap(param);
            worklet->output.configUpdate.pop_back();
            switch (param->coreIndex().coreIndex()) {
            case C2PortActualDelayTuning::CORE_INDEX: {
                if (param->forOutput() && intf()) {
                    C2PortActualDelayTuning::output outputDelay;
                    C2String name = intf()->getName();
                    bool isDecoder = name.find("decoder") != std::string::npos;
                    LOG_MESSAGE("Component intf name:%s, decoder:%u", name.c_str(), isDecoder);
                    if (isDecoder && outputDelay.updateFrom(*param)) {
                        LOG_MESSAGE("onWorkDone: updating output delay:%u.", outputDelay.value);
                        if (isUseExternalBuffer(BUFFER_POOL_BASIC_GRAPHIC)) {
                            /* Update the max acquirable buffer count for external buffer pool */
                            uint32_t maxBufCnt = outputDelay.value + MAX_EXT_BUF_CNT_EXTENSION;
                            if (interlaceMode != INTERLACE_MODE_PROGRESSIVE) {
                                maxBufCnt += MAX_EXT_BUF_CNT_EXTENSION;
                            }
                            mCallback->onUpdateMaxBufCount(maxBufCnt);
                        } else {
                            setMaxAllocationCount(outputDelay.value, BUFFER_POOL_BASIC_GRAPHIC);
                        }
                    }
                }
            } break;
            }
        }

        // Expected only one output stream.
        if (worklet->output.buffers.size() == 1u && !(outputFrameFlag & C2FrameData::FLAG_DROP_FRAME)) {
            buffer = worklet->output.buffers[0];
            bufferIdx = worklet->output.ordinal.frameIndex.peeku();
            if (!buffer) {
                LOG_ERROR("Invalid buffer");
            } else {

                LOG_MESSAGE("Component(%p) output buffer available, Frame index : %lu, Timestamp : %lu, Flag : 0x%x",
                    this, bufferIdx, worklet->output.ordinal.timestamp.peeku(), outputFrameFlag);

                paramHelper(buffer, bufferIdx);

                // Only hold the C2 buffer in case below:
                // 1. all encoder use cases
                // 2. internal buffer pool mode for decoder output
                if (buffer->data().type() == C2BufferData::LINEAR || !isUseExternalBuffer(BUFFER_POOL_BASIC_GRAPHIC)) {
                    std::unique_lock<std::mutex> lck(mLockOut);
                    mOutPendingBuffer[bufferIdx] = buffer;
                }

                if (mCallback) {
                    mCallback->onOutputBufferAvailable(buffer, bufferIdx, timestamp, interlaceInfo, frameQp, outputFrameFlag);
                } else {
                    LOG_ERROR("mCallback is null, not expected!");
                }
            }
        } else {
            if (outputFrameFlag & C2FrameData::FLAG_END_OF_STREAM) {
                LOG_MESSAGE("Component(%p) reached EOS on output", this);
                if (mCallback) {
                    mCallback->onOutputBufferAvailable(NULL, bufferIdx, timestamp, interlaceInfo, frameQp, outputFrameFlag);
                } else {
                    LOG_ERROR("mCallback is null when EOS, not expected!");
                }
            } else if (outputFrameFlag & C2FrameData::FLAG_INCOMPLETE) {
                LOG_MESSAGE("Component(%p) work incomplete, means an input frame results in multiple "
                            "output frames, or codec config update event",
                    this);
                continue;
            } else if (outputFrameFlag & C2FrameData::FLAG_DROP_FRAME
                || outputFrameFlag & C2FrameData::FLAG_DISCARD_FRAME) {
                /* When an input is dropped, output Buffer is not produced.
                 * To ensure the work gets evicted with an empty output so as
                 * to push it downstream against frame neither being finished
                 * nor upstream buffer being finalized.
                 * Most likely in superframe case.
                 */
                bufferIdx = work->input.ordinal.frameIndex.peeku();
                timestamp = work->input.ordinal.timestamp.peeku();

                LOG_MESSAGE("Component(%p) work drop frame, may mean a superframe. Input Frame index: %lu, pts %lu",
                    this, bufferIdx, timestamp);
                if (mCallback) {
                    mCallback->onOutputBufferAvailable(NULL, bufferIdx, timestamp, interlaceInfo, frameQp, outputFrameFlag);
                } else {
                    LOG_ERROR("mCallback is null during drop frame, not expected!");
                }
            } else {
                LOG_MESSAGE("Incorrect number of output buffers: %lu", worklet->output.buffers.size());
            }

            break;
        }
    }
}

void C2ComponentAdapter::handleTripped(
    std::weak_ptr<C2Component> component,
    std::vector<std::shared_ptr<C2SettingResult> > settingResult)
{

    LOG_MESSAGE("Component(%p) work tripped", this);

    UNUSED(component);

    for (auto& f : settingResult) {
        mCallback->onTripped(static_cast<uint32_t>(f->failure));
    }
}

void C2ComponentAdapter::handleError(std::weak_ptr<C2Component> component, uint32_t errorCode)
{
    LOG_MESSAGE("Component(%p) posts an error", this);

    UNUSED(component);
    mCallback->onError(errorCode);
}

c2_status_t C2ComponentAdapter::setCompStore(std::weak_ptr<C2ComponentStore> store)
{

    LOG_MESSAGE("Component store for component(%p) set", this);

    c2_status_t result = C2_BAD_VALUE;
    if (!store.expired()) {
        mStore = store;
        result = C2_OK;
    }
    return result;
}

c2_status_t C2ComponentAdapter::freeOutputBuffer(uint64_t bufferIdx)
{

    LOG_MESSAGE("Freeing component(%p) output buffer(%lu)", this, bufferIdx);

    c2_status_t result = C2_BAD_VALUE;
    std::map<uint64_t, std::shared_ptr<C2Buffer> >::iterator it;

    {
        std::unique_lock<std::mutex> lck(mLockOut);
        it = mOutPendingBuffer.find(bufferIdx);
        if (it != mOutPendingBuffer.end()) {
            mOutPendingBuffer.erase(it);
            result = C2_OK;

        } else {
            LOG_ERROR("Buffer index(%lu) not found", bufferIdx);
        }
    }

    return result;
}

c2_status_t C2ComponentAdapter::attachExternalFd(BUFFER_POOL_TYPE type, int fd)
{
    c2_status_t result = C2_NO_INIT;
    LOG_MESSAGE("Component(%p) attach external fd: %d for pool type %d", this, fd, type);

    if (type == BUFFER_POOL_BASIC_GRAPHIC) {
#ifdef USE_AGL_C2SERVICE
        if (mIC2AllocatorGBM) {
            result = mIC2AllocatorGBM->attachExternalFd(fd);
        } else
#endif
        if (mC2AllocatorGBM) {
            result = mC2AllocatorGBM->attachExternalFd(fd);
        } else {
            LOG_ERROR("mC2AllocatorGBM is NULL");
            result = C2_BAD_VALUE;
        }
    } else {
        LOG_ERROR("Invalid buffer pool type %d", type);
    }

    if (C2_OK != result) {
        LOG_ERROR("Failed to attach external fd with result=%d", result);
    }

    return result;
}

c2_status_t C2ComponentAdapter::setUseExternalBuffer(BUFFER_POOL_TYPE type, bool useExternal)
{
    c2_status_t result = C2_NO_INIT;
    LOG_MESSAGE("Component(%p) set to use external buffer: %s for pool type %d",
        this, useExternal ? "TRUE" : "FALSE", type);

    if (type == BUFFER_POOL_BASIC_GRAPHIC) {
#ifdef USE_AGL_C2SERVICE
        if (mIC2AllocatorGBM) {
            result = mIC2AllocatorGBM->setUseExternalBuffer(useExternal);
        } else
#endif
        if (mC2AllocatorGBM) {
            result = mC2AllocatorGBM->setUseExternalBuffer(useExternal);
        } else {
            LOG_ERROR("mC2AllocatorGBM is NULL");
            result = C2_BAD_VALUE;
        }
    } else {
        LOG_ERROR("Invalid buffer pool type %d", type);
    }

    return result;
}

bool C2ComponentAdapter::isUseExternalBuffer(BUFFER_POOL_TYPE type)
{
    bool ret = false;

    if (type == BUFFER_POOL_BASIC_GRAPHIC) {
#ifdef USE_AGL_C2SERVICE
        if (mIC2AllocatorGBM) {
            ret = mIC2AllocatorGBM->isUseExternalBuffer();
        } else
#endif
        if (mC2AllocatorGBM) {
            ret = mC2AllocatorGBM->isUseExternalBuffer();
        } else {
            /* mC2AllocatorGBM is not created in Codec2 service mode. */
#ifndef USE_AGL_C2SERVICE
            LOG_ERROR("mC2AllocatorGBM is NULL");
#endif
        }
    } else {
        LOG_ERROR("Invalid buffer pool type %d", type);
    }

    return ret;
}

c2_status_t C2ComponentAdapter::importExternalBuf(std::shared_ptr<C2Buffer>& c2Buf, int fd, uint32_t size)
{
    c2_status_t result = C2_OK;
    std::shared_ptr<C2LinearBlock> linearBlock = nullptr;
    std::shared_ptr<C2LinearAllocation> allocation = nullptr;
    bool need_release = false;
    C2Handle* linearHandle = nullptr;

    uint32_t alignSize = GST_ROUND_UP_N(size, 4096);
    /* dup the external buffer fd to decouple decoder and upstream element, and the
     * input external buffer fd should be closed by upstream element after use, dup_fd
     * will be closed in the destructor of C2AllocationIon::Impl after passing to it */
    int dup_fd = dup(fd);

#ifdef USE_DMAHEAP
    linearHandle = new android::C2DmaHandle(dup_fd, alignSize);
#else
    linearHandle = new android::C2HandleIon(dup_fd, alignSize);
#endif

    if (!mC2LinearAllocator || !linearHandle) {
        LOG_ERROR("Invalid mC2LinearAllocator or linearHandle");
        need_release = true;
        qcodec2_close_fd(dup_fd);
        result = C2_NO_MEMORY;
        goto do_exit;
    }
    /* linearHandle will be released in priorLinearAllocation if return C2_OK */
    result = mC2LinearAllocator->priorLinearAllocation(linearHandle, &allocation);
    if (result != C2_OK) {
        LOG_ERROR("Failed(%d) to call priorLinearAllocation", result);
        need_release = true;
        goto do_exit;
    }
    linearBlock = _C2BlockFactory::CreateLinearBlock(allocation);
    if (!linearBlock) {
        LOG_ERROR("Failed to CreateLinearBlock");
        result = C2_NO_MEMORY;
        goto do_exit;
    }
    linearBlock->mSize = size;
    c2Buf = createLinearBuffer(linearBlock);
    if (!c2Buf) {
        LOG_ERROR("Failed to createLinearBuffer");
        result = C2_NO_MEMORY;
    }

do_exit:
    if (need_release && linearHandle) {
        /* need to delete linearHandle here if priorLinearAllocation failed */
        delete linearHandle;
    }

    return result;
}

void C2ComponentAdapter::acquireExtBuf(uint32_t width, uint32_t height, bool isC2D)
{
    if (mCallback) {
        mCallback->onAcquireExtBuffer(width, height, isC2D);
    }
}

void C2ComponentAdapter::releaseExtBuf(int32_t extFd)
{
    if (mCallback) {
        mCallback->onReleaseExtBuffer(extFd);
    }
}

void C2ComponentAdapter::releaseInputBuf(uint64_t index)
{
    if (mCallback) {
        mCallback->onReleaseInputBuffer(index);
    }
}

void C2ComponentAdapter::cancelPendingWork()
{
    LOG_MESSAGE("Component(%p) cancelPendingWork.", this);

    {
        std::unique_lock<std::mutex> ul(mLock);
        LOG_MESSAGE("%s mNumPendingWorks %d", __func__, mNumPendingWorks);
        mPendingSignaled = true;
    }

    mPendingWorkCond.notify_all();
}

C2ComponentListenerAdapter::C2ComponentListenerAdapter(C2ComponentAdapter* comp)
{

    mComp = comp;
}

C2ComponentListenerAdapter::~C2ComponentListenerAdapter()
{

    mComp = nullptr;
}

void C2ComponentListenerAdapter::onWorkDone_nb(
    std::weak_ptr<C2Component> component,
    std::list<std::unique_ptr<C2Work> > workItems)
{

    LOG_MESSAGE("Component listener (%p) onWorkDone_nb", this);

    if (mComp) {
        mComp->handleWorkDone(component, std::move(workItems));
    }
}

void C2ComponentListenerAdapter::onTripped_nb(
    std::weak_ptr<C2Component> component,
    std::vector<std::shared_ptr<C2SettingResult> > settingResult)
{

    LOG_MESSAGE("Component listener (%p) onTripped_nb", this);

    if (mComp) {
        mComp->handleTripped(component, settingResult);
    }
}

void C2ComponentListenerAdapter::onError_nb(std::weak_ptr<C2Component> component, uint32_t errorCode)
{

    LOG_MESSAGE("Component listener (%p) onError_nb", this);

    if (mComp) {
        mComp->handleError(component, errorCode);
    }
}

} // namespace QTI
