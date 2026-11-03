cmake_minimum_required(VERSION 3.22)

project(Transcender VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE, pinned to the same version the rest of the suite builds against.
#     cmake -B build -DJUCE_PATH=/path/to/JUCE
# or let CMake fetch it.
# ---------------------------------------------------------------------------
if(DEFINED JUCE_PATH)
    message(STATUS "Transcender: using local JUCE at ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(TRANSCENDER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TRANSCENDER_FORMATS AU)
endif()

juce_add_plugin(Transcender
    COMPANY_NAME                "IndieWave Audio"
    COMPANY_WEBSITE             "https://indiewave.audio"
    BUNDLE_ID                   "audio.indiewave.transcender"
    PLUGIN_MANUFACTURER_CODE    Idwv
    PLUGIN_CODE                 Trsc
    FORMATS                     ${TRANSCENDER_FORMATS}
    PRODUCT_NAME                "Transcender"
    DESCRIPTION                 "Ten oscillator trance synth with tempo synced gate and delay"
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Transcender PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_include_directories(Transcender PRIVATE src)

target_compile_definitions(Transcender PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Transcender PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline checks:  cmake -B build -DTRANSCENDER_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(TRANSCENDER_BUILD_TESTS "Build the Transcender offline checks" OFF)

if(TRANSCENDER_BUILD_TESTS)
    juce_add_console_app(transcender_tests PRODUCT_NAME "transcender_tests")

    target_sources(transcender_tests PRIVATE tests/Tests.cpp)
    target_include_directories(transcender_tests PRIVATE src)
    target_link_libraries(transcender_tests PRIVATE
        Transcender
        juce::juce_audio_utils
        juce::juce_dsp)

    enable_testing()
    add_test(NAME transcender_offline_checks COMMAND transcender_tests)
endif()

# ---------------------------------------------------------------------------
# Offscreen editor snapshot:  cmake -B build -DTRANSCENDER_BUILD_TOOLS=ON
# ---------------------------------------------------------------------------
option(TRANSCENDER_BUILD_TOOLS "Build the editor snapshot tool" OFF)

if(TRANSCENDER_BUILD_TOOLS)
    juce_add_console_app(transcender_shot PRODUCT_NAME "transcender_shot")

    target_sources(transcender_shot PRIVATE tools/Snapshot.cpp)
    target_include_directories(transcender_shot PRIVATE src)
    target_link_libraries(transcender_shot PRIVATE
        Transcender
        juce::juce_audio_utils
        juce::juce_dsp)
endif()
