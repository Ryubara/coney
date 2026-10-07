# SPDX-License-Identifier: GPL-3.0-or-later
# Everything that talks to SDL3 or librw, as a library so the tests can use it too (with the headless renderer).
add_library(coney_platform STATIC render_engine.cpp sprite_sheets.cpp texture_dictionary.cpp texture_viewer_mode.cpp
                                  window.cpp world_atomic.cpp)
# The streamed-world viewer (docs/research/world.md#coneys-implementation).
target_sources(coney_platform PRIVATE level_file.cpp texture_lookup.cpp world_renderer.cpp world_set.cpp world_viewer_mode.cpp front_end_scene.cpp)
# Dynamic objects' models, drawn where the scripts and scenes put them (docs/research/objects.md#coneys-implementation).
target_sources(coney_platform PRIVATE object_models.cpp placed_objects.cpp)
# The character viewer (docs/research/characters.md#coneys-implementation).
target_sources(coney_platform PRIVATE character_lights.cpp character_mesh.cpp character_viewer_mode.cpp)
target_sources(coney_platform PRIVATE play_lighting.cpp scene_lighting.cpp)
# Playing a level as Rembrandt (docs/guides/building.md#playing-a-level), in a level's scenery or a sandbox's.
target_sources(coney_platform PRIVATE play_level_cast.cpp play_level_character.cpp play_level_mode.cpp play_level_objects.cpp play_level_scene.cpp
                                      play_level_world.cpp play_scenery.cpp scene_stage.cpp)
# The in-game HUD on screen: its sheets, batches and 2D pass (docs/research/hud.md#coneys-implementation).
target_sources(coney_platform PRIVATE hud_layer.cpp)
# The sandbox: Coney's own test worlds, drawn and flown round (docs/guides/sandbox.md).
target_sources(coney_platform PRIVATE sandbox_renderer.cpp sandbox_viewer_mode.cpp)
# The reference images of characters and objects (docs/guides/building.md#reference-images).
target_sources(coney_platform PRIVATE reference_renderer.cpp)
# Pad input from SDL3 gamepads and the keyboard (docs/research/frontend.md#coneys-implementation).
target_sources(coney_platform PRIVATE sdl_input.cpp)
# A level's particle systems and motion blur, drawn in play (docs/research/particles.md#coneys-implementation).
target_sources(coney_platform PRIVATE motion_blur_pass.cpp parked_cars.cpp particle_renderer.cpp play_level_effects.cpp)
# Real-time frame pacing: the one place that reads the clock (docs/guides/conventions.md#update-and-render).
target_sources(coney_platform PRIVATE frame_pacer.cpp)
# Sound output: the SDL3 audio device over the mixer, or the offline pull of test mode (docs/research/sound.md).
target_sources(coney_platform PRIVATE audio_output.cpp sdl_audio_device.cpp)
# The trimmed FFmpeg that decodes the Bink movies (docs/guides/building.md#ffmpeg); only src/platform/ sees it.
target_sources(coney_platform PRIVATE ffmpeg_info.cpp ffmpeg_movie_decoder.cpp movie_screen.cpp)
# Failures end the process on stderr, never in a dialog waiting for a click (test runs, CI, agents).
target_sources(coney_platform PRIVATE error_dialogs.cpp)
# The debug menus' platform glue: the pad menu drawn over every frame (docs/guides/debug-menu.md).
target_sources(coney_platform PRIVATE debug_menus.cpp)
# Where the player profiles are kept: the per-user data folder (docs/research/save.md#coney).
target_sources(coney_platform PRIVATE profile_folder.cpp)
# The developer overlay over the same menus, with Dear ImGui (docs/guides/debug-menu.md#the-developer-overlay).
target_sources(coney_platform PRIVATE imgui_model_view.cpp imgui_overlay.cpp)
coney_target_defaults(coney_platform)
target_link_libraries(coney_platform PUBLIC coney_core coney_audio coney_movies SDL3::SDL3-static librw::librw imgui)
target_link_libraries(coney_platform PRIVATE ffmpeg_bink)

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
add_test(NAME coney.play_level_needs_disc COMMAND coney --play-level level99)
set_tests_properties(coney.play_level_needs_disc PROPERTIES WILL_FAIL TRUE)
# Test mode is lockstep with no clock, so a frame cap there is a mistake (docs/guides/building.md#frame-rate).
add_test(NAME coney.fps_cap_refused_in_test_mode COMMAND coney --headless --frames 3 --fps-cap 60)
set_tests_properties(coney.fps_cap_refused_in_test_mode PROPERTIES WILL_FAIL TRUE)

add_test(NAME coney.rejects_missing_disc COMMAND coney --disc "${CMAKE_CURRENT_BINARY_DIR}/no-such-disc" --load x)
set_tests_properties(coney.rejects_missing_disc PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.plays_input_script
         COMMAND coney --headless --frames 10 --input-script "${PROJECT_SOURCE_DIR}/tests/support/menu_input.txt")
set_tests_properties(coney.plays_input_script PROPERTIES TIMEOUT 30)
add_test(NAME coney.rejects_missing_input_script COMMAND coney --headless --frames 1 --input-script no-such-script.txt)
set_tests_properties(coney.rejects_missing_input_script PROPERTIES WILL_FAIL TRUE)
# The pad debug menu, headless: the chord opens it, the pad walks to Cheats and sends a code through the script state
# (docs/guides/debug-menu.md). Without a disc the menus' sandbox state has no cheat callback, which the log reports.
add_test(NAME coney.debug_menu_by_pad
         COMMAND coney --headless --frames 30 --input-script "${PROJECT_SOURCE_DIR}/tests/support/debug_menu.txt")
