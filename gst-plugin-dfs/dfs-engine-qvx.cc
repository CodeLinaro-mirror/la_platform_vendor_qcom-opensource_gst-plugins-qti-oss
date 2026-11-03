/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * dfs-engine-qvx.cc
 *
 * DFS engine backend using the QVX (Qualcomm Vision Extension) SDK.
 * This replaces dfs-engine.cc (rvSDK backend) and implements the same
 * GstDfsEngine public API defined in dfs-engine.h:
 *
 *   gst_dfs_engine_new()
 *   gst_dfs_engine_execute()
 *   gst_dfs_engine_free()
 *
 * Key differences from the rvSDK backend:
 *  - Uses rvQVX::Base C++ class instead of raw dlopen/dlsym function pointers.
 *  - Pipeline lifecycle: create(nullptr) → get_config → set_config →
 *    set_report_pool → start → inject_frame (per frame) → stop → destroy.
 *  - Base::create(nullptr) selects pure inject-frame mode: no camera plugin
 *    is loaded and QVX core skips bind_camera_vtable() entirely.
 *  - config.stereo.camera.logical_id must be set to cam_id (3) before
 *    set_config() is called. QVX core uses this value to name the
 *    inject-cam stream element ("inject-cam-3"). inject_frame() then
 *    looks up that element by name — if the ids do not match the call
 *    fails with "no inject-cam element for cam_id".
 *  - Results are delivered asynchronously via a report callback
 *    (RVQVX_REPORT_DFS_DISPARITY / RVQVX_REPORT_DFS_DEPTH /
 *     RVQVX_REPORT_DFS_POINT) instead of being returned synchronously
 *    from a compute call.
 *  - Output buffer is written inside the callback and signalled back to
 *    gst_dfs_engine_execute() via a condition variable so that the
 *    GStreamer transform thread can block until the result is ready.
 *  - Disparity-to-image colorization (VIDEO output mode) is kept identical
 *    to the rvSDK backend so downstream behaviour is unchanged.
 */

#include "dfs-engine.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <vector>

#include "qvx-base.hpp"

#define GST_CAT_DEFAULT gst_dfs_debug
GST_DEBUG_CATEGORY_EXTERN (gst_dfs_debug);

/* Logical camera id for the stereo inject-cam stream element.
 * QVX core names the element "inject-cam-<id>" and inject_frame()
 * looks it up by that name, so this value must match logical_id
 * set in config.stereo.camera before set_config() is called. */
#define QVX_STEREO_CAM_ID  3

/* Number of float components per point cloud point (x, y, z) */
#define QVX_PCL_FLOATS_PER_POINT  3

/* -------------------------------------------------------------------------- */
/* Internal engine state                                                       */
/* -------------------------------------------------------------------------- */

struct _GstDfsEngine
{
  /* QVX pipeline instance */
  rvQVX::Base        *qvx;

  /* Report memory pool owned by this engine */
  std::vector<uint8_t> report_pool;

  /* Output mode and pixel format (mirrors DfsInitSettings) */
  gint                mode;           /* OutputMode */
  GstVideoFormat      format;
  guint32             width;          /* per-eye width  (stereo_frame_width  from DfsInitSettings) */
  guint32             height;         /* per-eye height (stereo_frame_height / 2 from DfsInitSettings) */
  guint32             stride;

  /* Logical camera id used when injecting frames */
  qrv_uint32_t        cam_id;

  /*
   * Synchronisation between the GStreamer transform thread (caller of
   * gst_dfs_engine_execute) and the QVX report callback thread.
   *
   * Protocol:
   *   1. execute() sets pending_output / pending_size, clears result_ready,
   *      then calls inject_frame().
   *   2. The QVX report callback writes into pending_output and sets
   *      result_ready = true, then signals the condition variable.
   *   3. execute() waits on the condition variable and returns once signalled.
   */
  std::mutex              result_mutex;
  std::condition_variable result_cv;
  bool                    result_ready;
  bool                    result_ok;

  /* Output buffer pointer and size provided by execute() for the callback */
  gpointer            pending_output;
  gsize               pending_size;
};

/* -------------------------------------------------------------------------- */
/* DFS mode translation: DFSMode (dfs-engine.h) → RVQVX_DFS_MODE_*           */
/* -------------------------------------------------------------------------- */

