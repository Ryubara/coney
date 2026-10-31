# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(coney_tests
    camera/camera_lens_test.cpp
    core/chunk_system_test.cpp
    core/clip_recording_test.cpp
    core/cpak_archive_test.cpp
    core/cpak_index_test.cpp
    core/cpak_writer_test.cpp
    core/deferred_input_test.cpp
    core/event_log_test.cpp
    core/log_test.cpp
    core/frame_clock_test.cpp
    core/frame_rate_meter_test.cpp
    core/game_random_test.cpp
    core/game_timer_test.cpp
    core/input_script_test.cpp
    core/json_test.cpp
    core/interpolation_test.cpp
    core/name_hash_test.cpp
    core/parse_number_test.cpp
    core/ps2_float_test.cpp
    core/script_patch_apply_test.cpp
    core/script_patch_merge_test.cpp
    core/script_patch_test.cpp
    core/sequence_diff_test.cpp
    core/sha1_test.cpp
    core/sha256_test.cpp
    core/story_graph_test.cpp
    core/mesh_view_option_test.cpp
    core/world_edit_option_test.cpp
    core/draw_hash_option_test.cpp
    core/options_test.cpp
    core/perf_test.cpp
    core/perf_bench_options_test.cpp
    core/perf_frames_test.cpp
    core/pad_test.cpp
    core/pad_turn_test.cpp
    core/renderer_choice_test.cpp
    core/renderer_option_test.cpp
    core/player_options_test.cpp
    core/window_display_test.cpp
    effects/effect_culling_test.cpp
    effects/glints_test.cpp
    effects/ground_fog_test.cpp
    effects/light_tasks_test.cpp
    effects/motion_blur_test.cpp
    effects/particles_test.cpp
    effects/room_smoke_test.cpp
    effects/screen_tint_test.cpp
    effects/water_test.cpp
    effects/blur_pulse_test.cpp
    fileio/disc_test.cpp
    fileio/executable_test.cpp
    fileio/reader_test.cpp
    fileio/stream_test.cpp
    fileio/wad_index_test.cpp
    fileio/wad_test.cpp
    install/install_manifest_test.cpp
    install/installer_test.cpp
    launcher/release_manifest_test.cpp
    launcher/update_plan_test.cpp
    gamemodes/disc_front_end_test.cpp
    gamemodes/disc_lighting_test.cpp
    gamemodes/frame_rate_test.cpp
    gamemodes/gameplay_objects_test.cpp
    gamemodes/gameplay_sound_call_test.cpp
    gamemodes/gameplay_tag_test.cpp
    gamemodes/game_mode_stack_test.cpp
    gamemodes/idle_mode_test.cpp
    gamemodes/legal_screen_mode_test.cpp
    gamemodes/hud_feed_test.cpp
    gamemodes/level_items_test.cpp
    gamemodes/level_pickups_test.cpp
    gamemodes/loading_screen_test.cpp
    gamemodes/pause_mode_test.cpp
    gamemodes/load_entry_mode_test.cpp
    gamemodes/movie_player_test.cpp
    movies/captions_test.cpp
    movies/movie_mode_test.cpp
    gamemodes/start_up_flow_test.cpp
    gamemodes/rumble_start_test.cpp
    gamemodes/story_start_test.cpp
    graphics/asset_store_test.cpp
    graphics/font_test.cpp
    graphics/frame_stats_test.cpp
    graphics/car_draw_test.cpp
    graphics/throw_arc_test.cpp
    graphics/human_blood_test.cpp
    graphics/human_lighting_test.cpp
    graphics/light_manager_test.cpp
    graphics/lighting_frame_test.cpp
    graphics/overlay_camera_test.cpp
    graphics/particle_page_test.cpp
    graphics/ps2_world_mesh_test.cpp
    graphics/reference_sprites_test.cpp
    graphics/rw_stream_test.cpp
    graphics/screen_fade_test.cpp
    graphics/screen_test.cpp
    graphics/sprite_batch_test.cpp
    graphics/texture_grid_test.cpp
    graphics/texture_lod_test.cpp
    gui/disc_text_test.cpp
    gui/markup_test.cpp
    gui/menu_input_test.cpp
    gui/message_paging_test.cpp
    gui/circled_text_header_test.cpp
    gui/game_stats_test.cpp
    gui/control_menu_test.cpp
    gui/option_menu_test.cpp
    gui/pause_menu_test.cpp
    gui/pause_stats_test.cpp
    gui/profile_management_gui/pm_controller_test.cpp
    gui/rumble_mode_gui/rumble_menu_test.cpp
    gui/rumble_mode_gui/rumble_match_screens_test.cpp
    gui/screen_flow_controller_test.cpp
    gui/text_layout_test.cpp
    gui/widgets_test.cpp
    gui/bar_test.cpp
    hud/armies_hud_test.cpp
    hud/credits_test.cpp
    hud/health_rings_test.cpp
    hud/hud_test.cpp
    hud/mash_meter_test.cpp
    hud/radar_test.cpp
    hud/player_panel_test.cpp
    hud/scripted_bars_test.cpp
    hud/stereo_hud_test.cpp
    hud/tag_hud_test.cpp
    hud/fixed_cam_icon_test.cpp
    hud/num_indicator_test.cpp
    hud/crime_panel_test.cpp
    hud/spinner_test.cpp
    hud/lock_pick_hud_test.cpp
    hud/mug_meter_test.cpp
    hud/war_command_display_test.cpp
    platform/disc_install_test.cpp
    platform/install_command_test.cpp
    platform/script_patch_command_test.cpp
    platform/install_folder_fs_test.cpp
    platform/disc_sprite_sheets_test.cpp
    platform/disc_rumble_gang_count_test.cpp
    platform/disc_rumble_test.cpp
    platform/disc_hub_cutscene_audio_test.cpp
    platform/disc_hub_test.cpp
    platform/disc_level80_end_test.cpp
    platform/disc_level80_test.cpp
    platform/disc_flash_test.cpp
    platform/disc_crew_commands_test.cpp
    platform/disc_given_weapon_test.cpp
    platform/disc_level80_wreck_test.cpp
    platform/disc_revive_test.cpp
    platform/disc_level80_story_test.cpp
    platform/disc_level5_boss_camera_test.cpp
    platform/disc_level5_fights_test.cpp
    platform/disc_level34_furies_test.cpp
    platform/disc_level99_audio_test.cpp
    platform/disc_level99_heap_test.cpp
    platform/disc_level99_scene_speech_test.cpp
    platform/disc_level99_hud_test.cpp
    platform/disc_level99_story_sounds_test.cpp
    platform/disc_level99_power_lesson_test.cpp
    platform/disc_level99_scene_grab_test.cpp
    platform/disc_level99_snaps_test.cpp
    platform/disc_level99_course_test.cpp
    platform/disc_level99_sparring_test.cpp
    platform/disc_level99_change_character_test.cpp
    platform/disc_scene_option_test.cpp
    platform/disc_bat_cross_test.cpp
    platform/disc_spray_paint_test.cpp
    platform/disc_spawner_money_test.cpp
    platform/disc_credits_test.cpp
    platform/disc_pause_controls_test.cpp
    platform/disc_pause_options_test.cpp
    platform/disc_pause_stats_test.cpp
    platform/disc_game_stats_test.cpp
    platform/disc_level99_class_steps_test.cpp
    platform/disc_level99_player2_test.cpp
    platform/disc_hub_workout_test.cpp
    platform/disc_level95_crew_lock_test.cpp
    platform/disc_level99_street_test.cpp
    platform/disc_level99_vermin_fence_test.cpp
    platform/disc_level99_world_test.cpp
    platform/disc_level34_objectives_test.cpp
    platform/disc_level99_fence_dealer_test.cpp
    platform/disc_level99_fence_lesson_test.cpp
    platform/disc_story_chain_test.cpp
    platform/disc_story_graph_test.cpp
    platform/disc_story_missions_test.cpp
    platform/disc_story_playthrough_test.cpp
    platform/disc_tagging_test.cpp
    platform/disc_story_start_test.cpp
    platform/disc_textures_test.cpp
    platform/disc_throw_arc_test.cpp
    platform/disc_world_test.cpp
    platform/disc_level_test.cpp
    platform/disc_movies_test.cpp
    platform/disc_play_frame_rate_test.cpp
    platform/disc_world_streaming_test.cpp
    platform/clip_recorder_test.cpp
    platform/draw_calls_test.cpp
    platform/draw_stream_test.cpp
    platform/ffmpeg_info_test.cpp
    platform/gpu_renderer_test.cpp
    platform/present_mode_test.cpp
    platform/profile_folder_test.cpp
    platform/render_engine_restart_test.cpp
    platform/room_smoke_overlay_test.cpp
    platform/scene_stage_test.cpp
    platform/sdl_devices_test.cpp
    platform/pad_pipe_test.cpp
    platform/sdl_input_test.cpp
    platform/window_activation_test.cpp
    platform/sprite_sheets_test.cpp
    platform/texture_dictionary_test.cpp
    platform/asset_convert_test.cpp
    platform/disc_asset_convert_test.cpp
    platform/world_atomic_test.cpp
    platform/disc_front_end_wheel_test.cpp
    platform/front_end_scene_test.cpp
    platform/placed_objects_test.cpp
    platform/world_renderer_test.cpp
    platform/world_set_test.cpp
    physics/overlaps_test.cpp
    physics/sphere_box_sweep_test.cpp
    raycast/collision_mesh_test.cpp
    physics/body_sweep_test.cpp
    physics/mesh_sweep_test.cpp
    physics/sphere_triangle_test.cpp
    raycast/disc_collision_test.cpp
    scripting/ai_bindings_test.cpp
    scripting/anim_callbacks_test.cpp
    scripting/binding_args_test.cpp
    scripting/config_strings_test.cpp
    scripting/disc_script_system_test.cpp
    scripting/hub_bindings_test.cpp
    scripting/mission4_bindings_test.cpp
    scripting/mission7_bindings_test.cpp
    scripting/hud_bindings_test.cpp
    scripting/disc_strings_test.cpp
    scripting/camera_bindings_test.cpp
    scripting/level_bindings_test.cpp
    scripting/lighting_bindings_test.cpp
    scripting/shadow_bindings_test.cpp
    scripting/player_bindings_test.cpp
    scripting/lua_chunk_test.cpp
    scripting/lua_chunk_writer_test.cpp
    scripting/lua_compiler_test.cpp
    scripting/lua_nil_compare_test.cpp
    scripting/lua_libraries_test.cpp
    scripting/lua_vm_test.cpp
    scripting/scene_bindings_test.cpp
    scripting/object_bindings_test.cpp
    scripting/natives_test.cpp
    scripting/optional_args_test.cpp
    scripting/script_bindings_test.cpp
    scripting/script_scheduler_test.cpp
    scripting/rumble_match_bindings_test.cpp
    scripting/arena_bindings_test.cpp
    scripting/script_system_test.cpp
    scripting/car_bindings_test.cpp
    scripting/effects_bindings_test.cpp
    scripting/spawn_bindings_test.cpp
    scripting/trigger_bindings_test.cpp
    scripting/world_bindings_test.cpp
    warriors/created_humans_test.cpp
    warriors/disk_profile_store_test.cpp
    warriors/game_over_test.cpp
    warriors/level_table_test.cpp
    warriors/player_state_test.cpp
    warriors/profile_record_test.cpp
    warriors/tag_game_test.cpp
    warriors/unlock_counts_test.cpp
    world/debug_camera_test.cpp
    world/level_object_test.cpp
    world/path_map_test.cpp
    world/streamed_world_test.cpp
    world/view_frustum_test.cpp
    world/world_manifest_test.cpp
    world/world_streamer_test.cpp
    world/world_streams_test.cpp
    world_objects/car_hits_test.cpp
    world_objects/cars_test.cpp
    world_objects/flags_test.cpp
    world_objects/spinning_icons_test.cpp
    world_objects/drawn_poses_test.cpp
    world_objects/object_tasks_test.cpp
    world_objects/props_test.cpp
    world_objects/disc_doors_test.cpp
    world_objects/disc_object_list_test.cpp
    world_objects/door_blocking_test.cpp
    world_objects/doors_test.cpp
    world_objects/glass_test.cpp
    world_objects/hats_test.cpp
    world_objects/object_breaks_test.cpp
    world_objects/lock_pick_test.cpp
    world_objects/loose_objects_test.cpp
    world_objects/object_bodies_test.cpp
    world_objects/nav_links_test.cpp
    world_objects/object_list_test.cpp
    world_objects/pickups_test.cpp
    world_objects/volume_boxes_test.cpp
    world_objects/radios_test.cpp
    world_objects/tag_spots_test.cpp
    world_objects/trigger_spheres_test.cpp
    world_objects/flag_net_test.cpp)
