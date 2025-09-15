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

#ifndef __VIDCWRAPPER_H__
#define __VIDCWRAPPER_H__

#include <glib.h>
#include <gmodule.h>
#include <dlfcn.h>
#include <gst/video/video.h>
#include <stdint.h>
#include "vidc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_FUNCTION_KEY_CODEC "codec"
#define CONFIG_FUNCTION_KEY_PIXELFORMAT "pixelformat"
#define CONFIG_FUNCTION_KEY_RESOLUTION "resolution"
#define CONFIG_FUNCTION_KEY_BITRATE "bitrate"
#define CONFIG_FUNCTION_KEY_MIRROR "mirror"
#define CONFIG_FUNCTION_KEY_ROTATION "rotation"
#define CONFIG_FUNCTION_KEY_RATECONTROL "ratecontrol"
#define CONFIG_FUNCTION_KEY_DEC_LOW_LATENCY "dec_low_latency"
#define CONFIG_FUNCTION_KEY_INTRAREFRESH "intra_refresh"
#define CONFIG_FUNCTION_KEY_INTRAREFRESH_TYPE "intra_refresh_type"
#define CONFIG_FUNCTION_KEY_OUTPUT_PICTURE_ORDER_MODE "output_picture_order_mode"
#define CONFIG_FUNCTION_KEY_DOWNSCALE "downscale"
#define CONFIG_FUNCTION_KEY_ENC_CSC "enc_colorspace_conversion"
#define CONFIG_FUNCTION_KEY_COLOR_ASPECTS_INFO "colorspace_color_aspects"
#define CONFIG_FUNCTION_KEY_SLICE_MODE "slice_mode"
#define CONFIG_FUNCTION_KEY_BLUR_MODE "blur_mode"
#define CONFIG_FUNCTION_KEY_BLUR_RESOLUTION "blur_resolution"
#define CONFIG_FUNCTION_KEY_ROIREGION "roiregion"
#define CONFIG_FUNCTION_KEY_BITRATE_SAVING_MODE "bitrate_saving_mode"
#define CONFIG_FUNCTION_KEY_PROFILE_LEVEL "profile_level"
#define CONFIG_FUNCTION_KEY_INTERLACE_INFO "interlace_info"
#define CONFIG_FUNCTION_KEY_DEINTERLACE "deinterlace"
#define CONFIG_FUNCTION_KEY_FRAMERATE "framerate"
#define CONFIG_FUNCTION_KEY_DYNAMIC_FRAMERATE "dynamic_framerate"
#define CONFIG_FUNCTION_KEY_INTRAFRAMES_PERIOD "intraframes_period"
#define CONFIG_FUNCTION_KEY_INTRA_VIDEO_FRAME_REQUEST "intra_video_frame_request"
#define CONFIG_FUNCTION_KEY_VIDEO_HEADER_MODE "video_header_mode"
#define CONFIG_FUNCTION_KEY_IPB_QP_RANGE "IPB_qp_range"
#define CONFIG_FUNCTION_KEY_IPB_QP_INIT "IPB_qp_init"
#define CONFIG_FUNCTION_KEY_REPORT_AVERAGE_FRAME_QP "report_average_frame_qp"
#define CONFIG_FUNCTION_KEY_TEMPORAL_LAYER "temporal_layer"
#define CONFIG_FUNCTION_KEY_LTR_COUNT "ltr_count"
#define CONFIG_FUNCTION_KEY_LTR_MARK_INDEX "ltr_mark_index"
#define CONFIG_FUNCTION_KEY_LTR_USE_INDEX "ltr_use_index"
#define CONFIG_FUNCTION_KEY_EXTERNAL_BUFFER "external_buffer"
#define CONFIG_FUNCTION_KEY_HDR_STATIC_INFO "hdr_static_info"

#define TICKS_PER_SECOND 1000000

typedef struct comp_cb {
    gpointer data_copy_func;
    gpointer data_copy_func_param;
} comp_cb;

