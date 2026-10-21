# SPDX-License-Identifier: GPL-3.0-or-later
# Two libraries live here (docs/guides/building.md#debug-tools): coney_platform, the game's side of SDL3 and librw,
# as a library so the tests can use it too (with the headless renderer); and coney_devtools, the debug tools'
# platform half, at the end of this file.
add_library(coney_platform STATIC render_engine.cpp sprite_sheets.cpp texture_dictionary.cpp texture_lod.cpp
                                  window.cpp world_atomic.cpp)
# The streamed-world viewer (docs/research/world.md#coneys-implementation).
target_sources(coney_platform PRIVATE level_file.cpp texture_lookup.cpp world_renderer.cpp world_set.cpp world_viewer_mode.cpp front_end_scene.cpp)
# Dynamic objects' models, drawn where the scripts and scenes put them (docs/research/objects.md#coneys-implementation).
target_sources(coney_platform PRIVATE object_models.cpp placed_objects.cpp)
# The character viewer (docs/research/characters.md#coneys-implementation).
target_sources(coney_platform PRIVATE character_lights.cpp character_mesh.cpp)
target_sources(coney_platform PRIVATE blood_textures.cpp play_lighting.cpp scene_lighting.cpp)
# Playing a level as Rembrandt (docs/guides/running-play-level.md#playing-a-level), in a level's scenery or a sandbox's.
target_sources(coney_platform PRIVATE play_level_cast.cpp play_level_character.cpp play_level_hats.cpp play_level_mode.cpp play_level_objects.cpp play_level_props.cpp play_level_scene.cpp play_level_throw_aim.cpp
                                      play_level_world.cpp play_scenery.cpp scene_stage.cpp)
# The in-game HUD on screen: its sheets, batches and 2D pass (docs/research/hud.md#coneys-implementation).
target_sources(coney_platform PRIVATE hud_layer.cpp humans_trace.cpp)
# The run log's outputs: the console, the log file and the crash handlers (docs/guides/run-log.md).
target_sources(coney_platform PRIVATE log_output.cpp)
# Pad input from SDL3 gamepads and the keyboard (docs/research/frontend.md#coneys-implementation).
target_sources(coney_platform PRIVATE sdl_input.cpp)
# A driver program's pad and the frame's observation (`--pad-pipe`), and the humans for the event log
# (docs/guides/running-play-level.md#pad-pipe).
target_sources(coney_platform PRIVATE pad_pipe.cpp)
# A level's particle systems and motion blur, drawn in play (docs/research/particles.md#coneys-implementation).
target_sources(coney_platform PRIVATE motion_blur_pass.cpp parked_cars.cpp particle_renderer.cpp play_level_effects.cpp
                                      room_smoke_overlay.cpp throw_arc_renderer.cpp water_renderer.cpp)
# Real-time frame pacing: the one place that reads the clock (docs/guides/conventions.md#update-and-render).
target_sources(coney_platform PRIVATE frame_pacer.cpp)
# The profiler's clock, OpenGL counters and allocation counter (docs/guides/performance.md); `--bench` drives the debug
# tools' fighter spawner, so it is coney_devtools' below.
target_sources(coney_platform PRIVATE perf_probes.cpp)
# In-game clips for issues and pull requests (`--record`, docs/guides/testing.md#clips): FFmpeg run as a program.
target_sources(coney_platform PRIVATE clip_recorder.cpp)
# Sound output: the SDL3 audio device over the mixer, or the offline pull of test mode (docs/research/sound.md).
target_sources(coney_platform PRIVATE audio_output.cpp sdl_audio_device.cpp)
# The trimmed FFmpeg that decodes the Bink movies (docs/guides/building.md#ffmpeg); only src/platform/ sees it.
target_sources(coney_platform PRIVATE ffmpeg_info.cpp ffmpeg_movie_decoder.cpp movie_screen.cpp)
# Failures end the process on stderr, never in a dialog waiting for a click (test runs, CI, agents).
target_sources(coney_platform PRIVATE error_dialogs.cpp)
# The one game set-up over a disc that every way into a level shares (docs/guides/testing.md#test-through-the-players-path).
target_sources(coney_platform PRIVATE game_session.cpp)
# `coney --install`: the player's disc into a portable install folder (docs/guides/running.md#install).
target_sources(coney_platform PRIVATE install_command.cpp install_folder_fs.cpp)
# Where the player profiles are kept: the per-user data folder (docs/research/save.md#coney).
target_sources(coney_platform PRIVATE profile_folder.cpp)
# What both entry points (main_release.cpp and main.cpp) set up the same way (docs/guides/building.md#debug-tools).
target_sources(coney_platform PRIVATE game_run.cpp)
# Network play over UDP with ENet, and the game's side of it (host, join, the Network page's figures); the sessions
# themselves are coney_net (docs/guides/multiplayer.md).
target_sources(coney_platform PRIVATE enet_transport.cpp net_play.cpp)
coney_target_defaults(coney_platform)
# Never coney_debug or Dear ImGui: the game reaches the debug tools only through hooks it offers
# (docs/guides/building.md#debug-tools).
target_link_libraries(coney_platform PUBLIC coney_core coney_audio coney_install coney_movies coney_net SDL3::SDL3-static
                                            librw::librw)
