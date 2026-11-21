cmake_minimum_required(VERSION 3.22)

project(Nazare VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE, pinned to the same version as the rest of the suite.
#     cmake -B build -DJUCE_PATH=/path/to/JUCE
# or let CMake fetch it.
# ---------------------------------------------------------------------------
if(DEFINED JUCE_PATH)
    message(STATUS "Nazare: using local JUCE at ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(NAZARE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND NAZARE_FORMATS AU)
endif()

juce_add_plugin(Nazare
    COMPANY_NAME                "IndieWave Audio"
    COMPANY_WEBSITE             "https://indiewave.audio"
    BUNDLE_ID                   "audio.indiewave.nazare"
    PLUGIN_MANUFACTURER_CODE    Idwv
    PLUGIN_CODE                 Nzre
    FORMATS                     ${NAZARE_FORMATS}
    PRODUCT_NAME                "Nazare"
    DESCRIPTION                 "Large format analog console"
    VST3_CATEGORIES             Fx Distortion
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

# The panel: plates and filmstrips rendered by art/build.py, packed by art/assemble.py.
juce_add_binary_data(NazareAssets
    NAMESPACE BinaryData
    SOURCES
        resources/plate_front.jpg
        resources/plate_rear.jpg
        resources/drive.png
        resources/small.png
        resources/console.png
        resources/trim.png
        resources/toggle.png
        resources/jewel.png)

target_sources(Nazare PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_include_directories(Nazare PRIVATE src)

target_compile_definitions(Nazare PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Nazare PRIVATE
    NazareAssets
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline checks:  cmake -B build -DNAZARE_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(NAZARE_BUILD_TESTS "Build the Nazare offline checks" OFF)

if(NAZARE_BUILD_TESTS)
    juce_add_console_app(nazare_tests PRODUCT_NAME "nazare_tests")
    target_sources(nazare_tests PRIVATE tests/Tests.cpp)
    target_include_directories(nazare_tests PRIVATE src tests ${CMAKE_CURRENT_SOURCE_DIR}/../../shared)
    target_compile_definitions(nazare_tests PRIVATE
        INDIEWAVE_COMPAT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/compat"
        INDIEWAVE_VERSION="${PROJECT_VERSION}")
    target_link_libraries(nazare_tests PRIVATE Nazare juce::juce_audio_utils juce::juce_dsp)
    enable_testing()
    add_test(NAME nazare_offline_checks COMMAND nazare_tests)
endif()

# ---------------------------------------------------------------------------
# Offscreen editor snapshot:  cmake -B build -DNAZARE_BUILD_TOOLS=ON
# ---------------------------------------------------------------------------
option(NAZARE_BUILD_TOOLS "Build the editor snapshot tool" OFF)

if(NAZARE_BUILD_TOOLS)
    juce_add_console_app(nazare_shot PRODUCT_NAME "nazare_shot")
    target_sources(nazare_shot PRIVATE tools/Snapshot.cpp)
    target_include_directories(nazare_shot PRIVATE src)
    target_link_libraries(nazare_shot PRIVATE Nazare juce::juce_audio_utils juce::juce_dsp)
endif()
