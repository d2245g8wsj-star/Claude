cmake_minimum_required(VERSION 3.22)

project(Hinge VERSION 0.3.0 LANGUAGES C CXX)

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
    message(STATUS "Hinge: using local JUCE at ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(HINGE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND HINGE_FORMATS AU)
endif()

juce_add_plugin(Hinge
    COMPANY_NAME                "IndieWave Audio"
    COMPANY_WEBSITE             "https://indiewave.audio"
    BUNDLE_ID                   "audio.indiewave.hinge"
    PLUGIN_MANUFACTURER_CODE    Idwv
    PLUGIN_CODE                 Hnge
    FORMATS                     ${HINGE_FORMATS}
    PRODUCT_NAME                "Hinge"
    DESCRIPTION                 "Two channel Baxandall shelving equaliser"
    VST3_CATEGORIES             Fx EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Hinge PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/gui/Panel.cpp)

target_include_directories(Hinge PRIVATE src)

target_compile_definitions(Hinge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# A shelf is one filter followed by the exact inverse of its mirror; at the
# centre detent they cancel to the last bit only if no compiler fuses their
# multiply-adds differently. Clang does by default on Apple silicon.
if(NOT MSVC)
    target_compile_options(Hinge PUBLIC -ffp-contract=off)
endif()

target_link_libraries(Hinge PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline checks:  cmake -B build -DHINGE_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(HINGE_BUILD_TESTS "Build the Hinge offline checks" OFF)

if(HINGE_BUILD_TESTS)
    juce_add_console_app(hinge_tests PRODUCT_NAME "hinge_tests")

    target_sources(hinge_tests PRIVATE tests/Tests.cpp)
    target_include_directories(hinge_tests PRIVATE src)
    target_link_libraries(hinge_tests PRIVATE
        Hinge
        juce::juce_audio_utils
        juce::juce_dsp)

    enable_testing()
    add_test(NAME hinge_offline_checks COMMAND hinge_tests)
endif()

# ---------------------------------------------------------------------------
# Offscreen editor snapshot:  cmake -B build -DHINGE_BUILD_TOOLS=ON
# ---------------------------------------------------------------------------
option(HINGE_BUILD_TOOLS "Build the editor snapshot tool" OFF)

if(HINGE_BUILD_TOOLS)
    juce_add_console_app(hinge_shot PRODUCT_NAME "hinge_shot")

    target_sources(hinge_shot PRIVATE tools/Snapshot.cpp)
    target_include_directories(hinge_shot PRIVATE src)
    target_link_libraries(hinge_shot PRIVATE
        Hinge
        juce::juce_audio_utils
        juce::juce_dsp)
endif()
