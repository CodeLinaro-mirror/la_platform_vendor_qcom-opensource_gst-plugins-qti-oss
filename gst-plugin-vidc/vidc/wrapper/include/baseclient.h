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

#ifndef BASECLIENT_H
#define BASECLIENT_H
#include <memory>
#include <map>
#include <mutex>
#include <vector>
#include <queue>
#include <functional>
#include <condition_variable>
#include <vidc_types.h>
#include <vidc_client.h>
#include <vidc_ioctl.h>
#include "planeinfo.h"
#include "types.h"
#include "mmqueue.h"
#include "threadclass.h"

#define VIDC_DRIVER "VideoCore/vidc_drv"

#if defined(ALIGN)
#undef ALIGN
#endif
#define ALIGN(x, to_align) ((((unsigned long)x) + (to_align - 1)) & ~(to_align - 1))

#define RETURN_BOOL_ON_ERROR(rc, msg, ...) \
    if (true != (rc)) {                    \
        MM_ERROR_MSG(msg, ##__VA_ARGS__);  \
        return (rc);                       \
    }

// Base class for components that use VIDC data types
// For methods that return a bool: true is success, false is failure
class BaseClient {
public:
    // Map of frame data buffers that are keyed by
    // the pmem handle of the data buffer.
    typedef std::map<pmem_handle_t, vidc_frame_data_type> HeaderMapType;

    // Map of frame data buffers that are keyed by
    // the pmem handle with buffer being pushed to the decoder
    typedef std::map<pmem_handle_t, bool> HeaderInUseType;

    // Callback function invoked for empty or filled buffer
    typedef std::function<void(
        BaseClient* vidcPtr, // Component that invoked the callback
        vidc_frame_data_type& frameData // Data buffer
        )>
        BufferCallbackType;

    // Callback function invoked for component reconfiguration
    typedef std::function<void(
        BaseClient* vidcPtr // Component that invoked the callback
        )>
        ReconfigureCallbackType;

    // Callback function invoked for component EOS done
    typedef std::function<void(
        BaseClient* vidcPtr // Component that invoked the callback
        )>
        EOSDoneCallbackType;

    typedef struct // Port information that describes the I/O data
    {
        int width; // Image width, pixels
        int height; // Image height, pixels
        int outputWidth; // Image output width, pixels
        int outputHeight; // Image output height, pixels
        int dyn_buffer_input; // Enable/Disable Dynamic buffer mode for input
        int dyn_buffer_output; // Enable/Disable Dynamic buffer mode for output
        bool sync_frame_seq_hdr; // Enable sync frame sequence header from user
        bool adaptive_coding; // Enable adaptive coding from user
        int input_buffer_size; // Buffer size specified by user for input
        int output_buffer_size; // Buffer size specified by user for output
        float frameRate; // Frame rate, FPS
        PlaneInfo::color_format_type pixelFmt; // Pixel format
        vidc_bit_depth_type vidcBitDepth; // Image Bit Depth
        uint32 scanFormat; // 1 - Progressive, 0 - Interlaced
        vidc_codec_type codec; // Codec (for encoded data)
        plane_def_type plane_def; // plane definition required for linear formats
        bool isValid; // True if the port information is valid, false if not initialized yet
        bool isAvsync; // True if AV sync enabled
        bool encPerfMode; // Performance mode used for high concurrency encode usecase
        int bitRate; // Encoding bit rate
    } PortDataType;

    BaseClient(const char* namePtr); // Name is only used for log messaging

    virtual ~BaseClient();

    virtual bool emptyBuffer(vidc_frame_data_type frameData) = 0; // Empty the input buffer

    virtual bool fillBuffer(vidc_frame_data_type frameData) = 0; // Fill the output buffer

    virtual bool freeBuffer // Stop tracking buffers
        (
            vidc_buffer_type buffer);

    bool getEncPerfMode() { return mconfig.encPerfMode; };

    void setEncPerfMode(bool encModeType) { mconfig.encPerfMode = encModeType; };

    int getBufferCount(vidc_buffer_type buffer); // Get number of I/O buffers required for the port

    int getBufferSize(vidc_buffer_type buffer); // Get bytes required for the port buffer

    const char* getname() { return mNamePtr; }

    const PortDataType& getPortInfo(vidc_buffer_type buffer); // Get info describing the port data

    // You must set the configuration and the input
    // port information before attempting to
    // initialize a component.
    virtual bool initialize() = 0;

    bool isInitialized() { return mInitialized; }

    bool isLoaded();

    bool isIdle();

    bool isPaused();

    bool useBuffer(vidc_buffer_type type, int32 handle, uint32 size);

    bool setParameter(
        vidc_property_id_type propId,
        void* payloadPtr,
        uint32 payloadBytes);

    bool getParameter(
        vidc_property_id_type propId,
        void* payloadPtr,
        uint32 payloadBytes);

    virtual bool outputPortReconfig();

    virtual bool configureDynProp(vidc_frame_data_type& frameData);

    void registerCallback // Register empty/filled & output reconfigure callbacks
        (
            BufferCallbackType emptyCallback,
            BufferCallbackType filledCallback,
            ReconfigureCallbackType reconfigureCallback,
            EOSDoneCallbackType eosDoneCallback);

    virtual void seekPosition(uint32 pos);

    void setPortInfo // Set info describing the data buffer
        (
            vidc_buffer_type buffer,
            PortDataType& dataInfo);

    virtual bool stateExecuting() = 0; // Put component into executing state, blocking call

    virtual bool stateIdle() = 0; // Put component into Idle state, blocking call

    virtual bool stateLoaded() = 0; // Put component into loaded state and release buffers, blocking call

    virtual bool statePause() = 0; // Put component into paused state, blocking call

    void resetBufferInUse(vidc_buffer_type buffer, pmem_handle_t handle)
    {
        mPort[buffer].bufferInUse.at(handle) = false;
    }

    void setBufferInUse(vidc_buffer_type buffer, pmem_handle_t handle)
    {
        mPort[buffer].bufferInUse.at(handle) = true;
    }

    bool getBufferInUse(vidc_buffer_type buffer, pmem_handle_t handle)
    {
        return mPort[buffer].bufferInUse.at(handle);
    }

    virtual void setEndOfStream(bool val)
    {
        mEndOfStream = val;
    }

    virtual bool endOfStream()
    {
        return mEndOfStream;
    }

protected:
    // cur state   command    new state
    //---------------------------------
    // LOADED    -- acquire -- IDLE
    // IDLE      -- start   -- EXECUTING
    // EXECUTING -- pause   -- PAUSE
    // EXECUTING -- stop    -- IDLE
    // PAUSE     -- resume  -- EXECUTING
    // PAUSE     -- stop    -- IDLE
    // IDLE      -- release -- LOADED
    typedef enum {
        VIDC_STATE_UNLOADED,
        VIDC_STATE_LOADED,
        VIDC_STATE_IDLE,
        VIDC_STATE_EXECUTING,
        VIDC_STATE_PAUSE,
        VIDC_STATE_COUNT
    } VidcStateType;

    typedef enum {
        COMMAND_EXIT, // Transport thread exit
        COMMAND_ACQUIRE, // Acquire resources, go to idle state
        COMMAND_RELEASE, // Release resources, return to loaded state
        COMMAND_START, // Start executing, go to executing state
        COMMAND_STOP, // Stop executing, return to idle state
        COMMAND_PAUSE, // Pause executing, go to pause state
        COMMAND_RESUME, // Resume executing, go to executing state
        COMMAND_INPUT_BUFFER, // Input buffer is availble to process
        COMMAND_OUTPUT_BUFFER, // Output buffer is available to fill
        COMMAND_OUTPUT_PORT_RECONFIG, // Output port reconfig event received
        COMMAND_DRAIN, // Drain input buffers
        COMMAND_INPUT_START, // Start input executing
        COMMAND_OUTPUT_START, // Start output executing
        COMMAND_INPUT_STOP, // Stop input executing
        COMMAND_OUTPUT_STOP, // Stop output executing
        COMMAND_LAST_FLAG // Last flag event received
    } CommandType;

    typedef struct
    {
        HeaderMapType buffers; // Map of buffers
        HeaderInUseType bufferInUse; // Map of buffers that are in-use by the decoder
        vidc_buffer_reqmnts_type requirements; // Buffer requirements
        PortDataType data; // Data information
        const char* namePtr; // Port name, only used for logging
    } PortType;

    typedef struct
    {
        bool isDynamicConfig;
        int fps;
    } DynamicFPSConfig;

    typedef std::map<vidc_buffer_type, PortType> PortMapType;

    ioctl_session_t* mHandle;
    PortDataType mconfig; // Config to access port information
    const char* mNamePtr; // Name of the component, only used for logging
    MMQueue<CommandType> mQueueCommand; // Queue for commands
    MMQueue<CommandType> mQueueCompleted; // Queue for completed commands
    BufferCallbackType mEmptyCallback; // Client callback to invoke when an input buffer is emptied
    BufferCallbackType mFilledCallback; // Client callback to invoke when an output buffer is filled
    ReconfigureCallbackType mReconfigureCallback; // Client callback to invoke for output reconfiguration
    EOSDoneCallbackType mEosDoneCallback; // Client callback to invoke for EOS done
    PortMapType mPort; // Information to describe the I/O ports
    bool mInitialized; // True if component has been initialized
    VidcStateType mState; // Current state of the component
    std::mutex mStateMutex; // Mutex to control state transitions
    bool mShutdown; // True if shutting down
    // void *                   mClientDataPtr;              // Client can attach whatever data it wants
    bool mEndOfStream;
    bool mInputStarted; // input port start flag
    bool mOutputStarted;
    bool mWaitLastFlagToReconfig; // wait Last Flag to reconfig
    bool mDrainSent;
    virtual void emptyDone // Invoked when input buffer is emptied
        (
            vidc_frame_data_type& frameData);

    virtual void fillDone // Invoked when output buffer is filled
        (
            vidc_frame_data_type& frameData);

    virtual void eosProcessingDone(); // Invoked when EOS processing is done

    vidc_frame_data_type* getFrame // Convenience method to retrieve the frame data info
        (
            vidc_frame_data_type& frameData, // Use the frame_addr to look up the info
            vidc_buffer_type buffer // Specify input or output buffer
        );

    virtual int event // Common event handling for all components
        (
            vidc_drv_msg_info_type& info,
            uint32 length);

    void resetPort(vidc_buffer_type buffer, const char* namePtr);

    static int eventCallback(uint8* msgPtr, uint32 length, void* clientPtr);

private:
    bool mDone;
    std::condition_variable mCondVarEBD;
    std::condition_variable mCondVarFBD;
    MMQueue<vidc_frame_data_type> mQueueEBD;
    MMQueue<vidc_frame_data_type> mQueueFBD;
    std::mutex mMutexBufferEBD;
    std::mutex mMutexBufferFBD;
    ThreadClass mThread; // Thread for handling response events
    ThreadClass mBufferThreadEBD; // Thread for handling buffer events
    ThreadClass mBufferThreadFBD; // Thread for handling buffer events
    void threadMain();
    void threadBufferEBD();
    void threadBufferFBD();
};

#endif // BASECLIENT_H
