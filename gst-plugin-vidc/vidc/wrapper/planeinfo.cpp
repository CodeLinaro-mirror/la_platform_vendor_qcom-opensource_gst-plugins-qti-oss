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

#include "planeinfo.h"
#include <memory.h>
#include <vector>
#include "types.h"

#define BUFFER_ALIGN(x, y) (((x) + ((y)-1)) & ~((y)-1))
#define STRIDE_ALIGN 128
#define P010_STRIDE_ALIGN 256
#define UYVY_STRIDE_ALIGN 16
#define LUMA_HEIGHT_ALIGN 32
#define CHROMA_HEIGHT_ALIGN 16
#define HEIC_GRID_DIMENSION 512

PlaneInfo::PlaneInfo()
{
    initialize();
}

vidc_color_format_type PlaneInfo::convertToVidcFromInt(
    color_format_type col_fmt)
{
    vidc_color_format_type vidc_color_fmt = VIDC_COLOR_FORMAT_UNUSED;

    switch (col_fmt) {
    case COLOR_FORMAT_UYVY:
        vidc_color_fmt = VIDC_COLOR_FORMAT_UYVY;
        break;

    case COLOR_FORMAT_NV12:
        vidc_color_fmt = VIDC_COLOR_FORMAT_NV12;
        break;

    case COLOR_FORMAT_NV21:
        vidc_color_fmt = VIDC_COLOR_FORMAT_NV21;
        break;

    case COLOR_FORMAT_NV12_UBWC:
        vidc_color_fmt = VIDC_COLOR_FORMAT_NV12_UBWC;
        break;

    case COLOR_FORMAT_TP10_UBWC:
        vidc_color_fmt = VIDC_COLOR_FORMAT_YUV420_TP10_UBWC;
        break;

    case COLOR_FORMAT_P010:
        vidc_color_fmt = VIDC_COLOR_FORMAT_NV12_P010;
        break;

    case COLOR_FORMAT_RGBA8888:
        vidc_color_fmt = VIDC_COLOR_FORMAT_RGBA8888;
        break;

    case COLOR_FORMAT_RGBA8888_UBWC:
        vidc_color_fmt = VIDC_COLOR_FORMAT_RGBA8888_UBWC;
        break;

    default:
        break;
    }

    return vidc_color_fmt;
}

PlaneInfo::color_format_type PlaneInfo::convertToIntFromVidc(
    vidc_color_format_type vidc_color_fmt)
{
    color_format_type color_fmt = COLOR_FORMAT_UNUSED;

    switch (vidc_color_fmt) {
    case VIDC_COLOR_FORMAT_UYVY:
        color_fmt = COLOR_FORMAT_UYVY;
        break;

    case VIDC_COLOR_FORMAT_NV12:
        color_fmt = COLOR_FORMAT_NV12;
        break;

    case VIDC_COLOR_FORMAT_NV21:
        color_fmt = COLOR_FORMAT_NV21;
        break;

    case VIDC_COLOR_FORMAT_NV12_UBWC:
        color_fmt = COLOR_FORMAT_NV12_UBWC;
        break;

    case VIDC_COLOR_FORMAT_YUV420_TP10_UBWC:
        color_fmt = COLOR_FORMAT_TP10_UBWC;
        break;

    case VIDC_COLOR_FORMAT_NV12_P010:
        color_fmt = COLOR_FORMAT_P010;
        break;

    case VIDC_COLOR_FORMAT_RGBA8888:
        color_fmt = COLOR_FORMAT_RGBA8888;
        break;

    case VIDC_COLOR_FORMAT_RGBA8888_UBWC:
        color_fmt = COLOR_FORMAT_RGBA8888_UBWC;
        break;

    default:
        break;
    }

    return color_fmt;
}

int PlaneInfo::computeBytes(
    int width,
    int height,
    color_format_type vidcColorFormat,
    vidc_codec_type codec_type,
    bool is_encoder)
{
    MM_DBG_MSG("PlaneInfo::computeBytes");
    mColorFormat = vidcColorFormat;
    setPlaneAttributes(
        width,
        height,
        vidcColorFormat,
        codec_type,
        is_encoder);
    for (int i = 0; i < mPlaneCount; i++) {
        mBytes += mPlane[i].bytes;
    }
    return mBytes;
}

void PlaneInfo::initialize()
{
    mBytes = 0;
    mPlaneCount = 0;
    mColorFormat = COLOR_FORMAT_UNUSED;
    memset(&mPlane, 0, sizeof(mPlane));
}

