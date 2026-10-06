# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(coney_tests
    camera/camera_lens_test.cpp
    core/chunk_system_test.cpp
    core/frame_clock_test.cpp
    core/frame_rate_meter_test.cpp
    core/game_random_test.cpp
    core/game_timer_test.cpp
    core/input_script_test.cpp
    core/interpolation_test.cpp
    core/name_hash_test.cpp
    core/parse_number_test.cpp
    core/ps2_float_test.cpp
    core/options_test.cpp
    core/pad_test.cpp
    fileio/disc_test.cpp
    fileio/executable_test.cpp
    fileio/reader_test.cpp
    fileio/stream_test.cpp
    fileio/wad_index_test.cpp
    fileio/wad_test.cpp
    gamemodes/disc_front_end_test.cpp
    gamemodes/frame_rate_test.cpp
    gamemodes/game_mode_stack_test.cpp
    gamemodes/idle_mode_test.cpp
    gamemodes/legal_screen_mode_test.cpp
    gamemodes/load_entry_mode_test.cpp
    gamemodes/sheet_viewer_mode_test.cpp
    gamemodes/start_up_flow_test.cpp
    gamemodes/rumble_start_test.cpp
    gamemodes/story_start_test.cpp
    gamemodes/text_viewer_mode_test.cpp
    graphics/font_test.cpp
    graphics/frame_stats_test.cpp
    graphics/overlay_camera_test.cpp
    graphics/particle_page_test.cpp
    graphics/ps2_world_mesh_test.cpp
    graphics/reference_sprites_test.cpp
    graphics/rw_stream_test.cpp
    graphics/screen_fade_test.cpp
    graphics/screen_test.cpp
    graphics/sprite_batch_test.cpp
    graphics/texture_grid_test.cpp
    gui/debug_menu_view_test.cpp
    gui/disc_text_test.cpp
    gui/markup_test.cpp
    gui/menu_input_test.cpp
    gui/profile_management_gui/pm_controller_test.cpp
    gui/rumble_mode_gui/rumble_menu_test.cpp
    gui/screen_flow_controller_test.cpp
    gui/text_layout_test.cpp
    gui/widgets_test.cpp
    platform/disc_change_character_test.cpp
    platform/disc_sprite_sheets_test.cpp
    platform/disc_story_start_test.cpp
    platform/disc_textures_test.cpp
    platform/disc_world_test.cpp
    platform/disc_level_test.cpp
    platform/disc_play_frame_rate_test.cpp
    platform/disc_world_streaming_test.cpp
    platform/imgui_model_view_test.cpp
    platform/sdl_input_test.cpp
    platform/sprite_sheets_test.cpp
    platform/texture_dictionary_test.cpp
    platform/world_atomic_test.cpp
    platform/world_renderer_test.cpp
    platform/world_set_test.cpp
    raycast/collision_mesh_test.cpp
    raycast/disc_collision_test.cpp
    scripting/ai_bindings_test.cpp
    scripting/config_strings_test.cpp
    scripting/disc_script_system_test.cpp
    scripting/disc_strings_test.cpp
    scripting/level_bindings_test.cpp
    scripting/lua_chunk_test.cpp
    scripting/lua_libraries_test.cpp
    scripting/lua_vm_test.cpp
    scripting/script_bindings_test.cpp
    scripting/script_system_test.cpp
    warriors/level_table_test.cpp
    world/debug_camera_test.cpp
    world/level_object_test.cpp
    world/path_map_test.cpp
    world/streamed_world_test.cpp
    world/view_frustum_test.cpp
    world/world_manifest_test.cpp
    world/world_streamer_test.cpp
    world/world_streams_test.cpp
    world_objects/flags_test.cpp
    world_objects/disc_object_list_test.cpp
    world_objects/object_list_test.cpp)
# Characters: the animation format and player, and the character resources.
target_sources(coney_tests PRIVATE
    animation/anim_clip_test.cpp
    animation/anim_math_test.cpp
    animation/anim_pose_test.cpp
    animation/disc_anim_clips_test.cpp
    animation/skeleton_test.cpp
    characters/character_data_test.cpp
    characters/character_class_test.cpp
    characters/character_types_test.cpp
    characters/character_list_test.cpp
    characters/character_model_test.cpp
    characters/character_rig_test.cpp
    characters/reference_render_test.cpp
    characters/disc_characters_test.cpp
    characters/orbit_camera_test.cpp
    platform/character_mesh_test.cpp)
