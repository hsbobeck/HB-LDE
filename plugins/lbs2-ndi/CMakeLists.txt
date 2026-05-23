cmake_minimum_required(VERSION 3.16...3.26)

add_library(obs-ndi MODULE)

target_link_libraries(obs-ndi PRIVATE OBS::libobs)

target_link_libraries(obs-ndi PRIVATE OBS::frontend-api)

find_qt(COMPONENTS Widgets Core)
target_link_libraries(obs-ndi PRIVATE Qt::Core Qt::Widgets)
target_compile_options(
  obs-ndi PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                -Wno-comma>)
set_target_properties(
  obs-ndi
  PROPERTIES AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON)

# cmake-format: off
# Add your custom source files here Header files are optional and only required for visibility.
# e.g. in Xcode or Visual Studio
# cmake-format: on
target_sources(
  obs-ndi
  PRIVATE src/plugin-main.cpp
          src/plugin-main.h
          src/obs-ndi-source.cpp
          src/obs-ndi-output.cpp
          src/obs-ndi-filter.cpp
          src/premultiplied-alpha-filter.cpp
          src/main-output.cpp
          src/preview-output.cpp
          src/Config.cpp
          src/forms/output-settings.cpp
          src/main-output.h
          src/preview-output.h
          src/Config.h
          src/forms/output-settings.h)

target_include_directories(obs-ndi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/ndi)

set_target_properties_obs(obs-ndi PROPERTIES
  FOLDER plugins
  PREFIX ""
  OUTPUT_NAME obs-ndi)