typedef int (*fnDataCopy)(int dstbuf_fd, void* srcbuf, uint32_t* pdatalen, void* param);

typedef enum {
    BUFFER_PORT_INPUT = 0,
    BUFFER_PORT_OUTPUT,
} BUFFER_PORT_TYPE;

typedef enum {
    BLOCK_MODE_DONT_BLOCK = 0,
    BLOCK_MODE_MAY_BLOCK
} BLOCK_MODE_TYPE;

typedef enum {
    DRAIN_MODE_COMPONENT_WITH_EOS = 0,
    DRAIN_MODE_COMPONENT_NO_EOS,
    DRAIN_MODE_CHAIN
} DRAIN_MODE_TYPE;

typedef enum {
    INTERLACE_MODE_PROGRESSIVE = 0, ///< progressive
    INTERLACE_MODE_INTERLEAVED_TOP_FIRST, ///< line-interleaved. top-field-first
    INTERLACE_MODE_INTERLEAVED_BOTTOM_FIRST, ///< line-interleaved. bottom-field-first
    INTERLACE_MODE_FIELD_TOP_FIRST, ///< field-sequential. top-field-first
    INTERLACE_MODE_FIELD_BOTTOM_FIRST, ///< field-sequential. bottom-field-first
} INTERLACE_MODE_TYPE;

typedef enum {
    FLAG_TYPE_DROP_FRAME = 1 << 0,
    FLAG_TYPE_END_OF_STREAM = 1 << 1, ///< For input frames: no output frame shall be generated when processing this frame.
    ///< For output frames: this frame shall be discarded.
    FLAG_TYPE_DISCARD_FRAME = 1 << 2, ///< This frame shall be discarded with its metadata.
    FLAG_TYPE_INCOMPLETE = 1 << 3, ///< This frame is not the last frame produced for the input
    FLAG_TYPE_CODEC_CONFIG = 1 << 4 ///< Frame contains only codec-specific configuration data, and no actual access unit
} FLAG_TYPE;

typedef enum {
    PIXEL_FORMAT_NV12_LINEAR = 0,
    PIXEL_FORMAT_NV12_UBWC,
    PIXEL_FORMAT_RGBA_8888,
    PIXEL_FORMAT_YV12,
    PIXEL_FORMAT_P010,
    PIXEL_FORMAT_TP10_UBWC,
    PIXEL_FORMAT_NV12_512
} PIXEL_FORMAT_TYPE;

typedef enum {
    EVENT_INPUTS_DONE = 0,
    EVENT_OUTPUTS_DONE,
    EVENT_ERROR,
    EVENT_RECONFIG,
    EVENT_DROP_FRAME,
} EVENT_TYPE;

typedef enum {
    DEFAULT_ORDER = 0,
    DISPLAY_ORDER,
    DECODER_ORDER,
} OUTPUT_PIC_ORDER;

typedef enum {
    MIRROR_NONE = 0,
    MIRROR_VERTICAL,
    MIRROR_HORIZONTAL,
    MIRROR_BOTH,
} MIRROR_TYPE;

typedef enum {
    RC_OFF = 0,
    RC_CONST,
    RC_CBR_VFR,
    RC_VBR_CFR,
    RC_VBR_VFR,
    RC_CQ,
    RC_UNSET = 0xFFFF
} RC_MODE_TYPE;

typedef enum {
    SLICE_MODE_DISABLE,
    SLICE_MODE_MB,
    SLICE_MODE_BYTES,
} SLICE_MODE;

typedef enum {
    BLUR_AUTO = 0,
    BLUR_MANUAL,
    BLUR_DISABLE,
} BLUR_MODE;

typedef enum {
    COLOR_PRIMARIES_UNSPECIFIED,
    COLOR_PRIMARIES_BT709,
    COLOR_PRIMARIES_BT470_M,
    COLOR_PRIMARIES_BT601_625,
    COLOR_PRIMARIES_BT601_525,
    COLOR_PRIMARIES_GENERIC_FILM,
    COLOR_PRIMARIES_BT2020,
    COLOR_PRIMARIES_RP431,
    COLOR_PRIMARIES_EG432,
    COLOR_PRIMARIES_EBU3213,
} COLOR_PRIMARIES;

