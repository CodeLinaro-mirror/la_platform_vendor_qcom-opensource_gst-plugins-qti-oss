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

Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.

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

#include "codec2wrapper.h"

#include "C2ComponentStoreAdapter.h"
#include "C2ComponentAdapter.h"
#include "C2ComponentInterfaceAdapter.h"
#include "C2WrapperUtils.h"
#include <sys/mman.h>

#include <string.h>
#include <C2PlatformSupport.h>
#include <C2Buffer.h>
#include <gst/gst.h>
#include <C2AllocatorGBM.h>
#include <ReflectedParamUpdater.h>

#ifdef USE_AGL_C2SERVICE
#include "QC2Client.h"
#endif /* USE_AGL_C2SERVICE */

GST_DEBUG_CATEGORY(gst_qcodec2_wrapper_debug);
#define GST_CAT_DEFAULT gst_qcodec2_wrapper_debug

using namespace QTI;

typedef struct {
    std::list<std::unique_ptr<C2Param> > settings;
    void* comp_intf;
} SettingsAndIntf;

struct char_cmp {
    bool operator()(const char* a, const char* b) const
    {
        return strcmp(a, b) < 0;
    }
};

// Give a comparison functor to the map to avoid comparing the pointer
typedef std::map<const char*, configFunction, char_cmp> configFunctionMap;
typedef std::map<const char*, configFunctionForVendorParams, char_cmp> configFunctionForVendorParamsMap;

// Function for C2 parameter configuration
std::unique_ptr<C2Param> setVideoPixelformat(gpointer param);
std::unique_ptr<C2Param> setVideoResolution(gpointer param);
std::unique_ptr<C2Param> setVideoBitrate(gpointer param);
std::unique_ptr<C2Param> setIntraRefresh(gpointer param);
std::unique_ptr<C2Param> setDecLowLatency(gpointer param);
std::unique_ptr<C2Param> setColorAspectsInfo(gpointer param);
std::unique_ptr<C2Param> setVideoProfileLevel(gpointer param);
std::unique_ptr<C2Param> setVideoFramerate(gpointer param);
std::unique_ptr<C2Param> setIntraframesPeriod(gpointer param);
std::unique_ptr<C2Param> setIntraVideoFrameRequest(gpointer param);
std::unique_ptr<C2Param> setVideoHeaderMode(gpointer param);
std::unique_ptr<C2Param> setVideoTemporalLayer(gpointer param);
std::unique_ptr<C2Param> setHDRStaticInfo(gpointer param);