target_link_libraries(coney_platform PRIVATE ffmpeg_bink enet)

# The executables (docs/guides/building.md#debug-tools). With the debug tools, `coney` is main.cpp over them, and
# `coney_release` is the release build's entry point built beside it, so every build with the tools also links the
# game without them. Without them (the release build), `coney` is the release build's entry point alone.
if(CONEY_DEBUG_TOOLS)
    # The debug tools' platform half (docs/guides/building.md#debug-tools), the only target that links Dear ImGui: the
    # debug menus' glue, the pad menu drawn over every frame (docs/guides/debug-menu.md); the developer overlay over the
    # same menus (docs/guides/debug-menu.md#the-developer-overlay); the menus' controls of the play mode with its Entity
    # inspector, Animation player, Debug draw lines and Spawner; the sandbox as the play mode's scenery and its renderer
    # (docs/guides/sandbox.md); the texture, character and sandbox viewers; the reference images of characters and objects
    # (docs/guides/running-viewers.md#reference-images); and `--bench`, the scripted benchmark run that spawns fighters
    # through the debug menus' controls (docs/guides/performance.md).
    add_library(
        coney_devtools STATIC
        character_viewer_mode.cpp
        debug_menus.cpp
        game_session_debug.cpp
        imgui_model_view.cpp
        imgui_overlay.cpp
        imgui_ui_scale.cpp
        perf_bench.cpp
        play_level_anim.cpp
        play_level_controls.cpp
        play_level_inspect.cpp
        reference_renderer.cpp
        sandbox_play_scenery.cpp
        sandbox_renderer.cpp
        sandbox_viewer_mode.cpp
        texture_viewer_mode.cpp)
    coney_target_defaults(coney_devtools)
    target_link_libraries(coney_devtools PUBLIC coney_platform coney_debug imgui)

    add_executable(coney main.cpp)
    coney_target_defaults(coney)
    target_link_libraries(coney PRIVATE coney_platform coney_devtools)
    # The build type a `--bench` report names (docs/guides/performance.md): Debug, RelWithDebInfo, Release.
    target_compile_definitions(coney PRIVATE CONEY_BUILD_TYPE="$<CONFIG>")
    add_executable(coney_release main_release.cpp)
    coney_target_defaults(coney_release)
    target_link_libraries(coney_release PRIVATE coney_platform)
    set(_coney_release_exe coney_release)
else()
    add_executable(coney main_release.cpp)
    coney_target_defaults(coney)
    target_link_libraries(coney PRIVATE coney_platform)
    set(_coney_release_exe coney)
endif()

# The release build's smoke tests, on its entry point in every build: it starts and stops headless, prints a player's
# options, refuses a developer's by name, plays a synthetic input script, and holds none of the debug tools (the
# debug pages' titles and Dear ImGui's settings file are not in the executable).
add_test(NAME coney.release.runs_headless COMMAND ${_coney_release_exe} --headless --frames 3)
set_tests_properties(coney.release.runs_headless PROPERTIES TIMEOUT 30)
add_test(NAME coney.release.help COMMAND ${_coney_release_exe} --help)
set_tests_properties(coney.release.help PROPERTIES PASS_REGULAR_EXPRESSION "--skip-movies" FAIL_REGULAR_EXPRESSION
                                                   "--play-level|--sandbox|--tunables")
add_test(NAME coney.release.refuses_developer_option COMMAND ${_coney_release_exe} --headless --frames 1 --play-level
                                                                level99)
set_tests_properties(coney.release.refuses_developer_option PROPERTIES PASS_REGULAR_EXPRESSION
                                                                       "--play-level is a developer's option")
add_test(NAME coney.release.plays_input_script
         COMMAND ${_coney_release_exe} --headless --frames 10 --input-script
                 "${PROJECT_SOURCE_DIR}/tests/support/menu_input.txt")
set_tests_properties(coney.release.plays_input_script PROPERTIES TIMEOUT 30)
add_test(NAME coney.release.hosts_headless COMMAND ${_coney_release_exe} --headless --frames 3 --host 127.0.0.1:0
                                                     --net-name Tester)
set_tests_properties(coney.release.hosts_headless PROPERTIES TIMEOUT 30 PASS_REGULAR_EXPRESSION
                                                             "net: hosting on port [1-9][0-9]* as Tester")