typedef enum {
    COLOR_TRANSFER_UNSPECIFIED,
    COLOR_TRANSFER_LINEAR,
    COLOR_TRANSFER_SRGB,
    COLOR_TRANSFER_170M,
    COLOR_TRANSFER_GAMMA22,
    COLOR_TRANSFER_GAMMA28,
    COLOR_TRANSFER_ST2084,
    COLOR_TRANSFER_HLG,
    COLOR_TRANSFER_240M,
    COLOR_TRANSFER_XVYCC,
    COLOR_TRANSFER_BT1361,
    COLOR_TRANSFER_ST428,
} TRANSFER_CHAR;

typedef enum {
    COLOR_MATRIX_UNSPECIFIED,
    COLOR_MATRIX_BT709,
    COLOR_MATRIX_FCC47_73_682,
    COLOR_MATRIX_BT601,
    COLOR_MATRIX_240M,
    COLOR_MATRIX_BT2020,
    COLOR_MATRIX_BT2020_CONSTANT,
} MATRIX;

typedef enum {
    COLOR_RANGE_UNSPECIFIED,
    COLOR_RANGE_FULL,
    COLOR_RANGE_LIMITED,
} FULL_RANGE;

typedef enum {
    IR_NONE = 0,
    IR_RANDOM,
    IR_CYCLIC,
} IR_MODE_TYPE;

typedef enum {
    BITRATE_SAVING_MODE_DISABLE_ALL = 0,
    BITRATE_SAVING_MODE_ENABLE_8BIT,
    BITRATE_SAVING_MODE_ENABLE_10BIT,
    BITRATE_SAVING_MODE_ENABLE_ALL,
} BITRATE_SAVING_MODE;

typedef enum {
    AVC_PROFILE_BASELINE = VIDC_PROFILE_H264_BASELINE, ///< AVC (H.264) Baseline
    AVC_PROFILE_MAIN = VIDC_PROFILE_H264_MAIN, ///< AVC (H.264) Main
    AVC_PROFILE_HIGH = VIDC_PROFILE_H264_HIGH, ///< AVC (H.264) High
    AVC_PROFILE_CONSTRAINT_BASELINE = VIDC_PROFILE_H264_CONSTRAINED_BASE, ///< AVC (H.264) Constrained Baseline
    AVC_PROFILE_CONSTRAINT_HIGH = VIDC_PROFILE_H264_CONSTRAINED_HIGH, ///< AVC (H.264) Constrained High

    HEVC_PROFILE_MAIN = VIDC_PROFILE_HEVC_MAIN, ///< HEVC (H.265) Main
    HEVC_PROFILE_MAIN10 = VIDC_PROFILE_HEVC_MAIN10, ///< HEVC (H.265) Main 10
    HEVC_PROFILE_MAIN_STILL_PIC = VIDC_PROFILE_HEVC_MAIN_STILL_PICTURE, ///< HEVC (H.265) Main Still Picture

    PROFILE_UNSPECIFIED = 0x10000000,
} VIDC_PROFILE_T;