# Characters: the animation format and player, and the character resources.
target_sources(coney_tests PRIVATE
    animation/anim_clip_test.cpp
    animation/anim_math_test.cpp
    animation/anim_pose_test.cpp
    animation/disc_anim_clips_test.cpp
    animation/skeleton_test.cpp
    characters/character_data_test.cpp
    characters/cfg_init_fields_test.cpp
    characters/character_class_test.cpp
    characters/character_types_test.cpp
    characters/starting_money_test.cpp
    characters/character_list_test.cpp
    characters/character_model_test.cpp
    characters/character_rig_test.cpp
    characters/reference_render_test.cpp
    characters/disc_characters_test.cpp
    characters/dynamic_clips_test.cpp
    characters/orbit_camera_test.cpp
    platform/character_mesh_test.cpp
    platform/character_mesh_skin_test.cpp
    platform/log_output_test.cpp)
# The player: animation tasks, locomotion, the human, the follow camera, and level99 driven by scripted pads.
target_sources(coney_tests PRIVATE
    animation/anim_task_test.cpp
    animation/held_flags_test.cpp
    camera/cameras_test.cpp
    camera/path_camera_test.cpp
    camera/rail_camera_test.cpp
    camera/win_camera_test.cpp
    camera/failed_camera_test.cpp
    camera/follow_camera_option_test.cpp
    camera/follow_camera_test.cpp
    camera/follow_collision_test.cpp
    camera/follow_placement_test.cpp
    camera/follow_script_test.cpp
    human/disc_player_placement_test.cpp
    human/disc_player_test.cpp
    human/class_flags_test.cpp
    human/human_edit_test.cpp
    human/human_test.cpp
    human/knockout_test.cpp
    human/workout_test.cpp
    human/human_revive_test.cpp
    human/human_uncuff_test.cpp
    human/debug_clip_test.cpp
    human/face_test.cpp
    human/way_clear_test.cpp
    human/flight_test.cpp
    human/hiding_test.cpp
    human/body_test.cpp
    human/climb_test.cpp
    human/combat_test.cpp
    human/spray_special_test.cpp
    human/throw_direction_test.cpp
    human/damage_flags_test.cpp
    human/combat_timing_test.cpp
    human/moving_attacks_test.cpp
    human/attack_turn_test.cpp
    human/player_stance_test.cpp
    human/player_targeting_test.cpp
    human/being_hit_test.cpp
    human/rise_counter_test.cpp
    human/human_grab_test.cpp
    human/human_tandem_test.cpp
    human/script_state_test.cpp
    human/jump_test.cpp
    human/locomotion_test.cpp
    human/pair_placement_test.cpp
    human/player_snapshot_test.cpp
    human/player_trace_test.cpp
    human/stamina_test.cpp
    human/strike_shapes_test.cpp
    human/traversal_test.cpp
    human/turn_and_slide_test.cpp
    human/humans_test.cpp
    human/human_contacts_test.cpp
    human/strike_hits_test.cpp
    human/locomotion_gate_test.cpp)
