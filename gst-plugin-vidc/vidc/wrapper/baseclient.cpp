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

#include "baseclient.h"
#include <cstring>

BaseClient::BaseClient(const char* namePtr)
    : mNamePtr(namePtr)
{
    MM_DBG_MSG("BaseClient::BaseClient %s", mNamePtr);
    resetPort(VIDC_BUFFER_INPUT, "Input");
    resetPort(VIDC_BUFFER_OUTPUT, "Output");
    resetPort(VIDC_BUFFER_METADATA_INPUT, "MetaInput");
    resetPort(VIDC_BUFFER_METADATA_OUTPUT, "MetaOutput");
    mState = VIDC_STATE_UNLOADED;
    mShutdown = false;
    mInitialized = false;
    mEndOfStream = false;
    mOutputStarted = false;
    mInputStarted = false;
    mWaitLastFlagToReconfig = false;
    mDrainSent = false;
    mDone = false;
    char buffer[128];

    ioctl_callback_t vidcCallback = {};
    vidcCallback.handler = eventCallback;
    vidcCallback.data = (void*)this;

    MM_DBG_MSG("BaseClient::BaseClient device_open");
    mHandle = device_open(VIDC_DRIVER, &vidcCallback);
    if (mHandle == NULL) {
        mState = VIDC_STATE_UNLOADED;
        MM_ERROR_MSG("BaseClient::BaseClient-%s Error opening vidc driver", mNamePtr);
    }

    memset(buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer) - 1, "%sThread", mNamePtr);
    ThreadClass::ThreadEntryType threadFcn = [this]() {
        return threadMain();
    };
    mThread.start(buffer, threadFcn, 0x8000); // Start the cmd processing thread

    memset(buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer) - 1, "%sBufferThreadEBD", mNamePtr);
    ThreadClass::ThreadEntryType threadBufferEBDFcn = [this]() {
        return threadBufferEBD();
    };
    mBufferThreadEBD.start(buffer, threadBufferEBDFcn, 0x8000); // Start the EBD processing thread

    memset(buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer) - 1, "%sBufferThreadFBD", mNamePtr);
    ThreadClass::ThreadEntryType threadBufferFBDFcn = [this]() {
        return threadBufferFBD();
    };
    mBufferThreadFBD.start(buffer, threadBufferFBDFcn, 0x8000); // Start the FBD processing thread
}

BaseClient::~BaseClient()
{
    std::unique_lock<std::mutex> uLockEBD(mMutexBufferEBD, std::defer_lock);
    std::unique_lock<std::mutex> uLockFBD(mMutexBufferFBD, std::defer_lock);
    std::unique_lock<std::mutex> uLockState(mStateMutex, std::defer_lock);

    MM_DBG_MSG("BaseClient::~BaseClient %s", mNamePtr);
    uLockState.lock();
    mState = VIDC_STATE_UNLOADED;
    uLockState.unlock();
    mQueueCommand.clear();
    mQueueCompleted.clear();
    mPort[VIDC_BUFFER_INPUT].buffers.clear();
    mPort[VIDC_BUFFER_OUTPUT].buffers.clear();

    if (mHandle != NULL) {
        int rc;

        rc = device_close(mHandle);
        if (rc != VIDC_ERR_NONE) {
            MM_ERROR_MSG("BaseClient::~BaseClient-%s Error 0x%X closing vidc driver",
                mNamePtr, rc);
        }
        mHandle = NULL;
        MM_DBG_MSG("BaseClient::~BaseClient %s device_closed", mNamePtr);
    }

    MM_DBG_MSG("BaseClient::~BaseClient %s join threads", mNamePtr);

    uLockFBD.lock();
    mDone = true;
    mCondVarFBD.notify_one();
    uLockFBD.unlock();
    mBufferThreadFBD.wait();
    MM_DBG_MSG("BaseClient::~BaseClient %s BufferThreadFBD quit", mNamePtr);

    uLockEBD.lock();
    // mDone = true;
    mCondVarEBD.notify_one();
    uLockEBD.unlock();
    mBufferThreadEBD.wait();
    MM_DBG_MSG("BaseClient::~BaseClient %s BufferThreadEBD quit", mNamePtr);

    mQueueCommand.push(COMMAND_EXIT); // Tell processing thread to exit
    mThread.wait();
    MM_DBG_MSG("BaseClient::~BaseClient %s ThreadMain quit", mNamePtr);

    mShutdown = true;
}