static inline guint32
translate_dfs_mode (DFSMode mode)
{
  switch (mode) {
    case MODE_CVP:
      return RVQVX_DFS_MODE_CVP;
    case MODE_COVERAGE:
      return RVQVX_DFS_MODE_COVERAGE;
    case MODE_SPEED:
      return RVQVX_DFS_MODE_SPEED;
    case MODE_BALANCE:
      return RVQVX_DFS_MODE_NORMAL;
    case MODE_ACCURACY:
      return RVQVX_DFS_MODE_ACCURACY;
    default:
      break;
  }
  return RVQVX_DFS_MODE_SPEED;
}

/* -------------------------------------------------------------------------- */
/* Video format translation: GstVideoFormat → uint32_t                        */
/* -------------------------------------------------------------------------- */

static inline uint32_t
translate_video_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      return QRV_FORMAT_NV12;
    case GST_VIDEO_FORMAT_NV21:
      return QRV_FORMAT_NV21;
    case GST_VIDEO_FORMAT_GRAY8:
      return QRV_FORMAT_GRAY8;
    default:
      break;
  }
  return QRV_FORMAT_GRAY8;
}

/* -------------------------------------------------------------------------- */
/* Disparity-to-image helpers (identical to rvSDK backend)                    */
/* -------------------------------------------------------------------------- */

static void
normalize_disparity_map (GstDfsEngine *engine, float *disparity_map,
    gpointer output)
{
  float min_val = disparity_map[0];
  float max_val = disparity_map[0];
  const guint32 npixels = engine->width * engine->height;

  for (guint32 x = 1; x < npixels; x++) {
    if (disparity_map[x] > max_val)
      max_val = disparity_map[x];
    if (disparity_map[x] < min_val)
      min_val = disparity_map[x];
  }

  float scale = (max_val > min_val) ? (255.0f / (max_val - min_val)) : 0.0f;

  uint8_t *dst = output ? (uint8_t *) output : (uint8_t *) disparity_map;
  for (guint32 x = 0; x < npixels; x++)
    dst[x] = (uint8_t) ((disparity_map[x] - min_val) * scale);
}

static void
convert_to_rgb_image (GstDfsEngine *engine, float *map, gpointer output)
{
  uint8_t *dst = (uint8_t *) output;
  uint8_t *src = (uint8_t *) map;
  const guint32 npixels = engine->width * engine->height;

  for (guint32 x = 0; x < npixels; x++) {
    uint8_t r = 0, g = 0, b = 0;
    uint8_t val = src[x];

    if (val < 64) {
      g = 4 * val;
      b = 255;
    } else if (val < 128) {
      g = 255;
      b = 255 + 4 * (64 - val);
    } else if (val < 192) {
      r = 4 * (val - 128);
      g = 255;
    } else {
      r = 255;
      g = 255 + 4 * (192 - val);
    }

    switch (engine->format) {
      case GST_VIDEO_FORMAT_RGBA:
      case GST_VIDEO_FORMAT_RGBx:
        dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = 0xFF;
        dst += 4;
        break;
      case GST_VIDEO_FORMAT_BGRA:
      case GST_VIDEO_FORMAT_BGRx:
        dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 0xFF;
        dst += 4;
        break;
      case GST_VIDEO_FORMAT_RGB:
        dst[0] = r; dst[1] = g; dst[2] = b;
        dst += 3;
        break;
      case GST_VIDEO_FORMAT_BGR:
        dst[0] = b; dst[1] = g; dst[2] = r;
        dst += 3;
        break;
      default:
        GST_ERROR ("convert_to_rgb_image: unsupported format %d",
            engine->format);
        return;
    }
  }
}

static void
convert_disparity_map_to_image (GstDfsEngine *engine, float *map,
    gpointer output)
{
  if (engine->format == GST_VIDEO_FORMAT_GRAY8) {
    normalize_disparity_map (engine, map, output);
  } else {
    /* Normalize in-place (output=NULL writes back into map), then colorize */
    normalize_disparity_map (engine, map, NULL);
    convert_to_rgb_image (engine, map, output);
  }
}

/* -------------------------------------------------------------------------- */
/* QVX report callback                                                         */
/*                                                                             */
/* Invoked from QVX's internal report thread after each frame is processed.   */
/* Writes the result into engine->pending_output and signals execute().        */
/* -------------------------------------------------------------------------- */