add_test(NAME coney.release.joins_headless COMMAND ${_coney_release_exe} --headless --frames 3 --join 127.0.0.1:1)
set_tests_properties(coney.release.joins_headless PROPERTIES TIMEOUT 30)
add_test(NAME coney.release.host_and_join_refused COMMAND ${_coney_release_exe} --headless --frames 3 --host 0 --join
                                                            127.0.0.1)
set_tests_properties(coney.release.host_and_join_refused PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.release.has_no_debug_tools
         COMMAND "${CMAKE_COMMAND}" -DEXE=$<TARGET_FILE:${_coney_release_exe}> -DEXPECT=absent -P
                 "${PROJECT_SOURCE_DIR}/cmake/debug_tools_markers.cmake")
if(CONEY_DEBUG_TOOLS)
    # The same check on the debug tools' `coney` finds the markers, so the check above cannot pass by finding nothing.
    add_test(NAME coney.debug_tools_markers_found
             COMMAND "${CMAKE_COMMAND}" -DEXE=$<TARGET_FILE:coney> -DEXPECT=present -P
                     "${PROJECT_SOURCE_DIR}/cmake/debug_tools_markers.cmake")
endif()

# The debug tools' smoke tests, on their `coney`; the block is not indented, as the list is long and often added to.
if(CONEY_DEBUG_TOOLS)
# Headless smoke tests: the NULL renderer needs no display or GPU, so these run on CI and in a future test harness.
add_test(NAME coney.runs_headless COMMAND coney --headless --frames 3)
set_tests_properties(coney.runs_headless PROPERTIES TIMEOUT 30)
add_test(NAME coney.help COMMAND coney --help)
set_tests_properties(coney.help PROPERTIES PASS_REGULAR_EXPRESSION "--view-sheet SHEET")
# `--install` refuses a disc that is not there with a message (it exits 1; the message is what is checked).
add_test(NAME coney.install_refuses_missing_disc COMMAND coney --install no-such-disc --data
                                                       "${CMAKE_CURRENT_BINARY_DIR}/install-smoke")
set_tests_properties(coney.install_refuses_missing_disc PROPERTIES PASS_REGULAR_EXPRESSION "not a folder or an ISO image")
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
# Test mode is lockstep with no clock, so a frame cap there is a mistake (docs/guides/running-options.md#frame-rate).
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
# The Time page's scale by pad, headless: the chord opens the menu, the left stick at a partial push walks to the 0.25x
# preset and cross chooses it, which the session prints as a notice (docs/guides/debug-menu.md#pages).
add_test(NAME coney.debug_menu_time_scale
         COMMAND coney --headless --frames 40
                 --input-script "${PROJECT_SOURCE_DIR}/tests/support/debug_menu_time_scale.txt")
set_tests_properties(coney.debug_menu_time_scale PROPERTIES TIMEOUT 30 PASS_REGULAR_EXPRESSION "Time scale 0[.]25x")
add_test(NAME coney.rejects_bad_tunables COMMAND coney --headless --frames 1
                                                 --tunables "${PROJECT_SOURCE_DIR}/tests/support/debug_menu.txt")
set_tests_properties(coney.rejects_bad_tunables PROPERTIES WILL_FAIL TRUE)
# --render-size sizes a window's frames: refused without one.
add_test(NAME coney.render_size_needs_window COMMAND coney --headless --frames 1 --render-size 640x448)
set_tests_properties(coney.render_size_needs_window PROPERTIES WILL_FAIL TRUE)