typedef enum {
    AVC_LEVEL_1 = VIDC_LEVEL_H264_1, ///< AVC (H.264) Level 1
    AVC_LEVEL_1b = VIDC_LEVEL_H264_1b, ///< AVC (H.264) Level 1b
    AVC_LEVEL_11 = VIDC_LEVEL_H264_1p1, ///< AVC (H.264) Level 1.1
    AVC_LEVEL_12 = VIDC_LEVEL_H264_1p2, ///< AVC (H.264) Level 1.2
    AVC_LEVEL_13 = VIDC_LEVEL_H264_1p3, ///< AVC (H.264) Level 1.3
    AVC_LEVEL_2 = VIDC_LEVEL_H264_2, ///< AVC (H.264) Level 2
    AVC_LEVEL_21 = VIDC_LEVEL_H264_2p1, ///< AVC (H.264) Level 2.1
    AVC_LEVEL_22 = VIDC_LEVEL_H264_2p2, ///< AVC (H.264) Level 2.2
    AVC_LEVEL_3 = VIDC_LEVEL_H264_3, ///< AVC (H.264) Level 3
    AVC_LEVEL_31 = VIDC_LEVEL_H264_3p1, ///< AVC (H.264) Level 3.1
    AVC_LEVEL_32 = VIDC_LEVEL_H264_3p2, ///< AVC (H.264) Level 3.2
    AVC_LEVEL_4 = VIDC_LEVEL_H264_4, ///< AVC (H.264) Level 4
    AVC_LEVEL_41 = VIDC_LEVEL_H264_4p1, ///< AVC (H.264) Level 4.1
    AVC_LEVEL_42 = VIDC_LEVEL_H264_4p2, ///< AVC (H.264) Level 4.2
    AVC_LEVEL_5 = VIDC_LEVEL_H264_5, ///< AVC (H.264) Level 5
    AVC_LEVEL_51 = VIDC_LEVEL_H264_5p1, ///< AVC (H.264) Level 5.1
    AVC_LEVEL_52 = VIDC_LEVEL_H264_5p2, ///< AVC (H.264) Level 5.2
    AVC_LEVEL_6 = VIDC_LEVEL_H264_6, ///< AVC (H.264) Level 6
    AVC_LEVEL_61 = VIDC_LEVEL_H264_6p1, ///< AVC (H.264) Level 6.1
    AVC_LEVEL_62 = VIDC_LEVEL_H264_6p2, ///< AVC (H.264) Level 6.2

    HEVC_LEVEL_MAIN_TIER_LEVEL1 = VIDC_LEVEL_HEVC_1, ///< HEVC (H.265) Main Tier Level 1
    HEVC_LEVEL_MAIN_TIER_LEVEL2 = VIDC_LEVEL_HEVC_2, ///< HEVC (H.265) Main Tier Level 2
    HEVC_LEVEL_MAIN_TIER_LEVEL21 = VIDC_LEVEL_HEVC_21, ///< HEVC (H.265) Main Tier Level 2.1
    HEVC_LEVEL_MAIN_TIER_LEVEL3 = VIDC_LEVEL_HEVC_3, ///< HEVC (H.265) Main Tier Level 3
    HEVC_LEVEL_MAIN_TIER_LEVEL31 = VIDC_LEVEL_HEVC_31, ///< HEVC (H.265) Main Tier Level 3.1
    HEVC_LEVEL_MAIN_TIER_LEVEL4 = VIDC_LEVEL_HEVC_4, ///< HEVC (H.265) Main Tier Level 4
    HEVC_LEVEL_MAIN_TIER_LEVEL41 = VIDC_LEVEL_HEVC_41, ///< HEVC (H.265) Main Tier Level 4.1
    HEVC_LEVEL_MAIN_TIER_LEVEL5 = VIDC_LEVEL_HEVC_5, ///< HEVC (H.265) Main Tier Level 5
    HEVC_LEVEL_MAIN_TIER_LEVEL51 = VIDC_LEVEL_HEVC_51, ///< HEVC (H.265) Main Tier Level 5.1
    HEVC_LEVEL_MAIN_TIER_LEVEL52 = VIDC_LEVEL_HEVC_52, ///< HEVC (H.265) Main Tier Level 5.2
    HEVC_LEVEL_MAIN_TIER_LEVEL6 = VIDC_LEVEL_HEVC_6, ///< HEVC (H.265) Main Tier Level 6
    HEVC_LEVEL_MAIN_TIER_LEVEL61 = VIDC_LEVEL_HEVC_61, ///< HEVC (H.265) Main Tier Level 6.1
    HEVC_LEVEL_MAIN_TIER_LEVEL62 = VIDC_LEVEL_HEVC_62, ///< HEVC (H.265) Main Tier Level 6.2

    // TODO: map tier level to vidc tier
    HEVC_LEVEL_HIGH_TIER_LEVEL1 = 256, ///< HEVC (H.265) High Tier Level 1
    HEVC_LEVEL_HIGH_TIER_LEVEL2, ///< HEVC (H.265) High Tier Level 2
    HEVC_LEVEL_HIGH_TIER_LEVEL21, ///< HEVC (H.265) High Tier Level 2.1
    HEVC_LEVEL_HIGH_TIER_LEVEL3, ///< HEVC (H.265) High Tier Level 3
    HEVC_LEVEL_HIGH_TIER_LEVEL31, ///< HEVC (H.265) High Tier Level 3.1
    HEVC_LEVEL_HIGH_TIER_LEVEL4, ///< HEVC (H.265) High Tier Level 4
    HEVC_LEVEL_HIGH_TIER_LEVEL41, ///< HEVC (H.265) High Tier Level 4.1
    HEVC_LEVEL_HIGH_TIER_LEVEL5, ///< HEVC (H.265) High Tier Level 5
    HEVC_LEVEL_HIGH_TIER_LEVEL51, ///< HEVC (H.265) High Tier Level 5.1
    HEVC_LEVEL_HIGH_TIER_LEVEL52, ///< HEVC (H.265) High Tier Level 5.2
    HEVC_LEVEL_HIGH_TIER_LEVEL6, ///< HEVC (H.265) High Tier Level 6
    HEVC_LEVEL_HIGH_TIER_LEVEL61, ///< HEVC (H.265) High Tier Level 6.1
    HEVC_LEVEL_HIGH_TIER_LEVEL62, ///< HEVC (H.265) High Tier Level 6.2

    LEVEL_UNSPECIFIED = 0x10000000,
} VIDC_LEVEL_T;

