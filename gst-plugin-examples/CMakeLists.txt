cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_EXAMPLES LANGUAGES C CXX)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

# Check whether DRM PlayReady libs are present.
include(CheckIncludeFileCXX)
check_include_file_cxx("media/drm/DrmAPI.h" HAVE_DRM_API_H)

if (HAVE_DRM_API_H)
  add_subdirectory(gst-drm-player-example)
endif()