// Use plane_def to determine stride and buffer sizes.
// Only a few of the formats are supported by plane_def.
void PlaneInfo::setPlaneAttributes(
    int width,
    int height,
    color_format_type pixelFormat,
    vidc_codec_type codec_type,
    bool is_encoder)
{
    unsigned long planedefUsage;

    unsigned long planedefColorFormat;
    plane_def_type planeDef;
    frame_res_type resolution;
    int bytes;
    std::vector<unsigned long> planeIndex[2];
    bool compressed;
    planedef_status_type rc;
    vidc_color_format_type vidcColorFormat = VIDC_COLOR_FORMAT_UNUSED;

    resolution.width_in_pixels = width;
    resolution.height_in_pixels = height;
    if ((VIDC_CODEC_HEIC == codec_type) && (true == is_encoder)) {
        resolution.width_in_pixels = BUFFER_ALIGN(width, HEIC_GRID_DIMENSION);
        resolution.height_in_pixels = BUFFER_ALIGN(height, HEIC_GRID_DIMENSION);
    }
    mPlane[0].width = width;
    mPlane[0].height = height;
    compressed = false;
    planedefColorFormat = 0;
    planedefUsage = PLANEDEF_USAGE_VIDEO;

    vidcColorFormat = convertToVidcFromInt(pixelFormat);

    if (vidcColorFormat & VIDC_COLOR_FORMAT_UBWC_BASE) // If a compressed format
    {
        planedefUsage |= PLANEDEF_USAGE_COMPRESSION; // Indicate compression
        compressed = true;
    }
    switch (vidcColorFormat) {
    case VIDC_COLOR_FORMAT_RGB888:
    case VIDC_COLOR_FORMAT_RGBA8888:
    case VIDC_COLOR_FORMAT_RGBA8888_UBWC:
        mPlaneCount = 1;
        planedefColorFormat = PLANEDEF_FORMAT_RGBA8888;
        if (vidcColorFormat == VIDC_COLOR_FORMAT_RGB888) {
            planedefColorFormat = PLANEDEF_FORMAT_RGB888;
        }
        if (compressed) {
            // Meta must be pushed first
            planeIndex[0].push_back(PLANEDEF_META_PLANE_INDEX_RGB_UBWC);
            planeIndex[0].push_back(PLANEDEF_PAYLOAD_PLANE_INDEX_RGB_UBWC);
        } else {
            planeIndex[0].push_back(PLANEDEF_PAYLOAD_PLANE_INDEX_RGB);
        }
        break;

    case VIDC_COLOR_FORMAT_NV12:
    case VIDC_COLOR_FORMAT_NV21:
    case VIDC_COLOR_FORMAT_NV12_UBWC:
        planedefColorFormat = PLANEDEF_FORMAT_NV12;
        mPlaneCount = 2;
        mPlane[1].width = width;
        mPlane[1].height = height / 2;
        if (compressed) {
            // Meta must be pushed first
            planeIndex[0].push_back(PLANEDEF_Y_META_PLANE_INDEX_NV12_UBWC);
            planeIndex[0].push_back(PLANEDEF_Y_PAYLOAD_PLANE_INDEX_NV12_UBWC);
            planeIndex[1].push_back(PLANEDEF_UV_META_PLANE_INDEX_NV12_UBWC);
            planeIndex[1].push_back(PLANEDEF_UV_PAYLOAD_PLANE_INDEX_NV12_UBWC);
        } else {
            planeIndex[0].push_back(PLANEDEF_Y_PLANE_INDEX_NV12);
            planeIndex[1].push_back(PLANEDEF_UV_PLANE_INDEX_NV12);
        }
        break;

    case VIDC_COLOR_FORMAT_YUV420_TP10_UBWC:
        planedefColorFormat = PLANEDEF_FORMAT_NV12_QC_TP10;
        mPlaneCount = 2;
        mPlane[1].width = width;
        mPlane[1].height = height / 2;

        // Meta must be pushed first
        planeIndex[0].push_back(PLANEDEF_Y_META_PLANE_INDEX_TP10_UBWC);
        planeIndex[0].push_back(PLANEDEF_Y_PAYLOAD_PLANE_INDEX_TP10_UBWC);
        planeIndex[1].push_back(PLANEDEF_UV_META_PLANE_INDEX_TP10_UBWC);
        planeIndex[1].push_back(PLANEDEF_UV_PAYLOAD_PLANE_INDEX_TP10_UBWC);
        break;

    default:
        // For other formats that are not supported by
        // plane_def do the manual calculation and return
        setPlaneAttributesInt(width, height, pixelFormat);

        return; // No more processing needed
    }

    for (int plane = 0; plane < mPlaneCount; plane++) {
        bytes = 0;
        for (unsigned long index : planeIndex[plane]) {
            planeDef.plane_index = index;
            rc = query_plane_def(planedefColorFormat, planedefUsage, &resolution, &planeDef, 0);
            if (rc != PLANEDEF_ERR_NONE) {
                MM_ERROR_MSG("PlaneInfo::setPlaneAttributes Error %d querying plane def", rc);
            }
            bytes += planeDef.plane_buf_size;
        }
        // If the format was compressed then the meta plane was first
        // and we are getting the stride from the payload plane.
        mPlane[plane].stride = planeDef.actual_stride;
        mPlane[plane].stride_multiples = planeDef.stride_multiples;
        mPlane[plane].height_multiples = planeDef.height_multiples;
        if ((VIDC_CODEC_HEIC == codec_type) && (true == is_encoder)) {
            mPlane[plane].stride_multiples = HEIC_GRID_DIMENSION;
            mPlane[plane].height_multiples = HEIC_GRID_DIMENSION;
        }
        mPlane[plane].bytes = bytes;

        MM_DBG_MSG("plane[%d] %dx%d, stride %d, bytes(offset) 0x%x",
            plane, mPlane[plane].width, mPlane[plane].height,
            mPlane[plane].stride, mPlane[plane].bytes);
    }
}

