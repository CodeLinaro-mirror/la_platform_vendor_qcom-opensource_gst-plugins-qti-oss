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

#include "gstclient.h"
#include <sys/mman.h>
#include <vidc_types.h>
#include <algorithm>

#define NUM_OF_BACKBUFFER_IN 4
#define NUM_OF_BACKBUFFER_OUT 6
#define DEFAULT_BITRATE 500000

typedef enum {
    INTERLACE_MODE_PROGRESSIVE = 0, ///< progressive
    INTERLACE_MODE_INTERLEAVED_TOP_FIRST, ///< line-interleaved. top-field-first
    INTERLACE_MODE_INTERLEAVED_BOTTOM_FIRST, ///< line-interleaved. bottom-field-first
    INTERLACE_MODE_FIELD_TOP_FIRST, ///< field-sequential. top-field-first
    INTERLACE_MODE_FIELD_BOTTOM_FIRST, ///< field-sequential. bottom-field-first
} INTERLACE_MODE_TYPE;

std::unordered_map<vidc_qmetadata_interlace_type, INTERLACE_MODE_TYPE>
    meta_interlace_type_table =
{
    {VIDC_QMETADATA_INTERLACE_NONE, INTERLACE_MODE_PROGRESSIVE},
    {VIDC_QMETADATA_FRAME_PROGRESSIVE, INTERLACE_MODE_PROGRESSIVE},
    {VIDC_QMETADATA_FRAME_INTERLEAVE_TOPFIELD_FIRST, INTERLACE_MODE_INTERLEAVED_TOP_FIRST},
    {VIDC_QMETADATA_FRAME_INTERLEAVE_BOTTOMFIELD_FIRST, INTERLACE_MODE_INTERLEAVED_BOTTOM_FIRST},
    {VIDC_QMETADATA_FRAME_INTERLACE_TOPFIELD_FIRST, INTERLACE_MODE_FIELD_TOP_FIRST},
    {VIDC_QMETADATA_FRAME_INTERLACE_BOTTOMFIELD_FIRST, INTERLACE_MODE_FIELD_BOTTOM_FIRST},
};

GstClient::GstClient(ComponentIdType id)
    : BaseClient(id == COMPONENT_DECODER ? "Decoder" : "Encoder")
{
    MM_DBG_MSG("GstClient::GstClient id %d", id);
    mCompId = id;
    BufferCallbackType emptyCallbackFcn = {};
    BufferCallbackType filledCallbackFcn = {};
    ReconfigureCallbackType reconfigureCallbackFcn = {};
    EOSDoneCallbackType eosDoneCallbackFcn = {};

    emptyCallbackFcn = [this](BaseClient* base, vidc_frame_data_type& frameData) {
        return EmptyCallback(base, frameData);
    };
    filledCallbackFcn = [this](BaseClient* base, vidc_frame_data_type& frameData) {
        return FilledCallback(base, frameData);
    };
    reconfigureCallbackFcn = [this](BaseClient* base) {
        return outputReconfigureCallback(base);
    };
    eosDoneCallbackFcn = [this](BaseClient* base) {
        return eosDoneCallback(base);
    };

    registerCallback(emptyCallbackFcn, filledCallbackFcn,
        reconfigureCallbackFcn, eosDoneCallbackFcn);

    memset(&mConfig, 0, sizeof(mConfig));

    mConfig.pix_fmt = PlaneInfo::COLOR_FORMAT_UNUSED;
    mConfig.inputBufferCount = NUM_OF_BACKBUFFER_IN;
    mConfig.outputBufferCount = NUM_OF_BACKBUFFER_OUT;
    mConfig.outputOrder = VIDC_DEC_ORDER_UNUSED;
    mConfig.nalStreamFmt = VIDC_NAL_FORMAT_UNUSED;
    mConfig.session.session = id == COMPONENT_DECODER
        ? VIDC_SESSION_DECODE
        : VIDC_SESSION_ENCODE;
    mConfig.session.codec = VIDC_CODEC_UNUSED;
    mConfig.rateControl = VIDC_RATE_CONTROL_UNUSED;
    mConfig.profile.profile = VIDC_PROFILE_H264_UNUSED;
    mConfig.level.level = VIDC_LEVEL_H264_UNUSED;
    mConfig.bitRate.target_bitrate = DEFAULT_BITRATE;

    mPlaneInfo.initialize();
}

GstClient::~GstClient()
{
    MM_DBG_MSG("GstClient::~GstClient");
}

int GstClient::flattenMetaData(vidc_frame_data_type& frameData)
{
    int rc = VIDC_ERR_NONE;
    if (frameData.metadata_handle && frameData.alloc_metadata_len) {
        void *meta_addr = mmap(NULL, frameData.alloc_metadata_len,
            PROT_READ|PROT_WRITE, MAP_SHARED, frameData.metadata_handle, 0);
        if (MAP_FAILED == meta_addr) {
            LOG_ERROR("failed to map metadata %d", frameData.metadata_handle);
            rc = VIDC_ERR_FAIL;
        } else {
            vidc_qmetabuf_header_type* metadataHdr =
                (vidc_qmetabuf_header_type*)meta_addr;
            if (metadataHdr) {
                memset(metadataHdr, 0, sizeof(vidc_qmetabuf_header_type));
                metadataHdr->size = sizeof(vidc_qmetabuf_header_type) +
                    sizeof(vidc_qmetapayload_header_type) + 8;
                metadataHdr->version = 1 << 16;

                vidc_qmetapayload_header_type* metaPayloadHdr =
                    (vidc_qmetapayload_header_type*) (metadataHdr + 1);
                uint32 metaPayloadOffset = metadataHdr->size - 8;

                memset(metaPayloadHdr, 0, sizeof(vidc_qmetapayload_header_type));

                // flatten for VIDC_QMETADATA_BUFFER_TAG
                metaPayloadHdr->qmetadata_type = VIDC_QMETADATA_BUFFER_TAG;
                metaPayloadHdr->size = 8;
                metaPayloadHdr->version = 1 << 16;
                metaPayloadHdr->offset = metaPayloadOffset;
                metaPayloadHdr->flags = 0;

                vidc_qmetadata_buffer_tag_type *payload =
                    (vidc_qmetadata_buffer_tag_type *)(((uint8 *)metadataHdr) + metaPayloadHdr->offset);
                payload->tag = (uint64)frameData.input_tag;

                LOG_INFO("meta[%d] type 0x%x", metadataHdr->count, metaPayloadHdr->qmetadata_type);
                if (metaPayloadHdr->qmetadata_type == VIDC_QMETADATA_BUFFER_TAG) {
                    LOG_INFO("tag 0x%x, size %d, offset %d", payload->tag,
                        metaPayloadHdr->size, metaPayloadHdr->offset);
                }

                metadataHdr->count++;
                metaPayloadHdr++;
            }

            munmap(meta_addr, frameData.alloc_metadata_len);
        }
    } else {
        LOG_ERROR("No valid metadata");
        rc = VIDC_ERR_FAIL;
    }

    return rc;
}