int BaseClient::event // Common event handling for all components
    (
        vidc_drv_msg_info_type& info,
        uint32 length)
{
    MM_DBG_MSG("BaseClient::event-%s 0x%x", mNamePtr, info.event_type);
    std::unique_lock<std::mutex> uLockState(mStateMutex, std::defer_lock);
    int ret = 0;

    switch (info.event_type) {
    case VIDC_EVT_RESP_START_INPUT_DONE: {
        MM_DBG_MSG("BaseClient::event-%s state executing start input done", mNamePtr);
        mInputStarted = true;
        uLockState.lock();
        mState = VIDC_STATE_EXECUTING;
        uLockState.unlock();
        mQueueCompleted.push(COMMAND_INPUT_START);
    } break;

    case VIDC_EVT_RESP_START_OUTPUT_DONE: {
        MM_DBG_MSG("BaseClient::event-%s state executing start output done", mNamePtr);
        mOutputStarted = true;
        uLockState.lock();
        mState = VIDC_STATE_EXECUTING;
        uLockState.unlock();
        mQueueCompleted.push(COMMAND_OUTPUT_START);
    } break;

        // case VIDC_EVT_RESP_LOAD_RESOURCES:
        //     {
        //         MM_DBG_MSG("BaseClient::event-%s state executing load resources done", mNamePtr);
        //         mState = VIDC_STATE_IDLE;
        //         mQueueCompleted.push(COMMAND_ACQUIRE);
        //     }
        //     break;

    case VIDC_EVT_RESP_STOP_INPUT_DONE: {
        MM_DBG_MSG("BaseClient::event-%s state idle stop input done", mNamePtr);
        mInputStarted = false;
        if (!mOutputStarted) {
            uLockState.lock();
            mState = VIDC_STATE_IDLE;
            uLockState.unlock();
        }
        mQueueCompleted.push(COMMAND_INPUT_STOP);
    } break;

    case VIDC_EVT_RESP_STOP_OUTPUT_DONE: {
        MM_DBG_MSG("BaseClient::event-%s state idle stop output done", mNamePtr);
        mOutputStarted = false;
        if (!mInputStarted) {
            uLockState.lock();
            mState = VIDC_STATE_IDLE;
            uLockState.unlock();
        }
        mQueueCompleted.push(COMMAND_OUTPUT_STOP);
    } break;

    case VIDC_EVT_RESP_INPUT_DONE: {
        std::lock_guard<std::mutex> lkg(mMutexBufferEBD);
        MM_DBG_MSG("BaseClient::event-%s state EBD", mNamePtr);
        mQueueEBD.push(info.payload.frame_data);
        mCondVarEBD.notify_one();
    } break;

    case VIDC_EVT_RESP_OUTPUT_DONE: {
        std::lock_guard<std::mutex> lkg(mMutexBufferFBD);
        MM_DBG_MSG("BaseClient::event-%s state FBD", mNamePtr);
        mPort[info.payload.frame_data.buf_type]
            .bufferInUse[info.payload.frame_data.frame_handle] = false;
        mQueueFBD.push(info.payload.frame_data);
        mCondVarFBD.notify_one();
    } break;

    case VIDC_EVT_RESP_PAUSE: {
        MM_DBG_MSG("BaseClient::event-%s state PAUSE", mNamePtr);
        uLockState.lock();
        mState = VIDC_STATE_PAUSE;
        uLockState.unlock();
        mQueueCompleted.push(COMMAND_PAUSE);
    } break;

    case VIDC_EVT_RESP_RESUME: {
        MM_DBG_MSG("BaseClient::event-%s state RESUME", mNamePtr);
        mQueueCompleted.push(COMMAND_RESUME);
    } break;

    case VIDC_EVT_RESP_DRAIN:
        MM_DBG_MSG("BaseClient::event-%s drain done", mNamePtr);
        if (mDrainSent) {
            MM_DBG_MSG("BaseClient::push COMMAND_DRAIN to completed queue");
            mQueueCompleted.push(COMMAND_DRAIN);
        }
        break;

    case VIDC_EVT_OUTPUT_RECONFIG: {
        MM_DBG_MSG("BaseClient::event-%s output reconfig", mNamePtr);
        mQueueCommand.push(COMMAND_OUTPUT_PORT_RECONFIG);
    } break;

    case VIDC_EVT_LAST_FLAG: {
        MM_DBG_MSG("BaseClient::event-%s last flag", mNamePtr);
        mQueueCommand.push(COMMAND_LAST_FLAG);
    } break;

    default: {
        MM_ERROR_MSG("BaseClient::event-%s UNKNOWN type=0x%x",
            mNamePtr, info.event_type);
        ret = -1;
    } break;
    }

    return ret;
}

