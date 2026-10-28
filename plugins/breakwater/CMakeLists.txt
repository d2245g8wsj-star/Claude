cmake_minimum_required(VERSION 3.22)

project(Breakwater VERSION 1.0.0 LANGUAGES C CXX)

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
    message(STATUS "Breakwater: using local JUCE at ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(BREAKWATER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND BREAKWATER_FORMATS AU)
endif()

juce_add_plugin(Breakwater
    COMPANY_NAME                "IndieWave Audio"
    COMPANY_WEBSITE             "https://indiewave.audio"
    BUNDLE_ID                   "audio.indiewave.breakwater"
    PLUGIN_MANUFACTURER_CODE    Idwv
    PLUGIN_CODE                 Brkw
    FORMATS                     ${BREAKWATER_FORMATS}
    PRODUCT_NAME                "Breakwater"
    DESCRIPTION                 "Mastering limiter"
    VST3_CATEGORIES             Fx Dynamics Mastering
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Breakwater PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/gui/Panel.cpp
    src/gui/Display.cpp)

target_include_directories(Breakwater PRIVATE src)

target_compile_definitions(Breakwater PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Breakwater PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline checks:  cmake -B build -DBREAKWATER_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(BREAKWATER_BUILD_TESTS "Build the Breakwater offline checks" OFF)

if(BREAKWATER_BUILD_TESTS)
    juce_add_console_app(breakwater_tests PRODUCT_NAME "breakwater_tests")

    target_sources(breakwater_tests PRIVATE tests/Tests.cpp)
    target_include_directories(breakwater_tests PRIVATE src)
    target_link_libraries(breakwater_tests PRIVATE
        Breakwater
        juce::juce_audio_utils
        juce::juce_dsp)

    enable_testing()
    add_test(NAME breakwater_offline_checks COMMAND breakwater_tests)
endif()

# ---------------------------------------------------------------------------
# Offscreen editor snapshot:  cmake -B build -DBREAKWATER_BUILD_TOOLS=ON
# ---------------------------------------------------------------------------
option(BREAKWATER_BUILD_TOOLS "Build the editor snapshot tool" OFF)

if(BREAKWATER_BUILD_TOOLS)
    juce_add_console_app(breakwater_shot PRODUCT_NAME "breakwater_shot")

    target_sources(breakwater_shot PRIVATE tools/Snapshot.cpp)
    target_include_directories(breakwater_shot PRIVATE src)
    target_link_libraries(breakwater_shot PRIVATE
        Breakwater
        juce::juce_audio_utils
        juce::juce_dsp)
endif()