int GstClient::extractMetaData(vidc_frame_data_type& frameData,
    std::vector<std::shared_ptr<MetaInfo>> *infos)
{
    int rc = VIDC_ERR_NONE;
    if (infos == NULL) {
        LOG_ERROR("invalid infos pointer");
        return VIDC_ERR_FAIL;
    }

    if (frameData.metadata_handle && frameData.alloc_metadata_len) {
        void *meta_addr = mmap(NULL, frameData.alloc_metadata_len, PROT_READ,
            MAP_SHARED, frameData.metadata_handle, 0);
        if (MAP_FAILED == meta_addr) {
            LOG_ERROR("failed to map metadata %d", frameData.metadata_handle);
            rc = VIDC_ERR_FAIL;
        } else {
            vidc_qmetabuf_header_type* metadataHdr =
                (vidc_qmetabuf_header_type*)meta_addr;
            if (metadataHdr) {
                if (metadataHdr->count > 0) {
                    vidc_qmetapayload_header_type* metaPayloadHdr =
                        (vidc_qmetapayload_header_type*) (metadataHdr + 1);
                    for (uint32 i = 0; i < metadataHdr->count; i++) {
                        LOG_INFO("meta[%d] type 0x%x", i, metaPayloadHdr->qmetadata_type);
                        auto metaInfo = std::make_shared<MetaInfo>();
                        if (!metaInfo) {
                            LOG_WARNING("failed to make metaInfo");
                            continue;
                        }

                        metaInfo->header = *metaPayloadHdr;
                        metaInfo->payload = malloc (metaPayloadHdr->size);
                        if (!metaInfo->payload) {
                            LOG_WARNING("failed to make metaInfo payload");
                            continue;
                        }

                        if (metaPayloadHdr->qmetadata_type == VIDC_QMETADATA_INTERLACE) {
                            vidc_qmetadata_interlace_type *payload =
                                (vidc_qmetadata_interlace_type *)(((uint8 *)metadataHdr) + metaPayloadHdr->offset);
                            LOG_INFO("VIDC_QMETADATA_INTERLACE 0x%x, payload 0x%x, size %d, offset %d",
                                VIDC_QMETADATA_INTERLACE, *payload,
                                metaPayloadHdr->size, metaPayloadHdr->offset);
                            memcpy (metaInfo->payload, payload, metaPayloadHdr->size);
                        } else if (metaPayloadHdr->qmetadata_type == VIDC_QMETADATA_BUFFER_TAG) {
                            vidc_qmetadata_buffer_tag_type *payload =
                                (vidc_qmetadata_buffer_tag_type *)(((uint8 *)metadataHdr) + metaPayloadHdr->offset);
                            LOG_INFO("VIDC_QMETADATA_BUFFER_TAG 0x%x, payload 0x%x, size %d, offset %d",
                                VIDC_QMETADATA_BUFFER_TAG, payload->tag,
                                metaPayloadHdr->size, metaPayloadHdr->offset);
                            frameData.input_tag = payload->tag;
                            memcpy (metaInfo->payload, payload, metaPayloadHdr->size);
                        }
                        metaPayloadHdr++;
                        infos->push_back(metaInfo);
                    }
                }
            }

            munmap(meta_addr, frameData.alloc_metadata_len);
        }
    } else {
        LOG_ERROR("No valid metadata");
        rc = VIDC_ERR_FAIL;
    }

    return rc;
}

