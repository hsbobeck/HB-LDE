cmake_minimum_required(VERSION 3.22...3.25)

legacy_check()

add_subdirectory(obs-frontend-api)

option(ENABLE_UI "Enable building with UI (requires Qt)" ON)

if(NOT ENABLE_UI)
  target_disable_feature(obs "User Interface")
  return()
else()
  target_enable_feature(obs "User Interface")
endif()

find_package(FFmpeg REQUIRED COMPONENTS avcodec avutil avformat)
find_package(CURL REQUIRED)

if(NOT TARGET OBS::json11)
  add_subdirectory("${CMAKE_SOURCE_DIR}/deps/json11" "${CMAKE_BINARY_DIR}/deps/json11")
endif()

add_executable(obs-studio)
add_executable(OBS::studio ALIAS obs-studio)

target_link_libraries(
  obs-studio
  PRIVATE CURL::libcurl
          FFmpeg::avcodec
          FFmpeg::avutil
          FFmpeg::avformat
          OBS::libobs
          OBS::frontend-api
          OBS::json11)

if(OS_WINDOWS)
  target_include_directories(
    obs-studio
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../lde2-deps/WinSparkle-0.8.1/include")
  target_link_libraries(
    obs-studio
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../lde2-deps/WinSparkle-0.8.1/x64/Release/WinSparkle.lib")
endif()

include(cmake/ui-qt.cmake)
include(cmake/ui-elements.cmake)
include(cmake/ui-windows.cmake)
include(cmake/feature-importers.cmake)
include(cmake/feature-browserpanels.cmake)

if(NOT OAUTH_BASE_URL)
  # cmake-format: off
  set(OAUTH_BASE_URL "https://auth.obsproject.com/" CACHE STRING "Default OAuth base URL")
  mark_as_advanced(OAUTH_BASE_URL)
  # cmake-format: on
endif()
include(cmake/feature-twitch.cmake)
include(cmake/feature-restream.cmake)
include(cmake/feature-youtube.cmake)
include(cmake/feature-sparkle.cmake)
include(cmake/feature-whatsnew.cmake)

add_subdirectory(frontend-plugins)

configure_file(ui-config.h.in ui-config.h)

target_sources(
  obs-studio
  PRIVATE # cmake-format: sortable
          api-interface.cpp
          auth-base.cpp
          auth-base.hpp
          auth-listener.cpp
          auth-listener.hpp
          auth-oauth.cpp
          auth-oauth.hpp
          display-helpers.hpp
          ffmpeg-utils.cpp
          ffmpeg-utils.hpp
          multiview.cpp
          multiview.hpp
          obf.c
          obf.h
          obs-app.cpp
          obs-app.hpp
          obs-proxy-style.cpp
          obs-proxy-style.hpp
          platform.hpp
          qt-display.cpp
          qt-display.hpp
          qt-wrappers.cpp
          qt-wrappers.hpp
          ui-config.h
          ui-validation.cpp
          ui-validation.hpp)

if(OS_WINDOWS)
  include(cmake/os-windows.cmake)
elseif(OS_MACOS)
  include(cmake/os-macos.cmake)
elseif(OS_LINUX)
  include(cmake/os-linux.cmake)
elseif(OS_FREEBSD)
  include(cmake/os-freebsd.cmake)
endif()

foreach(graphics_library IN ITEMS opengl metal d3d11)
  string(TOUPPER ${graphics_library} graphics_library_U)
  if(TARGET OBS::libobs-${graphics_library})
    target_compile_definitions(obs-studio
                               PRIVATE DL_${graphics_library_U}="$<TARGET_FILE_NAME:OBS::libobs-${graphics_library}>")
  else()
    target_compile_definitions(obs-studio PRIVATE DL_${graphics_library_U}="")
  endif()
endforeach()

get_property(obs_module_list GLOBAL PROPERTY OBS_MODULES_ENABLED)
list(JOIN obs_module_list "|" SAFE_MODULES)
target_compile_definitions(obs-studio PRIVATE "SAFE_MODULES=\"${SAFE_MODULES}\"")

if(OS_WINDOWS)
  add_custom_command(
    TARGET obs-studio
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../lde2-deps/WinSparkle-0.8.1/x64/Release/WinSparkle.dll"
            "${OBS_OUTPUT_DIR}/$<CONFIG>/${OBS_EXECUTABLE_DESTINATION}/"
    COMMENT "Copy WinSparkle"
    VERBATIM)

  add_custom_command(
    TARGET obs-studio
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${LDE_AWSCPPSDK_DIR}/bin/aws-c-auth.dll"
            "${LDE_AWSCPPSDK_DIR}/bin/aws-c-cal.dll"
            "${LDE_AWSCPPSDK_DIR}/bin/aws-c-common.dll"
            "${LDE_AWSCPPSDK_DIR}/bin/aws-c-compression.dll"
            "${LDE_AWSCPPSDK_DIR}/bin/aws-c-event-stream.dll"
            "${LDE_AWSCPPSDK_DIR}/bin/aws-c-http.dll"
            "${LDE_AWSCPPSDK_DIR}/bin/aws-c-io.dll"
            "${LDE_AWSCPPSDK_DIR}/bin/aws-c-mqtt.dll"
            "${LDE_AWSCPPSDK_DIR}/bin/aws-c-s3.dll"
            "${LDE_AWSCPPSDK_DIR}/bin/aws-c-sdkutils.dll"
            "${LDE_AWSCPPSDK_DIR}/bin/aws-checksums.dll"
            "${LDE_AWSCPPSDK_DIR}/bin/aws-cpp-sdk-core.dll"
            "${LDE_AWSCPPSDK_DIR}/bin/aws-cpp-sdk-s3.dll"
            "${LDE_AWSCPPSDK_DIR}/bin/aws-crt-cpp.dll"
            "${OBS_OUTPUT_DIR}/$<CONFIG>/${OBS_EXECUTABLE_DESTINATION}/"
    COMMENT "Copy AWS DLLs"
    VERBATIM)
endif()

# cmake-format: off
set_target_properties_obs(obs-studio PROPERTIES FOLDER frontend OUTPUT_NAME "$<IF:$<PLATFORM_ID:Windows>,hb-lde64,hb-lde>")
# cmake-format: on
