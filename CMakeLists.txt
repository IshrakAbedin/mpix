cmake_minimum_required(VERSION 3.23)
project(mpix LANGUAGES CXX)

# Required for intellisense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Tests (and their dependencies) are only built by default when mpix is the
# top-level project, so FetchContent / CPM consumers don't pull in Catch2.
option(MPIX_BUILD_TESTS "Build the mpix tests" ${PROJECT_IS_TOP_LEVEL})

# Dependencies
if(MPIX_BUILD_TESTS)
    include(CTest) # Required before Dependencies.cmake to provide BUILD_TESTING
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake) # Utilizes CPM to download dependencies
endif()

# Add stb_image using subdirectory
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb)

# Core Library
add_library(mpix INTERFACE)
add_library(mpix::mpix ALIAS mpix)

target_compile_features(mpix INTERFACE cxx_std_20)

target_sources(mpix INTERFACE
    FILE_SET HEADERS
    BASE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/include/mpix/mpix.hpp
)

target_link_libraries(mpix
    INTERFACE
        mpix_stb_image
)

# Test target
if(MPIX_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()