// This is originally adapted from:
// AMSS/multimedia/camera/ais/test/test_util/src/qnx/test_util_qnx.cpp
// static void test_util_fill_planes(qcarcam_buffer_t* p_buffer, qcarcam_color_fmt_t fmt)
// It doesn't handle compressed color formats
void PlaneInfo::setPlaneAttributesInt(
    int width,
    int height,
    color_format_type pixelFormat)
{
    mPlane[0].width = width;
    mPlane[0].height = height;

    unsigned int y_sclines = 0, uv_sclines = 0;

    MM_DBG_MSG("PlaneInfo::setPlaneAttributesInt");
    switch (pixelFormat) {
    case COLOR_FORMAT_P010:
        mPlaneCount = 2;
        mPlane[1].width = width;
        mPlane[1].height = height / 2;

        mPlane[0].stride = BUFFER_ALIGN(width * 2, P010_STRIDE_ALIGN);
        mPlane[1].stride = BUFFER_ALIGN(width * 2, P010_STRIDE_ALIGN);

        mPlane[0].stride_multiples = P010_STRIDE_ALIGN;
        mPlane[1].stride_multiples = P010_STRIDE_ALIGN;

        mPlane[0].height_multiples = LUMA_HEIGHT_ALIGN;
        mPlane[1].height_multiples = CHROMA_HEIGHT_ALIGN;

        y_sclines = BUFFER_ALIGN(height, LUMA_HEIGHT_ALIGN);
        uv_sclines = BUFFER_ALIGN(mPlane[1].height, CHROMA_HEIGHT_ALIGN);

        mPlane[0].bytes = mPlane[0].stride * y_sclines;
        mPlane[1].bytes = mPlane[1].stride * uv_sclines;
        break;

    case COLOR_FORMAT_UYVY:
        mPlaneCount = 1;

        mPlane[0].stride = BUFFER_ALIGN((mPlane[0].width * 2), 16);
        mPlane[0].bytes = mPlane[0].stride * BUFFER_ALIGN(mPlane[0].height, LUMA_HEIGHT_ALIGN);
        break;

    case COLOR_FORMAT_UYVY10:
        mPlaneCount = 1;

        mPlane[0].stride = BUFFER_ALIGN((mPlane[0].width * 4), UYVY_STRIDE_ALIGN);
        mPlane[0].bytes = mPlane[0].stride * BUFFER_ALIGN(mPlane[0].height, LUMA_HEIGHT_ALIGN);
        mPlane[0].stride_multiples = UYVY_STRIDE_ALIGN;
        mPlane[0].height_multiples = LUMA_HEIGHT_ALIGN;

        break;
        /*
      case YUV semiplanar:
         // Y and UV planes are separate
         mPlaneCount = 2;
         mPlane[1].width  = width;
         mPlane[1].height = height / 2;
         break;

      case YUV planar:
         // Y, U and V planes are separate
         mPlaneCount = 3;
         mPlane[1].width  = width / 2;
         mPlane[2].width  = width / 2;
         mPlane[1].height = height / 2;
         mPlane[2].height = height / 2;
         break;
*/
    default:
        MM_ERROR_MSG("PlaneInfo::setPlaneAttributesInt unsupported VIDC color format 0x%X", pixelFormat);
        break;
    }
}
