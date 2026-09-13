# ARA 2 plug-in built on the VocalAI native core.
#
# The ARA SDK is fetched rather than vendored: it is Apache 2.0, so either
# would be allowed, but a pinned fetch keeps the licence boundary obvious and
# the repository small.
cmake_minimum_required(VERSION 3.16)
# C is enabled because ARA_Library requests C language features of its targets.
project(vocalai_ara VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(ARA_SDK_TAG "releases/2.3.0" CACHE STRING "ARA SDK release tag to build against")

include(FetchContent)
FetchContent_Declare(ara_sdk
  GIT_REPOSITORY https://github.com/Celemony/ARA_SDK.git
  GIT_TAG ${ARA_SDK_TAG}
  GIT_SUBMODULES ARA_API ARA_Library
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
)
# The SDK root carries no CMakeLists of its own - it is a set of submodules -
# so the library project is added explicitly.
FetchContent_MakeAvailable(ara_sdk)
add_subdirectory("${ara_sdk_SOURCE_DIR}/ARA_Library" "${CMAKE_CURRENT_BINARY_DIR}/ARA_Library.build")

if(NOT TARGET vocalai_core)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. vocalai_core_build)
endif()

add_library(vocalai_ara STATIC VocalAIARA.cpp)
target_include_directories(vocalai_ara PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vocalai_ara PUBLIC vocalai_core ARA_PlugIn_Library)
target_compile_options(vocalai_ara PRIVATE -Wall -Wextra)

# The in-process harness: it drives the plug-in through the ARA C API exactly
# as a DAW would, so the integration is exercised without needing a host or a
# companion API wrapper.  ARA's own validation layer is left switched on.
add_executable(vocalai_ara_harness tests/ara_harness.cpp)
target_link_libraries(vocalai_ara_harness PRIVATE vocalai_ara ARA_Host_Library)
target_compile_definitions(vocalai_ara_harness PRIVATE ARA_VALIDATE_API_CALLS=1)

enable_testing()
add_test(NAME ara_integration COMMAND vocalai_ara_harness)
