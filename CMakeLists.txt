cmake_minimum_required(VERSION 3.22)

project(playbach LANGUAGES CXX C OBJCXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(USE_MUSESCORE "Build with MuseScore engine support (adds --musescore flag)" OFF)

# --- MuseScore build infrastructure (only when enabled) ---
if(USE_MUSESCORE)
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
endif()

find_package(Qt6 6.3 REQUIRED COMPONENTS Core Gui Widgets Svg Xml WebEngineWidgets WebChannel)
qt_standard_project_setup()

if(USE_MUSESCORE)
    find_package(Qt6 6.3 REQUIRED COMPONENTS Quick)
    include(MuseCreateModule)
endif()

# Verovio engine
set(VEROVIO_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/thirdparty/verovio)
set(BUILD_AS_LIBRARY ON)
add_subdirectory(${VEROVIO_SOURCE_DIR}/cmake ${CMAKE_CURRENT_BINARY_DIR}/build/verovio)

if(USE_MUSESCORE)
    include_directories(
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/thirdparty
        ${MU_ROOT}/thirdparty/dtl
        ${MU_ROOT}/thirdparty
    )

    set(THIRDPARTY_DIR ${MU_ROOT}/thirdparty)
    set(MUSE_QT_SUPPORT ON)

    # Disable features we don't need
    set(ENGRAVING_NO_ACCESSIBILITY ON)
    set(MUSICXML_NO_INTERNAL ON)
    set(GLOBAL_NO_INTERNAL ON)

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

    # Add MuseScore modules
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

    target_include_directories(engraving PRIVATE ${MU_ROOT})
    target_include_directories(iex_musicxml PRIVATE ${MU_ROOT})
    target_link_libraries(iex_musicxml PRIVATE muse_draw)

    # Exclude API v1 source files from engraving
    get_target_property(ENG_SOURCES engraving SOURCES)
    list(FILTER ENG_SOURCES EXCLUDE REGEX ".*/api/v1/.*")
    set_target_properties(engraving PROPERTIES SOURCES "${ENG_SOURCES}")

    # Add back internal files that engraving/draw modules need
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
else()
    include_directories(
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/thirdparty
    )
endif()

# FluidSynth static library (used by both engines for play-along)
set(MU_A_ROOT ${CMAKE_CURRENT_LIST_DIR}/thirdparty/musescore_a)
set(FLUIDSYNTH_CMAKE_DIR ${MU_A_ROOT}/src/framework/audio/thirdparty/fluidsynth)
include(${FLUIDSYNTH_CMAKE_DIR}/fluidsynth.cmake)

add_library(fluidsynth_lib STATIC
    ${FLUIDSYNTH_SRC}
    src/vorbis_bridge.c
)
target_include_directories(fluidsynth_lib PUBLIC ${FLUIDSYNTH_INC})
target_include_directories(fluidsynth_lib PRIVATE
    ${MU_A_ROOT}/src/framework/audio/thirdparty
)
target_compile_definitions(fluidsynth_lib PUBLIC
    ${FLUIDSYNTH_DEF}
    EXTERN_VORBIS_SUPPORT=1
)
set_target_properties(fluidsynth_lib PROPERTIES COMPILE_FLAGS "-fPIC")

# --- Executable sources ---
set(PLAYBACH_SOURCES
    src/main.cpp
    src/app.h
    src/app.cpp
    src/engine/ScoreEngine.h
    src/engine/VerovioEngine.h
    src/engine/VerovioEngine.cpp
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
    src/youtubeplayer.h
    src/youtubeplayer.cpp
    src/syncmode.h
    src/syncmode.cpp
    src/syncpanel.h
    src/syncpanel.cpp
    src/waveformwidget.h
    src/waveformwidget.mm
    src/playalongsynth.h
    src/playalongsynth.cpp
    src/worldmodel.h
    src/worldbrowser.h
    src/worldbrowser.cpp
    src/miniaudio_impl.c
)

if(USE_MUSESCORE)
    list(APPEND PLAYBACH_SOURCES
        src/engine/MuseScoreEngine.h
        src/engine/MuseScoreEngine.cpp
    )
endif()

add_executable(playbach ${PLAYBACH_SOURCES})

qt_add_resources(playbach "icons"
    PREFIX "/"
    FILES
        src/icons/youtube.png
)

set_target_properties(playbach PROPERTIES COMPILE_FLAGS "-fPIC")

if(USE_MUSESCORE)
    target_compile_definitions(playbach PRIVATE USE_MUSESCORE)

    set_target_properties(engraving PROPERTIES COMPILE_FLAGS "-fPIC")
    set_target_properties(muse_global PROPERTIES COMPILE_FLAGS "-fPIC")
    set_target_properties(muse_draw PROPERTIES COMPILE_FLAGS "-fPIC")
    set_target_properties(iex_musicxml PROPERTIES COMPILE_FLAGS "-fPIC")

    target_include_directories(playbach PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/thirdparty
        ${MU_ROOT}/src/framework
        ${MU_ROOT}/src/framework/global
        ${MU_ROOT}/src/framework/draw
        ${MU_ROOT}/src
        ${MU_ROOT}/src/engraving
        ${MU_ROOT}
        ${FLUIDSYNTH_INC}
    )
    target_compile_options(playbach PRIVATE ${PCH_FIX})
else()
    target_include_directories(playbach PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/thirdparty
        ${FLUIDSYNTH_INC}
    )
endif()

target_include_directories(playbach PRIVATE
    ${VEROVIO_SOURCE_DIR}/include/vrv
    ${VEROVIO_SOURCE_DIR}/include/json
    ${VEROVIO_SOURCE_DIR}/include/pugi
    ${VEROVIO_SOURCE_DIR}/include/hum
    ${VEROVIO_SOURCE_DIR}/include/crc
    ${VEROVIO_SOURCE_DIR}/include/midi
    ${VEROVIO_SOURCE_DIR}/include/zip
    ${VEROVIO_SOURCE_DIR}/include/tuning-library
    ${VEROVIO_SOURCE_DIR}/libmei/dist
    ${VEROVIO_SOURCE_DIR}/libmei/addons
)

set(PLAYBACH_LIBS
    verovio
    fluidsynth_lib
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::Xml
    Qt6::WebEngineWidgets
    Qt6::WebChannel
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework CoreFoundation"
    "-framework AppKit"
    "-framework AVFoundation"
    "-framework Accelerate"
)

if(USE_MUSESCORE)
    list(APPEND PLAYBACH_LIBS
        muse_global
        muse_draw
        engraving
        iex_musicxml
    )
endif()

target_link_libraries(playbach PRIVATE ${PLAYBACH_LIBS})
