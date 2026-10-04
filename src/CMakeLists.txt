# SPDX-License-Identifier: GPL-3.0-or-later
add_library(coney_core STATIC core/assert.cpp core/options.cpp)
coney_target_defaults(coney_core)
target_include_directories(coney_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory(platform)
