# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.28)
project(Coney VERSION 0.1.0 LANGUAGES C CXX)  # C: SDL3 has C sources

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
# No C++ module scanning, for Coney's targets and the dependencies alike. CMake scans every C++20-and-later source
# for `import` before compiling it (a scan step per source, and a dependency step per target that every compile waits
# for), but nothing here uses modules, and the compiler cache cannot cache the scans, so they were most of what was
# left of a build whose compiles came from the cache (docs/guides/building.md#module-scanning).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
include(coney_target)
include(deps)
if(CONEY_SHADERS)
    include(coney_shaders)
    coney_provision_shader_compiler()
endif()

enable_testing()
if(CONEY_SHADERS)
    add_subdirectory(src/platform/shaders)
endif()
add_subdirectory(src)
# The game's executables carry the shaders (docs/guides/shaders.md): a build of the game compiles them, and the linker
# keeps the ones the renderer refers to. Added here, from outside src/platform, where the executables are made.
if(CONEY_SHADERS)
    foreach(_executable coney coney_release)
        if(TARGET ${_executable})
            target_link_libraries(${_executable} PRIVATE coney_shaders)
        endif()
    endforeach()
endif()
add_subdirectory(tests)