# Combat (docs/research/combat/index.md).
target_sources(coney_tests PRIVATE
    combat/ai_counter_test.cpp
    combat/anim_ranges_test.cpp
    combat/commands_test.cpp
    combat/attack_target_test.cpp
    combat/attacks_test.cpp
    combat/being_hit_test.cpp
    combat/disc_combat_test.cpp
    combat/grab_test.cpp
    combat/grabbed_test.cpp
    combat/lock_on_test.cpp
    combat/meters_test.cpp
    combat/player_combat_test.cpp
    combat/rage_awards_test.cpp
    combat/revive_test.cpp
    combat/reactions_test.cpp
    combat/stick_games_test.cpp
    combat/tandem_test.cpp
    combat/throw_aim_test.cpp
    combat/throw_velocity_test.cpp)
# Scenes: the records, the slots and the player (docs/research/scenes.md).
target_sources(coney_tests PRIVATE
    scenes/disc_scenes_test.cpp
    scenes/scene_player_test.cpp
    scenes/scene_record_test.cpp)
# The collision writer the sandbox and the Spawner use (the sandbox's own tests are the debug tools', below).
target_sources(coney_tests PRIVATE
    raycast/collision_builder_test.cpp)
# The AI humans' brains (docs/research/ai/index.md).
target_sources(coney_tests PRIVATE
    ai/face_mood_test.cpp
    ai/hu_create_init_test.cpp
    ai/attack_choice_test.cpp
    ai/attack_reach_test.cpp
    ai/fidget_action_test.cpp
    ai/fighter_sides_test.cpp
    ai/fight_goal_test.cpp
    ai/fight_trace_test.cpp
    ai/fight_reactions_test.cpp
    ai/boss_goals_test.cpp
    ai/brain_events_test.cpp
    ai/brain_test.cpp
    ai/goal_lifecycle_test.cpp
    ai/crew_test.cpp
    ai/smash_goals_test.cpp
    ai/steal_goals_test.cpp
    ai/disc_ai_config_test.cpp
    ai/disc_route_test.cpp
    ai/formations_test.cpp
    ai/gangs_test.cpp
    ai/give_way_test.cpp
    ai/move_action_test.cpp
    ai/perception_test.cpp
    ai/hiding_test.cpp
    ai/enemy_scan_test.cpp
    ai/scout_tactic_test.cpp
    ai/route_planner_test.cpp
    ai/scripted_goals_test.cpp
    ai/scripted_hub_test.cpp
    ai/run_carrot_test.cpp
    ai/scripted_character_test.cpp
    ai/scripted_humans_test.cpp
    ai/riot_goals_test.cpp
    ai/scripted_story_test.cpp
    ai/sectors_test.cpp
    ai/steering_test.cpp
    ai/spawner_money_test.cpp
    ai/spawners_test.cpp
    ai/rumble_tactics_test.cpp
    ai/swap_prompt_test.cpp
    ai/tactic_attack_test.cpp
    ai/taunt_action_test.cpp
    ai/tactics_test.cpp
    ai/targeting_test.cpp
    ai/threats_test.cpp)