typedef struct {
    guint8* data;
    gint32 fd;
    gint32 meta_fd;
    gint32 ext_fd;
    guint32 size;
    guint32 metasize;
    guint32 capacity; ///< Total allocation size
    guint64 timestamp;
    guint64 index;
    guint32 width;
    guint32 height;
    guint32 stride[2];
    gsize offset[2];
    GstVideoFormat format;
    guint32 ubwc_flag;
    FLAG_TYPE flag;
    BUFFER_PORT_TYPE port_type;
    guint8* config_data; // codec config data
    guint32 config_size; // size of codec config data
    void* vidcBuffer;
    void* gbm_bo;
    gboolean secure;
    guint32 interlaceMode;
    gboolean heic_flag;
    guint32 avg_frame_qp;
    gboolean deinterlaced;
} BufferDescriptor;

typedef struct {
    const char* config_name;
    gboolean isInput;
    vidc_codec_type codec;

    // Each parameter should only use one member of union. For example,
    // member val and sliceMode can not be used at the same time.
    // Otherwise, date overlapped since members in union shares the
    // same address.
    union {
        guint output_picture_order_mode;
        gboolean low_latency_mode;
        gboolean color_space_conversion;
        gboolean deinterlace;
        gboolean force_idr;
        gboolean inline_sps_pps_headers;
        gboolean report_average_frame_qp;
        gboolean use_external_buf;

        union {
            guint32 u32;
            guint64 u64;
            gint32 i32;
            gint64 i64;
        } val;

        struct {
            guint32 width;
            guint32 height;
        } resolution;

        struct {
            PIXEL_FORMAT_TYPE fmt;
        } pixelFormat;

        struct {
            INTERLACE_MODE_TYPE type;
        } interlaceMode;

        struct {
            MIRROR_TYPE type;
        } mirror;

        struct {
            RC_MODE_TYPE type;
        } rcMode;

        struct {
            guint32 slice_size;
            SLICE_MODE type;
        } sliceMode;

        struct {
            BLUR_MODE mode;
        } blur;

        struct {
            int64_t timestampUs;
            char* type;
            char* rectPayload;
            char* rectPayloadExt;
        } roiRegion;

        struct {
            guint32 layerCount;
            guint32 bLayerCount;
            guint32 ratioSize;
            gfloat* ratios;
        } temporalLayer;

        struct {
            IR_MODE_TYPE type;
            guint32 intra_refresh_mbs;
        } irMode;

        struct {
            COLOR_PRIMARIES primaries;
            TRANSFER_CHAR transfer_char;
            MATRIX matrix;
            FULL_RANGE full_range;
        } colorAspects;

        struct {
            BITRATE_SAVING_MODE saving_mode;
        } bitrate_saving_mode;

        struct {
            VIDC_PROFILE_T profile;
            VIDC_LEVEL_T level;
        } profileAndLevel;

        gfloat framerate;

        struct {
            guint32 min_i_qp;
            guint32 max_i_qp;
            guint32 min_p_qp;
            guint32 max_p_qp;
            guint32 min_b_qp;
            guint32 max_b_qp;
        } qp_ranges;

        struct {
            gboolean quant_i_frames_enable;
            guint32 quant_i_frames;
            gboolean quant_p_frames_enable;
            guint32 quant_p_frames;
            gboolean quant_b_frames_enable;
            guint32 quant_b_frames;
        } qp_init;

        struct {
            guint count;
            guint mark_index;
            guint use_index;
        } ltr;

        struct {
            gfloat red_x;
            gfloat red_y;
            gfloat green_x;
            gfloat green_y;
            gfloat blue_x;
            gfloat blue_y;
            gfloat white_x;
            gfloat white_y;
            gfloat maxLuminance;
            gfloat minLuminance;
            gfloat maxCll;
            gfloat maxFall;
        } hdr_static_info;
    };
} ConfigParams;

