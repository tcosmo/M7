cmake_minimum_required(VERSION 3.15)
project(fluidsynth_addon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── cmake-js integration ──────────────────────────────────────────
# cmake-js sets these variables:
#   CMAKE_JS_INC  - Node.js / N-API include dirs
#   CMAKE_JS_LIB  - Node.js library (Windows only)

# Find node-addon-api headers
execute_process(
  COMMAND node -p "require('node-addon-api').include"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE NODE_ADDON_API_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
# Strip quotes
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})

# ── FluidSynth from local M7 project ────────────────────────────
# Use the same FluidSynth 2.3.3 sources that the Qt app uses,
# and link the pre-built static library from the Qt build.
set(M7_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(FLUIDSYNTH_INCLUDE_DIR "${M7_ROOT}/thirdparty/musescore_a/src/framework/audio/thirdparty/fluidsynth/fluidsynth-2.3.3/include")
set(FLUIDSYNTH_LIBRARY "${M7_ROOT}/build/release/libfluidsynth_lib.a")

if(NOT EXISTS "${FLUIDSYNTH_LIBRARY}")
  message(FATAL_ERROR
    "FluidSynth static lib not found at ${FLUIDSYNTH_LIBRARY}\n"
    "Build the Qt app first: cmake --build build/release")
endif()

# ── Addon target ──────────────────────────────────────────────────
add_library(${PROJECT_NAME} SHARED
  fluidsynth_addon.cpp
)

# .node extension
set_target_properties(${PROJECT_NAME} PROPERTIES
  PREFIX ""
  SUFFIX ".node"
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_JS_INC}
  ${NODE_ADDON_API_DIR}
  ${FLUIDSYNTH_INCLUDE_DIR}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  NAPI_VERSION=8
  NAPI_DISABLE_CPP_EXCEPTIONS
)

# Link local FluidSynth static library
target_link_libraries(${PROJECT_NAME} PRIVATE ${FLUIDSYNTH_LIBRARY})

# Link cmake-js Node library (needed on Windows)
if(CMAKE_JS_LIB)
  target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_JS_LIB})
endif()

# macOS frameworks (required by FluidSynth's CoreAudio driver)
if(APPLE)
  target_link_libraries(${PROJECT_NAME} PRIVATE
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework CoreMIDI"
    "-framework CoreFoundation"
    "-framework AppKit"
    "-framework AVFoundation"
    "-framework Accelerate"
  )
endif()