int BaseClient::eventCallback(
    uint8* msgPtr,
    uint32 length,
    void* clientPtr)
{
    if (clientPtr == NULL || msgPtr == NULL) {
        MM_ERROR_MSG("BaseClient::eventCallback client or msg data is NULL");
        return -1;
    }

    BaseClient* thisPtr = NULL;
    vidc_drv_msg_info_type* pEvent = (vidc_drv_msg_info_type*)msgPtr;

    if (clientPtr) {
        thisPtr = (BaseClient*)clientPtr;
    }

    return thisPtr->event(*pEvent, length);
}

bool BaseClient::outputPortReconfig()
{
    MM_DBG_MSG("BaseClient::outputPortReconfig-%s default implementation", mNamePtr);
    if (mReconfigureCallback) {
        mReconfigureCallback(this);
    }

    return true;
}

// Derived class overrides this if special processing is
// needed prior to invoking client callback
void BaseClient::emptyDone(
    vidc_frame_data_type& frameData)
{
    //   MM_DBG_MSG("BaseClient::emptyDone-%s", mNamePtr);
    if (mShutdown == true) // If shutting down
    {
        // We are shutting down and the buffers are
        // being returned.  Don't notify the client.
        MM_DBG_MSG("BaseClient::emptyDone-%s buffer returned at shutdown", mNamePtr);
        return;
    }
    frameData.data_len = 0;
    if (mEmptyCallback) // If a client callback is registered
    {
        mEmptyCallback(this, frameData); // Invoke the client callback
    }
}

