cmake_minimum_required(VERSION 3.22)

project(scoretracker LANGUAGES CXX C OBJCXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(MU_ROOT ${CMAKE_CURRENT_LIST_DIR}/thirdparty/musescore)
set(PROJECT_ROOT_DIR ${MU_ROOT})
set(MUSE_FRAMEWORK_PATH ${MU_ROOT}/src)

set(CMAKE_MODULE_PATH
    ${MU_ROOT}/buildscripts
    ${MU_ROOT}/buildscripts/cmake
    ${MU_ROOT}/src/framework/cmake
    ${CMAKE_MODULE_PATH}
)

include(SetupBuildEnvironment)
include(GetPlatformInfo)

find_package(Qt6 6.3 REQUIRED COMPONENTS Core Gui Widgets Svg Quick)
qt_standard_project_setup()

include(MuseCreateModule)

include_directories(
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/thirdparty
    ${MU_ROOT}/thirdparty/dtl
    ${MU_ROOT}/thirdparty
)

set(THIRDPARTY_DIR ${MU_ROOT}/thirdparty)
set(MUSE_QT_SUPPORT ON)

# Disable features we don't need (CMake variables for subdirectory CMakeLists)
set(ENGRAVING_NO_ACCESSIBILITY ON)
set(MUSICXML_NO_INTERNAL ON)
set(GLOBAL_NO_INTERNAL ON)

# Compile definitions
add_compile_definitions(
    MUSICXML_NO_INTERNAL
    ENGRAVING_NO_API
)

# Generate muse_framework_config.h
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/src/muse_framework_config.h
    ${CMAKE_CURRENT_BINARY_DIR}/muse_framework_config.h
    COPYONLY
)

# Add MuseScore modules (with full internal support)
add_subdirectory(${MU_ROOT}/src/framework/global global)
add_subdirectory(${MU_ROOT}/src/framework/draw draw)
add_subdirectory(${MU_ROOT}/src/engraving engraving)
add_subdirectory(${MU_ROOT}/src/importexport/musicxml iex_musicxml)

# Fix: Some modules use Qt macros that are normally provided via PCH
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/src/pch_fix.h
    ${CMAKE_CURRENT_BINARY_DIR}/pch_fix.h
    COPYONLY
)
set(PCH_FIX "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_BINARY_DIR}/pch_fix.h>")
target_compile_options(muse_global PRIVATE ${PCH_FIX})
target_compile_options(muse_draw PRIVATE ${PCH_FIX})
target_compile_options(engraving PRIVATE ${PCH_FIX})
target_compile_options(iex_musicxml PRIVATE ${PCH_FIX})

# Fix: engraving and musicxml use thirdparty/ relative includes
target_include_directories(engraving PRIVATE ${MU_ROOT})
target_include_directories(iex_musicxml PRIVATE ${MU_ROOT})

# Fix: iex_musicxml uses muse_draw for FontMetrics but doesn't link it
target_link_libraries(iex_musicxml PRIVATE muse_draw)

# Exclude API v1 source files from engraving (they need full QML/MOC support)
get_target_property(ENG_SOURCES engraving SOURCES)
list(FILTER ENG_SOURCES EXCLUDE REGEX ".*/api/v1/.*")
set_target_properties(engraving PROPERTIES SOURCES "${ENG_SOURCES}")

# GLOBAL_NO_INTERNAL excludes globalmodule, settings, API files, and internals.
# Add back the specific internal files that engraving/draw modules need.
set(GLOBAL_SRC_DIR ${MU_ROOT}/src/framework/global)
target_sources(muse_global PRIVATE
    ${GLOBAL_SRC_DIR}/settings.cpp
    ${GLOBAL_SRC_DIR}/settings.h
    ${GLOBAL_SRC_DIR}/io/internal/filesystem.cpp
    ${GLOBAL_SRC_DIR}/io/internal/filesystem.h
    ${GLOBAL_SRC_DIR}/internal/globalconfiguration.cpp
    ${GLOBAL_SRC_DIR}/internal/globalconfiguration.h
    ${GLOBAL_SRC_DIR}/internal/interactive.cpp
    ${GLOBAL_SRC_DIR}/internal/interactive.h
    ${GLOBAL_SRC_DIR}/internal/cryptographichash.cpp
    ${GLOBAL_SRC_DIR}/internal/cryptographichash.h
    ${GLOBAL_SRC_DIR}/internal/process.cpp
    ${GLOBAL_SRC_DIR}/internal/process.h
    ${GLOBAL_SRC_DIR}/internal/systeminfo.cpp
    ${GLOBAL_SRC_DIR}/internal/systeminfo.h
    ${GLOBAL_SRC_DIR}/internal/tickerprovider.cpp
    ${GLOBAL_SRC_DIR}/internal/tickerprovider.h
)

# macOS-specific global sources
target_sources(muse_global PRIVATE
    ${GLOBAL_SRC_DIR}/internal/platform/macos/macosinteractivehelper.mm
    ${GLOBAL_SRC_DIR}/internal/platform/macos/macosinteractivehelper.h
)
set_source_files_properties(
    ${GLOBAL_SRC_DIR}/internal/platform/macos/macosinteractivehelper.mm
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON SKIP_PRECOMPILE_HEADERS ON
)
find_library(AppKit_Lib NAMES AppKit)
target_link_libraries(muse_global PRIVATE ${AppKit_Lib})

add_executable(scoretracker
    src/main.cpp
    src/app.h
    src/app.cpp
    src/scorewidget.h
    src/scorewidget.cpp
    src/audioplayer.h
    src/audioplayer.mm
    src/synctimer.h
    src/synctimer.cpp
    src/partpanel.h
    src/partpanel.cpp
    src/collapsiblesection.h
    src/displaysettings.h
    src/displaysettings.cpp
    src/trackingsettings.h
    src/trackingsettings.cpp
    src/beatdata.h
)

set_target_properties(engraving PROPERTIES COMPILE_FLAGS "-fPIC")
set_target_properties(muse_global PROPERTIES COMPILE_FLAGS "-fPIC")
set_target_properties(muse_draw PROPERTIES COMPILE_FLAGS "-fPIC")
set_target_properties(iex_musicxml PROPERTIES COMPILE_FLAGS "-fPIC")
set_target_properties(scoretracker PROPERTIES COMPILE_FLAGS "-fPIC")

target_include_directories(scoretracker PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/thirdparty
    ${MU_ROOT}/src/framework
    ${MU_ROOT}/src/framework/global
    ${MU_ROOT}/src/framework/draw
    ${MU_ROOT}/src
    ${MU_ROOT}/src/engraving
    ${MU_ROOT}
)

target_compile_options(scoretracker PRIVATE ${PCH_FIX})

target_link_libraries(scoretracker
    muse_global
    muse_draw
    engraving
    iex_musicxml
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework CoreFoundation"
    "-framework AppKit"
    "-framework AVFoundation"
)
