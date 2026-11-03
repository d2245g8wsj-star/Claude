cmake_minimum_required(VERSION 3.22)

project(Tide VERSION 0.3.0 LANGUAGES C CXX)

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
    message(STATUS "Tide: using local JUCE at ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(TIDE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TIDE_FORMATS AU)
endif()

juce_add_plugin(Tide
    COMPANY_NAME                "IndieWave Audio"
    COMPANY_WEBSITE             "https://indiewave.audio"
    BUNDLE_ID                   "audio.indiewave.tide"
    PLUGIN_MANUFACTURER_CODE    Idwv
    PLUGIN_CODE                 Tide
    FORMATS                     ${TIDE_FORMATS}
    PRODUCT_NAME                "Tide"
    DESCRIPTION                 "Three band mastering equaliser with mid/side"
    VST3_CATEGORIES             Fx EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Tide PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/gui/Panel.cpp)

target_include_directories(Tide PRIVATE src)

target_compile_definitions(Tide PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Tide PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline checks:  cmake -B build -DTIDE_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(TIDE_BUILD_TESTS "Build the Tide offline checks" OFF)

if(TIDE_BUILD_TESTS)
    juce_add_console_app(tide_tests PRODUCT_NAME "tide_tests")

    target_sources(tide_tests PRIVATE tests/Tests.cpp)
    target_include_directories(tide_tests PRIVATE src)
    target_link_libraries(tide_tests PRIVATE
        Tide
        juce::juce_audio_utils
        juce::juce_dsp)

    enable_testing()
    add_test(NAME tide_offline_checks COMMAND tide_tests)
endif()

# ---------------------------------------------------------------------------
# Offscreen editor snapshot:  cmake -B build -DTIDE_BUILD_TOOLS=ON
# ---------------------------------------------------------------------------
option(TIDE_BUILD_TOOLS "Build the editor snapshot tool" OFF)

if(TIDE_BUILD_TOOLS)
    juce_add_console_app(tide_shot PRODUCT_NAME "tide_shot")

    target_sources(tide_shot PRIVATE tools/Snapshot.cpp)
    target_include_directories(tide_shot PRIVATE src)
    target_link_libraries(tide_shot PRIVATE
        Tide
        juce::juce_audio_utils
        juce::juce_dsp)
endif()
