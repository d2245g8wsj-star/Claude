cmake_minimum_required(VERSION 3.16)
project(vocalai_core VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(vocalai_core
  src/fft.cpp
  src/filters.cpp
  src/dsp.cpp
  src/wav.cpp
  src/pitch.cpp
  src/notes.cpp
  src/key.cpp
  src/psola.cpp
  src/tuner.cpp
  src/features.cpp
  src/model.cpp
  src/engine.cpp
)
target_include_directories(vocalai_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(vocalai_core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(vocalai_core PRIVATE $<$<CONFIG:Release>:-O3>)

add_executable(vocalai_cli tools/vocalai_cli.cpp)
target_link_libraries(vocalai_cli PRIVATE vocalai_core)

enable_testing()
add_executable(vocalai_tests tests/test_core.cpp)
target_link_libraries(vocalai_tests PRIVATE vocalai_core)
add_test(NAME core COMMAND vocalai_tests)