// Function for vendor parameter configuration
std::unique_ptr<C2Param> setRateControl(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setRotation(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setMirrorType(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setOutputPictureOrderMode(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setDownscale(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setEncColorSpaceConv(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setSliceMode(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setBlurMode(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setBlurResolution(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setRoiRegion(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setBitrateSavingMode(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setInterlaceInfo(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setDeInterlace(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setIPBQPInit(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setIntraRefreshType(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setLTRCount(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setLTRMarkIndex(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setLTRUseIndex(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setVideoDynamicFramerate(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setUseExternalBuf(gpointer param, void* const comp_intf);
std::unique_ptr<C2Param> setVUITimingInfoEnable(gpointer param, void* const comp_intf);

// Function for C2 parameter configuration or vendor parameter configuration
#if GST_QPRANGE_OPTION == 0
std::unique_ptr<C2Param> setIPBQPRanges(gpointer param, void* const comp_intf);
#elif GST_QPRANGE_OPTION == 1
std::unique_ptr<C2Param> setIPBQPRanges(gpointer param);
#endif

#if GST_REPORT_FRAME_QP_OPTION == 0
std::unique_ptr<C2Param> enableGetAvgFrameQP(gpointer param, void* const comp_intf);
#elif GST_REPORT_FRAME_QP_OPTION == 1
std::unique_ptr<C2Param> enableGetAvgFrameQP(gpointer param);
#endif

// Function map for parameter configuration
static configFunctionMap sConfigFunctionMap = {
    { CONFIG_FUNCTION_KEY_PIXELFORMAT, setVideoPixelformat },
    { CONFIG_FUNCTION_KEY_RESOLUTION, setVideoResolution },
    { CONFIG_FUNCTION_KEY_BITRATE, setVideoBitrate },
    { CONFIG_FUNCTION_KEY_DEC_LOW_LATENCY, setDecLowLatency },
    { CONFIG_FUNCTION_KEY_COLOR_ASPECTS_INFO, setColorAspectsInfo },
    { CONFIG_FUNCTION_KEY_INTRAREFRESH, setIntraRefresh },
    { CONFIG_FUNCTION_KEY_PROFILE_LEVEL, setVideoProfileLevel },
    { CONFIG_FUNCTION_KEY_FRAMERATE, setVideoFramerate },
    { CONFIG_FUNCTION_KEY_INTRAFRAMES_PERIOD, setIntraframesPeriod },
    { CONFIG_FUNCTION_KEY_INTRA_VIDEO_FRAME_REQUEST, setIntraVideoFrameRequest },
    { CONFIG_FUNCTION_KEY_VIDEO_HEADER_MODE, setVideoHeaderMode },
    { CONFIG_FUNCTION_KEY_TEMPORAL_LAYER, setVideoTemporalLayer },
    { CONFIG_FUNCTION_KEY_HDR_STATIC_INFO, setHDRStaticInfo },
#if GST_QPRANGE_OPTION == 1
    { CONFIG_FUNCTION_KEY_IPB_QP_RANGE, setIPBQPRanges },
#endif
#if GST_REPORT_FRAME_QP_OPTION == 1
    { CONFIG_FUNCTION_KEY_REPORT_AVERAGE_FRAME_QP, enableGetAvgFrameQP },
#endif
};

// Function map for vendor parameter configuration
static configFunctionForVendorParamsMap sConfigFunctionForVendorParamsMap = {
    { CONFIG_FUNCTION_KEY_RATECONTROL, setRateControl },
    { CONFIG_FUNCTION_KEY_ROTATION, setRotation },
    { CONFIG_FUNCTION_KEY_MIRROR, setMirrorType },
    { CONFIG_FUNCTION_KEY_OUTPUT_PICTURE_ORDER_MODE, setOutputPictureOrderMode },
    { CONFIG_FUNCTION_KEY_DOWNSCALE, setDownscale },
    { CONFIG_FUNCTION_KEY_ENC_CSC, setEncColorSpaceConv },
    { CONFIG_FUNCTION_KEY_SLICE_MODE, setSliceMode },
    { CONFIG_FUNCTION_KEY_BLUR_MODE, setBlurMode },
    { CONFIG_FUNCTION_KEY_BLUR_RESOLUTION, setBlurResolution },
    { CONFIG_FUNCTION_KEY_ROIREGION, setRoiRegion },
    { CONFIG_FUNCTION_KEY_BITRATE_SAVING_MODE, setBitrateSavingMode },
    { CONFIG_FUNCTION_KEY_INTERLACE_INFO, setInterlaceInfo },
    { CONFIG_FUNCTION_KEY_DEINTERLACE, setDeInterlace },
#if GST_QPRANGE_OPTION == 0
    { CONFIG_FUNCTION_KEY_IPB_QP_RANGE, setIPBQPRanges },
#endif
    { CONFIG_FUNCTION_KEY_IPB_QP_INIT, setIPBQPInit },
    { CONFIG_FUNCTION_KEY_INTRAREFRESH_TYPE, setIntraRefreshType },
#if GST_REPORT_FRAME_QP_OPTION == 0
    { CONFIG_FUNCTION_KEY_REPORT_AVERAGE_FRAME_QP, enableGetAvgFrameQP },
#endif
    { CONFIG_FUNCTION_KEY_LTR_COUNT, setLTRCount },
    { CONFIG_FUNCTION_KEY_LTR_MARK_INDEX, setLTRMarkIndex },
    { CONFIG_FUNCTION_KEY_LTR_USE_INDEX, setLTRUseIndex },
    { CONFIG_FUNCTION_KEY_DYNAMIC_FRAMERATE, setVideoDynamicFramerate },
    { CONFIG_FUNCTION_KEY_EXTERNAL_BUFFER, setUseExternalBuf },
    { CONFIG_FUNCTION_KEY_VUI_TIMING_INFO, setVUITimingInfoEnable },
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Parameter Builder
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
std::unique_ptr<C2Param> setHDRStaticInfo(gpointer param)
{

    if (param == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        C2StreamHdrStaticInfo::input hdrStaticInfo;

        hdrStaticInfo.mastering.red.x = config->hdr_static_info.red_x;
        hdrStaticInfo.mastering.red.y = config->hdr_static_info.red_y;
        hdrStaticInfo.mastering.green.x = config->hdr_static_info.green_x;
        hdrStaticInfo.mastering.green.y = config->hdr_static_info.green_y;
        hdrStaticInfo.mastering.blue.x = config->hdr_static_info.blue_x;
        hdrStaticInfo.mastering.blue.y = config->hdr_static_info.blue_y;
        hdrStaticInfo.mastering.white.x = config->hdr_static_info.white_x;
        hdrStaticInfo.mastering.white.y = config->hdr_static_info.white_y;
        hdrStaticInfo.mastering.maxLuminance = config->hdr_static_info.maxLuminance;
        hdrStaticInfo.mastering.minLuminance = config->hdr_static_info.minLuminance;
        hdrStaticInfo.maxCll = config->hdr_static_info.maxCll;
        hdrStaticInfo.maxFall = config->hdr_static_info.maxFall;

        return C2Param::Copy(hdrStaticInfo);
    } else {
        C2StreamHdrStaticInfo::output hdrStaticInfo;

        hdrStaticInfo.mastering.red.x = config->hdr_static_info.red_x;
        hdrStaticInfo.mastering.red.y = config->hdr_static_info.red_y;
        hdrStaticInfo.mastering.green.x = config->hdr_static_info.green_x;
        hdrStaticInfo.mastering.green.y = config->hdr_static_info.green_y;
        hdrStaticInfo.mastering.blue.x = config->hdr_static_info.blue_x;
        hdrStaticInfo.mastering.blue.y = config->hdr_static_info.blue_y;
        hdrStaticInfo.mastering.white.x = config->hdr_static_info.white_x;
        hdrStaticInfo.mastering.white.y = config->hdr_static_info.white_y;
        hdrStaticInfo.mastering.maxLuminance = config->hdr_static_info.maxLuminance;
        hdrStaticInfo.mastering.minLuminance = config->hdr_static_info.minLuminance;
        hdrStaticInfo.maxCll = config->hdr_static_info.maxCll;
        hdrStaticInfo.maxFall = config->hdr_static_info.maxFall;

        return C2Param::Copy(hdrStaticInfo);
    }

}

std::unique_ptr<C2Param> setVideoPixelformat(gpointer param)
{

    if (param == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        C2StreamPixelFormatInfo::input inputColorFmt;

        inputColorFmt.value = toC2PixelFormat(config->pixelFormat.fmt);

        return C2Param::Copy(inputColorFmt);
    } else {
        C2StreamPixelFormatInfo::output outputColorFmt;

        outputColorFmt.value = toC2PixelFormat(config->pixelFormat.fmt);

        return C2Param::Copy(outputColorFmt);
    }
}

std::unique_ptr<C2Param> setVideoResolution(gpointer param)
{

    if (param == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        C2StreamPictureSizeInfo::input size;

        size.width = config->resolution.width;
        size.height = config->resolution.height;

        return C2Param::Copy(size);
    } else {
        C2StreamPictureSizeInfo::output size;

        size.width = config->resolution.width;
        size.height = config->resolution.height;

        return C2Param::Copy(size);
    }
}

std::unique_ptr<C2Param> setVideoBitrate(gpointer param)
{

    if (param == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        GST_WARNING("setVideoBitrate input not implemented");

    } else {
        C2StreamBitrateInfo::output bitrate;

        bitrate.value = config->val.u32;

        return C2Param::Copy(bitrate);
    }

    return nullptr;
}

std::unique_ptr<C2Param> setMirrorType(gpointer param, void* const comp_intf)
{

    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
        android::ReflectedParamUpdater::Dict kvpairs;
        android::ReflectedParamUpdater::Value item;
        std::unique_ptr<C2Param> mirror;

        item.set((int32_t)config->mirror.type);
        kvpairs.emplace("vendor.qti-ext-enc-preprocess-mirror.flip", item);

        mirror = intf_wrapper->updateParamFromConfig(kvpairs);

        return mirror;
    }
    GST_WARNING("setMirrorType output not implemented");

    return nullptr;
}

std::unique_ptr<C2Param> setRotation(gpointer param, void* const comp_intf)
{

    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
        android::ReflectedParamUpdater::Dict kvpairs;
        android::ReflectedParamUpdater::Value item;
        std::unique_ptr<C2Param> rotation;

        item.set((int32_t)config->val.u32);
        kvpairs.emplace("vendor.qti-ext-enc-preprocess-rotate.angle", item);

        rotation = intf_wrapper->updateParamFromConfig(kvpairs);

        return rotation;
    }
    GST_WARNING("setRotation output not implemented");

    return nullptr;
}

std::unique_ptr<C2Param> setRateControl(gpointer param, void* const comp_intf)
{

    if (param == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->rcMode.type == RC_LOSSLESS) {
        if (comp_intf == NULL) {
            return nullptr;
        }

        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
        android::ReflectedParamUpdater::Dict kvpairs;
        android::ReflectedParamUpdater::Value item;
        std::unique_ptr<C2Param> bitrateModeExt;

        item.set((int32_t)toC2RateControlMode(config->rcMode.type));
        kvpairs.emplace("vendor.qti-ext-enc-bitrate-mode.value", item);
        bitrateModeExt = intf_wrapper->updateParamFromConfig(kvpairs);
        return bitrateModeExt;
    }

    C2StreamBitrateModeTuning::output bitrateMode;
    bitrateMode.value = (C2Config::bitrate_mode_t)toC2RateControlMode(config->rcMode.type);
    return C2Param::Copy(bitrateMode);
}

std::unique_ptr<C2Param> setOutputPictureOrderMode(gpointer param, void* const comp_intf)
{

    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;
    C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
    std::unique_ptr<C2Param> outputPictureOrderMode;
    android::ReflectedParamUpdater::Dict kvpairs;
    android::ReflectedParamUpdater::Value item;

    if (config->output_picture_order_mode == DECODER_ORDER) {
        item.set((int32_t)C2_TRUE);
    } else {
        item.set((int32_t)C2_FALSE);
    }

    kvpairs.emplace("vendor.qti-ext-dec-picture-order.enable", item);
    outputPictureOrderMode = intf_wrapper->updateParamFromConfig(kvpairs);

    return outputPictureOrderMode;
}

std::unique_ptr<C2Param> setSliceMode(gpointer param, void* const comp_intf)
{

    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;
    C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
    std::unique_ptr<C2Param> sliceModeBytesOrMb = nullptr;
    android::ReflectedParamUpdater::Dict kvpairs;
    android::ReflectedParamUpdater::Value item;

    if (config->sliceMode.type == SLICE_MODE_BYTES) {
        item.set((int32_t)config->val.u32);
        kvpairs.emplace("vendor.qti-ext-enc-error-correction.resync-marker-spacing-bits", item);

        sliceModeBytesOrMb = intf_wrapper->updateParamFromConfig(kvpairs);
    } else if (config->sliceMode.type == SLICE_MODE_MB) {
        item.set((int32_t)config->val.u32);
        kvpairs.emplace("vendor.qti-ext-enc-slice.spacing", item);

        sliceModeBytesOrMb = intf_wrapper->updateParamFromConfig(kvpairs);
    }

    return sliceModeBytesOrMb;
}

std::unique_ptr<C2Param> setDecLowLatency(gpointer param)
{

    if (param == NULL) {
        return nullptr;
    }

    C2GlobalLowLatencyModeTuning lowLatencyMode;
    lowLatencyMode.value = C2_TRUE;

    return C2Param::Copy(lowLatencyMode);
}

std::unique_ptr<C2Param> setDownscale(gpointer param, void* const comp_intf)
{

    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput == FALSE) {

        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
        std::unique_ptr<C2Param> scale;
        android::ReflectedParamUpdater::Dict kvpairs;
        android::ReflectedParamUpdater::Value item1, item2;

        item1.set((int32_t)config->resolution.width);
        item2.set((int32_t)config->resolution.height);
        kvpairs.emplace("vendor.qti-ext-down-scalar.output-width", item1);
        kvpairs.emplace("vendor.qti-ext-down-scalar.output-height", item2);

        scale = intf_wrapper->updateParamFromConfig(kvpairs);

        return scale;
    }
    GST_WARNING("setDownscale input not implemented");

    return nullptr;
}

std::unique_ptr<C2Param> setEncColorSpaceConv(gpointer param, void* const comp_intf)
{

    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;
    C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
    std::unique_ptr<C2Param> colorSpaceConv;
    android::ReflectedParamUpdater::Dict kvpairs;
    android::ReflectedParamUpdater::Value item;
    item.set((int32_t)config->color_space_conversion);
    kvpairs.emplace("vendor.qti-ext-enc-colorspace-conversion.enable", item);

    colorSpaceConv = intf_wrapper->updateParamFromConfig(kvpairs);

    return colorSpaceConv;
}

std::unique_ptr<C2Param> setColorAspectsInfo(gpointer param)
{

    if (param == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    C2StreamColorAspectsInfo::input colorAspects;
    colorAspects.primaries = toC2Primaries(config->colorAspects.primaries);
    colorAspects.transfer = toC2TransferChar(config->colorAspects.transfer_char);
    colorAspects.matrix = toC2Matrix(config->colorAspects.matrix);
    colorAspects.range = toC2FullRange(config->colorAspects.full_range);
    return C2Param::Copy(colorAspects);
}

std::unique_ptr<C2Param> setIntraRefresh(gpointer param)
{
    if (param == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    C2StreamIntraRefreshTuning::output intraRefreshMode;
    intraRefreshMode.mode = (C2Config::intra_refresh_mode_t)config->irMode.type;
    intraRefreshMode.period = (float)config->irMode.intra_refresh_mbs;
    return C2Param::Copy(intraRefreshMode);
}

std::unique_ptr<C2Param> setIntraRefreshType(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
    std::unique_ptr<C2Param> ir_type;
    android::ReflectedParamUpdater::Dict kvpairs;
    android::ReflectedParamUpdater::Value type;
    type.set((int32_t)config->irMode.type);
    kvpairs.emplace("vendor.qti-ext-enc-intra-refresh-type.value", type);

    ir_type = intf_wrapper->updateParamFromConfig(kvpairs);

    return ir_type;
}

std::unique_ptr<C2Param> setBlurMode(gpointer param, void* const comp_intf)
{

    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
        std::unique_ptr<C2Param> blur;
        android::ReflectedParamUpdater::Dict kvpairs;
        android::ReflectedParamUpdater::Value item;
        item.set((int32_t)config->blur.mode);
        kvpairs.emplace("vendor.qti-ext-enc-blurinfo.info", item);

        blur = intf_wrapper->updateParamFromConfig(kvpairs);

        return blur;
    }
    GST_WARNING("setBlurMode output not implemented");

    return nullptr;
}

std::unique_ptr<C2Param> setBlurResolution(gpointer param, void* const comp_intf)
{

    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
        std::unique_ptr<C2Param> blur;
        android::ReflectedParamUpdater::Dict kvpairs;
        android::ReflectedParamUpdater::Value item;

        guint32 info = ((config->resolution.width << 16) | (config->resolution.height & 0xFFFF));
        item.set((int32_t)info);
        kvpairs.emplace("vendor.qti-ext-enc-blurinfo.info", item);

        blur = intf_wrapper->updateParamFromConfig(kvpairs);

        return blur;
    }
    GST_WARNING("setBlurResolution output not implemented");

    return nullptr;
}

std::unique_ptr<C2Param> setRoiRegion(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;
    C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
    std::unique_ptr<C2Param> roi = nullptr;
    android::ReflectedParamUpdater::Dict kvpairs;
    android::ReflectedParamUpdater::Value timestamp, type, payload, payloadext;

    timestamp.set((int64_t)config->roiRegion.timestampUs);
    type.set((android::AString)config->roiRegion.type);
    payload.set((android::AString)config->roiRegion.rectPayload);
    payloadext.set((android::AString)config->roiRegion.rectPayloadExt);

    kvpairs.emplace("vendor.qti-ext-enc-roiinfo.timestamp", timestamp);
    kvpairs.emplace("vendor.qti-ext-enc-roiinfo.type", type);
    kvpairs.emplace("vendor.qti-ext-enc-roiinfo.rect-payload", payload);
    kvpairs.emplace("vendor.qti-ext-enc-roiinfo.rect-payload-ext", payloadext);

    roi = intf_wrapper->updateParamFromConfig(kvpairs);

    return roi;
}

std::unique_ptr<C2Param> setBitrateSavingMode(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL)
        return nullptr;

    ConfigParams* config = (ConfigParams*)param;
    if (config->isInput) {
        GST_WARNING("setVideoBitrateSavingMode input not implemented");

    } else {
        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
        std::unique_ptr<C2Param> bitrateSavingMode;
        android::ReflectedParamUpdater::Dict kvpairs;
        android::ReflectedParamUpdater::Value item;
        item.set((int32_t)config->bitrate_saving_mode.saving_mode);
        kvpairs.emplace("vendor.qti-ext-enc-content-adaptive-mode.value", item);

        bitrateSavingMode = intf_wrapper->updateParamFromConfig(kvpairs);

        return bitrateSavingMode;
    }
    return nullptr;
}

std::unique_ptr<C2Param> setVideoProfileLevel(gpointer param)
{
    if (param == NULL)
        return nullptr;

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        GST_WARNING("setVideoProfileLevel input not implemented");
    } else {
        C2StreamProfileLevelInfo::output profileAndLevel;
        profileAndLevel.profile = toC2Profile(config->profileAndLevel.profile);
        profileAndLevel.level = toC2Level(config->profileAndLevel.level);

        return C2Param::Copy(profileAndLevel);
    }

    return nullptr;
}

std::unique_ptr<C2Param> setInterlaceInfo(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;
    C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
    std::unique_ptr<C2Param> interlaceInfo;
    android::ReflectedParamUpdater::Dict kvpairs;
    android::ReflectedParamUpdater::Value item;

    item.set((int32_t)config->interlaceMode.type);
    kvpairs.emplace("vendor.qti-ext-dec-info-interlace.format", item);

    interlaceInfo = intf_wrapper->updateParamFromConfig(kvpairs);

    return interlaceInfo;
}

std::unique_ptr<C2Param> setDeInterlace(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;
    C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
    std::unique_ptr<C2Param> deinterlace;
    android::ReflectedParamUpdater::Dict kvpairs;
    android::ReflectedParamUpdater::Value item;

    item.set((int32_t)config->deinterlace);
    kvpairs.emplace("vendor.qti-ext-dec-deinterlace.enable", item);
    GST_DEBUG("enable deinterlace:%d", config->deinterlace);

    deinterlace = intf_wrapper->updateParamFromConfig(kvpairs);

    return deinterlace;
}

std::unique_ptr<C2Param> setVideoFramerate(gpointer param)
{
    if (param == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        C2StreamFrameRateInfo::input framerate;
        framerate.value = config->framerate;
        return C2Param::Copy(framerate);
    } else {
        C2StreamFrameRateInfo::output framerate;
        framerate.value = config->framerate;
        return C2Param::Copy(framerate);
    }

    return nullptr;
}

std::unique_ptr<C2Param> setVideoDynamicFramerate(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;
    C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
    std::unique_ptr<C2Param> dyn_fps;
    android::ReflectedParamUpdater::Dict kvpairs;
    android::ReflectedParamUpdater::Value fps;

    fps.set((int32_t)(config->framerate * 65536.0f));
    kvpairs.emplace("vendor.qti-ext-enc-dynamic-frame-rate.frame-rate", fps);

    dyn_fps = intf_wrapper->updateParamFromConfig(kvpairs);

    return dyn_fps;
}

std::unique_ptr<C2Param> setIntraframesPeriod(gpointer param)
{
    if (param == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        GST_WARNING("setIntraframesPeriod input not implemented");
    } else {
        C2StreamSyncFrameIntervalTuning::output syncFrameInterval;
        syncFrameInterval.value = config->val.i64;
        return C2Param::Copy(syncFrameInterval);
    }

    return nullptr;
}

std::unique_ptr<C2Param> setIntraVideoFrameRequest(gpointer param)
{
    if (param == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        GST_WARNING("setIntraVideoFrameRequest input not implemented");
    } else if (config->force_idr) {
        C2StreamRequestSyncFrameTuning::output frameRequest;
        frameRequest.value = (uint32_t)C2Config::SYNC_FRAME;
        return C2Param::Copy(frameRequest);
    }

    return nullptr;
}

std::unique_ptr<C2Param> setVideoHeaderMode(gpointer param)
{
    if (param == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    C2PrependHeaderModeSetting header_mode;
    header_mode.value = config->inline_sps_pps_headers ? C2Config::PREPEND_HEADER_TO_ALL_SYNC : C2Config::PREPEND_HEADER_TO_NONE;

    return C2Param::Copy(header_mode);
}

std::unique_ptr<C2Param> setVideoTemporalLayer(gpointer param)
{
    if (param == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    auto pTemporalLayering = C2StreamTemporalLayeringTuning::output::AllocUnique(
        config->temporalLayer.ratioSize);
    pTemporalLayering->m.layerCount = config->temporalLayer.layerCount;
    pTemporalLayering->m.bLayerCount = config->temporalLayer.bLayerCount;

    if (config->temporalLayer.ratios) {
        for (uint32_t i = 0; i < config->temporalLayer.ratioSize; i++) {
            pTemporalLayering->m.bitrateRatios[i] = config->temporalLayer.ratios[i];
            GST_LOG("bitrateRatios[%d]=%.2f", i, config->temporalLayer.ratios[i]);
        }
    }

    return C2Param::Copy(*pTemporalLayering);
}

#if GST_QPRANGE_OPTION == 0
std::unique_ptr<C2Param> setIPBQPRanges(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;
    C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
    std::unique_ptr<C2Param> qp_ranges;
    android::ReflectedParamUpdater::Dict kvpairs;
    android::ReflectedParamUpdater::Value min_i_qp, max_i_qp, min_p_qp, max_p_qp, min_b_qp, max_b_qp;

    min_i_qp.set((int32_t)config->qp_ranges.min_i_qp);
    max_i_qp.set((int32_t)config->qp_ranges.max_i_qp);
    min_p_qp.set((int32_t)config->qp_ranges.min_p_qp);
    max_p_qp.set((int32_t)config->qp_ranges.max_p_qp);
    min_b_qp.set((int32_t)config->qp_ranges.min_b_qp);
    max_b_qp.set((int32_t)config->qp_ranges.max_b_qp);

    kvpairs.emplace("vendor.qti-ext-enc-qp-range.qp-i-min", min_i_qp);
    kvpairs.emplace("vendor.qti-ext-enc-qp-range.qp-i-max", max_i_qp);
    kvpairs.emplace("vendor.qti-ext-enc-qp-range.qp-p-min", min_p_qp);
    kvpairs.emplace("vendor.qti-ext-enc-qp-range.qp-p-max", max_p_qp);
    kvpairs.emplace("vendor.qti-ext-enc-qp-range.qp-b-min", min_b_qp);
    kvpairs.emplace("vendor.qti-ext-enc-qp-range.qp-b-max", max_b_qp);

    qp_ranges = intf_wrapper->updateParamFromConfig(kvpairs);

    return qp_ranges;
}
#elif GST_QPRANGE_OPTION == 1
std::unique_ptr<C2Param> setIPBQPRanges(gpointer param)
{
    if (param == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;
    auto qp_ranges = C2StreamPictureQuantizationTuning::output::AllocUnique(3, 0u);

    qp_ranges->m.values[0] = { I_FRAME, (int32_t)config->qp_ranges.min_i_qp, (int32_t)config->qp_ranges.max_i_qp };
    qp_ranges->m.values[1] = { P_FRAME, (int32_t)config->qp_ranges.min_p_qp, (int32_t)config->qp_ranges.max_p_qp };
    qp_ranges->m.values[2] = { B_FRAME, (int32_t)config->qp_ranges.min_b_qp, (int32_t)config->qp_ranges.max_b_qp };

    return C2Param::Copy(*qp_ranges);
}
#endif

std::unique_ptr<C2Param> setIPBQPInit(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;
    C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
    std::unique_ptr<C2Param> qp_init;
    android::ReflectedParamUpdater::Dict kvpairs;
    android::ReflectedParamUpdater::Value qp_i, qp_i_enable, qp_p, qp_p_enable, qp_b, qp_b_enable;

    qp_i.set((int32_t)config->qp_init.quant_i_frames);
    qp_i_enable.set((int32_t)config->qp_init.quant_i_frames_enable);
    qp_p.set((int32_t)config->qp_init.quant_p_frames);
    qp_p_enable.set((int32_t)config->qp_init.quant_p_frames_enable);
    qp_b.set((int32_t)config->qp_init.quant_b_frames);
    qp_b_enable.set((int32_t)config->qp_init.quant_b_frames_enable);

    kvpairs.emplace("vendor.qti-ext-enc-initial-qp.qp-i", qp_i);
    kvpairs.emplace("vendor.qti-ext-enc-initial-qp.qp-i-enable", qp_i_enable);
    kvpairs.emplace("vendor.qti-ext-enc-initial-qp.qp-p", qp_p);
    kvpairs.emplace("vendor.qti-ext-enc-initial-qp.qp-p-enable", qp_p_enable);
    kvpairs.emplace("vendor.qti-ext-enc-initial-qp.qp-b", qp_b);
    kvpairs.emplace("vendor.qti-ext-enc-initial-qp.qp-b-enable", qp_b_enable);

    qp_init = intf_wrapper->updateParamFromConfig(kvpairs);

    return qp_init;
}

#if GST_REPORT_FRAME_QP_OPTION == 0
std::unique_ptr<C2Param> enableGetAvgFrameQP(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
    std::unique_ptr<C2Param> avg_qp;
    android::ReflectedParamUpdater::Dict kvpairs;
    android::ReflectedParamUpdater::Value frame_qp;

    frame_qp.set((int32_t)0);
    kvpairs.emplace("vendor.qti-ext-enc-info-coded_avgqp.frameQP", frame_qp);

    avg_qp = intf_wrapper->updateParamFromConfig(kvpairs);

    return avg_qp;
}
#elif GST_REPORT_FRAME_QP_OPTION == 1
std::unique_ptr<C2Param> enableGetAvgFrameQP(gpointer param)
{
    if (param == NULL) {
        return nullptr;
    }

    C2AndroidStreamAverageBlockQuantizationInfo::output avg_frame_qp;

    avg_frame_qp.value = 0;

    return C2Param::Copy(avg_frame_qp);
}
#endif

std::unique_ptr<C2Param> setLTRCount(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
        std::unique_ptr<C2Param> ltrcount = nullptr;
        android::ReflectedParamUpdater::Dict kvpairs;
        android::ReflectedParamUpdater::Value item;

        GST_LOG("ltrCount %d", config->ltr.count);
        item.set((int32_t)config->ltr.count);
        kvpairs.emplace("vendor.qti-ext-enc-ltr-count.num-ltr-frames", item);
        ltrcount = intf_wrapper->updateParamFromConfig(kvpairs);

        return ltrcount;
    } else {
        GST_WARNING("%s output not implemented", __FUNCTION__);
    }

    return nullptr;
}

std::unique_ptr<C2Param> setLTRMarkIndex(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
        std::unique_ptr<C2Param> ltrmark = nullptr;
        android::ReflectedParamUpdater::Dict kvpairs;
        android::ReflectedParamUpdater::Value item;

        GST_LOG("ltrMarkIndex %d", config->ltr.mark_index);
        item.set((int32_t)config->ltr.mark_index);
        kvpairs.emplace("vendor.qti-ext-enc-ltr.mark-frame", item);
        ltrmark = intf_wrapper->updateParamFromConfig(kvpairs);

        return ltrmark;
    } else {
        GST_WARNING("%s output not implemented", __FUNCTION__);
    }

    return nullptr;
}

std::unique_ptr<C2Param> setLTRUseIndex(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;

    if (config->isInput) {
        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
        std::unique_ptr<C2Param> ltruse = nullptr;
        android::ReflectedParamUpdater::Dict kvpairs;
        android::ReflectedParamUpdater::Value item;

        GST_LOG("ltrUseIndex %d", config->ltr.use_index);
        item.set((int32_t)config->ltr.use_index);
        kvpairs.emplace("vendor.qti-ext-enc-ltr.use-frame", item);
        ltruse = intf_wrapper->updateParamFromConfig(kvpairs);

        return ltruse;
    } else {
        GST_WARNING("%s output not implemented", __FUNCTION__);
    }

    return nullptr;
}

std::unique_ptr<C2Param> setUseExternalBuf(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;
    C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
    std::unique_ptr<C2Param> use_external_buf;
    android::ReflectedParamUpdater::Dict kvpairs;
    android::ReflectedParamUpdater::Value item;

    item.set((int32_t)config->use_external_buf);
    kvpairs.emplace("vendor.qti-ext-dec-external-buf.enable", item);
    GST_DEBUG("set to use external buffer:%d", config->use_external_buf);

    use_external_buf = intf_wrapper->updateParamFromConfig(kvpairs);

    return use_external_buf;
}

std::unique_ptr<C2Param> setVUITimingInfoEnable(gpointer param, void* const comp_intf)
{
    if (param == NULL || comp_intf == NULL) {
        return nullptr;
    }

    ConfigParams* config = (ConfigParams*)param;
    C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
    std::unique_ptr<C2Param> vui_timing_info_param;
    android::ReflectedParamUpdater::Dict kvpairs;
    android::ReflectedParamUpdater::Value enable_item;

    enable_item.set((int32_t)config->vui_timing_info.enable);
    kvpairs.emplace("vendor.qti-ext-enc-vui-timing-info.value", enable_item);
    GST_DEBUG("set vui timing info enable : %d", config->vui_timing_info.enable);

    vui_timing_info_param = intf_wrapper->updateParamFromConfig(kvpairs);

    return vui_timing_info_param;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CodecCallback API handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CodecCallback : public EventCallback {
public:
    CodecCallback(const void* handle, listener_cb cb);
    ~CodecCallback();

    void onOutputBufferAvailable(
        const std::shared_ptr<C2Buffer>& buffer,
        uint64_t index,
        uint64_t timestamp,
        InterlaceInfo &interlaceInfo,
        uint32_t frameQp,
        C2FrameData::flags_t flag) override;
    void onTripped(uint32_t errorCode) override;
    void onError(uint32_t errorCode) override;
    void onUpdateMaxBufCount(uint32_t outputDelay) override;
    void onAcquireExtBuffer(uint32_t width, uint32_t height, bool isC2D) override;
    void onReleaseExtBuffer(int32_t extFd) override;
    void onReleaseInputBuffer(uint64_t index) override;

private:
    listener_cb mCallback;
    const void* mHandle;
};

CodecCallback::CodecCallback(const void* handle, listener_cb cb)
{

    GST_LOG("CodecCallback(%p) created", this);

    mCallback = cb;
    mHandle = handle;
}

CodecCallback::~CodecCallback()
{

    GST_LOG("CodecCallback(%p) destroyed", this);
}

// set buffer's layout information
// 1. set buffer valid size
// 2. set buffer stride and offset
void setBufLayout(BufferDescriptor* buf, uint32_t format, uint64_t gbm_usage,
    uint32_t width, uint32_t height, uint32_t interlace_mode)
{
    uint32_t validDataSize = 0;
    uint32_t color_fmt = 0;
    uint32_t y_stride, uv_stride, y_sclines, uv_sclines;
    uint32_t y_meta_plane, y_ubwc_plane, y_meta_stride, y_meta_scanlines;
    const char* color_fmt_str = "NULL";
    uint32_t offset[2] = { 0 };
    uint32_t stride[2] = { 0 };

    if (!buf) {
        SG_ERR("invalid buffer");
        return;
    }

    if (format == GBM_FORMAT_NV12 && (gbm_usage & GBM_BO_USAGE_UBWC_ALIGNED_QTI)) {
        color_fmt = COLOR_FMT_NV12_UBWC;
        color_fmt_str = "NV12_UBWC";
    } else if (format == GBM_FORMAT_YCbCr_420_TP10_UBWC) {
        color_fmt = COLOR_FMT_NV12_BPP10_UBWC;
        color_fmt_str = "TP10";
    } else {
        if (format == GBM_FORMAT_NV12) {
            color_fmt = COLOR_FMT_NV12;
            color_fmt_str = "NV12";
        } else if (format == GBM_FORMAT_P010) {
            color_fmt = COLOR_FMT_P010;
            color_fmt_str = "P010";
        } else {
            SG_ERR("format 0x%x is not implemented", format);
            return;
        }
    }

    y_stride = VENUS_Y_STRIDE(color_fmt, width);
    uv_stride = VENUS_UV_STRIDE(color_fmt, width);
    y_sclines = VENUS_Y_SCANLINES(color_fmt, height);
    uv_sclines = VENUS_UV_SCANLINES(color_fmt, height);

    switch (color_fmt) {
    case COLOR_FMT_NV12:
    case COLOR_FMT_P010:
        validDataSize = y_stride * y_sclines + uv_stride * uv_sclines;
        offset[1] = y_stride * y_sclines;
        break;
    case COLOR_FMT_NV12_UBWC:
        y_meta_stride = VENUS_Y_META_STRIDE(color_fmt, width);
        y_meta_scanlines = VENUS_Y_META_SCANLINES(color_fmt, height);
        y_meta_plane = GST_ROUND_UP_N(y_meta_stride * y_meta_scanlines, 4096);
        y_ubwc_plane = GST_ROUND_UP_N(y_stride * y_sclines, 4096);

        if (interlace_mode != INTERLACE_MODE_PROGRESSIVE) {
            validDataSize = VENUS_BUFFER_SIZE_USED(color_fmt, width, height, 1);
            offset[1] = VENUS_BUFFER_SIZE(color_fmt, width, height) / 2;
            GST_DEBUG("output format is NV12_UBWC interlaced");
        } else {
            validDataSize = VENUS_BUFFER_SIZE_USED(color_fmt, width, height, 0);
            offset[1] = y_meta_plane + y_ubwc_plane;
        }
        break;
    case COLOR_FMT_NV12_BPP10_UBWC:
        validDataSize = VENUS_BUFFER_SIZE(color_fmt, width, height);
        y_meta_stride = VENUS_Y_META_STRIDE(color_fmt, width);
        y_meta_scanlines = VENUS_Y_META_SCANLINES(color_fmt, height);
        y_meta_plane = GST_ROUND_UP_N(y_meta_stride * y_meta_scanlines, 4096);
        y_ubwc_plane = GST_ROUND_UP_N(y_stride * y_sclines, 4096);
        offset[1] = y_meta_plane + y_ubwc_plane;
        break;
    default:
        SG_ERR("color format 0x%x is not implemented", color_fmt);
        break;
    }

    stride[0] = y_stride;
    stride[1] = uv_stride;
    buf->size = validDataSize;
    for (int i = 0; i < 2; i++) {
        buf->offset[i] = offset[i];
        buf->stride[i] = stride[i];
    }

    GST_LOG("format:%s Y stride:%u Y scanline:%u UV stride:%u  offset[0:1]:[%d:%d] valid size:%u",
        color_fmt_str, y_stride, y_sclines, uv_stride, offset[0], offset[1], validDataSize);
}

void CodecCallback::onOutputBufferAvailable(
    const std::shared_ptr<C2Buffer>& buffer,
    uint64_t index,
    uint64_t timestamp,
    InterlaceInfo &interlaceInfo,
    uint32_t frameQp,
    C2FrameData::flags_t flag)
{

    if (!mCallback) {
        GST_LOG("Callback not set in CodecCallback(%p)", this);
        return;
    }

    BufferDescriptor outBuf;
    memset(&outBuf, 0, sizeof(BufferDescriptor));

    if (buffer) {
        C2BufferData::type_t buf_type = buffer->data().type();
        outBuf.timestamp = timestamp;
        outBuf.index = index;
        outBuf.flag = toWrapperFlag(flag);
        outBuf.interlaceMode = interlaceInfo.interlaceMode;
        outBuf.deinterlaced = interlaceInfo.deinterlaced;

        if (buf_type == C2BufferData::GRAPHIC) {
            const C2ConstGraphicBlock graphic_block = buffer->data().graphicBlocks().front();
            C2Handle* handle = const_cast<C2Handle*>(graphic_block.handle());
            if (nullptr == handle) {
                SG_ERR("C2ConstGraphicBlock handle is null");
                return;
            }
            outBuf.fd = handle->data[0];
            outBuf.meta_fd = handle->data[1];
#ifndef USE_AGL_C2SERVICE
            outBuf.ext_fd = handle->data[2];
#else
            auto extraData = C2HandleGBM::getExtraData(handle);
            outBuf.ext_fd = extraData != nullptr ? extraData->idx : -1;
#endif
            /* Since this buffer will be pushed downstream, set data[15] which represents
             * need_free_ext_buf to 0, refer to ExtraData definition in C2AllocatorGBM.h */
            handle->data[15] = 0;
            outBuf.c2Buffer = static_cast<void*>(buffer.get());
            guint32 stride = 0;
            guint64 usage = 0;
            guint32 size = 0;
            guint32 format = 0;
            guint64 bo = 0;
            guint32 width = 0;
            guint32 height = 0;
            guint32 gbmUsage = 0;
            C2Rect crop;
            const C2GraphicView view = graphic_block.map().get();

            _UnwrapNativeCodec2GBMMetadata(handle, &width, &height, &format, &usage, &stride, &size, &bo);

            C2MemoryUsageGBM c2GbmUsage(usage);
            gbmUsage = c2GbmUsage.gbmUsage();

            /* The actual value of bo here is a pointer to struct gbm_bo.
             * To avoid including GBM header, use void* instead. */
            outBuf.gbm_bo = reinterpret_cast<void*>(bo);
            crop = view.crop();
            GST_LOG("get crop info (%d,%d) [%dx%d] bo:%p", crop.left, crop.top, crop.width, crop.height, outBuf.gbm_bo);
            outBuf.width = crop.width;
            outBuf.height = crop.height;
            setBufLayout(&outBuf, format, gbmUsage, width, height, outBuf.interlaceMode);

            /* graphic_block unmapped once out of scope. */
            mCallback(mHandle, EVENT_OUTPUTS_DONE, &outBuf);
            GST_DEBUG("out buffer size:%d(valid %u) gbm's width:%d height:%d stride:%d data:%p\n",
                size, outBuf.size, width, height, stride, outBuf.data);
        } else if (buf_type == C2BufferData::LINEAR) {
            const C2ConstLinearBlock linear_block = buffer->data().linearBlocks().front();
            const C2Handle* handle = linear_block.handle();
            if (nullptr == handle) {
                SG_ERR("C2ConstLinearBlock handle is null");
                return;
            }
            C2ReadView view(linear_block.map().get());
            outBuf.size = linear_block.size();
            outBuf.fd = handle->data[0];
            outBuf.data = (guint8*)view.data();
            outBuf.avg_frame_qp = frameQp;
            GST_DEBUG("outBuf linear data:%p fd:%d size:%d\n", outBuf.data, outBuf.fd, outBuf.size);
            /* Check for codec data */
            auto csd = std::static_pointer_cast<const C2StreamInitDataInfo::output>(
                buffer->getInfo(C2StreamInitDataInfo::output::PARAM_TYPE));
            if (csd) {
                GST_INFO("get codec config data, size: %lu data:%p", csd->flexCount(), (guint8*)csd->m.value);
                outBuf.config_data = (guint8*)&csd->m.value;
                outBuf.config_size = csd->flexCount();
                outBuf.flag = (FLAG_TYPE)(outBuf.flag | FLAG_TYPE_CODEC_CONFIG);
            }
            mCallback(mHandle, EVENT_OUTPUTS_DONE, &outBuf);
        }
    } else if (flag & C2FrameData::FLAG_DROP_FRAME) {
        outBuf.timestamp = timestamp;
        outBuf.index = index;
        outBuf.flag = toWrapperFlag(flag);
        outBuf.interlaceMode = interlaceInfo.interlaceMode;
        GST_LOG("Mark drop frame index: %lu, pts: %lu", index, timestamp);
        mCallback(mHandle, EVENT_DROP_FRAME, &outBuf);
    } else if (flag & C2FrameData::FLAG_END_OF_STREAM) {
        GST_LOG("Mark EOS buffer");
        outBuf.data = NULL;
        outBuf.fd = -1;
        outBuf.meta_fd = -1;
        outBuf.size = 0;
        outBuf.capacity = 0;
        outBuf.offset[0] = 0;
        outBuf.timestamp = 0;
        outBuf.index = 0;
        outBuf.flag = toWrapperFlag(flag);

        mCallback(mHandle, EVENT_OUTPUTS_DONE, &outBuf);
    } else {
        GST_LOG("Buffer is null");
    }
}

void CodecCallback::onTripped(uint32_t errorCode)
{

    if (!mCallback) {
        GST_LOG("Callback not set in CodecCallback(%p)", this);
        return;
    }

    mCallback(mHandle, EVENT_TRIPPED, &errorCode);
}

void CodecCallback::onError(uint32_t errorCode)
{

    if (!mCallback) {
        GST_LOG("Callback not set in CodecCallback(%p)", this);
        return;
    }

    mCallback(mHandle, EVENT_ERROR, &errorCode);
}

void CodecCallback::onUpdateMaxBufCount(uint32_t outputDelay)
{

    if (!mCallback) {
        GST_LOG("Callback not set in CodecCallback(%p)", this);
        return;
    }

    mCallback(mHandle, EVENT_UPDATE_MAX_BUF_CNT, &outputDelay);
}

void CodecCallback::onAcquireExtBuffer(uint32_t width, uint32_t height, bool isC2D)
{

    if (!mCallback) {
        GST_LOG("Callback not set in CodecCallback(%p)", this);
        return;
    }

    AcquireExtBufInfo acquireInfo;
    acquireInfo.resolution.width = width;
    acquireInfo.resolution.height = height;
    acquireInfo.is_c2d = isC2D;

    mCallback(mHandle, EVENT_ACQUIRE_EXT_BUF, &acquireInfo);
}

void CodecCallback::onReleaseExtBuffer(int32_t extFd)
{
    if (!mCallback) {
        GST_LOG("Callback not set in CodecCallback(%p)", this);
        return;
    }

    mCallback(mHandle, EVENT_RELEASE_EXT_BUF, &extFd);
}

void CodecCallback::onReleaseInputBuffer(uint64_t index)
{
    if (!mCallback) {
        GST_LOG("Callback not set in CodecCallback(%p)", this);
        return;
    }

    mCallback(mHandle, EVENT_RELEASE_INPUT_BUF, &index);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ComponentStore API handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void* c2componentStore_create()
{

    GST_DEBUG_CATEGORY_INIT(gst_qcodec2_wrapper_debug,
        "qcodec2wrapper", 0, "GST QTI codec2.0 wrapper");

    GST_LOG("Creating component store");

#ifndef USE_AGL_C2SERVICE
    void* lib = dlopen("libqcodec2_core.so", RTLD_NOW);
    if (lib == nullptr) {
        SG_ERR("failed to open %s: %s", "libqcodec2_core.so", dlerror());
        return nullptr;
    }

    auto factoryGetter = (QC2ComponentStoreFactoryGetter_t)dlsym(lib, kFn_QC2ComponentStoreFactoryGetter);

    if (factoryGetter == nullptr) {
        SG_ERR("failed to load symbol %s: %s", kFn_QC2ComponentStoreFactoryGetter, dlerror());
        dlclose(lib);
        return nullptr;
    }

    auto c2StoreFactory = (*factoryGetter)(1, 0); // get version 1.0
    if (c2StoreFactory == nullptr) {
        SG_ERR("failed to get Store factory !");
        dlclose(lib);
        return nullptr;
    }

    std::shared_ptr<C2ComponentStore> store = c2StoreFactory->getInstance();

    return new C2ComponentStoreAdapter(store, c2StoreFactory, lib);

#else
    // Enabled death notifier on client side. When server died, client can receive message.
    auto client = aglqc2::QC2Client::create();
    auto store = client->getC2ComponentStore();
    GST_LOG("Created component store %p", store.get());

    return new C2ComponentStoreAdapter(store, nullptr, nullptr);
#endif /* USE_AGL_C2SERVICE */
}

const gchar* c2componentStore_getName(void* const comp_store)
{

    gchar* name = NULL;

    if (comp_store) {
        C2ComponentStoreAdapter* store_Wrapper = (C2ComponentStoreAdapter*)comp_store;
        name = g_strdup(store_Wrapper->getName().c_str());
    } else {
        SG_ERR("Component store is null");
    }

    return name;
}

gboolean c2componentStore_createComponent(void* const comp_store, const gchar* name, void** const component, comp_cb* cb)
{

    GST_LOG("Creating component");

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp_store) {
        C2ComponentStoreAdapter* store_Wrapper = (C2ComponentStoreAdapter*)comp_store;

        c2Status = store_Wrapper->createComponent(C2String(name), component);
        if (c2Status == C2_OK) {
            C2ComponentAdapter* comp_adapter = *(C2ComponentAdapter**)component;
            if (cb) {
                GST_DEBUG("comp name:%s, set func for copying", name);
                comp_adapter->setDataCopyFunc(cb->data_copy_func, cb->data_copy_func_param);
            }
            ret = TRUE;
        } else {
            SG_ERR("Failed(%d) to create component (%s)", c2Status, name);
        }
    } else {
        SG_ERR("Component store is null");
    }

    return ret;
}

gboolean c2componentStore_createInterface(void* const comp_store, const gchar* name, void** const interface)
{

    GST_LOG("Creating component interface");

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp_store) {
        C2ComponentStoreAdapter* store_Wrapper = (C2ComponentStoreAdapter*)comp_store;

        c2Status = store_Wrapper->createInterface(C2String(name), interface);
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            SG_ERR("Failed(%d) to create component interface (%s)", c2Status, name);
        }
    } else {
        SG_ERR("Component store is null");
    }

    return ret;
}

gboolean c2componentStore_listComponents(void* const comp_store, GPtrArray* array)
{
    gboolean ret = FALSE;
    GST_LOG("%s", __func__);

    if (comp_store) {
        C2ComponentStoreAdapter* store_Wrapper = (C2ComponentStoreAdapter*)comp_store;

        std::vector<std::shared_ptr<const C2Component::Traits> > traits = store_Wrapper->listComponents();
        GST_LOG("component traits size %zu", traits.size());
        for (auto &trait : traits) {
            const char *name = trait->name.c_str();
            GST_LOG("trait name: %s", name);
            g_ptr_array_add(array, (gpointer)name);
        }
        ret = TRUE;
    }

    return ret;
}

gboolean c2componentStore_isComponentSupported(void* const comp_store, gchar* name)
{
    gboolean ret = FALSE;

    if (comp_store) {
        C2ComponentStoreAdapter* store_Wrapper = (C2ComponentStoreAdapter*)comp_store;

        bool ret = store_Wrapper->isComponentSupported(name);
        if (ret == true)
            return TRUE;
    }

    return ret;
}

gboolean c2componentStore_delete(void* comp_store)
{

    GST_LOG("Deleting component store");

    gboolean ret = FALSE;

    if (comp_store) {
        C2ComponentStoreAdapter* store_Wrapper = (C2ComponentStoreAdapter*)comp_store;

        delete store_Wrapper;
        ret = TRUE;
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Component API handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
gboolean c2component_setListener(void* const comp, void* cb_context, listener_cb listener, BLOCK_MODE_TYPE mayBlock)
{

    GST_LOG("Updating component listener");

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;
        std::unique_ptr<EventCallback> callback = std::make_unique<CodecCallback>(cb_context, listener);

        c2Status = comp_wrapper->setListenercallback(std::move(callback), toC2BlocingType(mayBlock));
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            SG_ERR("Failed(%d) to set component listener is null", c2Status);
        }
    } else {
        SG_ERR("Component is null");
    }

    return ret;
}

gboolean c2component_setMaxAllocationCount(void* const comp, BUFFER_POOL_TYPE type, guint max)
{

    GST_LOG("Comp %p set max: %u type: %d", comp, max, type);
    c2_status_t status = C2_BAD_VALUE;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;
        status = comp_wrapper->setMaxAllocationCount((uint32_t)max, type);
    }

    return C2_OK == status ? TRUE : FALSE;
}

guint c2component_getMaxAllocationCount(void* const comp, BUFFER_POOL_TYPE type)
{
    uint32_t count = 0;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;
        count = comp_wrapper->getMaxAllocationCount(type);
    }

    GST_LOG("Comp %p get max: %u type: %d", comp, count, type);

    return (guint)count;
}

gboolean c2component_alloc(void* const comp, BufferDescriptor* buffer)
{

    GST_LOG("Comp %p allocate buffer type: %d", comp, buffer->pool_type);

    gboolean ret = FALSE;
    std::shared_ptr<C2Buffer> buf = nullptr;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;

        buf = comp_wrapper->alloc(buffer);

        if (buf != nullptr) {
            if (buffer->pool_type == BUFFER_POOL_BASIC_GRAPHIC) {
                ret = TRUE;
            } else {
                SG_ERR("Unsupported pool type: %d", buffer->pool_type);
            }
        } else {
            SG_ERR("Failed to alloc buffer");
        }
    } else {
        SG_ERR("Component is null");
    }

    return ret;
}

gboolean c2component_queue(void* const comp, BufferDescriptor* buffer)
{

    GST_LOG("Queueing work");

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;

        c2Status = comp_wrapper->queue(buffer);

        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            SG_ERR("Failed to queue work (%d)", c2Status);
        }
    } else {
        SG_ERR("Component is null");
    }

    return ret;
}

gboolean c2component_flush(void* const comp, FLUSH_MODE_TYPE mode)
{
    GST_LOG("Flushing work");

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;
        c2Status = comp_wrapper->flush(toC2FlushMode(mode));
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            SG_ERR("Failed to flush work (%d)", c2Status);
        }
    } else {
        SG_ERR("Component is null");
    }

    return ret;
}

gboolean c2component_drain(void* const comp, DRAIN_MODE_TYPE mode)
{

    GST_LOG("Draining work");

    gboolean ret = FALSE;

    if (comp) {
        GST_LOG("Not implemented");
    }

    return ret;
}

gboolean c2component_start(void* const comp)
{

    GST_LOG("Starting component");

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;

        c2Status = comp_wrapper->start();
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            SG_ERR("Failed(%d) to start component", c2Status);
        }
    } else {
        SG_ERR("Component is null");
    }

    return ret;
}

gboolean c2component_stop(void* const comp)
{

    GST_LOG("Stopping component");

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp) {

        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;
        c2Status = comp_wrapper->stop();
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            SG_ERR("Failed(%d) to stop component", c2Status);
        }
    } else {
        SG_ERR("Component is null");
    }

    return ret;
}

gboolean c2component_reset(void* const comp)
{

    GST_LOG("Resetting component");

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;

        c2Status = comp_wrapper->reset();
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            SG_ERR("Failed(%d) to reset component", c2Status);
        }
    } else {
        SG_ERR("Component is null");
    }

    return ret;
}

gboolean c2component_release(void* const comp)
{

    GST_LOG("Releasing component");

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;

        c2Status = comp_wrapper->release();
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            SG_ERR("Failed(%d) to release component", c2Status);
        }
    } else {
        SG_ERR("Component is null");
    }

    return ret;
}