static qrv_int32_t
qvx_report_callback (void *context, qrv_uint32_t event,
    qrv_uint32_t argc, void *argv)
{
  GstDfsEngine *engine = static_cast<GstDfsEngine *> (context);

  if (event != RVQVX_EVENT_REPORT)
    return 0;

  bool ok = false;

  GST_LOG ("Received QVX report (argc=%u)", argc);
  for (qrv_uint32_t i = 0; i < argc; ++i) {
    auto *report = static_cast<qvx_report_t **> (argv)[i];
    if (report == nullptr)
      continue;

    switch (report->type) {

      /* ------------------------------------------------------------------ */
      /* Disparity map report                                                */
      /* ------------------------------------------------------------------ */
      case RVQVX_REPORT_DFS_DISPARITY: {
        auto *map = reinterpret_cast<qvx_xmap_t *> (report);

        GST_LOG ("QVX report: DISPARITY %ux%u ts=%" G_GUINT64_FORMAT,
            map->map_spec.width, map->map_spec.height,
            (guint64) map->common.timestamp);

        if (engine->pending_output == nullptr) {
          GST_WARNING ("QVX DISPARITY report received but no pending output");
          break;
        }

        if (engine->mode == OUTPUT_MODE_VIDEO) {
          /*
           * VIDEO mode: the raw float disparity map is in map->common.buffer.
           * Normalize and colorize it into the GStreamer output buffer.
           */
          if (map->common.buffer == nullptr) {
            GST_ERROR ("QVX DISPARITY report has NULL buffer");
            break;
          }
          convert_disparity_map_to_image (engine,
              static_cast<float *> (map->common.buffer),
              engine->pending_output);
          ok = true;

        } else if (engine->mode == OUTPUT_MODE_DISPARITY) {
          /*
           * DISPARITY mode: copy the raw float array directly into the
           * GStreamer output buffer.
           */
          gsize expected = engine->width * engine->height * sizeof (float);
          if (map->common.length < expected) {
            GST_ERROR ("QVX DISPARITY buffer too small: got %u, need %" G_GSIZE_FORMAT,
                map->common.length, expected);
            break;
          }
          memcpy (engine->pending_output, map->common.buffer, expected);
          ok = true;
        }
        break;
      }

      /* ------------------------------------------------------------------ */
      /* Depth map report                                                    */
      /* ------------------------------------------------------------------ */
      case RVQVX_REPORT_DFS_DEPTH: {
        /*
         * Depth map is only consumed when output mode is DISPARITY and the
         * downstream explicitly negotiated a depth format. For now the depth
         * report is logged but not forwarded — extend here if a dedicated
         * depth output mode is added to OutputMode in dfs-engine.h.
         */
        auto *map = reinterpret_cast<qvx_xmap_t *> (report);
        GST_LOG ("QVX report: DEPTH %ux%u ts=%" G_GUINT64_FORMAT,
            map->map_spec.width, map->map_spec.height,
            (guint64) map->common.timestamp);
        break;
      }

      /* ------------------------------------------------------------------ */
      /* Point cloud report                                                  */
      /* ------------------------------------------------------------------ */
      case RVQVX_REPORT_DFS_POINT: {
        auto *map = reinterpret_cast<qvx_xmap_t *> (report);

        GST_LOG ("QVX report: POINT_CLOUD ts=%" G_GUINT64_FORMAT,
            (guint64) map->common.timestamp);

        if (engine->mode != OUTPUT_MODE_POINT_CLOUD)
          break;

        if (engine->pending_output == nullptr) {
          GST_WARNING ("QVX POINT report received but no pending output");
          break;
        }

        if (map->common.buffer == nullptr || map->common.length == 0) {
          GST_ERROR ("QVX POINT report has empty buffer");
          break;
        }

        /*
         * The QVX SDK delivers the point cloud as a packed binary array of
         * float32 triples: [ x0,y0,z0, x1,y1,z1, ... ]
         * common.length = point_count * QVX_PCL_FLOATS_PER_POINT * sizeof(float)
         *
         * Downstream consumers expect ASCII PLY format (same as the rvSDK
         * backend produces via write_point_cloud_ply()), so serialize here
         * using the same header structure and setprecision(2) formatting.
         */
        {
          const size_t point_count =
              map->common.length / (QVX_PCL_FLOATS_PER_POINT * sizeof (float));
          const float *pts = static_cast<const float *> (map->common.buffer);

          std::stringstream ply;
          ply << "ply" << '\n';
          ply << "format ascii 1.0" << '\n';
          ply << "element vertex " << point_count << '\n';
          ply << "property float x" << '\n';
          ply << "property float y" << '\n';
          ply << "property float z" << '\n';
          ply << "end_header" << '\n';
          for (size_t p = 0; p < point_count; ++p) {
            ply << std::fixed << std::setprecision (2)
                << pts[p * QVX_PCL_FLOATS_PER_POINT + 0] << " "
                << pts[p * QVX_PCL_FLOATS_PER_POINT + 1] << " "
                << pts[p * QVX_PCL_FLOATS_PER_POINT + 2] << '\n';
          }

          std::string ply_str = ply.str ();
          if (ply_str.size () > engine->pending_size) {
            GST_ERROR ("QVX POINT PLY overflow: need %zu, "
                "capacity %" G_GSIZE_FORMAT,
                ply_str.size (), engine->pending_size);
            break;
          }

          memcpy (engine->pending_output, ply_str.data (), ply_str.size ());
          ok = true;
        }
        break;
      }

      default:
        GST_LOG ("QVX report: unhandled type=0x%08x", report->type);
        break;
    }
  }

  /* Signal execute() that the result is ready */
  {
    std::lock_guard<std::mutex> lock (engine->result_mutex);
    engine->result_ready = true;
    engine->result_ok    = ok;
  }
  engine->result_cv.notify_one ();

  return 0;
}