// Derived class overrides this if special processing is
// needed prior to invoking client callback
void BaseClient::fillDone(
    vidc_frame_data_type& frameData)
{
    MM_DBG_MSG("BaseClient::fillDone-%s flags 0x%X len %d timestamp %lld",
        mNamePtr, frameData.flags, frameData.data_len, frameData.timestamp / 1000);
    std::unique_lock<std::mutex> uLockState(mStateMutex, std::defer_lock);
    uLockState.lock();
    if (VIDC_STATE_EXECUTING != mState) {
        uLockState.unlock();
        // We are not executing and the buffers are
        // being returned.  Don't notify the client.
        MM_DBG_MSG("BaseClient::fillDone-%s buffer returned not executing", mNamePtr);
        return;
    }

    uLockState.unlock();
    if (frameData.flags & VIDC_FRAME_FLAG_DATACORRUPT) {
        MM_DBG_MSG("BaseClient::fillDone-%s buffer %d corrupt",
            mNamePtr, frameData.data_len);
    }
    if (frameData.flags & VIDC_FRAME_FLAG_CODECCONFIG) {
        MM_DBG_MSG("BaseClient::fillDone-%s buffer %d with Sequence Header",
            mNamePtr, frameData.data_len);
    }
    if (frameData.flags & VIDC_FRAME_FLAG_EOS) {
        MM_DBG_MSG("BaseClient::fillDone-%s buffer EOS", mNamePtr);
    }
    if (frameData.flags & VIDC_FRAME_FLAG_SYNCFRAME) {
        MM_DBG_MSG("BaseClient::fillDone-%s sync frame", mNamePtr);
    }
    if (frameData.flags & VIDC_FRAME_FLAG_LAST) {
        // LAST_FLAG event handles the drc/drain last flag operation
        MM_DBG_MSG("BaseClient::fillDone-%s buffer LAST", mNamePtr);
    }
    if (frameData.flags & VIDC_FRAME_FLAG_READONLY) {
        if (frameData.data_len > 0) {
            MM_DBG_MSG("BaseClient::fillDone-%s buffer read-only clear flag", mNamePtr);
            frameData.flags &= ~(VIDC_FRAME_FLAG_READONLY);
        } else {
            // handle read-only frame in gst
            MM_DBG_MSG("BaseClient::fillDone-%s buffer read-only", mNamePtr);
        }
    }
    if (frameData.data_len == 0 && // If buffer is empty and
        (frameData.flags & VIDC_FRAME_FLAG_CODECCONFIG) == 0 &&
        (frameData.flags & VIDC_FRAME_FLAG_READONLY) == 0) // config and read-only flag is not set
    {
        // The buffer was empty, recycle it but don't notify client.
        if (!(frameData.flags & VIDC_FRAME_FLAG_EOS)) // Allow to call client callback for EOS case
        {
            fillBuffer(frameData);
            return;
        }
    }
    if (mFilledCallback) // If a client callback is registered
    {
        if (0 == strcmp(mNamePtr, "Decoder") &&
            frameData.frame_decsp.luma_plane.width != 0 &&
            frameData.frame_decsp.luma_plane.height != 0 &&
            mPort[VIDC_BUFFER_OUTPUT].data.scanFormat == 0 &&
            (frameData.frame_decsp.luma_plane.width != (uint32)mPort[VIDC_BUFFER_OUTPUT].data.width ||
            frameData.frame_decsp.luma_plane.height / 2 != (uint32)mPort[VIDC_BUFFER_OUTPUT].data.height))
        {
            mPort[VIDC_BUFFER_OUTPUT].data.width = frameData.frame_decsp.luma_plane.width;
            mPort[VIDC_BUFFER_OUTPUT].data.height = frameData.frame_decsp.luma_plane.height / 2;

            MM_DBG_MSG("BaseClient::fillDone-%s interlaced update resolution width %d, height %d",
                mNamePtr, mPort[VIDC_BUFFER_OUTPUT].data.width,
                mPort[VIDC_BUFFER_OUTPUT].data.height);
        }

        mFilledCallback(this, frameData); // Invoke the client callback
    }
}

void BaseClient::registerCallback(
    BufferCallbackType emptyCallback,
    BufferCallbackType filledCallback,
    ReconfigureCallbackType reconfigureCallback,
    EOSDoneCallbackType eosDoneCallback)
{
    MM_DBG_MSG("BaseClient::registerCallback-%s", mNamePtr);
    mEmptyCallback = emptyCallback;
    mFilledCallback = filledCallback;
    mReconfigureCallback = reconfigureCallback;
    mEosDoneCallback = eosDoneCallback;
}

void BaseClient::resetPort(vidc_buffer_type buffer, const char* namePtr)
{
    MM_DBG_MSG("BaseClient::resetPort-%s port %d", mNamePtr, buffer);
    memset(&mPort[buffer].requirements, 0, sizeof(mPort[VIDC_BUFFER_INPUT].requirements));
    memset(&mPort[buffer].data, 0, sizeof(mPort[VIDC_BUFFER_INPUT].data));
    mPort[buffer].requirements.buf_type = buffer;
    mPort[buffer].data.codec = VIDC_CODEC_UNUSED;
    mPort[buffer].data.pixelFmt = PlaneInfo::COLOR_FORMAT_UNUSED;
    mPort[buffer].data.isValid = false;
    mPort[buffer].data.isAvsync = true;
    mPort[buffer].data.frameRate = 30.0;
    mPort[buffer].namePtr = namePtr;
}

