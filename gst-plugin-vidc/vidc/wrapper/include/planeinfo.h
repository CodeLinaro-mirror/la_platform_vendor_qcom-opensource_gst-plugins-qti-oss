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

#ifndef PLANEINFO_H
#define PLANEINFO_H
#include <plane_def.h>
#include "vidc_types.h"

class PlaneInfo {
public:
    PlaneInfo();

    virtual ~PlaneInfo() {}

    typedef enum {
        COLOR_FORMAT_UYVY = 1,
        COLOR_FORMAT_UYVY10,
        COLOR_FORMAT_NV12,
        COLOR_FORMAT_NV12_UBWC,
        COLOR_FORMAT_NV21,
        COLOR_FORMAT_P010,
        COLOR_FORMAT_TP10_UBWC,
        COLOR_FORMAT_RGBA8888,
        COLOR_FORMAT_RGBA8888_UBWC,
        COLOR_FORMAT_UNUSED
    } color_format_type;

    int computeBytes // Returns bytes required for image, accounts for alignment
        (
            int width,
            int height,
            color_format_type vidcColorFormat,
            vidc_codec_type codec_type = VIDC_CODEC_UNUSED,
            bool is_encoder = false);

    int getBytes() { return mBytes; }

    int getPlaneCount() { return mPlaneCount; }

    int getPlaneBytes(int plane) { return mPlane[plane].bytes; }

    int getPlaneStride(int plane) { return mPlane[plane].stride; }

    int getPlaneStrideMultiple(int plane) { return mPlane[plane].stride_multiples; }

    int getPlaneHeightMultiple(int plane) { return mPlane[plane].height_multiples; }

    int getHeight(int plane) { return mPlane[plane].height; }

    int getWidth(int plane) { return mPlane[plane].width; }

    color_format_type getPortColorFormat() { return mColorFormat; }

    vidc_color_format_type convertToVidcFromInt(color_format_type col_fmt);

    void initialize();

protected:
    typedef struct
    {
        int height; // Image height, pixels (non-aligned)
        int width; // Image width, pixels (non-aligned)
        int stride; // stride, aligned
        int bytes; // Plane size, bytes
        int height_multiples; // Height multiple for NV12 support
        int stride_multiples; // Stride multiple for NV12 support
    } PlaneType;

    int mBytes; // Size of a image, bytes
    PlaneType mPlane[3]; // Plane attributes
    int mPlaneCount; // Number of planes
    color_format_type mColorFormat; // VIDC Color format of the image

    void setPlaneAttributes(
        int width,
        int height,
        color_format_type vidcColorFormat,
        vidc_codec_type codec_type = VIDC_CODEC_UNUSED,
        bool is_encoder = false);

private:
    void setPlaneAttributesInt(
        int width,
        int height,
        color_format_type vidcColorFormat);
};
#endif // PLANEINFO_H
