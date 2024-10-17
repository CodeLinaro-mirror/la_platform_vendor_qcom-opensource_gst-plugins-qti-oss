// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "gstqvidcutils.h"

GST_DEBUG_CATEGORY_EXTERN (qvidcutils_debug);
#define GST_CAT_DEFAULT qvidcutils_debug

GST_DEFINE_MINI_OBJECT_TYPE (GstVIDCComp, gst_vidc_comp);

static void gst_vidc_comp_free (GstVIDCComp * gst_vidc_comp);

GstVIDCComp *
gst_vidc_comp_create (void *comp)
{
  GstVIDCComp *gst_vidc_comp = NULL;

  gst_vidc_comp = g_new0 (GstVIDCComp, 1);
  if (gst_vidc_comp) {
    gst_vidc_comp->comp = comp;
    gst_mini_object_init (GST_MINI_OBJECT_CAST (gst_vidc_comp), 0,
        gst_vidc_comp_get_type (), NULL, NULL,
        (GstMiniObjectFreeFunction) gst_vidc_comp_free);

    GST_DEBUG ("gst vidc comp created");
  }

  return gst_vidc_comp;
}

static void
gst_vidc_comp_free (GstVIDCComp * gst_vidc_comp)
{
  if (gst_vidc_comp->comp) {
    vidc_delete (gst_vidc_comp->comp);
    GST_DEBUG ("vidc comp(%p) freed", gst_vidc_comp->comp);

    gst_vidc_comp->comp = NULL;
  }

  g_free (gst_vidc_comp);

  GST_DEBUG ("gst vidc comp freed");
}

void *
get_vidc_comp (GstVIDCComp * gst_vidc_comp)
{
  void *vidc_comp = NULL;

  if (gst_vidc_comp) {
    vidc_comp = gst_vidc_comp->comp;
  }

  return vidc_comp;
}

GstVIDCComp *
gst_vidc_comp_ref (GstVIDCComp * comp)
{
  g_return_val_if_fail (comp, NULL);

  gint old_refcount = 0;

  old_refcount = g_atomic_int_get (&GST_MINI_OBJECT_CAST (comp)->refcount);

  GST_LOG ("gst vidc comp ref:%d->%d", old_refcount, old_refcount + 1);

  return (GstVIDCComp *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (comp));
}

void
gst_vidc_comp_unref (GstVIDCComp * comp)
{
  g_return_if_fail (comp);

  gint old_refcount = 0;

  old_refcount = g_atomic_int_get (&GST_MINI_OBJECT_CAST (comp)->refcount);

  GST_LOG ("gst vidc comp unref:%d->%d", old_refcount, old_refcount - 1);

  gst_mini_object_unref (GST_MINI_OBJECT_CAST (comp));
}

ConfigParams
make_framerate_param (gfloat framerate, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.isInput = is_input;
  param.config_name = CONFIG_FUNCTION_KEY_FRAMERATE;
  param.framerate = framerate;

  return param;
}

ConfigParams
make_hdr_static_info_param (GstVideoMasteringDisplayInfo display_info,
    GstVideoContentLightLevel content_light_level, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.isInput = is_input;
  param.config_name = CONFIG_FUNCTION_KEY_HDR_STATIC_INFO;
  param.hdr_static_info.red_x = display_info.display_primaries[0].x * 0.00002;
  param.hdr_static_info.red_y = display_info.display_primaries[0].y * 0.00002;
  param.hdr_static_info.green_x = display_info.display_primaries[1].x * 0.00002;
  param.hdr_static_info.green_y = display_info.display_primaries[1].y * 0.00002;
  param.hdr_static_info.blue_x = display_info.display_primaries[2].x * 0.00002;
  param.hdr_static_info.blue_y = display_info.display_primaries[2].y * 0.00002;
  param.hdr_static_info.white_x = display_info.white_point.x * 0.00002;
  param.hdr_static_info.white_y = display_info.white_point.y * 0.00002;
  param.hdr_static_info.maxLuminance =
      display_info.max_display_mastering_luminance * 0.0001;
  param.hdr_static_info.minLuminance =
      display_info.min_display_mastering_luminance * 0.0001;
  param.hdr_static_info.maxCll = content_light_level.max_content_light_level;
  param.hdr_static_info.maxFall =
      content_light_level.max_frame_average_light_level;

  return param;
}
