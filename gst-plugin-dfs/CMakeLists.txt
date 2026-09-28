cmake_minimum_required(VERSION 3.16)
project(GST_PLUGINS_QTI_OSS_DFS
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C CXX
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_ALLOC
  REQUIRED gstreamer-allocators-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})
# Use in-tree CMake targets if built together, otherwise fall back to pkg-config
if(TARGET gstqtiutilsbase)
  set(GST_QCOM_UTILS_INCLUDE_DIRS "")
  set(GST_QCOM_UTILS_LIBRARIES    gstqtiutilsbase)
else()
  pkg_check_modules(GST_QCOM_UTILS
    REQUIRED gstreamer-qcom-oss-utils-1.0>=1.0.0)
endif()
if(TARGET gstqtivideobase)
  set(GST_QCOM_VIDEO_INCLUDE_DIRS "")
  set(GST_QCOM_VIDEO_LIBRARIES    gstqtivideobase)
else()
  pkg_check_modules(GST_QCOM_VIDEO
    REQUIRED gstreamer-qcom-oss-video-1.0>=1.0.0)
endif()
if(TARGET gstqtiallocatorsbase)
  set(GST_QCOM_ALLOC_INCLUDE_DIRS "")
  set(GST_QCOM_ALLOC_LIBRARIES    gstqtiallocatorsbase)
else()
  pkg_check_modules(GST_QCOM_ALLOC
    REQUIRED gstreamer-qcom-oss-allocators-1.0>=1.0.0)
endif()

# Generate configuration header file with plugin describing definitions
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")

# GStreamer DFS plugin.
set(GST_DFS gstqtidfs)

# ---------------------------------------------------------------------------
# Engine backend selection: QVX takes priority over the legacy rvSDK.
#
#   1. If qcom-qvx pkg-config is found  -> compile dfs-engine-qvx.cc
#   2. Else if dfs_factory.h is found   -> compile dfs-engine.cc (rvSDK)
#   3. Else                             -> fatal error, no engine available
# ---------------------------------------------------------------------------
pkg_check_modules(QVX qcom-qvx)

if(QVX_FOUND)

  message(STATUS "DFS engine: QVX backend (qcom-qvx ${QVX_VERSION})")

  add_library(${GST_DFS} SHARED
    dfs.c
    dfs-engine-qvx.cc
  )

  target_compile_definitions(${GST_DFS} PRIVATE USE_QVX=1)

  target_include_directories(${GST_DFS} PRIVATE
    ${GST_INCLUDE_DIRS}
    ${QVX_INCLUDE_DIRS}
  )

  target_link_libraries(${GST_DFS} PRIVATE
    ${GST_LIBRARIES}
    ${GST_BASE_LIBRARIES}
    ${GST_ALLOC_LIBRARIES}
    ${GST_VIDEO_LIBRARIES}
    ${CMAKE_DL_LIBS}
    ${GST_QCOM_VIDEO_LIBRARIES}
    ${GST_QCOM_UTILS_LIBRARIES}
    ${GST_QCOM_ALLOC_LIBRARIES}
    ${QVX_LIBRARIES}
  )

else()

  include(CheckIncludeFileCXX)
  check_include_file_cxx("dfs_factory.h" HAVE_DFS_FACTORY_H)

  if(HAVE_DFS_FACTORY_H)

    message(STATUS "DFS engine: rvSDK backend (dfs_factory.h found)")

    add_library(${GST_DFS} SHARED
      dfs.c
      dfs-engine.cc
    )

    # Add target specific precompiler definitions.
    set(RVSDK_API_VERSION "0x202404" CACHE STRING "RVSDK_API_VERSION")
    target_compile_definitions(${GST_DFS} PRIVATE
      RVSDK_API_VERSION=${RVSDK_API_VERSION})

    target_include_directories(${GST_DFS} PRIVATE
      ${GST_INCLUDE_DIRS}
    )

    target_link_libraries(${GST_DFS} PRIVATE
      ${GST_LIBRARIES}
      ${GST_BASE_LIBRARIES}
      ${GST_ALLOC_LIBRARIES}
      ${GST_VIDEO_LIBRARIES}
      ${CMAKE_DL_LIBS}
      ${GST_QCOM_VIDEO_LIBRARIES}
      ${GST_QCOM_UTILS_LIBRARIES}
      ${GST_QCOM_ALLOC_LIBRARIES}
    )

  else()

    message(FATAL_ERROR
      "DFS engine: no backend found. "
      "Provide either qcom-qvx (pkg-config) or the rvSDK (dfs_factory.h).")

  endif()

endif()

install(
  TARGETS ${GST_DFS}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