bool GstClient::configureEncoder(ConfigType& config)
{
    MM_DBG_MSG("GstClient::configureEncoder");
    bool rc = false;
    vidc_session_codec_type session;
    vidc_iperiod_type intraPeriod;
    vidc_idr_period_type idrPeriod;
    vidc_frame_rate_type frameRate;
    vidc_color_format_config_type colorFormat;
    vidc_frame_size_type frameSize;
    vidc_rate_control_mode_type rateControl;
    vidc_target_bitrate_type bitrate;
    vidc_intra_refresh_type intraRefresh;
    vidc_spatial_transform_type transform;
    vidc_session_qp_type qp;
    vidc_multi_slice_type slice_type;
    vidc_blur_filter_type blur_value;
    vidc_vpe_csc_type vpe_csc;
    vidc_vui_video_signal_info_type vui_signal_info;

    rc = setParameter(VIDC_I_SESSION_CODEC, &config.session, sizeof(config.session));
    if (true != rc) {
        MM_ERROR_MSG("GstClient::configureEncoder Error failed to set codec");
    }

    if (true == rc) {
        memset(&colorFormat, 0, sizeof(vidc_color_format_config_type));
        colorFormat.buf_type = VIDC_BUFFER_INPUT;
        colorFormat.color_format =
            mPlaneInfo.convertToVidcFromInt(mPort[VIDC_BUFFER_INPUT].data.pixelFmt);
        MM_DBG_MSG("GstClient::configureCodec set color_format 0x%08x",
            colorFormat.color_format);
        rc = setParameter(VIDC_I_COLOR_FORMAT, &colorFormat, sizeof(vidc_color_format_config_type));
        if (true != rc) {
            MM_ERROR_MSG("GstClient::configureEncoder Error failed to set color format");
        }
    }

    if (true == rc) {
        memset(&rateControl, 0, sizeof(vidc_rate_control_mode_type));
        if (config.rateControl != VIDC_RATE_CONTROL_UNUSED) {
            rateControl = config.rateControl;
            rc = setParameter(VIDC_I_ENC_RATE_CONTROL, &rateControl, sizeof(rateControl));
            if (true != rc) {
                MM_ERROR_MSG("GstClient::configureEncoder Error failed to set ratecontrol");
            }
        }
    }

    if (true == rc) {
        memset(&frameSize, 0, sizeof(vidc_frame_size_type));
        frameSize.buf_type = VIDC_BUFFER_INPUT;
        frameSize.width = mPort[VIDC_BUFFER_INPUT].data.width;
        frameSize.height = mPort[VIDC_BUFFER_INPUT].data.height;
        rc = setParameter(VIDC_I_FRAME_SIZE, &frameSize, sizeof(frameSize));
        if (true != rc) {
            MM_ERROR_MSG("GstClient::configureEncoder Error failed to set input frame size");
        } else {
            MM_DBG_MSG("GstClient::configureEncoder set input frame size %dx%d",
                frameSize.width, frameSize.height);
        }
    }

    if (true == rc) {
        memset(&frameSize, 0, sizeof(vidc_frame_size_type));
        frameSize.buf_type = VIDC_BUFFER_OUTPUT;
        if (mPort[VIDC_BUFFER_OUTPUT].data.outputWidth != 0 &&
            mPort[VIDC_BUFFER_OUTPUT].data.outputHeight != 0) {
            frameSize.width = mPort[VIDC_BUFFER_OUTPUT].data.outputWidth;
            frameSize.height = mPort[VIDC_BUFFER_OUTPUT].data.outputHeight;
        } else {
            frameSize.width = mPort[VIDC_BUFFER_OUTPUT].data.width;
            frameSize.height = mPort[VIDC_BUFFER_OUTPUT].data.height;
        }
        rc = setParameter(VIDC_I_FRAME_SIZE, &frameSize, sizeof(frameSize));
        if (true != rc) {
            MM_ERROR_MSG("GstClient::configureEncoder Error failed to set output frame size");
        } else {
            MM_DBG_MSG("GstClient::configureEncoder set output frame size %dx%d",
                frameSize.width, frameSize.height);
        }
    }

    if (true == rc) {
        vidc_profile_type profile;
        memset(&profile, 0, sizeof(vidc_profile_type));
        profile.profile = config.profile.profile;
        rc = setParameter(VIDC_I_PROFILE, &profile, sizeof(profile));
        if (rc != true) {
            MM_ERROR_MSG("GstClient::configureEncoder failed to set profile");
        } else {
            MM_DBG_MSG("GstClient::configureEncoder set profile 0x%08x", profile.profile);
        }
    }

    if (true == rc) {
        vidc_level_type lvl;
        memset(&lvl, 0, sizeof(vidc_level_type));
        lvl.level = config.level.level;
        rc = setParameter(VIDC_I_LEVEL, &lvl, sizeof(lvl));
        if (rc != true) {
            MM_ERROR_MSG("GstClient::configureEncoder failed to set level");
        } else {
            MM_DBG_MSG("GstClient::configureEncoder set level 0x%08x", lvl.level);
        }
    }

    if (true == rc) {
        memset(&frameRate, 0, sizeof(vidc_frame_rate_type));
        // Input and output frame rate must be the same,
        // Encoder doesn't support different rates.
        // We will use the input port frame rate for
        // both.  A non-zero frame rate must be specified
        // or the HW encoder will crash.
        frameRate.buf_type = VIDC_BUFFER_OUTPUT; // Must set output frame rate before input frame rate
        frameRate.fps_numerator = mPort[VIDC_BUFFER_INPUT].data.frameRate * 0x10000;
        frameRate.fps_denominator = 0x10000;
        MM_DBG_MSG("GstClient::configureEncoder set framerate %0.2f",
            mPort[VIDC_BUFFER_OUTPUT].data.frameRate);
        rc = setParameter(VIDC_I_FRAME_RATE, &frameRate, sizeof(frameRate));
        if (rc != true) {
            MM_ERROR_MSG("GstClient::configureEncoder failed to set output framerate");
        } else {
            frameRate.buf_type = VIDC_BUFFER_INPUT;
            rc = setParameter(VIDC_I_FRAME_RATE, &frameRate, sizeof(frameRate));
        }

        if (rc != true) {
            MM_ERROR_MSG("GstClient::configureEncoder failed to set input framerate");
        } else {
            MM_DBG_MSG("GstClient::configureEncoder framerate %0.2f",
                frameRate.fps_numerator / frameRate.fps_denominator);
        }
    }

    if (true == rc) {
        memset(&bitrate, 0, sizeof(vidc_target_bitrate_type));
        bitrate.target_bitrate = config.bitRate.target_bitrate;
        rc = setParameter(VIDC_I_TARGET_BITRATE, &bitrate, sizeof(bitrate));

        if (rc != true) {
            MM_ERROR_MSG("GstClient::configureEncoder failed to set bitrate");
        } else {
            MM_DBG_MSG("GstClient::configureEncoder set bitrate %d", bitrate.target_bitrate);
        }
    }

    if (true == rc) {
        vidc_metadata_header_type payload;
        memset(&payload, 0, sizeof(vidc_metadata_header_type));
        payload.metadata_type = VIDC_QMETADATA_BUFFER_TAG;
        payload.enable = true;
        payload.port_index = VIDC_QMETADATA_PORT_INPUT_FROM_CLIENT|VIDC_QMETADATA_PORT_OUTPUT_TO_CLIENT;
        MM_DBG_MSG("GstClient::configureEncoder enable VIDC_QMETADATA_BUFFER_TAG");
        rc = setParameter(VIDC_I_METADATA_HEADER, &payload, sizeof(payload));
        if (true != rc)
        {
            MM_ERROR_MSG("GstClient::configureEncoder Failed to enable buftag metadata property");
        }
    }

    if (true == rc) {
        int bytes = mPlaneInfo.computeBytes(
            mPort[VIDC_BUFFER_INPUT].data.width,
            mPort[VIDC_BUFFER_INPUT].data.height,
            mPort[VIDC_BUFFER_INPUT].data.pixelFmt,
            mConfig.session.codec,
            true);

        MM_DBG_MSG("GstClient::configureEncoder computeBytes %d", bytes);
    }

    return rc;
}

