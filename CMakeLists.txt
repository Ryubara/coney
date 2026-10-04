# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.28)
project(Coney VERSION 0.1.0 LANGUAGES C CXX)  # C: SDL3 has C sources

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(coney_target)
include(deps)

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
