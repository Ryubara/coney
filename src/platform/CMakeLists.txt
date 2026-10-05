# SPDX-License-Identifier: GPL-3.0-or-later
# Everything that talks to SDL3 or librw, as a library so the tests can use it too (with the headless renderer).
add_library(coney_platform STATIC render_engine.cpp sprite_sheets.cpp texture_dictionary.cpp texture_viewer_mode.cpp
                                  window.cpp world_atomic.cpp)
# The streamed-world viewer (docs/research/world.md#coneys-implementation).
target_sources(coney_platform PRIVATE level_file.cpp texture_lookup.cpp world_renderer.cpp world_set.cpp world_viewer_mode.cpp)
# The character viewer (docs/research/characters.md#coneys-implementation).
target_sources(coney_platform PRIVATE character_lights.cpp character_mesh.cpp character_viewer_mode.cpp)
# The character reference images (docs/guides/building.md#character-reference-images).
target_sources(coney_platform PRIVATE reference_renderer.cpp)
# Pad input from SDL3 gamepads and the keyboard (docs/research/frontend.md#coneys-implementation).
target_sources(coney_platform PRIVATE sdl_input.cpp)
coney_target_defaults(coney_platform)
target_link_libraries(coney_platform PUBLIC coney_core SDL3::SDL3-static librw::librw)

add_executable(coney main.cpp)
coney_target_defaults(coney)
target_link_libraries(coney PRIVATE coney_platform)

# Headless smoke tests: the NULL renderer needs no display or GPU, so these run on CI and in a future test harness.
add_test(NAME coney.runs_headless COMMAND coney --headless --frames 3)
set_tests_properties(coney.runs_headless PROPERTIES TIMEOUT 30)
add_test(NAME coney.help COMMAND coney --help)
set_tests_properties(coney.help PROPERTIES PASS_REGULAR_EXPRESSION "--view-sheet SHEET")
add_test(NAME coney.rejects_bad_argument COMMAND coney --frames abc)
set_tests_properties(coney.rejects_bad_argument PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.load_needs_disc COMMAND coney --load level1.lev)
set_tests_properties(coney.load_needs_disc PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.view_needs_disc COMMAND coney --view-txd 0x1234)
set_tests_properties(coney.view_needs_disc PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.view_sheet_needs_disc COMMAND coney --view-sheet menu_system)
set_tests_properties(coney.view_sheet_needs_disc PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.view_text_needs_disc COMMAND coney --view-text big_font hello)
set_tests_properties(coney.view_text_needs_disc PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.view_world_needs_disc COMMAND coney --view-world level2)
set_tests_properties(coney.view_world_needs_disc PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.render_references_needs_disc COMMAND coney --render-references out)
set_tests_properties(coney.render_references_needs_disc PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.view_character_needs_disc COMMAND coney --view-character)
set_tests_properties(coney.view_character_needs_disc PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.rejects_missing_disc COMMAND coney --disc "${CMAKE_CURRENT_BINARY_DIR}/no-such-disc" --load x)
set_tests_properties(coney.rejects_missing_disc PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.plays_input_script
         COMMAND coney --headless --frames 10 --input-script "${PROJECT_SOURCE_DIR}/tests/support/menu_input.txt")
set_tests_properties(coney.plays_input_script PROPERTIES TIMEOUT 30)
add_test(NAME coney.rejects_missing_input_script COMMAND coney --headless --frames 1 --input-script no-such-script.txt)
set_tests_properties(coney.rejects_missing_input_script PROPERTIES WILL_FAIL TRUE)

# With CONEY_DISC set when CMake configures, a headless run from the legal screen to the main menu: START on PM_Greet
# must reach PM_Mode (docs/research/frontend.md#coneys-implementation). It prints states only, never game data.
if(DEFINED ENV{CONEY_DISC} AND NOT "$ENV{CONEY_DISC}" STREQUAL "")
    add_test(NAME coney.reaches_main_menu
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 220
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/start_menu.txt")
    set_tests_properties(coney.reaches_main_menu PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "profile manager: PM_Mode")
    # The character viewer headless: Rembrandt loaded through the Character List, the orbit camera driven by partial
    # stick deflections and a clip change, 90 frames (docs/guides/building.md#the-character-viewer). Counts only.
    add_test(NAME coney.views_character
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 90 --view-character
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/character_orbit.txt")
    set_tests_properties(coney.views_character PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "character viewer: 90 frames")
endif()