bool GstClient::configureDecoder(ConfigType& config)
{
    MM_DBG_MSG("GstClient::configureDecoder");

    bool rc = false;
    vidc_output_order_type order;
    vidc_enable_type enable;
    vidc_frame_size_type frameSize;
    vidc_frame_rate_type frameRate;
    vidc_color_format_config_type colorFormat;
    vidc_buffer_reqmnts_type bufreq;

    rc = setParameter(VIDC_I_SESSION_CODEC, &config.session, sizeof(config.session));
    if (true != rc) {
        MM_ERROR_MSG("GstClient::configureDecoder Error failed to set codec");
    }

    if (true == rc) {
        memset(&order, 0, sizeof(vidc_output_order_type));
        if (config.outputOrder == VIDC_DEC_ORDER_UNUSED) {
            order.output_order = VIDC_DEC_ORDER_DISPLAY; // default will be used when nothing is set
        } else {
            order.output_order = config.outputOrder;
        }

        MM_DBG_MSG("GstClient::configureDecoder set decode output order %d", order.output_order);
        rc = setParameter(VIDC_I_DEC_OUTPUT_ORDER, &order, sizeof(order));
        if (true != rc) {
            MM_ERROR_MSG("GstClient::configureDecoder Error failed to set decode output order");
        }
    }

    if (true == rc) {
        memset(&enable, 0, sizeof(vidc_enable_type));
        enable.enable = true;
        rc = setParameter(VIDC_I_DEC_CONT_ON_RECONFIG, &enable, sizeof(enable));
        if (true != rc) {
            MM_ERROR_MSG("GstClient::configureDecoder Error failed to enable decoder property");
        }
    }

    if (true == rc) {
        // set input frame size
        memset(&frameSize, 0, sizeof(vidc_frame_size_type));
        frameSize.buf_type = VIDC_BUFFER_INPUT;
        frameSize.width = mPort[VIDC_BUFFER_INPUT].data.width;
        frameSize.height = mPort[VIDC_BUFFER_INPUT].data.height;
        MM_DBG_MSG("GstClient::configureDecoder set input frame size %dx%d",
            frameSize.width, frameSize.height);
        rc = setParameter(VIDC_I_FRAME_SIZE, &frameSize, sizeof(frameSize));
        if (true != rc) {
            MM_ERROR_MSG("GstClient::configureDecoder Error failed to set input frame size");
        }
    }

    if (true == rc) {
        // set output frame size
        memset(&frameSize, 0, sizeof(vidc_frame_size_type));
        frameSize.buf_type = VIDC_BUFFER_OUTPUT;
        frameSize.width = mPort[VIDC_BUFFER_OUTPUT].data.width;
        frameSize.height = mPort[VIDC_BUFFER_OUTPUT].data.height;
        MM_DBG_MSG("GstClient::configureDecoder set output frame size %dx%d",
            frameSize.width, frameSize.height);
        rc = setParameter(VIDC_I_FRAME_SIZE, &frameSize, sizeof(frameSize));
        if (true != rc) {
            MM_ERROR_MSG("GstClient::configureDecoder Error failed to set output frame size");
        }
    }

    if (true == rc) {
        memset(&frameRate, 0, sizeof(vidc_frame_rate_type));
        if (mPort[VIDC_BUFFER_INPUT].data.frameRate > 0) {
            // Input and output frame rate must be the same,
            // Decoder doesn't support different rates. So
            // we use the input frame rate for both.
            frameRate.buf_type = VIDC_BUFFER_OUTPUT; // Must set output frame rate before input frame rate
            frameRate.fps_numerator = mPort[VIDC_BUFFER_INPUT].data.frameRate * 0x10000;
            frameRate.fps_denominator = 0x10000;
            MM_DBG_MSG("GstClient::configureDecoder set framerate %0.2f",
                mPort[VIDC_BUFFER_INPUT].data.frameRate);
            rc = setParameter(VIDC_I_FRAME_RATE, &frameRate, sizeof(frameRate));
            if (true != rc) {
                MM_ERROR_MSG("GstClient::configureDecoder Error failed to set output framerate");
            } else {
                frameRate.buf_type = VIDC_BUFFER_INPUT;
                rc = setParameter(VIDC_I_FRAME_RATE, &frameRate, sizeof(frameRate));
                if (true != rc) {
                    MM_ERROR_MSG("GstClient::configureDecoder Error failed to set input framerate");
                }
            }
        }
    }

    if (true == rc) {
        if (mCompId == COMPONENT_DECODER) {
            memset(&colorFormat, 0, sizeof(vidc_color_format_config_type));
            colorFormat.buf_type = VIDC_BUFFER_OUTPUT;
            colorFormat.color_format =
                mPlaneInfo.convertToVidcFromInt(mPort[VIDC_BUFFER_OUTPUT].data.pixelFmt);
            MM_DBG_MSG("GstClient::configureDecoder set color_format 0x%08x", colorFormat.color_format);
            rc = setParameter(VIDC_I_COLOR_FORMAT, &colorFormat, sizeof(vidc_color_format_config_type));
            if (true != rc) {
                MM_ERROR_MSG("GstClient::configureDecoder Error failed to set color format");
            }
        }
    }

    if (true == rc) {
        vidc_metadata_header_type payload;
        memset(&payload, 0, sizeof(vidc_metadata_header_type));
        payload.metadata_type = VIDC_QMETADATA_INTERLACE;
        payload.enable = true;
        payload.port_index = VIDC_QMETADATA_PORT_OUTPUT_TO_CLIENT;
        MM_DBG_MSG("GstClient::configureDecoder enable VIDC_QMETADATA_INTERLACE");
        rc = setParameter(VIDC_I_METADATA_HEADER, &payload, sizeof(payload));
        if (true != rc)
        {
            MM_ERROR_MSG("GstClient::configureDecoder Failed to enable interlace metadata property");
        }
    }

    if (true == rc) {
        vidc_metadata_header_type payload;
        memset(&payload, 0, sizeof(vidc_metadata_header_type));
        payload.metadata_type = VIDC_QMETADATA_BUFFER_TAG;
        payload.enable = true;
        payload.port_index = VIDC_QMETADATA_PORT_INPUT_FROM_CLIENT|VIDC_QMETADATA_PORT_OUTPUT_TO_CLIENT;
        MM_DBG_MSG("GstClient::configureDecoder enable VIDC_QMETADATA_BUFFER_TAG");
        rc = setParameter(VIDC_I_METADATA_HEADER, &payload, sizeof(payload));
        if (true != rc)
        {
            MM_ERROR_MSG("GstClient::configureDecoder Failed to enable buftag metadata property");
        }
    }

    if (true == rc) {
        int bytes = mPlaneInfo.computeBytes(
            mPort[VIDC_BUFFER_OUTPUT].data.width,
            mPort[VIDC_BUFFER_OUTPUT].data.height,
            mPort[VIDC_BUFFER_OUTPUT].data.pixelFmt,
            mConfig.session.codec,
            true);

        MM_DBG_MSG("GstClient::configureDecoder computeBytes %d", bytes);
    }

    return rc;
}

bool GstClient::configureCodec(ConfigType& config)
{
    boolean rc = false;

    MM_DBG_MSG("GstClient::configureCodec session %d", config.session.session);
    if (config.session.session == VIDC_SESSION_ENCODE) {
        rc = configureEncoder(config);
    } else if (config.session.session == VIDC_SESSION_DECODE) {
        rc = configureDecoder(config);
    } else {
        MM_ERROR_MSG("GstClient::configureCodec unknown session type");
    }

    return rc;
}

