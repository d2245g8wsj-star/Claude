cmake_minimum_required(VERSION 3.22)

project(Ember VERSION 1.0.0 LANGUAGES C CXX)

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
    message(STATUS "Ember: using local JUCE at ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(EMBER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND EMBER_FORMATS AU)
endif()

juce_add_plugin(Ember
    COMPANY_NAME                "IndieWave Audio"
    COMPANY_WEBSITE             "https://indiewave.audio"
    BUNDLE_ID                   "audio.indiewave.ember"
    PLUGIN_MANUFACTURER_CODE    Idwv
    PLUGIN_CODE                 Embr
    FORMATS                     ${EMBER_FORMATS}
    PRODUCT_NAME                "Ember"
    DESCRIPTION                 "Two channel harmonic equaliser"
    VST3_CATEGORIES             Fx EQ Distortion
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Ember PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/gui/Panel.cpp)

target_include_directories(Ember PRIVATE src)

target_compile_definitions(Ember PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Ember PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline checks:  cmake -B build -DEMBER_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(EMBER_BUILD_TESTS "Build the Ember offline checks" OFF)

if(EMBER_BUILD_TESTS)
    juce_add_console_app(ember_tests PRODUCT_NAME "ember_tests")

    target_sources(ember_tests PRIVATE tests/Tests.cpp)
    target_include_directories(ember_tests PRIVATE src)
    target_link_libraries(ember_tests PRIVATE
        Ember
        juce::juce_audio_utils
        juce::juce_dsp)

    enable_testing()
    add_test(NAME ember_offline_checks COMMAND ember_tests)
endif()

# ---------------------------------------------------------------------------
# Offscreen editor snapshot:  cmake -B build -DEMBER_BUILD_TOOLS=ON
# ---------------------------------------------------------------------------
option(EMBER_BUILD_TOOLS "Build the editor snapshot tool" OFF)

if(EMBER_BUILD_TOOLS)
    juce_add_console_app(ember_shot PRODUCT_NAME "ember_shot")

    target_sources(ember_shot PRIVATE tools/Snapshot.cpp)
    target_include_directories(ember_shot PRIVATE src)
    target_link_libraries(ember_shot PRIVATE
        Ember
        juce::juce_audio_utils
        juce::juce_dsp)
endif()
