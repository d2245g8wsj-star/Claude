cmake_minimum_required(VERSION 3.22)

project(Crest VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE
#
# Either point the build at a local checkout:
#     cmake -B build -DJUCE_PATH=/path/to/JUCE
# or let CMake fetch a pinned copy automatically.
# ---------------------------------------------------------------------------
if(DEFINED JUCE_PATH)
    message(STATUS "Crest: using local JUCE at ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(CREST_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND CREST_FORMATS AU)
endif()

juce_add_plugin(Crest
    COMPANY_NAME                "IndieWave Audio"
    COMPANY_WEBSITE             "https://indiewave.audio"
    BUNDLE_ID                   "audio.indiewave.crest"
    PLUGIN_MANUFACTURER_CODE    Idwv
    PLUGIN_CODE                 Crst
    FORMATS                     ${CREST_FORMATS}
    PRODUCT_NAME                "Crest"
    DESCRIPTION                 "Converter and clipper"
    VST3_CATEGORIES             Fx Distortion Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

# ---------------------------------------------------------------------------
# Panel art. The plate and the knob filmstrips are rendered in Blender and
# dropped in here; the frame count is read from the image at runtime so a
# higher resolution render replaces the file without touching code.
# ---------------------------------------------------------------------------
juce_add_binary_data(CrestAssets SOURCES
    resources/gui/plate.png
    resources/gui/knob_large.png
    resources/gui/character_selector_5.png
    resources/gui/series_selector_4.png
    resources/gui/led_off.png
    resources/gui/led_green.png
    resources/gui/led_amber.png
    resources/gui/led_red.png)

set_target_properties(CrestAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(Crest PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/gui/Panel.cpp)

target_include_directories(Crest PRIVATE src)

target_compile_definitions(Crest PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Crest PRIVATE
    CrestAssets
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline checks:  cmake -B build -DCREST_BUILD_TESTS=ON && ./build/crest_tests
# ---------------------------------------------------------------------------
option(CREST_BUILD_TESTS "Build the Crest offline checks" OFF)

if(CREST_BUILD_TESTS)
    juce_add_console_app(crest_tests PRODUCT_NAME "crest_tests")

    target_sources(crest_tests PRIVATE tests/Tests.cpp)
    target_include_directories(crest_tests PRIVATE src)
    target_link_libraries(crest_tests PRIVATE
        Crest
        juce::juce_audio_utils
        juce::juce_dsp)

    enable_testing()
    add_test(NAME crest_offline_checks COMMAND crest_tests)
endif()

# ---------------------------------------------------------------------------
# Offscreen editor snapshot:
#     cmake -B build -DCREST_BUILD_TOOLS=ON && ./build/.../crest_shot docs/ui.png
# ---------------------------------------------------------------------------
option(CREST_BUILD_TOOLS "Build the editor snapshot tool" OFF)

if(CREST_BUILD_TOOLS)
    juce_add_console_app(crest_shot PRODUCT_NAME "crest_shot")

    target_sources(crest_shot PRIVATE tools/Snapshot.cpp)
    target_include_directories(crest_shot PRIVATE src)
    target_link_libraries(crest_shot PRIVATE
        Crest
        juce::juce_audio_utils
        juce::juce_dsp)
endif()