bool GstClient::initialize()
{
    bool rc = false;
    vidc_buffer_alloc_mode_type buffer_alloc_mode = {};
    std::unique_lock<std::mutex> uLockState(mStateMutex, std::defer_lock);

    MM_DBG_MSG("GstClient::initialize");
    if (mHandle == NULL) {
        MM_ERROR_MSG("GstClient::initialize Handle is NULL");
        return false;
    }
    uLockState.lock();
    if (mState != VIDC_STATE_UNLOADED) // if already initialized
    {
        uLockState.unlock();
        MM_HIGH_MSG("GstClient::initialize Component already initialized");
        return true;
    }
    uLockState.unlock();

    // Default to using input dimensions
    mPort[VIDC_BUFFER_OUTPUT].data.isValid = true;
    mPort[VIDC_BUFFER_OUTPUT].data.isAvsync = mConfig.isAvsync;
    mPort[VIDC_BUFFER_OUTPUT].data.codec = VIDC_CODEC_UNUSED;
    if (mConfig.width != 0) // If output dimension were specified by client
    {
        mPort[VIDC_BUFFER_INPUT].data.width = mConfig.width; // Use the client specified output dimensions
        mPort[VIDC_BUFFER_INPUT].data.height = mConfig.height;
    }
    mPort[VIDC_BUFFER_OUTPUT].data.width = mPort[VIDC_BUFFER_INPUT].data.width;
    mPort[VIDC_BUFFER_OUTPUT].data.height = mPort[VIDC_BUFFER_INPUT].data.height;
    if (mConfig.frameRate > 0) {
        mPort[VIDC_BUFFER_INPUT].data.frameRate = mConfig.frameRate;
    }
    mPort[VIDC_BUFFER_OUTPUT].data.frameRate = mPort[VIDC_BUFFER_INPUT].data.frameRate;
    mPort[VIDC_BUFFER_OUTPUT].data.scanFormat = 1; // Progressive, default
    mPort[VIDC_BUFFER_INPUT].data.pixelFmt = mConfig.pix_fmt;
    mPort[VIDC_BUFFER_OUTPUT].data.pixelFmt = mConfig.pix_fmt;
    mPort[VIDC_BUFFER_OUTPUT].data.dyn_buffer_input = mConfig.dyn_buffer_input;
    mPort[VIDC_BUFFER_OUTPUT].data.dyn_buffer_output = mConfig.dyn_buffer_output;

    rc = configureCodec(mConfig); // Configure the common codec parameters
    RETURN_BOOL_ON_ERROR(rc, "GstClient::initialize Error configuring codec");

    if (mConfig.dyn_buffer_input)
    {
        memset(&buffer_alloc_mode, 0, sizeof(vidc_buffer_alloc_mode_type));
        buffer_alloc_mode.buf_type = VIDC_BUFFER_INPUT;
        buffer_alloc_mode.buf_mode = VIDC_BUFFER_MODE_DYNAMIC;
        rc = setParameter(VIDC_I_BUFFER_ALLOC_MODE, &buffer_alloc_mode, sizeof(buffer_alloc_mode));
        if (false == rc) {
            MM_ERROR_MSG("GstClient::Dynamic buffer initilization of input failed %d", rc);
        }
    }
    if (true == rc && mConfig.dyn_buffer_output) {
        memset(&buffer_alloc_mode, 0, sizeof(vidc_buffer_alloc_mode_type));
        buffer_alloc_mode.buf_type = VIDC_BUFFER_OUTPUT;
        buffer_alloc_mode.buf_mode = VIDC_BUFFER_MODE_DYNAMIC;
        rc = setParameter(VIDC_I_BUFFER_ALLOC_MODE, &buffer_alloc_mode, sizeof(buffer_alloc_mode));
        if (false == rc) {
            MM_ERROR_MSG("GstClient::Dynamic buffer initilization of output failed %d", rc);
        }
    }

    if (true == rc) {
        mInitialized = true;
        stateLoaded();
    }

    return rc;
}

bool GstClient::setConfiguration(ConfigType& config)
{
    MM_DBG_MSG("GstClient::setConfiguration");
    mConfig = config;
    return true;
}

bool GstClient::getConfiguration(ConfigType* config)
{
    MM_DBG_MSG("GstClient::getConfiguration");
    if (config) {
        *config = mConfig;
        return true;
    }

    return false;
}

bool GstClient::setListenercallback(std::unique_ptr<EventCallback> callback)
{
    MM_DBG_MSG("GstClient::setListenercallback-%s", mNamePtr);
    if (callback != NULL) {
        mCallback = std::move(callback);
        return true;
    } else {
        MM_ERROR_MSG("GstClient::setListenercallback-%s failed", mNamePtr);
    }

    return false;
}

bool GstClient::getBufferRequirement(
    vidc_buffer_type type, uint32* min_count, uint32* size, uint32* metasize, bool is_set)
{
    MM_DBG_MSG("GstClient::getBufferRequirement type %d", type);
    bool rc = false;
    vidc_buffer_reqmnts_type bufreq;
    memset(&bufreq, 0, sizeof(vidc_buffer_reqmnts_type));

    vidc_buffer_reqmnts_type meta_bufreq;
    memset(&meta_bufreq, 0, sizeof(vidc_buffer_reqmnts_type));

    bufreq.buf_type = type;
    rc = getParameter(VIDC_I_BUFFER_REQUIREMENTS, &bufreq, sizeof(bufreq));
    if (true != rc) {
        MM_ERROR_MSG("GstClient::getBufferRequirement Error failed to get buffer requirement");
    } else {
        MM_DBG_MSG("GstClient::getBufferRequirement: min_count=%d, actual_count=%d, min_size=0x%x",
            bufreq.min_count, bufreq.actual_count, bufreq.size);

        if (type == VIDC_BUFFER_OUTPUT) {
            bufreq.actual_count = bufreq.min_count + NUM_OF_BACKBUFFER_OUT;
        } else {
            bufreq.actual_count = bufreq.min_count + NUM_OF_BACKBUFFER_IN;
        }

        meta_bufreq.buf_type = type == VIDC_BUFFER_OUTPUT ?
            VIDC_BUFFER_METADATA_OUTPUT : VIDC_BUFFER_METADATA_INPUT;
        rc = getParameter(VIDC_I_BUFFER_REQUIREMENTS, &meta_bufreq, sizeof(meta_bufreq));
        meta_bufreq.actual_count = bufreq.actual_count;

        if (true != rc) {
            MM_ERROR_MSG("GstClient::getBufferRequirement Error failed to get metabuffer requirement");
            return rc;
        } else {
            MM_DBG_MSG("GstClient::getBufferRequirement:"
                "min_count=%d, actual_count=%d, min_size=0x%x, is_set %d",
                bufreq.min_count, bufreq.actual_count, bufreq.size, is_set);
            if (is_set) {
                rc = setParameter(VIDC_I_BUFFER_REQUIREMENTS, &bufreq, sizeof(bufreq));
                if (true != rc) {
                    MM_ERROR_MSG("GstClient::getBufferRequirement Error failed to set buffer requirement");
                    return rc;
                }

                MM_DBG_MSG("GstClient::getBufferRequirement:metadata"
                    "min_count=%d, actual_count=%d, min_size=0x%x, is_set %d",
                    meta_bufreq.min_count, meta_bufreq.actual_count, meta_bufreq.size, is_set);

                rc = setParameter(VIDC_I_BUFFER_REQUIREMENTS, &meta_bufreq, sizeof(meta_bufreq));
                if (true != rc) {
                    MM_ERROR_MSG("GstClient::getBufferRequirement Error failed to set metabuffer requirement");
                    return rc;
                }
            }
        }

        *min_count = bufreq.actual_count;
        *size = bufreq.size;
        *metasize = meta_bufreq.size;
    }

    return rc;
}

bool GstClient::stateLoaded()
{
    bool rcBool = true;
    int rcInt;
    CommandType command;
    std::unique_lock<std::mutex> uLockState(mStateMutex, std::defer_lock);

    uLockState.lock();
    MM_DBG_MSG("GstClient::stateLoaded-%s state %d", mNamePtr, mState);
    if (mState == VIDC_STATE_EXECUTING || // If in the exeucting state
        mState == VIDC_STATE_PAUSE) //   or in the pause state
    {
        uLockState.unlock();
        rcBool = stateIdle(); // Return to the idle state
        RETURN_BOOL_ON_ERROR(rcBool,
            "GstClient::stateLoadeded-%s Error transitioning to idle", mNamePtr);
    }

    //Only unlock in case where mstate is not executing or paused
    if (uLockState.owns_lock()) {
        uLockState.unlock();
    }

    uLockState.lock();
    if (mState == VIDC_STATE_IDLE || mState == VIDC_STATE_UNLOADED) // If in the idle state
    {
        mState = VIDC_STATE_LOADED;
    } else {
        MM_ERROR_MSG("GstClient::stateLoadeded-%s Not in the correct state %d",
            mNamePtr, mState);
        rcBool = false;
    }
    uLockState.unlock();

    return rcBool;
}