void* c2component_intf(void* const comp)
{

    GST_LOG("Creating component interface");

    void* compIntf = NULL;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;

        compIntf = comp_wrapper->intf();
    } else {
        SG_ERR("Component is null");
    }

    if (compIntf == NULL) {
        SG_ERR("Component interface is null");
    }

    return compIntf;
}

gboolean c2component_createBlockpool(void* comp, BUFFER_POOL_TYPE poolType)
{

    GST_LOG("Creating block pool");

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;

        c2Status = comp_wrapper->createBlockpool(toC2BufferPoolType(poolType));
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            SG_ERR("Failed(%d) to allocate block pool(%d)", c2Status, poolType);
        }
    }

    return ret;
}

gboolean c2component_configBlockpool(void* comp, BUFFER_POOL_TYPE poolType)
{

    GST_LOG("Configing block pool");

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;

        c2Status = comp_wrapper->configBlockPool(toC2BufferPoolType(poolType));
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            SG_ERR("Failed(%d) to allocate block pool(%d)", c2Status, poolType);
        }
    }

    return ret;
}

gboolean c2component_freeOutBuffer(void* const comp, guint64 bufferId)
{

    GST_LOG("Freeing buffer");

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;

        c2Status = comp_wrapper->freeOutputBuffer(bufferId);
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            SG_ERR("Failed(%d) to free buffer(%lu)", c2Status, bufferId);
        }
    }

    return ret;
}