typedef struct {
    guint32 width;
    guint32 height;
} BufferResolution;

typedef struct {
    BufferResolution resolution;
    gboolean is_c2d;
} AcquireExtBufInfo;

typedef struct {
    const gchar* codec;
    const gchar* element;
    guint rank;
    GType (*register_type)(void);
    vidc_codec_type vidc_codec;
} ElementInfo;

typedef void (*listener_cb)(const void* handle, EVENT_TYPE type, void* data);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Component Store API
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void* vidcStore_create(void);
gboolean vidcStore_createComponent(void* const comp_store, const gchar* name, void** const component, comp_cb* cb);
gboolean vidcStore_isComponentSupported(void* const comp_store, gchar* name);
gboolean vidcStore_delete(void* comp_store);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Component API
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
gboolean vidc_setListener(void* const comp, void* cb_context, listener_cb callback, BLOCK_MODE_TYPE block);
guint vidc_getPlaneCount(void* const comp);
guint vidc_getPlaneStride(void* const comp, const guint plane);
guint vidc_getPlaneOffset(void* const comp, const guint plane);
gboolean vidc_getAllocationCountAndSize(void* const comp, BUFFER_PORT_TYPE type, guint* count, guint* size, guint* metasize);
gboolean vidc_alloc(void* const comp, BufferDescriptor* buffer);
gboolean vidc_queue(void* const comp, BufferDescriptor* buffer);
gboolean vidc_start(void* const comp, BUFFER_PORT_TYPE port);
gboolean vidc_stop(void* const comp, BUFFER_PORT_TYPE port);
gboolean vidc_config(void* const comp, GPtrArray* config, BLOCK_MODE_TYPE block);
gboolean vidc_freeOutBuffer(void* const comp, BufferDescriptor* buffer);
gboolean vidc_delete(void* comp);
gboolean writePlane(void* comp, uint8_t* dest, BufferDescriptor* buffer_info);
gboolean vidc_isEncoder(void* const comp);
gboolean vidc_isProgressive(void* const comp);

#ifdef __cplusplus
}
#endif

#endif /* __VIDCWRAPPER_H__ */
