cmake_minimum_required(VERSION 3.22)

project(Swell VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE, pinned to the same version Crest builds against.
#     cmake -B build -DJUCE_PATH=/path/to/JUCE
# or let CMake fetch it.
# ---------------------------------------------------------------------------
if(DEFINED JUCE_PATH)
    message(STATUS "Swell: using local JUCE at ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(SWELL_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SWELL_FORMATS AU)
endif()

juce_add_plugin(Swell
    COMPANY_NAME                "IndieWave Audio"
    COMPANY_WEBSITE             "https://indiewave.audio"
    BUNDLE_ID                   "audio.indiewave.swell"
    PLUGIN_MANUFACTURER_CODE    Idwv
    PLUGIN_CODE                 Swll
    FORMATS                     ${SWELL_FORMATS}
    PRODUCT_NAME                "Swell"
    DESCRIPTION                 "Two channel harmonic equaliser"
    VST3_CATEGORIES             Fx EQ Distortion
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Swell PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/gui/Panel.cpp)

target_include_directories(Swell PRIVATE src)

target_compile_definitions(Swell PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Swell PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline checks:  cmake -B build -DSWELL_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(SWELL_BUILD_TESTS "Build the Swell offline checks" OFF)

if(SWELL_BUILD_TESTS)
    juce_add_console_app(swell_tests PRODUCT_NAME "swell_tests")

    target_sources(swell_tests PRIVATE tests/Tests.cpp)
    target_include_directories(swell_tests PRIVATE src)
    target_link_libraries(swell_tests PRIVATE
        Swell
        juce::juce_audio_utils
        juce::juce_dsp)

    enable_testing()
    add_test(NAME swell_offline_checks COMMAND swell_tests)
endif()

# ---------------------------------------------------------------------------
# Offscreen editor snapshot:  cmake -B build -DSWELL_BUILD_TOOLS=ON
# ---------------------------------------------------------------------------
option(SWELL_BUILD_TOOLS "Build the editor snapshot tool" OFF)

if(SWELL_BUILD_TOOLS)
    juce_add_console_app(swell_shot PRODUCT_NAME "swell_shot")

    target_sources(swell_shot PRIVATE tools/Snapshot.cpp)
    target_include_directories(swell_shot PRIVATE src)
    target_link_libraries(swell_shot PRIVATE
        Swell
        juce::juce_audio_utils
        juce::juce_dsp)
endif()