gboolean c2component_delete(void* comp)
{

    GST_LOG("Deleting component");

    gboolean ret = FALSE;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;
        delete comp_wrapper;
        ret = TRUE;
    }
    return ret;
}

gboolean c2component_attachExternalFd(void* comp, BUFFER_POOL_TYPE type, int fd)
{
    GST_LOG("Attach external fd: %d", fd);

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;
        c2Status = comp_wrapper->attachExternalFd(type, fd);
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            SG_ERR("Failed(%d) to attach external fd: %d", c2Status, fd);
        }
    }
    return ret;
}

gboolean c2component_setUseExternalBuffer(void* comp, BUFFER_POOL_TYPE type, gboolean useExternal)
{
    GST_LOG("Set to use external buffer: %s", useExternal ? "TRUE" : "FALSE");

    gboolean ret = FALSE;
    c2_status_t c2Status = C2_NO_INIT;

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;
        c2Status = comp_wrapper->setUseExternalBuffer(type, useExternal);
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            SG_ERR("Failed(%d) to set using external buffer", c2Status);
        }
    }
    return ret;
}

void c2component_cancelPendingWork(void* const comp)
{
    GST_LOG("unblock waiting for pending C2 works");

    if (comp) {
        C2ComponentAdapter* comp_wrapper = (C2ComponentAdapter*)comp;
        comp_wrapper->cancelPendingWork();
    } else {
        SG_ERR("Component is null");
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ComponentInterface API handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const gchar* c2componentInterface_getName(void* const comp_intf)
{

    gchar* name = NULL;

    if (comp_intf) {
        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;

        name = g_strdup(intf_wrapper->getName().c_str());
    }

    return name;
}

const gint c2componentInterface_getId(void* const comp_intf)
{

    gint ret = -1;
    if (comp_intf) {
        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;

        ret = intf_wrapper->getId();
    }

    return ret;
}

void _push_to_settings(gpointer data, gpointer user_data)
{
    SettingsAndIntf* settings_intf = (SettingsAndIntf*)user_data;
    ConfigParams* conf_param = (ConfigParams*)data;
    std::unique_ptr<C2Param> param = nullptr;

    auto iter = sConfigFunctionMap.find(conf_param->config_name);
    if (iter != sConfigFunctionMap.end()) {
        GST_DEBUG("C2 config name:%s", conf_param->config_name);
        param = (*iter->second)(conf_param);
    } else {
        /* Iterator for vendor parameters */
        auto iterVendor = sConfigFunctionForVendorParamsMap.find(conf_param->config_name);
        if (iterVendor != sConfigFunctionForVendorParamsMap.end()) {
            GST_DEBUG("vendor config name:%s", conf_param->config_name);
            param = (*iterVendor->second)(conf_param, settings_intf->comp_intf);
        }
    }
    if (param) {
        settings_intf->settings.push_back(C2Param::Copy(*param));
    } else {
        SG_ERR("param is NULL for %s", conf_param->config_name);
    }
}

gboolean c2componentInterface_initReflectedParamUpdater(void* const comp_store,
    void* const comp_intf)
{

    GST_LOG("Init ReflectedParamUpdater");

    gboolean ret = FALSE;

    if (comp_store && comp_intf) {
        std::shared_ptr<C2ParamReflector> reflector;
        c2_status_t c2Status = C2_NO_INIT;

        C2ComponentStoreAdapter* store_Wrapper = (C2ComponentStoreAdapter*)comp_store;
        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;

        reflector = store_Wrapper->getParamReflector();

        c2Status = intf_wrapper->initReflectedParamUpdater(reflector);
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            GST_WARNING("Failed(%d) to initReflectedParamUpdater", c2Status);
        }
    }

    return ret;
}

gboolean c2componentInterface_config(void* const comp_intf, GPtrArray* config, BLOCK_MODE_TYPE block)
{

    GST_LOG("Applying configuration");

    gboolean ret = FALSE;

    if (comp_intf && config) {
        C2ComponentInterfaceAdapter* intf_wrapper = (C2ComponentInterfaceAdapter*)comp_intf;
        std::vector<C2Param*> stackParams;
        SettingsAndIntf settings_intf;
        settings_intf.comp_intf = comp_intf;
        c2_status_t c2Status = C2_NO_INIT;

        g_ptr_array_foreach(config, _push_to_settings, &settings_intf);

        for (auto& item : settings_intf.settings) {
            stackParams.push_back(item.get());
        }

        c2Status = intf_wrapper->config(stackParams, toC2BlocingType(block));
        if (c2Status == C2_OK) {
            ret = TRUE;
        } else {
            GST_WARNING("Failed(%d) to apply the configuration", c2Status);
        }
    }

    return ret;
}
