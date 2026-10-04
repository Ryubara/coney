# SPDX-License-Identifier: GPL-3.0-or-later
add_library(coney_core STATIC
    core/assert.cpp
    core/chunk_stacks.cpp
    core/chunk_system.cpp
    core/chunk_types.cpp
    core/game_timer.cpp
    core/input_script.cpp
    core/name_hash.cpp
    core/options.cpp
    core/pad.cpp
    core/pads.cpp
    fileio/disc.cpp
    fileio/file_stream.cpp
    fileio/reader.cpp
    fileio/stream.cpp
    fileio/wad.cpp
    fileio/wad_index.cpp
    gamemodes/game_mode_stack.cpp
    gamemodes/legal_screen_mode.cpp
    gamemodes/load_entry_mode.cpp
    gamemodes/sheet_viewer_mode.cpp
    graphics/frame_stats.cpp
    graphics/overlay_camera.cpp
    graphics/particle_page.cpp
    graphics/rw_stream.cpp
    graphics/screen.cpp
    graphics/sprite_batch.cpp
    graphics/texture_grid.cpp)
coney_target_defaults(coney_core)
target_include_directories(coney_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory(platform)
