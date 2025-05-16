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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstqvidch265enc.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qvidc_venc_debug);
#define GST_CAT_DEFAULT gst_qvidc_venc_debug

/* class initialization */
G_DEFINE_TYPE (GstQvidcH265Enc, gst_qvidc_h265_enc, GST_TYPE_QVIDC_VENC);

#define DEFAULT_HEVC_PROFILE HEVC_PROFILE_MAIN
#define DEFAULT_HEVC_TIER "main"
#define DEFAULT_HEVC_LEVEL HEVC_LEVEL_MAIN_TIER_LEVEL6

static gboolean gst_qvidc_h265_enc_set_format (GstQvidcVenc * encoder,
    GstVideoCodecState * state);

#define GST_QVIDC_H265_ENC_SINK_TEMPLATE_CAP \
    GST_QVIDCVENC_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_DMABUF,"NV12",128,8192)";" \
    GST_QVIDCVENC_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_DMABUF,"P010_10LE",128,8192)";" \
    GST_QVIDCVENC_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_DMABUF,"NV12_10LE32",128,8192)";" \
    GST_QVIDCVENC_CAPS_MAKE("NV12",128,8192)";" \
    GST_QVIDCVENC_CAPS_MAKE("P010_10LE",128,8192)";" \
    GST_QVIDCVENC_CAPS_MAKE("NV12_10LE32",128,8192)

static GstStaticPadTemplate gst_qvidc_h265_enc_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_ENCODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_QVIDC_H265_ENC_SINK_TEMPLATE_CAP));

static GstStaticPadTemplate gst_qvidc_h265_enc_src_template =
    GST_STATIC_PAD_TEMPLATE (GST_VIDEO_ENCODER_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265,"
        "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }"
        ";"
        "video/x-heic,"
        "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }"));

static void
gst_qvidc_h265_enc_class_init (GstQvidcH265EncClass * klass)
{
  GstQvidcVencClass *videoenc_class = GST_QVIDC_VENC_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qvidc_h265_enc_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qvidc_h265_enc_src_template));

  videoenc_class->set_format =
      GST_DEBUG_FUNCPTR (gst_qvidc_h265_enc_set_format);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "VIDC video H.265/HEIC encoder", "Encoder/Video",
      "Video H.265/HEIC Encoder based on HW codec", "QTI");
}

static void
gst_qvidc_h265_enc_init (GstQvidcH265Enc * self)
{
}

static const ProfileMapping h265_profiles[] = {
  {"main", HEVC_PROFILE_MAIN},
  {"main-10", HEVC_PROFILE_MAIN10},
  {"main-still-picture", HEVC_PROFILE_MAIN_STILL_PIC},
};

static VIDC_PROFILE_T
gst_qvidc_h265_get_profile_from_str (const gchar * profile)
{
  guint i = 0;
  for (i = 0; i < G_N_ELEMENTS (h265_profiles); i++) {
    if (g_str_equal (profile, h265_profiles[i].profile))
      return h265_profiles[i].e;
  }
  return PROFILE_UNSPECIFIED;
}

static VIDC_LEVEL_T
gst_qvidc_h265_get_level_from_str (const gchar * level, const gchar * tier)
{
  if (g_str_equal (tier, "main")) {
    if (g_str_equal (level, "1"))
      return HEVC_LEVEL_MAIN_TIER_LEVEL1;
    else if (g_str_equal (level, "2"))
      return HEVC_LEVEL_MAIN_TIER_LEVEL2;
    else if (g_str_equal (level, "2.1"))
      return HEVC_LEVEL_MAIN_TIER_LEVEL21;
    else if (g_str_equal (level, "3"))
      return HEVC_LEVEL_MAIN_TIER_LEVEL3;
    else if (g_str_equal (level, "3.1"))
      return HEVC_LEVEL_MAIN_TIER_LEVEL31;
    else if (g_str_equal (level, "4"))
      return HEVC_LEVEL_MAIN_TIER_LEVEL4;
    else if (g_str_equal (level, "4.1"))
      return HEVC_LEVEL_MAIN_TIER_LEVEL41;
    else if (g_str_equal (level, "5"))
      return HEVC_LEVEL_MAIN_TIER_LEVEL5;
    else if (g_str_equal (level, "5.1"))
      return HEVC_LEVEL_MAIN_TIER_LEVEL51;
    else if (g_str_equal (level, "5.2"))
      return HEVC_LEVEL_MAIN_TIER_LEVEL52;
    else if (g_str_equal (level, "6"))
      return HEVC_LEVEL_MAIN_TIER_LEVEL6;
    else if (g_str_equal (level, "6.1"))
      return HEVC_LEVEL_MAIN_TIER_LEVEL61;
    else if (g_str_equal (level, "6.2"))
      return HEVC_LEVEL_MAIN_TIER_LEVEL62;
  } else if (g_str_equal (tier, "high")) {
    if (g_str_equal (level, "4"))
      return HEVC_LEVEL_HIGH_TIER_LEVEL4;
    else if (g_str_equal (level, "4.1"))
      return HEVC_LEVEL_HIGH_TIER_LEVEL41;
    else if (g_str_equal (level, "5"))
      return HEVC_LEVEL_HIGH_TIER_LEVEL5;
    else if (g_str_equal (level, "5.1"))
      return HEVC_LEVEL_HIGH_TIER_LEVEL51;
    else if (g_str_equal (level, "5.2"))
      return HEVC_LEVEL_HIGH_TIER_LEVEL52;
    else if (g_str_equal (level, "6"))
      return HEVC_LEVEL_HIGH_TIER_LEVEL6;
    else if (g_str_equal (level, "6.1"))
      return HEVC_LEVEL_HIGH_TIER_LEVEL61;
    else if (g_str_equal (level, "6.2"))
      return HEVC_LEVEL_HIGH_TIER_LEVEL62;
  }
  return LEVEL_UNSPECIFIED;
}