bool GstClient::stateExecuting()
{
    bool rcBool = true;
    std::unique_lock<std::mutex> uLockState(mStateMutex, std::defer_lock);
    uLockState.lock();
    MM_DBG_MSG("GstClient::stateExecuting-%s state %d", mNamePtr, mState);

    if (mState == VIDC_STATE_IDLE) {
        uLockState.unlock();
        if (!mInputStarted) {
            MM_DBG_MSG("GstClient::stateExecuting - start input");
            mQueueCompleted.clear();
            vidc_start_mode_type start_mode = VIDC_START_INPUT;
            if (VIDC_ERR_NONE != device_ioctl(mHandle, VIDC_IOCTL_START, (uint8*)&start_mode, sizeof(vidc_start_mode_type), NULL, 0)) {
                rcBool = false;
                MM_ERROR_MSG("GstClient::stateExecuting started output failed");
            } else {
                CommandType command = mQueueCompleted.pop(); // Wait for executing state
                if (COMMAND_INPUT_START != command) {
                    rcBool = false;
                    MM_ERROR_MSG("GstClient::stateExecuting-%s "
                        "Error waiting for start output, command %d", mNamePtr, command);
                }
                MM_DBG_MSG("GstClient::stateExecuting - input completed");
            }
        }

        if (!mOutputStarted) {
            MM_DBG_MSG("GstClient::stateExecuting - start output");
            mQueueCompleted.clear();
            vidc_start_mode_type start_mode = VIDC_START_OUTPUT;
            if (VIDC_ERR_NONE != device_ioctl(mHandle, VIDC_IOCTL_START, (uint8*)&start_mode, sizeof(vidc_start_mode_type), NULL, 0)) {
                rcBool = false;
                MM_ERROR_MSG("GstClient::stateExecuting started output failed");
            } else {
                CommandType command = mQueueCompleted.pop();
                if (COMMAND_OUTPUT_START != command) {
                    rcBool = false;
                    MM_ERROR_MSG("GstClient::stateExecuting-%s Error waiting "
                        "for start output, command %d", mNamePtr, command);
                }
                MM_DBG_MSG("GstClient::stateExecuting - output completed");
            }
        }
    } else if (mState == VIDC_STATE_PAUSE) {
        uLockState.unlock();
        if (VIDC_ERR_NONE != device_ioctl(mHandle, VIDC_IOCTL_RESUME, NULL, 0, NULL, 0)) {
            rcBool = false;
            MM_ERROR_MSG("GstClient::stateExecuting started output failed");
        } else {
            CommandType command = mQueueCompleted.pop(); // Wait for executing state
            if (COMMAND_RESUME != command) {
                MM_ERROR_MSG("GstClient::stateExecuting-%s Error waiting for resume, command %d",
                    mNamePtr, command);
                rcBool = false;
            }
        }
    } else {
        rcBool = false;
        MM_ERROR_MSG("GstClient::stateExecuting-%s Not in the correct state %d",
            mNamePtr, mState);
        uLockState.unlock();
    }

    return rcBool;
}

bool GstClient::statePause()
{
    int rcInt = VIDC_ERR_NONE;
    bool rc = false;

    std::unique_lock<std::mutex> uLockState(mStateMutex, std::defer_lock);
    uLockState.lock();
    MM_DBG_MSG("GstClient::statePause-%s state %d", mNamePtr, mState);
    if (mState != VIDC_STATE_EXECUTING) // If not in the exeucting state
    {
        MM_ERROR_MSG("GstClient::statePause-%s Not in the correct state %d",
            mNamePtr, mState);
        uLockState.unlock();
        return false;
    }
    uLockState.unlock();
    mQueueCompleted.clear();
    rcInt = device_ioctl(mHandle, VIDC_IOCTL_PAUSE, NULL, 0, NULL, 0);
    if (rcInt == VIDC_ERR_NONE) {
        CommandType command = mQueueCompleted.pop(); // Wait for executing state
        if (command == COMMAND_PAUSE) {
            rc = true;
        } else {
            MM_ERROR_MSG("GstClient::statePause-%s Error waiting for pause, command %d",
                mNamePtr, command);
        }
    } else {
        MM_ERROR_MSG("GstClient::statePause-%s Error 0x%X sending ioctl pause",
            mNamePtr, rc);
    }

    return rc;
}

