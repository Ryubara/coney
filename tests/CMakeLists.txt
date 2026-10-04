# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(coney_tests
    core/chunk_system_test.cpp
    core/game_timer_test.cpp
    core/name_hash_test.cpp
    core/options_test.cpp
    fileio/disc_test.cpp
    fileio/reader_test.cpp
    fileio/stream_test.cpp
    fileio/wad_index_test.cpp
    fileio/wad_test.cpp
    gamemodes/game_mode_stack_test.cpp
    gamemodes/load_entry_mode_test.cpp)
coney_target_defaults(coney_tests)
# tests/ on the include path for the shared synthetic fixtures (support/fixtures.h).
target_include_directories(coney_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(coney_tests PRIVATE coney_core Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(coney_tests)