# With CONEY_DISC set when CMake configures, a headless run from the legal screen to the main menu: START on PM_Greet
# must reach PM_Mode (docs/research/frontend.md#coneys-implementation). It prints states only, never game data.
if(DEFINED ENV{CONEY_DISC} AND NOT "$ENV{CONEY_DISC}" STREQUAL "")
    add_test(NAME coney.reaches_main_menu
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 530 --skip-movies
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/start_menu.txt")
    set_tests_properties(coney.reaches_main_menu PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "profile manager: PM_Mode")
    # STORY with a new profile through the profile screens into level99 at checkpoint 1, headless
    # (docs/guides/running-input.md#input-scripts). States only.
    add_test(NAME coney.story_reaches_level99
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 790 --skip-movies
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/story_new_profile_session.txt")
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
    # stick deflections and a clip change, 90 frames (docs/guides/running-viewers.md#the-character-viewer). Counts only.
    add_test(NAME coney.views_character
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 90 --view-character
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/character_orbit.txt")
    set_tests_properties(coney.views_character PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "character viewer: 90 frames")
    # Playing level99 headless: Rembrandt walked, run, turned and stopped by partial stick deflections, the follow
    # camera turned by the right stick, 390 frames (docs/guides/running-play-level.md#playing-a-level). Counts only.
    add_test(NAME coney.plays_level
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 390 --play-level level99
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/play_walk.txt")
    set_tests_properties(coney.plays_level PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "play: 390 frames.*grounded")
    # Levels played with the game's sound exit cleanly: a test with PASS_REGULAR_EXPRESSION ignores the exit code, so a
    # crash after the summary line (a destructor reading the freed sound output) passed unseen. These check the exit
    # code alone (docs/guides/conventions.md#shutdown-order).
    foreach(level level95 level99)
        add_test(NAME coney.exits_cleanly_${level}
                 COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 60 --play-level ${level})
        set_tests_properties(coney.exits_cleanly_${level} PROPERTIES TIMEOUT 120)
    endforeach()
    # Rembrandt in the parkour sandbox headless: the same play mode and script on the sandbox's collision mesh, from
    # the run-up lane's spawn point (docs/guides/sandbox.md). Counts only.
    add_test(NAME coney.plays_sandbox
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 390 --play-level sandbox:parkour --spawn lane
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/play_walk.txt")
    set_tests_properties(coney.plays_sandbox PROPERTIES TIMEOUT 60
                         PASS_REGULAR_EXPRESSION "play: 390 frames.*grounded.*respawns 0.*sandbox \"Traversal course\"")
    # A matched view of level99 headless (docs/guides/running-play-level.md#matched-views): the view pinned, the world
    # frozen after its first step of play while the walk script pushes the stick, so the player stays where he started.
    add_test(NAME coney.freezes_level
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 200 --play-level level99 --skip-movies
                     --freeze-world --camera 75.2,41.1,1.8,0,0,0.7071068,0.7071068,65
                     --input-script "${PROJECT_SOURCE_DIR}/tests/support/play_walk.txt")
    set_tests_properties(coney.freezes_level PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION
                         "camera: pinned at \\(75.200, 41.100, 1.800\\).*fov 65.0.*world: frozen after frame")
    # A bench (docs/guides/performance.md): level99 headless with 10 AI fighters spawned on its first frame of play,
    # 30 frames of warm-up, then 60 measured; the report is written to the build folder. Counts and times only.
    add_test(NAME coney.bench_level
             COMMAND coney --disc "$ENV{CONEY_DISC}" --headless --frames 90 --play-level level99
                     --bench "${CMAKE_CURRENT_BINARY_DIR}/bench_level.json" --bench-fighters 10 --bench-warmup 30)
    set_tests_properties(coney.bench_level PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION
                         "bench: 10 fighters, 60 frames: frame [0-9.]+ ms mean")
    # Every check above needs the disc: label them like the disc tests of coney_tests, so `ctest -L disc` runs (or
    # lists) them all, and `-LE disc` leaves them out.
    set_tests_properties(coney.reaches_main_menu coney.story_reaches_level99 coney.plays_movies coney.views_character
                         coney.bench_level coney.plays_level coney.exits_cleanly_level95 coney.exits_cleanly_level99 coney.plays_sandbox
                         coney.freezes_level PROPERTIES LABELS disc)
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
# (docs/guides/running-options.md#sound). Counts and a hash only.
add_test(NAME coney.audio_test_offline COMMAND coney --headless --frames 60 --audio-test)
set_tests_properties(coney.audio_test_offline PROPERTIES TIMEOUT 30
                     PASS_REGULAR_EXPRESSION "audio: offline, 96000 frames mixed .2.00 s at 48000 Hz., peak [1-9]")
add_test(NAME coney.audio_test_refuses_no_audio COMMAND coney --headless --frames 1 --audio-test --no-audio)
set_tests_properties(coney.audio_test_refuses_no_audio PROPERTIES WILL_FAIL TRUE)
# Network play from the command line (docs/guides/multiplayer.md). The host listens on 127.0.0.1 only, so no desktop
# firewall asks about the test program; the port is any free one.
add_test(NAME coney.hosts_headless COMMAND coney --headless --frames 3 --host 127.0.0.1:0 --net-name Tester)
set_tests_properties(coney.hosts_headless PROPERTIES TIMEOUT 30
                     PASS_REGULAR_EXPRESSION "net: hosting on port [1-9][0-9]* as Tester")
add_test(NAME coney.host_and_join_refused COMMAND coney --headless --frames 3 --host 0 --join 127.0.0.1)
set_tests_properties(coney.host_and_join_refused PROPERTIES WILL_FAIL TRUE)
add_test(NAME coney.bad_net_name_refused COMMAND coney --headless --frames 3 --net-name 0123456789012345678901234567890123)
set_tests_properties(coney.bad_net_name_refused PROPERTIES WILL_FAIL TRUE)
endif()