bool GstClient::stateIdle()
{
    bool rc = true;
    int rcInt = VIDC_ERR_NONE;
    CommandType command;
    std::unique_lock<std::mutex> uLockState(mStateMutex, std::defer_lock);
    uLockState.lock();
    MM_DBG_MSG("GstClient::stateIdle-%s state %d", mNamePtr, mState);

    if (mState == VIDC_STATE_LOADED) {
        if (!mInitialized) {
            MM_ERROR_MSG("GstClient::stateIdle-%s Unable to transition to IDLE, not initialized",
                mNamePtr);
        }

        mState = VIDC_STATE_IDLE;
        uLockState.unlock();
    } else if (mState == VIDC_STATE_EXECUTING || mState == VIDC_STATE_PAUSE) {
        uLockState.unlock();
        MM_DBG_MSG("GstClient::stateIdle-%s current state is executing or paused"
            " and is being transitioned to IDLE", mNamePtr);

        if (mInputStarted && mOutputStarted && !mEndOfStream) {
            mQueueCompleted.clear();
            mDrainSent = true;
            rcInt = device_ioctl(mHandle, VIDC_IOCTL_DRAIN, NULL, 0, NULL, 0);
            if (rcInt != VIDC_ERR_NONE) {
                MM_ERROR_MSG("GstClient::stateIdle-%s Error 0x%X sending ioctl drain",
                    mNamePtr, rcInt);
            } else {
                MM_DBG_MSG("GstClient::stateIdle sent VIDC_IOCTL_DRAIN and wait for corresponding response");
                command = mQueueCompleted.pop(); // Wait for command to complete
                RETURN_BOOL_ON_ERROR(command == COMMAND_DRAIN,
                    "GstClient::stateIdle-%s Error waiting for drain command return, command %d",
                    mNamePtr, command);
                MM_DBG_MSG("GstClient::stateIdle VidcIoctl completed VIDC_IOCTL_DRAIN before VIDC_IOCTL_STOP");
                command = mQueueCompleted.pop(); // Wait for command to complete
                RETURN_BOOL_ON_ERROR(command == COMMAND_LAST_FLAG,
                    "GstClient::stateIdle-%s Error waiting for drain last flag command return, command %d",
                    mNamePtr, command);
            }
        }

        vidc_stop_mode_type stop_mode = VIDC_STOP_UNUSED;
        mShutdown = true;
        if (mInputStarted) {
            mQueueCompleted.clear();
            stop_mode = VIDC_STOP_INPUT;
            rcInt = device_ioctl(mHandle, VIDC_IOCTL_STOP, (uint8*)&stop_mode, sizeof(vidc_stop_mode_type), NULL, 0);
            if (rcInt != VIDC_ERR_NONE) {
                MM_ERROR_MSG("GstClient::stateIdle-%s Error 0x%X sending ioctl stop input",
                    mNamePtr, rcInt);
            } else {
                MM_DBG_MSG("GstClient::stateIdle completed VIDC_IOCTL_STOP input"
                    "::stateIdle-%s being transition to IDLE, no output buffers",
                    mNamePtr);
                command = mQueueCompleted.pop(); // Wait for command to complete
                RETURN_BOOL_ON_ERROR(command == COMMAND_INPUT_STOP,
                    "GstClient::stateIdle-%s Error waiting for stop input, command %d",
                    mNamePtr, command);
            }
        }
        if (mOutputStarted) {
            mQueueCompleted.clear();
            stop_mode = VIDC_STOP_OUTPUT;
            rcInt = device_ioctl(mHandle, VIDC_IOCTL_STOP, (uint8*)&stop_mode, sizeof(vidc_stop_mode_type), NULL, 0);
            if (rcInt != VIDC_ERR_NONE) {
                MM_ERROR_MSG("GstClient::stateIdle-%s Error 0x%X "
                    "sending ioctl stop output", mNamePtr, rcInt);
            } else {
                MM_DBG_MSG("GstClient::stateIdle completed VIDC_IOCTL_STOP output"
                    " ::stateIdle-%s being transition to IDLE, no output buffers", mNamePtr);
                command = mQueueCompleted.pop(); // Wait for command to complete
                RETURN_BOOL_ON_ERROR(command == COMMAND_OUTPUT_STOP,
                    "GstClient::stateIdle-%s Error waiting for stop output, command %d",
                    mNamePtr, command);
            }
        }
    }

    return rc;
}

bool GstClient::emptyBuffer(vidc_frame_data_type frameData)
{
    vidc_frame_data_type* frameDataPtr = &frameData;
    int rc;
    std::unique_lock<std::mutex> uLockState(mStateMutex, std::defer_lock);

    MM_DBG_MSG("GstClient::emptyBuffer-%s", mNamePtr);

    if (mShutdown == true) {
        MM_DBG_MSG("GstClient::emptyBuffer-%s drop buffer during shutdown", mNamePtr);
        return true;
    }

    uLockState.lock();
    if (mState == VIDC_STATE_LOADED) {
        MM_DBG_MSG("GstClient::emptyBuffer-%s in wrong state %d", mNamePtr, mState);
        uLockState.unlock();
        return false;
    }
    uLockState.unlock();

    RETURN_BOOL_ON_ERROR(frameDataPtr != NULL,
        "GstClient::emptyBuffer-%s buffer not found", mNamePtr);
    MM_DBG_MSG("GstClient::emptyBuffer-%s type %d, handle %d, len %d, tag %lu "
        "metadata_handle %d, metasize %d",
        mNamePtr, frameDataPtr->buf_type, frameDataPtr->frame_handle,
        frameDataPtr->data_len, frameDataPtr->input_tag,
        frameDataPtr->metadata_handle, frameDataPtr->alloc_metadata_len);

    rc = flattenMetaData(frameData);
    RETURN_BOOL_ON_ERROR(rc == VIDC_ERR_NONE,
        "GstClient::emptyBuffer-%s flattenMeta error", mNamePtr);

    rc = device_ioctl(
        mHandle,
        VIDC_IOCTL_EMPTY_INPUT_BUFFER,
        (uint8*)frameDataPtr,
        sizeof(vidc_frame_data_type),
        NULL,
        0);
    RETURN_BOOL_ON_ERROR(rc == VIDC_ERR_NONE,
        "GstClient::emptyBuffer-%s Error 0x%x", mNamePtr, rc);

    return true;
}

bool GstClient::fillBuffer(vidc_frame_data_type frameData)
{
    vidc_frame_data_type* frameDataPtr = &frameData;
    int rc;
    std::unique_lock<std::mutex> uLockState(mStateMutex, std::defer_lock);

    MM_DBG_MSG("GstClient::fillBuffer-%s", mNamePtr);

    if (mShutdown == true) {
        MM_DBG_MSG("GstClient::fillBuffer-%s drop buffer during shutdown",
            mNamePtr);
        return true;
    }

    if (!mOutputStarted) {
        MM_DBG_MSG("GstClient::fillBuffer-%s ignore buffer output not started",
            mNamePtr);
        return true;
    }

    if (!isEncoder() && !mWaitLastFlagToReconfig) {
        MM_DBG_MSG("GstClient::fillBuffer-%s ignore buffer output not reconfigured",
            mNamePtr);
        return true;
    }

    uLockState.lock();
    if (mState == VIDC_STATE_LOADED) {
        MM_DBG_MSG("GstClient::fillBuffer-%s in wrong state %d",
            mNamePtr, mState);
        uLockState.unlock();
        return false;
    }
    uLockState.unlock();

    RETURN_BOOL_ON_ERROR(frameDataPtr != NULL,
        "GstClient::fillBuffer-%s buffer not found", mNamePtr);
    MM_DBG_MSG("GstClient::fillBuffer-%s type %d, handle %d, len %d, meta_fd %d",
        mNamePtr, frameDataPtr->buf_type,
        frameDataPtr->frame_handle, frameDataPtr->data_len, frameDataPtr->metadata_handle);

    rc = device_ioctl(
        mHandle,
        VIDC_IOCTL_FILL_OUTPUT_BUFFER,
        (uint8*)frameDataPtr,
        sizeof(vidc_frame_data_type),
        NULL,
        0);
    RETURN_BOOL_ON_ERROR(rc == VIDC_ERR_NONE,
        "GstClient::fillBuffer-%s Error 0x%x", mNamePtr, rc);

    return true;
}

bool GstClient::start(vidc_buffer_type buffer)
{
    int rc = VIDC_ERR_NONE;
    vidc_start_mode_type mode = VIDC_START_UNUSED;
    CommandType commandSend;
    CommandType commandRecv;

    mQueueCompleted.clear();
    if (buffer == VIDC_BUFFER_INPUT) {
        commandSend = COMMAND_INPUT_START;
        mode = VIDC_START_INPUT;
    } else {
        commandSend = COMMAND_OUTPUT_START;
        mode = VIDC_START_OUTPUT;
    }

    rc = device_ioctl(mHandle, VIDC_IOCTL_START, (uint8*)&mode, sizeof(vidc_start_mode_type), NULL, 0);
    RETURN_BOOL_ON_ERROR(rc == VIDC_ERR_NONE,
        "GstClient::start-%s %d Error 0x%X sending ioctl start", mNamePtr, mode, rc);
    commandRecv = mQueueCompleted.pop(); // Wait for start to complete
    RETURN_BOOL_ON_ERROR(commandSend == commandRecv,
        "GstClient::start-%s %d Error waiting for complete", mNamePtr, mode);

    return true;
}