bool BaseClient::setParameter(
    vidc_property_id_type propId,
    void* payloadPtr,
    uint32 payloadBytes)
{
    int rc;
    uint8 buffer[256];
    uint32 bytes;
    vidc_drv_property_type* propertyPtr;

    MM_DBG_MSG("BaseClient::setParameter-%s id 0x%X", mNamePtr, propId);
    memset(buffer, 0, sizeof(buffer));

    // vidc_drv_property_type.payload is not defined correctly so we
    // use a buffer big enough to hold the payload.
    propertyPtr = (vidc_drv_property_type*)buffer;
    bytes = sizeof(vidc_property_hdr_type) + payloadBytes;
    memcpy(propertyPtr->payload, payloadPtr, payloadBytes);
    propertyPtr->prop_hdr.size = payloadBytes;
    propertyPtr->prop_hdr.prop_id = propId;
    rc = device_ioctl(
        mHandle,
        VIDC_IOCTL_SET_PROPERTY,
        (uint8*)propertyPtr,
        bytes,
        NULL,
        0);

    RETURN_BOOL_ON_ERROR(rc == VIDC_ERR_NONE,
        "BaseClient::setParameter-%s Error 0x%X setting id 0x%X", mNamePtr, rc, propId);

    return true;
}

bool BaseClient::getParameter(
    vidc_property_id_type propId,
    void* payloadPtr,
    uint32 payloadBytes)
{
    int32 rc;
    uint8 buffer[256];
    uint32 bytes;
    vidc_drv_property_type* propertyPtr;

    MM_DBG_MSG("BaseClient::getParameter-%s id 0x%X", mNamePtr, propId);
    memset(buffer, 0, sizeof(buffer));

    // vidc_drv_property_type.payload is not defined correctly so we
    // use a buffer big enough to hold the payload.
    propertyPtr = (vidc_drv_property_type*)buffer;
    bytes = sizeof(vidc_property_hdr_type) + payloadBytes;
    memcpy(propertyPtr->payload, payloadPtr, payloadBytes);
    propertyPtr->prop_hdr.size = payloadBytes;
    propertyPtr->prop_hdr.prop_id = propId;
    rc = device_ioctl(
        mHandle,
        VIDC_IOCTL_GET_PROPERTY,
        (uint8*)propertyPtr,
        bytes,
        (uint8*)payloadPtr,
        payloadBytes);
    MM_DBG_MSG("BaseClient::getParameter-%s id 0x%X return rc %d", mNamePtr, propId, rc);
    RETURN_BOOL_ON_ERROR(rc == VIDC_ERR_NONE,
        "BaseClient::getParameter-%s Error 0x%X getting id 0x%X", mNamePtr, rc, propId);

    return true;
}

bool BaseClient::useBuffer(vidc_buffer_type type, int32 handle, uint32 size)
{
    vidc_buffer_info_type buf_info = { VIDC_BUFFER_UNUSED, 0 };

    int32 nMsgSize = sizeof(vidc_buffer_info_type);
    buf_info.buf_type = type;
    buf_info.contiguous = true;
    buf_info.buf_size = size;
    buf_info.buf_handle = handle;

    MM_DBG_MSG("BaseClient::useBuffer-%s type %d, handle %d, size %u",
        mNamePtr, type, handle, size);
    int32 rc = device_ioctl(mHandle,
        VIDC_IOCTL_SET_BUFFER,
        (uint8*)(&buf_info),
        nMsgSize,
        NULL,
        0);

    RETURN_BOOL_ON_ERROR(rc == VIDC_ERR_NONE,
        "useBuffer.VIDC_IOCTL_SET_BUFFER.fail.handle %d rc=0x%x", handle, rc);

    return true;
}