# The player: animation tasks, locomotion, the human, the follow camera, and level99 driven by scripted pads.
target_sources(coney_tests PRIVATE
    animation/anim_task_test.cpp
    animation/held_flags_test.cpp
    camera/follow_camera_test.cpp
    camera/follow_collision_test.cpp
    human/disc_player_test.cpp
    human/human_test.cpp
    human/body_test.cpp
    human/climb_test.cpp
    human/combat_test.cpp
    human/combat_timing_test.cpp
    human/being_hit_test.cpp
    human/jump_test.cpp
    human/locomotion_test.cpp
    human/pair_placement_test.cpp
    human/player_trace_test.cpp
    human/stamina_test.cpp
    human/traversal_test.cpp
    human/turn_and_slide_test.cpp
    human/humans_test.cpp
    human/locomotion_gate_test.cpp)
# Combat (docs/research/combat.md).
target_sources(coney_tests PRIVATE
    combat/anim_ranges_test.cpp
    combat/commands_test.cpp
    combat/attacks_test.cpp
    combat/being_hit_test.cpp
    combat/disc_combat_test.cpp
    combat/grab_test.cpp
    combat/grabbed_test.cpp
    combat/lock_on_test.cpp
    combat/meters_test.cpp
    combat/player_combat_test.cpp
    combat/rage_awards_test.cpp
    combat/reactions_test.cpp
    combat/stick_games_test.cpp
    debug/combat_tunables_test.cpp)
# The sandbox and the collision writer it uses (docs/guides/sandbox.md).
target_sources(coney_tests PRIVATE
    raycast/collision_builder_test.cpp
    sandbox/disc_sandbox_combat_test.cpp
    sandbox/disc_sandbox_player_test.cpp
    sandbox/disc_sandbox_traversal_test.cpp
    sandbox/sandbox_layout_test.cpp
    sandbox/sandbox_mesh_test.cpp
    sandbox/sandbox_world_test.cpp)
# The shipped sandbox layouts are Coney's own (assets/sandbox), so the tests may read them.
target_compile_definitions(coney_tests PRIVATE CONEY_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")
# The debug menus (docs/guides/debug-menu.md).
target_sources(coney_tests PRIVATE
    debug/debug_session_test.cpp
    debug/input_gate_test.cpp
    debug/menu_navigator_test.cpp
    debug/native_caller_test.cpp
    debug/pad_menu_input_test.cpp
    debug/play_pages_test.cpp
    debug/tunables_test.cpp)
# The AI humans' brains (docs/research/ai.md).
target_sources(coney_tests PRIVATE
    ai/brain_test.cpp
    ai/disc_ai_config_test.cpp
    ai/disc_route_test.cpp
    ai/formations_test.cpp
    ai/gangs_test.cpp
    ai/move_action_test.cpp
    ai/route_planner_test.cpp
    ai/scripted_goals_test.cpp
    ai/tactics_test.cpp)
# A failed runtime check fails the run on stderr instead of opening a dialog (platform/error_dialogs.h).
target_sources(coney_tests PRIVATE support/error_dialogs_off.cpp)
coney_target_defaults(coney_tests)
# tests/ on the include path for the shared synthetic fixtures (support/fixtures.h).
target_include_directories(coney_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# Where the input scripts the disc tests play live.
target_compile_definitions(coney_tests PRIVATE CONEY_TEST_SUPPORT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/support")
# coney_platform for librw, which the texture tests run on its NULL device (no window, no GPU).
target_link_libraries(coney_tests PRIVATE coney_platform Catch2::Catch2WithMain)
include(Catch)
# The disc check skips (Catch2's exit code 4) unless CONEY_DISC names the player's disc; ctest reports it as skipped.
catch_discover_tests(coney_tests)

# The debug C runtime's checks must fail a run on stderr, not open a dialog nobody will click (platform/error_dialogs.h).
# Only MSVC's debug runtime has those dialogs and those checks.
if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(error_dialogs_check support/error_dialogs_check.cpp)
    coney_target_defaults(error_dialogs_check)
    target_link_libraries(error_dialogs_check PRIVATE coney_platform)
    add_test(NAME coney.error_dialogs_off
             COMMAND ${CMAKE_COMMAND} -DCHECK_EXE=$<TARGET_FILE:error_dialogs_check> -P
                     "${CMAKE_CURRENT_SOURCE_DIR}/support/error_dialogs_check.cmake")
    set_tests_properties(coney.error_dialogs_off PROPERTIES TIMEOUT 60)
endif()