/* -------------------------------------------------------------------------- */
/* Public API: gst_dfs_engine_get_eye_dims                                     */
/* -------------------------------------------------------------------------- */

void
gst_dfs_engine_get_eye_dims (guint frame_width, guint frame_height,
    guint *eye_width, guint *eye_height)
{
  /* QVX backend: over-under layout — full width, half height per eye */
  *eye_width  = frame_width;
  *eye_height = frame_height / 2;
}

/* -------------------------------------------------------------------------- */
/* Public API: gst_dfs_engine_new                                              */
/* -------------------------------------------------------------------------- */

GstDfsEngine *
gst_dfs_engine_new (DfsInitSettings *settings)
{
  g_return_val_if_fail (settings != NULL, NULL);

  GstDfsEngine *engine = new (std::nothrow) GstDfsEngine ();
  if (!engine) {
    GST_ERROR ("Failed to allocate GstDfsEngine");
    return NULL;
  }

  engine->qvx            = nullptr;
  engine->mode           = settings->mode;
  engine->format         = settings->format;
  engine->width          = settings->stereo_frame_width;
  engine->height         = settings->stereo_frame_height / 2;
  engine->stride         = settings->stride;
  engine->cam_id         = QVX_STEREO_CAM_ID;
  engine->result_ready   = false;
  engine->result_ok      = false;
  engine->pending_output = nullptr;
  engine->pending_size   = 0;

  /* ---- 1. Create QVX instance ------------------------------------------ */
  engine->qvx = rvQVX::Base::create (nullptr);
  if (engine->qvx == nullptr) {
    GST_ERROR ("rvQVX::Base::create() failed");
    goto cleanup;
  }

  /* ---- 2. Get default config, then override fields --------------------- */
  {
    qvx_config_t config;
    if (!engine->qvx->get_config (config)) {
      GST_ERROR ("rvQVX::Base::get_config() failed");
      goto cleanup;
    }

    /* ---- 3. Stereo camera spec and logical id --------------------------- */
    /*
     * Stereo camera spec: format must be GRAY8 to match the Y-plane
     * data injected by gst_dfs_engine_execute(). scanline and height
     * are the FULL over-under frame (both eyes combined) — QVX core
     * splits the OU frame into top/bottom halves internally.
     * engine->height is the per-eye height (stereo_frame_height / 2),
     * so multiply by 2 here to recover the full OU frame dimensions
     * for the camera spec.
     */
    config.stereo.camera.logical_id        = (qrv_int32_t) engine->cam_id;
    config.stereo.camera.spec.format       = translate_video_format (settings->format);
    config.stereo.camera.spec.stride       = engine->stride;
    config.stereo.camera.spec.scanline     = engine->height * 2;
    config.stereo.camera.spec.width        = engine->width;
    config.stereo.camera.spec.height       = engine->height * 2;
    config.stereo.camera.frames_per_second = settings->frames_per_second;

    /* ---- 4. DFS parameters ---------------------------------------------- */
    config.dfs.mode = translate_dfs_mode ((DFSMode) settings->dfs_mode);

    config.dfs.resolution.input.stride   = engine->stride;
    config.dfs.resolution.input.scanline = engine->height;
    config.dfs.resolution.input.width    = engine->width;
    config.dfs.resolution.input.height   = engine->height;

    config.dfs.resolution.output.width   = engine->width;
    config.dfs.resolution.output.height  = engine->height;

    config.dfs.resolution.roi.x          = 0;
    config.dfs.resolution.roi.y          = 0;
    config.dfs.resolution.roi.width      = engine->width;
    config.dfs.resolution.roi.height     = engine->height;

    config.dfs.disparity.d_min   = (qrv_uint32_t) settings->min_disparity;
    config.dfs.disparity.d_level = (qrv_uint32_t) settings->num_disparity_levels;

    config.dfs.filter.width  = (qrv_uint32_t) settings->filter_width;
    config.dfs.filter.height = (qrv_uint32_t) settings->filter_height;

    config.dfs.depth.d_near = (qrv_float32_t) QRV_DEPTH_RANGE_NEAR;
    config.dfs.depth.d_far  = (qrv_float32_t) QRV_DEPTH_RANGE_FAR;

    /* ---- 5. Stereo calibration ----------------------------------------- */
    /*
     * Parse the YAML calibration file into a qrv_stereo_cp_t via
     * rvQVX::Base::get_calibration() and apply it to config.stereo.calib.
     * Calibration is applied whenever a config file path is provided;
     * if no path is set, config.stereo.calib keeps the get_config() defaults.
     */
    if (settings->config_path != nullptr) {
      std::string cal_content;
      std::ifstream fin (settings->config_path, std::ios::in | std::ios::binary);
      if (fin.is_open ()) {
        std::ostringstream oss;
        oss << fin.rdbuf ();
        cal_content = oss.str ();
      } else {
        GST_WARNING ("QVX engine: failed to open calibration file: %s",
            settings->config_path);
      }

      if (!cal_content.empty ()) {
        qrv_stereo_cp_t calib;
        if (rvQVX::Base::get_calibration (cal_content, /*stereo_index=*/0,
                engine->width, engine->height, calib)) {
          config.stereo.calib = calib;
          GST_INFO ("QVX engine: stereo calibration applied from %s",
              settings->config_path);
        } else {
          GST_WARNING ("QVX engine: get_calibration() failed for %s — "
              "stereo calib left at get_config() defaults",
              settings->config_path);
        }
      }
    } else {
      GST_INFO ("QVX engine: no config_path set, "
          "stereo calib left at get_config() defaults");
    }

    /* ---- 6. Report types ------------------------------------------------ */
    switch (engine->mode) {
      case OUTPUT_MODE_VIDEO:
      case OUTPUT_MODE_DISPARITY:
        config.reports = RVQVX_REPORT_DFS_DISPARITY;
        break;
      case OUTPUT_MODE_POINT_CLOUD:
        config.reports = RVQVX_REPORT_DFS_POINT;
        break;
      default:
        config.reports = RVQVX_REPORT_DFS_DISPARITY;
        break;
    }

    /* ---- 7. Callback ---------------------------------------------------- */
    config.callback = qvx_report_callback;
    config.context  = engine;

    if (!engine->qvx->set_config (config)) {
      GST_ERROR ("rvQVX::Base::set_config() failed");
      goto cleanup;
    }

    GST_INFO ("QVX engine: cam_id=%u mode=%d dim=%ux%u stride=%u "
        "disparity=(min=%d levels=%d) filter=%dx%d",
        engine->cam_id, settings->dfs_mode,
        engine->width, engine->height, engine->stride,
        settings->min_disparity, settings->num_disparity_levels,
        settings->filter_width, settings->filter_height);
  }

  /* ---- 8. Report memory pool ------------------------------------------- */
  {
    uint32_t pool_size = engine->qvx->get_report_page_size ();
    if (pool_size == 0) {
      GST_ERROR ("QVX engine: get_report_page_size() returned 0");
      goto cleanup;
    }

    engine->report_pool.resize (pool_size);
    if (!engine->qvx->set_report_pool (engine->report_pool.data (), pool_size)) {
      GST_ERROR ("QVX engine: set_report_pool() failed");
      goto cleanup;
    }
  }

  /* ---- 9. Start the pipeline ------------------------------------------- */
  if (!engine->qvx->start ()) {
    GST_ERROR ("QVX engine: start() failed");
    goto cleanup;
  }

  GST_INFO ("QVX engine started: cam_id=%u mode=%d dim=%ux%u stride=%u",
      engine->cam_id, settings->dfs_mode,
      engine->width, engine->height, engine->stride);

  return engine;

cleanup:
  if (engine->qvx) {
    engine->qvx->stop ();
    rvQVX::Base::destroy (engine->qvx);
    engine->qvx = nullptr;
  }
  delete engine;
  return NULL;
}

