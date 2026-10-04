# SPDX-License-Identifier: GPL-3.0-or-later
add_library(coney_core STATIC
    core/assert.cpp
    core/chunk_stacks.cpp
    core/chunk_system.cpp
    core/chunk_types.cpp
    core/game_timer.cpp
    core/name_hash.cpp
    core/options.cpp
    fileio/disc.cpp
    fileio/file_stream.cpp
    fileio/reader.cpp
    fileio/stream.cpp
    fileio/wad.cpp
    fileio/wad_index.cpp
    gamemodes/game_mode_stack.cpp
    gamemodes/load_entry_mode.cpp)
coney_target_defaults(coney_core)
target_include_directories(coney_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory(platform)