# The sound engine, its data and streams, the mixer and the game-facing sound calls (docs/research/sound.md).
target_sources(coney_tests PRIVATE audio/adpcm_test.cpp audio/disc_sound_test.cpp audio/game_sound_test.cpp
                                   audio/mixer_test.cpp audio/music_player_test.cpp audio/object_sounds_test.cpp
                                   audio/sound_data_test.cpp audio/sound_engine_test.cpp audio/sound_matrix_test.cpp scripting/sound_bindings_test.cpp
                                   audio/human_sound_events_test.cpp animation/clip_sound_events_test.cpp
                                   audio/type_traits_test.cpp)
# Network play: the bit stream, the transports, the protocol and the sessions over the loopback, and ENet on 127.0.0.1
# (docs/guides/multiplayer.md).
target_sources(coney_tests PRIVATE net/bit_stream_test.cpp net/input_buffer_test.cpp net/link_conditioner_test.cpp
                                   net/link_quality_test.cpp net/loopback_transport_test.cpp net/net_id_test.cpp net/protocol_test.cpp net/replicated_fields_test.cpp net/snapshot_test.cpp
                                   net/jitter_buffer_test.cpp net/client_world_test.cpp
                                   net/sessions_test.cpp platform/enet_transport_test.cpp platform/net_play_test.cpp)
