cmake_minimum_required(VERSION 3.22)

project(Beacon VERSION 0.5.0 LANGUAGES C CXX)

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
    message(STATUS "Beacon: using local JUCE at ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(BEACON_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND BEACON_FORMATS AU)
endif()

juce_add_plugin(Beacon
    COMPANY_NAME                "IndieWave Audio"
    COMPANY_WEBSITE             "https://indiewave.audio"
    BUNDLE_ID                   "audio.indiewave.beacon"
    PLUGIN_MANUFACTURER_CODE    Idwv
    PLUGIN_CODE                 Bcon
    FORMATS                     ${BEACON_FORMATS}
    PRODUCT_NAME                "Beacon"
    DESCRIPTION                 "Vari-mu tube compressor"
    VST3_CATEGORIES             Fx Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Beacon PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/gui/Panel.cpp)

target_include_directories(Beacon PRIVATE src)

target_compile_definitions(Beacon PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Beacon PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline checks:  cmake -B build -DBEACON_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(BEACON_BUILD_TESTS "Build the Beacon offline checks" OFF)

if(BEACON_BUILD_TESTS)
    juce_add_console_app(beacon_tests PRODUCT_NAME "beacon_tests")

    target_sources(beacon_tests PRIVATE tests/Tests.cpp)
    target_include_directories(beacon_tests PRIVATE src)
    target_link_libraries(beacon_tests PRIVATE
        Beacon
        juce::juce_audio_utils
        juce::juce_dsp)

    enable_testing()
    add_test(NAME beacon_offline_checks COMMAND beacon_tests)
endif()

# ---------------------------------------------------------------------------
# Offscreen editor snapshot:  cmake -B build -DBEACON_BUILD_TOOLS=ON
# ---------------------------------------------------------------------------
option(BEACON_BUILD_TOOLS "Build the editor snapshot tool" OFF)

if(BEACON_BUILD_TOOLS)
    juce_add_console_app(beacon_shot PRODUCT_NAME "beacon_shot")

    target_sources(beacon_shot PRIVATE tools/Snapshot.cpp)
    target_include_directories(beacon_shot PRIVATE src)
    target_link_libraries(beacon_shot PRIVATE
        Beacon
        juce::juce_audio_utils
        juce::juce_dsp)
endif()