/* -------------------------------------------------------------------------- */
/* Public API: gst_dfs_engine_execute                                          */
/* -------------------------------------------------------------------------- */

gboolean
gst_dfs_engine_execute (GstDfsEngine *engine,
    const GstVideoFrame *inframe, gpointer output, gsize size)
{
  g_return_val_if_fail (engine != NULL, FALSE);
  g_return_val_if_fail (inframe != NULL, FALSE);
  g_return_val_if_fail (output != NULL, FALSE);

  /* ---- Build qvx_frame_t from the GstVideoFrame ------------------------ */
  qvx_frame_t frame = {};

  frame.cam_id   = (qrv_uint32_t) engine->cam_id;
  frame.format   = translate_video_format (engine->format);
  frame.stride   = engine->stride;
  /*
   * scanline and height are the FULL over-under frame (both eyes).
   * QVX core splits the OU frame into top/bottom halves internally.
   * Passing only the per-eye height here causes the inject-cam element
   * to reject the frame as undersized.
   */
  frame.scanline = engine->height * 2;
  frame.width    = engine->width;
  frame.height   = engine->height * 2;
  frame.size     = engine->stride * engine->height * 2;
  frame.data     = GST_VIDEO_FRAME_PLANE_DATA (inframe, 0); /* Y-plane only */

  /* Timestamps: use GStreamer buffer PTS converted to CLOCK_BOOTTIME nsec.
   * If PTS is invalid fall back to the current monotonic clock. */
  struct timespec ts_now;
  clock_gettime (CLOCK_BOOTTIME, &ts_now);
  uint64_t now_ns = (uint64_t) ts_now.tv_sec * 1000000000ULL
                  + (uint64_t) ts_now.tv_nsec;

  GstClockTime pts = inframe->buffer
      ? GST_BUFFER_PTS (inframe->buffer)
      : GST_CLOCK_TIME_NONE;

  uint64_t frame_ts = GST_CLOCK_TIME_IS_VALID (pts) ? (uint64_t) pts : now_ns;

  frame.ts.timestamp = frame_ts;
  frame.ts.eoe       = frame_ts;
  frame.ts.hal       = frame_ts;
  frame.ts.qmmf      = frame_ts;
  frame.ts.gst       = frame_ts;

  /* ---- Register the output buffer for the callback --------------------- */
  {
    std::lock_guard<std::mutex> lock (engine->result_mutex);
    engine->pending_output = output;
    engine->pending_size   = size;
    engine->result_ready   = false;
    engine->result_ok      = false;
  }

  /* ---- Inject the frame into the QVX pipeline -------------------------- */
  if (!engine->qvx->inject_frame (frame)) {
    GST_ERROR ("QVX engine: inject_frame() failed");
    std::lock_guard<std::mutex> lock (engine->result_mutex);
    engine->pending_output = nullptr;
    return FALSE;
  }

  /* ---- Wait for the report callback to signal completion --------------- */
  {
    std::unique_lock<std::mutex> lock (engine->result_mutex);
    engine->result_cv.wait (lock, [engine] { return engine->result_ready; });
    engine->pending_output = nullptr;

    if (!engine->result_ok) {
      GST_ERROR ("QVX engine: report callback indicated failure");
      return FALSE;
    }
  }

  return TRUE;
}

/* -------------------------------------------------------------------------- */
/* Public API: gst_dfs_engine_free                                             */
/* -------------------------------------------------------------------------- */

void
gst_dfs_engine_free (GstDfsEngine *engine)
{
  if (engine == nullptr)
    return;

  if (engine->qvx) {
    engine->qvx->stop ();
    rvQVX::Base::destroy (engine->qvx);
    engine->qvx = nullptr;
  }

  delete engine;
}
