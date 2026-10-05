# SPDX-License-Identifier: GPL-3.0-or-later
add_library(coney_core STATIC
    camera/camera_lens.cpp
    core/assert.cpp
    core/chunk_stacks.cpp
    core/chunk_system.cpp
    core/chunk_types.cpp
    core/frame_clock.cpp
    core/game_timer.cpp
    core/input_script.cpp
    core/name_hash.cpp
    core/parse_number.cpp
    core/options.cpp
    core/pad.cpp
    core/pads.cpp
    fileio/disc.cpp
    fileio/file_stream.cpp
    fileio/reader.cpp
    fileio/stream.cpp
    fileio/wad.cpp
    fileio/wad_index.cpp
    gamemodes/front_end_services.cpp
    gamemodes/game_mode_stack.cpp
    gamemodes/gameplay_mode.cpp
    gamemodes/legal_screen_mode.cpp
    gamemodes/level_flow_mode.cpp
    gamemodes/level_start.cpp
    gamemodes/load_entry_mode.cpp
    gamemodes/memory_card_mode.cpp
    gamemodes/mission_complete_mode.cpp
    gamemodes/profile_manager_mode.cpp
    gamemodes/sheet_viewer_mode.cpp
    gamemodes/start_up_flow.cpp
    gamemodes/text_viewer_mode.cpp
    graphics/font.cpp
    graphics/frame_stats.cpp
    graphics/overlay_camera.cpp
    graphics/ps2_world_mesh.cpp
    graphics/particle_page.cpp
    graphics/rw_stream.cpp
    graphics/screen.cpp
    graphics/screen_fade.cpp
    graphics/sprite_batch.cpp
    graphics/texture_grid.cpp
    raycast/collision_mesh.cpp
    gui/base_widget.cpp
    gui/global_strings.cpp
    gui/markup.cpp
    gui/menu_input.cpp
    gui/option_grid.cpp
    gui/profile_management_gui/pm_controller.cpp
    gui/profile_management_gui/pm_greet.cpp
    gui/profile_management_gui/pm_mode.cpp
    gui/profile_management_gui/pm_placeholder.cpp
    gui/screen_flow_controller.cpp
    gui/text_layout.cpp
    gui/text_widget.cpp
    gui/usage_info.cpp
    gui/widget.cpp
    scripting/config_strings.cpp
    scripting/lua_chunk.cpp
    scripting/lua_libraries.cpp
    scripting/lua_value.cpp
    scripting/lua_vm.cpp
    scripting/script_bindings.cpp
    scripting/script_system.cpp
    warriors/created_humans.cpp
    warriors/level_table.cpp
    world/debug_camera.cpp
    world/level_object.cpp
    world/sector_budget.cpp
    world/streamed_world.cpp
    world/view_frustum.cpp
    world/world_manifest.cpp
    world/world_streamer.cpp
    world/world_streams.cpp)
# Characters: the animation format and player, and the character resources (docs/research/characters.md).
target_sources(coney_core PRIVATE
    animation/anim_clip.cpp
    animation/anim_math.cpp
    animation/anim_pose.cpp
    animation/skeleton.cpp
    characters/character_assets.cpp
    characters/character_data.cpp
    characters/character_list.cpp
    characters/character_model.cpp
    characters/character_rig.cpp
    characters/clump_reader.cpp
    characters/orbit_camera.cpp
    characters/reference_render.cpp)
# The player: animation tasks, the human's locomotion, body and ground, sprint, jump and climbs, the follow camera
# (docs/research/characters.md, docs/research/camera.md).
target_sources(coney_core PRIVATE
    animation/anim_task.cpp
    camera/follow_camera.cpp
    characters/anim_set.cpp
    human/body.cpp
    human/climb.cpp
    human/human.cpp
    human/human_animator.cpp
    human/jump.cpp
    human/locomotion.cpp
    human/fighter.cpp
    human/player.cpp
    human/stamina.cpp
    human/target_human.cpp)
# Combat: the player's commands, attack chains, damage, grabs and meters (docs/research/combat.md).
target_sources(coney_core PRIVATE
    combat/anim_ranges.cpp
    combat/attacks.cpp
    combat/combat_tuning.cpp
    combat/commands.cpp
    combat/grab.cpp
    combat/meters.cpp
    combat/player_combat.cpp
    combat/reactions.cpp
    combat/stick_games.cpp
    debug/combat_tunables.cpp)
# The sandbox: Coney's own test worlds of primitives (docs/guides/sandbox.md), and the collision writer they use.
target_sources(coney_core PRIVATE
    raycast/collision_builder.cpp
    sandbox/sandbox_layout.cpp
    sandbox/sandbox_lighting.cpp
    sandbox/sandbox_mesh.cpp
    sandbox/sandbox_world.cpp)
# The debug menus: the model every front end renders, and its pages (docs/guides/debug-menu.md).
target_sources(coney_core PRIVATE
    debug/debug_session.cpp
    debug/game_tunables.cpp
    debug/input_gate.cpp
    debug/lua_console.cpp
    debug/menu_model.cpp
    debug/menu_navigator.cpp
    debug/native_caller.cpp
    debug/native_signatures.cpp
    debug/pad_menu_input.cpp
    debug/pages_engine.cpp
    debug/pages_play.cpp
    debug/pages_scripts.cpp
    debug/time_control.cpp
    debug/time_series.cpp
    debug/tunables.cpp)
# The pad debug menu's renderer: the in-game list in the game's font or Coney's bitmap font.
target_sources(coney_core PRIVATE graphics/bitmap_font.cpp gui/debug_menu_view.cpp gui/debug_text_painter.cpp)
coney_target_defaults(coney_core)
target_include_directories(coney_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory(platform)
