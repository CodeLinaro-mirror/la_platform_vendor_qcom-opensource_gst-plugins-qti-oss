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

#ifndef GSTCLIENT_H
#define GSTCLIENT_H
#include <mutex>
#include "baseclient.h"
#include "types.h"

using namespace QTI;

typedef enum {
    COMPONENT_DECODER,
    COMPONENT_ENCODER
} ComponentIdType;

class GstClient : public BaseClient {
public:
    typedef struct
    {
        int width; // frame output width/height pixels
        int height;
        PlaneInfo::color_format_type pix_fmt; // Pixel Format
        int inputBufferCount; // Number of input buffers to use, 0 for default
        int outputBufferCount; // Number of output buffers to use, 0 for default
        vidc_output_order_mode_type outputOrder; // decode frame order
        int dyn_buffer_input; // Dynamic buffer selection for input
        int dyn_buffer_output; // Dynamic buffer selection for ouptut
        int input_buffer_size; // Buffer size specified by user for input
        int output_buffer_size; // Buffer size specified by user for output
        bool isAvsync;
        float frameRate; // Frame rate, FPS
        vidc_nal_stream_fromat_type nalStreamFmt; // NAL Stream Format Type
        vidc_session_codec_type session;

        vidc_rate_control_mode_type rateControl; // Rate control method
        vidc_target_bitrate_type bitRate;
        vidc_profile_type profile;
        vidc_level_type level;
    } ConfigType;

    GstClient(ComponentIdType id);

    virtual ~GstClient();

    virtual bool initialize();

    virtual bool emptyBuffer(vidc_frame_data_type frameData);

    virtual bool fillBuffer(vidc_frame_data_type frameData);

    virtual bool stateExecuting();

    virtual bool stateIdle();

    virtual bool stateLoaded();

    virtual bool statePause();

    bool start(vidc_buffer_type buffer);

    bool stop(vidc_buffer_type buffer);

    bool setConfiguration(ConfigType& config);

    bool getConfiguration(ConfigType* config);

    bool setListenercallback(std::unique_ptr<EventCallback> callback);

    bool getBufferRequirement(vidc_buffer_type type, uint32* count, uint32* size, bool is_set);

    bool freeBuffer(vidc_frame_data_type frameData);

    bool isEncoder();

    int getPlaneCount();
    int getPlaneBytes(int plane);
    int getPlaneHeight(int plane);
    int getPlaneWidth(int plane);
    int getPlaneStride(int plane);

protected:
    ConfigType mConfig; // Client config info

    bool configureCodec(ConfigType& config);

private:
    /* prevent copies being made */
    GstClient& operator=(const GstClient&) { return *this; }
    GstClient(const GstClient& src)
        : BaseClient("")
    {
    }

    void EmptyCallback(BaseClient* base, vidc_frame_data_type& frameData);
    void FilledCallback(BaseClient* base, vidc_frame_data_type& frameData);
    void outputReconfigureCallback(BaseClient* base);
    void eosDoneCallback(BaseClient* base);
    bool configureEncoder(ConfigType& config);
    bool configureDecoder(ConfigType& config);

    std::mutex mMutexShutdown;
    ComponentIdType mCompId;

    std::unique_ptr<EventCallback> mCallback;
    PlaneInfo mPlaneInfo;
    int mTag;
};

#endif // GSTCLIENT_H
