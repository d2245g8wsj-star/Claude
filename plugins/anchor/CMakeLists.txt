cmake_minimum_required(VERSION 3.22)

project(Anchor VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE, pinned to the same version as the rest of the range.
#     cmake -B build -DJUCE_PATH=/path/to/JUCE
# or let CMake fetch it.
# ---------------------------------------------------------------------------
if(DEFINED JUCE_PATH)
    message(STATUS "Anchor: using local JUCE at ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ANCHOR_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND ANCHOR_FORMATS AU)
endif()

juce_add_plugin(Anchor
    COMPANY_NAME                "IndieWave Audio"
    COMPANY_WEBSITE             "https://indiewave.audio"
    BUNDLE_ID                   "audio.indiewave.anchor"
    PLUGIN_MANUFACTURER_CODE    Idwv
    PLUGIN_CODE                 Ancr
    FORMATS                     ${ANCHOR_FORMATS}
    PRODUCT_NAME                "Anchor"
    DESCRIPTION                 "VCA bus compressor"
    VST3_CATEGORIES             Fx Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Anchor PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/indiewave/ui/Panel.cpp)

target_include_directories(Anchor PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../../shared)

target_compile_definitions(Anchor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Anchor PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline checks:  cmake -B build -DANCHOR_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(ANCHOR_BUILD_TESTS "Build the Anchor offline checks" OFF)

if(ANCHOR_BUILD_TESTS)
    juce_add_console_app(anchor_tests PRODUCT_NAME "anchor_tests")

    target_sources(anchor_tests PRIVATE tests/Tests.cpp)
    target_include_directories(anchor_tests PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../../shared)
    target_compile_definitions(anchor_tests PRIVATE
        INDIEWAVE_COMPAT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/compat"
        INDIEWAVE_VERSION="${PROJECT_VERSION}")
    target_link_libraries(anchor_tests PRIVATE
        Anchor
        juce::juce_audio_utils
        juce::juce_dsp)

    enable_testing()
    add_test(NAME anchor_offline_checks COMMAND anchor_tests)
endif()

# ---------------------------------------------------------------------------
# Offscreen editor snapshot:  cmake -B build -DANCHOR_BUILD_TOOLS=ON
# ---------------------------------------------------------------------------
option(ANCHOR_BUILD_TOOLS "Build the editor snapshot tool" OFF)

if(ANCHOR_BUILD_TOOLS)
    juce_add_console_app(anchor_shot PRODUCT_NAME "anchor_shot")

    target_sources(anchor_shot PRIVATE tools/Snapshot.cpp)
    target_include_directories(anchor_shot PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../../shared)
    target_link_libraries(anchor_shot PRIVATE
        Anchor
        juce::juce_audio_utils
        juce::juce_dsp)
endif()