set_tests_properties(coney.debug_menu_by_pad PROPERTIES TIMEOUT 30 PASS_REGULAR_EXPRESSION "> DbgEnterCheat\\(0\\)")
add_test(NAME coney.rejects_bad_tunables COMMAND coney --headless --frames 1
                                                 --tunables "${PROJECT_SOURCE_DIR}/tests/support/debug_menu.txt")
set_tests_properties(coney.rejects_bad_tunables PROPERTIES WILL_FAIL TRUE)

# With CONEY_DISC set when CMake configures, a headless run from the legal screen to the main menu: START on PM_Greet
# must reach PM_Mode (docs/research/frontend.md#coneys-implementation). It prints states only, never game data.
if(DEFINED ENV{CONEY_DISC} AND NOT "$ENV{CONEY_DISC}" STREQUAL "")
    add_test(NAME coney.reaches_main_menu
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 310 --skip-movies
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/start_menu.txt")
    set_tests_properties(coney.reaches_main_menu PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "profile manager: PM_Mode")
    # STORY with a new profile through the profile screens into level99 at checkpoint 1, headless
    # (docs/guides/building.md#input-scripts). States only.
    add_test(NAME coney.story_reaches_level99
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 570 --skip-movies
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/story_new_profile.txt")
    set_tests_properties(coney.story_reaches_level99 PROPERTIES TIMEOUT 120
                         PASS_REGULAR_EXPRESSION "movie: L99_IN")
    # The start-up movies headless (docs/research/movies.md#coneys-implementation): LOGO plays to its end, a tap skips
    # PLOGO and another L1_IN; then the legal screen. Counts only.
    add_test(NAME coney.plays_movies
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 160
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/movie_skip.txt")
    set_tests_properties(coney.plays_movies PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION
                         "LOGO ended after 114 frames.*PLOGO skipped.*L1_IN skipped.*movies: 3 played \\(2 skipped")
    # The character viewer headless: Rembrandt loaded through the Character List, the orbit camera driven by partial
    # stick deflections and a clip change, 90 frames (docs/guides/building.md#the-character-viewer). Counts only.
    add_test(NAME coney.views_character
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 90 --view-character
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/character_orbit.txt")
    set_tests_properties(coney.views_character PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "character viewer: 90 frames")
    # Playing level99 headless: Rembrandt walked, run, turned and stopped by partial stick deflections, the follow
    # camera turned by the right stick, 390 frames (docs/guides/building.md#playing-a-level). Counts only.
    add_test(NAME coney.plays_level
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 390 --play-level level99
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/play_walk.txt")
    set_tests_properties(coney.plays_level PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "play: 390 frames.*grounded")
    # Rembrandt in the parkour sandbox headless: the same play mode and script on the sandbox's collision mesh, from
    # the run-up lane's spawn point (docs/guides/sandbox.md). Counts only.
    add_test(NAME coney.plays_sandbox
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 390 --play-level sandbox:parkour --spawn lane
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/play_walk.txt")
    set_tests_properties(coney.plays_sandbox PROPERTIES TIMEOUT 60
                         PASS_REGULAR_EXPRESSION "play: 390 frames.*grounded.*respawns 0.*sandbox \"Traversal course\"")
endif()

# Coney's own assets (the sandbox's layouts and textures, assets/sandbox) copied beside the executable on every build,
# where coney looks for them at run time unless --assets names another folder.
add_custom_target(coney_assets ALL
                  COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${PROJECT_SOURCE_DIR}/assets"
                          "$<TARGET_FILE_DIR:coney>/assets"
                  VERBATIM)
add_dependencies(coney coney_assets)
# The sandbox needs no disc: fly round each shipped layout headless, the free camera driven by partial stick
# deflections and a viewpoint change (docs/guides/sandbox.md). Counts only.
add_test(NAME coney.flies_sandbox
         COMMAND coney --headless --frames 60 --sandbox --input-script "${PROJECT_SOURCE_DIR}/tests/support/sandbox_fly.txt")
set_tests_properties(coney.flies_sandbox PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "sandbox: 60 frames.*view overview")
add_test(NAME coney.flies_parkour_sandbox
         COMMAND coney --headless --frames 60 --sandbox parkour
                 --input-script "${PROJECT_SOURCE_DIR}/tests/support/sandbox_fly.txt")
set_tests_properties(coney.flies_parkour_sandbox PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "sandbox: 60 frames")
add_test(NAME coney.rejects_unknown_sandbox COMMAND coney --headless --frames 1 --sandbox no-such-layout)
set_tests_properties(coney.rejects_unknown_sandbox PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.play_sandbox_needs_disc COMMAND coney --play-level sandbox:parkour)
set_tests_properties(coney.play_sandbox_needs_disc PROPERTIES WILL_FAIL TRUE)
# The sound output in test mode: the tone sweep mixed offline, a step of frames per frame, with no device and no clock
# (docs/guides/building.md#sound). Counts and a hash only.
add_test(NAME coney.audio_test_offline COMMAND coney --headless --frames 60 --audio-test)
set_tests_properties(coney.audio_test_offline PROPERTIES TIMEOUT 30
                     PASS_REGULAR_EXPRESSION "audio: offline, 96000 frames mixed .2.00 s at 48000 Hz., peak [1-9]")
add_test(NAME coney.audio_test_refuses_no_audio COMMAND coney --headless --frames 1 --audio-test --no-audio)
set_tests_properties(coney.audio_test_refuses_no_audio PROPERTIES WILL_FAIL TRUE)