static gboolean
gst_qvidc_h265_enc_set_format (GstQvidcVenc * encoder,
    GstVideoCodecState * state)
{
  GstQvidcH265Enc *enc = GST_QVIDC_H265_ENC (encoder);
  GPtrArray *config = NULL;
  ConfigParams profile_level;
  VIDC_PROFILE_T profile = DEFAULT_HEVC_PROFILE;
  VIDC_LEVEL_T level = DEFAULT_HEVC_LEVEL;
  GstCaps *output_caps;
  const gchar *profile_string, *level_string, *tier_string;

  /* Set profile and level */
  output_caps = encoder->output_state->caps;
  if (output_caps) {
    GST_INFO_OBJECT (enc, "output state caps: %" GST_PTR_FORMAT, output_caps);
    GstStructure *s;
    if (gst_caps_is_empty (output_caps)) {
      GST_ERROR_OBJECT (enc, "Empty caps");
      return FALSE;
    }
    s = gst_caps_get_structure (output_caps, 0);
    profile_string = gst_structure_get_string (s, "profile");
    if (profile_string) {
      profile = gst_qvidc_h265_get_profile_from_str (profile_string);
      if (profile == PROFILE_UNSPECIFIED)
        goto unsupported_profile;
    }
    level_string = gst_structure_get_string (s, "level");
    tier_string = gst_structure_get_string (s, "tier");
    if (NULL == tier_string) {
      GST_INFO_OBJECT (enc, "HEVC tier is not specified, use default tier: %s",
          DEFAULT_HEVC_TIER);
      tier_string = DEFAULT_HEVC_TIER;
    }
    if (level_string && tier_string) {
      level = gst_qvidc_h265_get_level_from_str (level_string, tier_string);
      if (level == LEVEL_UNSPECIFIED)
        goto unsupported_level;
    }
  }

  config = g_ptr_array_new ();

  if (config) {
    /* For profile and level settings, there are 4 cases here:
     * 1. If profile and level are all specified, the values will be set to driver.
     * 2. If profile is set but level is unspecified, the specified profile will be
     *    set to driver and the level will use a default value accordingly.
     * 3. If level is set but profile is unspecified, this case is not allowed in
     *    VIDC HAL. Need to use the DEFAULT_HEVC_PROFILE.
     * 4. If profile and level are all unspecified, the encoded stream will have
     *    default profile and level values accordingly. */
    if (profile != PROFILE_UNSPECIFIED || level != LEVEL_UNSPECIFIED) {
      if (profile == PROFILE_UNSPECIFIED && level != LEVEL_UNSPECIFIED) {
        profile = DEFAULT_HEVC_PROFILE;
      }
      profile_level = make_profile_level_param (profile, level);
      g_ptr_array_add (config, &profile_level);
    }

    if (config->len && !vidc_config (encoder->comp,
            config, BLOCK_MODE_MAY_BLOCK)) {
      GST_WARNING_OBJECT (encoder,
          "Failed to set encoder config for profile(0x%08x)/level(0x%08x)",
          profile, level);
    }

    g_ptr_array_free (config, TRUE);
  }

  return TRUE;

unsupported_profile:
  GST_ERROR_OBJECT (enc, "Unsupported profile %s", profile_string);
  return FALSE;

unsupported_level:
  GST_ERROR_OBJECT (enc, "Unsupported level %s", level_string);
  return FALSE;
}
