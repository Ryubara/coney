# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(coney_tests
    core/chunk_system_test.cpp
    core/game_timer_test.cpp
    core/input_script_test.cpp
    core/name_hash_test.cpp
    core/options_test.cpp
    core/pad_test.cpp
    fileio/disc_test.cpp
    fileio/reader_test.cpp
    fileio/stream_test.cpp
    fileio/wad_index_test.cpp
    fileio/wad_test.cpp
    gamemodes/game_mode_stack_test.cpp
    gamemodes/idle_mode_test.cpp
    gamemodes/legal_screen_mode_test.cpp
    gamemodes/load_entry_mode_test.cpp
    gamemodes/sheet_viewer_mode_test.cpp
    graphics/frame_stats_test.cpp
    graphics/overlay_camera_test.cpp
    graphics/particle_page_test.cpp
    graphics/rw_stream_test.cpp
    graphics/screen_test.cpp
    graphics/sprite_batch_test.cpp
    graphics/texture_grid_test.cpp
    platform/disc_sprite_sheets_test.cpp
    platform/disc_textures_test.cpp
    platform/sdl_input_test.cpp
    platform/sprite_sheets_test.cpp
    platform/texture_dictionary_test.cpp)
coney_target_defaults(coney_tests)
# tests/ on the include path for the shared synthetic fixtures (support/fixtures.h).
target_include_directories(coney_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# coney_platform for librw, which the texture tests run on its NULL device (no window, no GPU).
target_link_libraries(coney_tests PRIVATE coney_platform Catch2::Catch2WithMain)
include(Catch)
# The disc check skips (Catch2's exit code 4) unless CONEY_DISC names the player's disc; ctest reports it as skipped.
catch_discover_tests(coney_tests)