# The helper that registers one test case per item, so that ctest runs a long disc test's items in parallel
# (docs/guides/testing.md#one-test-per-item).
target_sources(coney_tests PRIVATE support/each_case_test.cpp)
# A failed runtime check fails the run on stderr instead of opening a dialog (platform/error_dialogs.h).
target_sources(coney_tests PRIVATE support/error_dialogs_off.cpp)
coney_target_defaults(coney_tests)
# tests/ on the include path for the shared synthetic fixtures (support/fixtures.h).
target_include_directories(coney_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# The checkout's folders the tests read: the shipped sandbox layouts (Coney's own, assets/sandbox), the input
# scripts the disc tests play and the Lua 4.0 fixtures the decompiler's and the compiler's tests share. Only support/test_paths.cpp gets them, never the whole target: they are absolute paths
# of this checkout, and on every test file they made the command lines differ between worktrees, so the shared
# compiler cache missed every test object (docs/guides/building.md#compiler-cache). A Python test refuses such a
# definition on a target.
target_sources(coney_tests PRIVATE support/test_paths.cpp)
set_source_files_properties(support/test_paths.cpp PROPERTIES COMPILE_DEFINITIONS
    "CONEY_ASSETS_DIR=\"${PROJECT_SOURCE_DIR}/assets\";CONEY_TEST_SUPPORT_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/support\";CONEY_LUA4_FIXTURES_DIR=\"${PROJECT_SOURCE_DIR}/python/tests/lua4/fixtures\";CONEY_SHADERS_DIR=\"${PROJECT_SOURCE_DIR}/src/platform/shaders\"")
# coney_platform for librw, which the texture tests run on its NULL device (no window, no GPU); coney_launcher for the
# launcher's library (docs/guides/launcher.md).
target_link_libraries(coney_tests PRIVATE coney_platform coney_launcher Catch2::Catch2WithMain)
# The tests of the debug tools (the menus, the overlay, the sandbox, the viewers, the play mode's debug controls and the
# disc tests that drive them through those), only in a build with them (docs/guides/building.md#debug-tools); the
# release build runs every other test, so the game is tested without them.
if(CONEY_DEBUG_TOOLS)
    target_sources(
        coney_tests PRIVATE
        debug/anim_catalog_test.cpp
        debug/anim_page_test.cpp
        debug/audio_page_test.cpp
        debug/combat_tunables_test.cpp
        debug/debug_session_test.cpp
        debug/entity_edit_test.cpp
        debug/entity_editor_page_test.cpp
        debug/entity_inspector_test.cpp
        debug/fighter_spawn_test.cpp
        debug/human_shapes_test.cpp
        debug/input_gate_test.cpp
        debug/lua_console_test.cpp
        debug/menu_navigator_test.cpp
        debug/mesh_views_test.cpp
        debug/movies_page_test.cpp
        debug/net_page_test.cpp
        debug/native_caller_test.cpp
        debug/pad_menu_input_test.cpp
        debug/record_debug_draw_test.cpp
        debug/play_pages_test.cpp
        debug/story_missions_test.cpp
        debug/time_scale_test.cpp
        debug/tunables_test.cpp
        debug/ui_scale_test.cpp
        debug/world_split_test.cpp
        gamemodes/sheet_viewer_mode_test.cpp
        gamemodes/text_viewer_mode_test.cpp
        gui/debug_menu_view_test.cpp
        platform/disc_anim_player_test.cpp
        platform/disc_armies_test.cpp
        platform/disc_face_test.cpp
        platform/disc_change_character_test.cpp
        platform/disc_debug_ai_sides_test.cpp
        platform/disc_debug_no_gang_test.cpp
        platform/disc_debug_clear_fighters_test.cpp
        platform/disc_debug_fighter_sounds_test.cpp
        platform/disc_debug_movies_test.cpp
        platform/disc_fall_camera_test.cpp
        platform/disc_fighter_models_test.cpp
        platform/disc_entity_editor_test.cpp
        platform/disc_game_session_test.cpp
        platform/disc_hub_doors_test.cpp
        platform/disc_inspector_freeze_test.cpp
        platform/disc_level93_molotov_test.cpp
        platform/disc_level99_car_climb_test.cpp
        platform/disc_level99_handover_test.cpp
        platform/disc_level99_money_sound_test.cpp
        platform/disc_level99_noclip_test.cpp
        platform/disc_level99_powerups_test.cpp
        platform/disc_level99_spawner_test.cpp
        platform/disc_level99_store_glass_test.cpp
        platform/disc_level99_store_register_test.cpp
        platform/disc_level99_street_props_test.cpp
        platform/disc_mesh_views_draw_test.cpp
        platform/disc_mesh_views_test.cpp
        platform/disc_model_swap_test.cpp
        platform/disc_rise_counter_test.cpp
        platform/disc_sprint_attacks_test.cpp
        platform/disc_strike_contact_test.cpp
        platform/disc_tandem_test.cpp
        platform/imgui_model_view_test.cpp
        platform/imgui_ui_scale_test.cpp
        platform/mesh_views_test.cpp
        platform/world_edit_test.cpp
        platform/disc_world_split_test.cpp
        raycast/collision_extras_test.cpp
        sandbox/disc_sandbox_combat_test.cpp
        sandbox/disc_sandbox_player_test.cpp
        sandbox/disc_sandbox_traversal_test.cpp
        sandbox/disc_throw_direction_test.cpp
        sandbox/sandbox_ground_test.cpp
        sandbox/sandbox_layout_test.cpp
        sandbox/sandbox_mesh_test.cpp
        sandbox/sandbox_world_test.cpp)
    target_link_libraries(coney_tests PRIVATE coney_devtools)
    # Coney Studio's editor over bare Dear ImGui contexts and synthetic projects (docs/guides/studio/prototype.md).
    target_sources(coney_tests PRIVATE studio/inspector_test.cpp studio/level_camera_test.cpp
                                       studio/level_document_test.cpp studio/project_test.cpp studio/studio_app_test.cpp)
    target_link_libraries(coney_tests PRIVATE coney_studio)
endif()
include(Catch)
# The disc check skips (Catch2's exit code 4) unless CONEY_DISC names the player's disc; ctest reports it as skipped.
# A Catch2 tag is also a ctest label: a test case tagged [disc] is the label `disc`, so `ctest -L disc` runs (or lists)
# the tests that need the disc and `-LE disc` leaves them out. Every test that opens the disc carries the tag.
catch_discover_tests(coney_tests ADD_TAGS_AS_LABELS)

# The HLSL shaders the build compiles and embeds (docs/guides/shaders.md): that each one has its SPIR-V, DXIL and MSL, and
# that the compiler refuses a shader that does not compile instead of leaving an empty file behind. A small executable of
# its own, so it builds and runs without the engine's thousand test files.
if(CONEY_SHADERS)
    add_executable(coney_shader_tests platform/embedded_shaders_test.cpp support/test_paths.cpp)
    coney_target_defaults(coney_shader_tests)
    target_include_directories(coney_shader_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(coney_shader_tests PRIVATE coney_shaders Catch2::Catch2WithMain)
    catch_discover_tests(coney_shader_tests ADD_TAGS_AS_LABELS)
    foreach(_format SPIRV DXIL MSL)
        add_test(NAME coney.shaders.refuse_a_broken_shader_${_format}
                 COMMAND ${CMAKE_COMMAND} -DTOOL=${CONEY_SHADERCROSS} -DFORMAT=${_format}
                         -DCOMPILE_SCRIPT=${PROJECT_SOURCE_DIR}/cmake/coney_shader_compile.cmake
                         -DBROKEN=${CMAKE_CURRENT_SOURCE_DIR}/support/shaders/broken.frag.hlsl
                         -DGOOD=${PROJECT_SOURCE_DIR}/src/platform/shaders/blit.frag.hlsl
                         -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/shader_refusal_${_format}
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/support/shader_refusal_check.cmake)
        set_tests_properties(coney.shaders.refuse_a_broken_shader_${_format} PROPERTIES TIMEOUT 120)
    endforeach()
endif()

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

# Coney's Lua 4.0 compiler behind luac's command line, for the decompiler's round trip (support/coney_luac.cpp,
# docs/guides/coney-tools-fidelity.md#lua). A developer tool, built with the tests; no test runs it.
add_executable(coney_luac support/coney_luac.cpp)
coney_target_defaults(coney_luac)
target_link_libraries(coney_luac PRIVATE coney_core)

# The passes that copy the frame and lay it back must not flip it (support/frame_copy_check.cpp). It needs a display and
# OpenGL 3.3, so it reports itself skipped (77) on machines without them, such as CI's.
add_executable(frame_copy_check support/frame_copy_check.cpp)
coney_target_defaults(frame_copy_check)
target_link_libraries(frame_copy_check PRIVATE coney_platform)
add_test(NAME coney.frame_copies_upright COMMAND frame_copy_check)
set_tests_properties(coney.frame_copies_upright PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