bool GstClient::stop(vidc_buffer_type buffer)
{
    int rc = VIDC_ERR_NONE;
    vidc_stop_mode_type mode = VIDC_STOP_UNUSED;
    CommandType commandSend;
    CommandType commandRecv;

    mQueueCompleted.clear();
    if (buffer == VIDC_BUFFER_INPUT) {
        commandSend = COMMAND_INPUT_STOP;
        mode = VIDC_STOP_INPUT;
    } else {
        commandSend = COMMAND_OUTPUT_STOP;
        mode = VIDC_STOP_OUTPUT;
    }

    rc = device_ioctl(mHandle, VIDC_IOCTL_STOP, (uint8*)&mode, sizeof(vidc_stop_mode_type), NULL, 0);
    RETURN_BOOL_ON_ERROR(rc == VIDC_ERR_NONE,
        "GstClient::stop-%s %d Error 0x%X sending ioctl stop", mNamePtr, mode, rc);
    commandRecv = mQueueCompleted.pop(); // Wait for stop to complete
    RETURN_BOOL_ON_ERROR(commandSend == commandRecv,
        "GstClient::stop-%s %d Error waiting for complete", mNamePtr, mode);

    return true;
}

bool GstClient::freeBuffer(vidc_frame_data_type buffer)
{
    MM_DBG_MSG("GstClient::freeBuffer-%s type: %d fd: %d",
        mNamePtr, buffer.buf_type, buffer.frame_handle);
    int rc = VIDC_ERR_NONE;
    vidc_buffer_info_type info = {};

    info.buf_type = buffer.buf_type;
    info.buf_addr = 0;
    info.buf_handle = buffer.frame_handle;
    info.extradata_buf_handle = buffer.metadata_handle;
    if (0 != info.extradata_buf_size) {
        info.contiguous = false;
    } else {
        info.contiguous = true;
    }
    rc = device_ioctl(
        mHandle,
        VIDC_IOCTL_FREE_BUFFER,
        (uint8*)&info,
        sizeof(info),
        NULL,
        0);
    if (rc != VIDC_ERR_NONE) {
        MM_ERROR_MSG("GstClient::freeBuffer-%s Error 0x%X freeing buffer",
            mNamePtr, rc);
    } else {
        MM_DBG_MSG("GstClient::freeBuffer-%s 0x%X freeing buffer",
            mNamePtr, rc);
    }

    return rc == VIDC_ERR_NONE;
}

bool GstClient::isEncoder()
{
    return mCompId == COMPONENT_ENCODER ? true : false;
}

bool GstClient::isProgressive()
{
    uint32 isProgressive = 1;

    if (mCompId == COMPONENT_DECODER) {
        bool rc = getParameter(VIDC_I_DEC_PROGRESSIVE_ONLY, &isProgressive, sizeof(uint32));
        if (rc != true)
        {
            MM_ERROR_MSG("GstClient::isProgressive-%s query scanType failed", mNamePtr);
        }

        MM_DBG_MSG("GstClient::isProgressive-%s query scanType progressive %d", mNamePtr, isProgressive);
    }

    return isProgressive ? true : false;
}

void GstClient::EmptyCallback(BaseClient* base, vidc_frame_data_type& frameData)
{
    MM_DBG_MSG("GstClient::EmptyCallback-%s", mNamePtr);
    if (frameData.flags & VIDC_FRAME_FLAG_EOS) {
        MM_DBG_MSG("GstClient::EmptyCallback-%s, EOS detected", mNamePtr);
    }

    InterlaceInfo interlaceInfo = {INTERLACE_MODE_PROGRESSIVE, true};

    mCallback->onBufferAvailable(frameData, interlaceInfo);
}

void GstClient::FilledCallback(BaseClient* base, vidc_frame_data_type& frameData)
{
    MM_DBG_MSG("GstClient::FilledCallback-%s", mNamePtr);
    if (frameData.flags & VIDC_FRAME_FLAG_EOS) {
        MM_DBG_MSG("GstClient::FilledCallback-%s, EOS detected", mNamePtr);
    }

    std::vector<std::shared_ptr<MetaInfo>> infos;

    extractMetaData(frameData, &infos);

    InterlaceInfo interlaceInfo = {INTERLACE_MODE_PROGRESSIVE, true};
    for (auto &info : infos) {
        if (info) {
            vidc_qmetapayload_header_type *header = &info->header;
            void *payload = info->payload;

            if (payload) {
                if (header) {
                    if (header->qmetadata_type == VIDC_QMETADATA_INTERLACE) {
                        vidc_qmetadata_interlace_type interlace =
                            *(vidc_qmetadata_interlace_type *) payload;
                        auto interlaceMode =
                            meta_interlace_type_table.find(interlace);
                        if (interlaceMode != meta_interlace_type_table.end()) {
                            interlaceInfo.interlaceMode = interlaceMode->second;
                        }
                        uint32 isProgressive = VIDC_SCAN_PROGRESSIVE;
                        if (getParameter(VIDC_I_DEC_PROGRESSIVE_ONLY,
                            &isProgressive, sizeof(uint32))) {
                            interlaceInfo.deinterlaced = isProgressive ? true : false;
                        }
                    }
                }

                free (payload);
            }
        }
    }

    mCallback->onBufferAvailable(frameData, interlaceInfo);
}

void GstClient::outputReconfigureCallback(BaseClient* base)
{
    MM_DBG_MSG("GstClient::outputReconfigureCallback-%s", mNamePtr);

    mCallback->onReconfig(mOutputStarted);

    MM_DBG_MSG("GstClient::outputReconfigureCallback-%s done", mNamePtr);
}

void GstClient::eosDoneCallback(BaseClient* base)
{
    MM_DBG_MSG("GstClient::eosDoneCallback-%s", mNamePtr);
}

int GstClient::getPlaneCount()
{
    return mPlaneInfo.getPlaneCount();
}

int GstClient::getPlaneBytes(int plane)
{
    return mPlaneInfo.getPlaneBytes(plane);
}

int GstClient::getPlaneHeight(int plane)
{
    return mPlaneInfo.getHeight(plane);
}

int GstClient::getPlaneWidth(int plane)
{
    return mPlaneInfo.getWidth(plane);
}

int GstClient::getPlaneStride(int plane)
{
    return mPlaneInfo.getPlaneStride(plane);
}

PlaneInfo::color_format_type GstClient::getPortColorFormat()
{
    return mPlaneInfo.getPortColorFormat();
}