void BaseClient::threadMain()
{
    CommandType command = COMMAND_EXIT;
    bool exit = false;
    bool rc = false;
    MM_DBG_MSG("BaseClient::threadMain-%s", mNamePtr);

    while (exit == false) {
        command = mQueueCommand.pop();
        switch (command) {
        case COMMAND_EXIT:
            exit = true;
            break;
        case COMMAND_OUTPUT_PORT_RECONFIG:
            MM_DBG_MSG("BaseClient::threadMain receive COMMAND_OUTPUT_PORT_RECONFIG");
            if (!mOutputStarted) {
                mWaitLastFlagToReconfig = false;
            } else {
                mWaitLastFlagToReconfig = true;
            }

            outputPortReconfig();
            break;
        case COMMAND_LAST_FLAG:
            MM_DBG_MSG("BaseClient::threadMain receive COMMAND_LAST_FLAG mWaitLastFlagToReconfig=%d",
                mWaitLastFlagToReconfig);
            break;
        default:
            break;
        }
    }

    MM_DBG_MSG("BaseClient::threadMain-%s exit", mNamePtr);
}

void BaseClient::threadBufferEBD()
{
    std::unique_lock<std::mutex> uLock(mMutexBufferEBD, std::defer_lock);

    MM_HIGH_MSG("BaseClient::threadBufferEBD-%s Enter bufThread...", mNamePtr);
    while (true) {
        uLock.lock();

        MM_DBG_MSG("BaseClient::threadBufferEBD-%s wait", mNamePtr);
        mCondVarEBD.wait(uLock, [this] { return (
                                             false == mQueueEBD.isEmpty() || true == mDone); });

        if (mDone) {
            MM_DBG_MSG("BaseClient::threadBufferEBD-%s quit mDone", mNamePtr);
            uLock.unlock();
            break;
        }

        if (false == mQueueEBD.isEmpty()) {
            vidc_frame_data_type buffer = mQueueEBD.pop();
            uLock.unlock();

            MM_DBG_MSG("BaseClient::threadBufferEBD-%s bufThread handle EBD for buffer %p",
                mNamePtr, &buffer);
            emptyDone(buffer);
        } else {
            uLock.unlock();
        }
    }
    MM_HIGH_MSG("BaseClient::threadBufferEBD-%s Exiting bufThreadEBD...", mNamePtr);
}

void BaseClient::threadBufferFBD()
{
    std::unique_lock<std::mutex> uLock(mMutexBufferFBD, std::defer_lock);

    MM_HIGH_MSG("BaseClient::threadBufferFBD-%s Enter bufThreadFBD...", mNamePtr);
    while (true) {
        uLock.lock();

        MM_DBG_MSG("BaseClient::threadBufferFBD-%s wait", mNamePtr);
        mCondVarFBD.wait(uLock, [this] { return (false == mQueueFBD.isEmpty()
                                             || true == mDone); });

        if (mDone) {
            MM_DBG_MSG("BaseClient::threadBufferFBD-%s quit mDone", mNamePtr);
            uLock.unlock();
            break;
        }

        if (false == mQueueFBD.isEmpty()) {
            vidc_frame_data_type buffer = mQueueFBD.pop();
            uLock.unlock();

            MM_DBG_MSG("BaseClient::threadBufferFBD-%s bufThread handle FBD for buffer %p",
                mNamePtr, &buffer);
            fillDone(buffer);
        } else {
            uLock.unlock();
        }
    }
    MM_HIGH_MSG("BaseClient::threadBufferFBD-%s Exiting bufThreadFBD...", mNamePtr);
}

bool BaseClient::isLoaded()
{
    std::lock_guard<std::mutex> lkg(mStateMutex);

    return mState == VIDC_STATE_LOADED;
}

bool BaseClient::isIdle()
{
    std::lock_guard<std::mutex> lkg(mStateMutex);

    return mState == VIDC_STATE_IDLE;
}

bool BaseClient::isPaused()
{
    std::lock_guard<std::mutex> lkg(mStateMutex);

    return mState == VIDC_STATE_PAUSE;
}
