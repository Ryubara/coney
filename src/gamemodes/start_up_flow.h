// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "gamemodes/front_end_services.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/legal_screen_mode.h"
#include "gamemodes/level_flow_mode.h"
#include "gamemodes/memory_card_mode.h"
#include "gamemodes/mission_complete_mode.h"
#include "gamemodes/profile_manager_mode.h"
#include "gamemodes/rumble_menu_mode.h"
#include "graphics/render_device.h"
#include "graphics/screen_fade.h"
#include "gui/global_strings.h"
#include "gui/rumble_mode_gui/rumble_data.h"
#include "scripting/anim_callbacks.h"
#include "scripting/message_handlers.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/volume_boxes.h"

namespace coney {

/// The game's start-up path from the movies to the main menu, as `main` sets it up: the script system, the game state,
/// the modes it pushes (8, 6, 5), the ones the front end pushes (0x12, then 0xb and 1 for a story game), with the
/// services they share. `coney --disc` and the disc test build it the same way.
///
/// It is also what the script bindings reach outside the script system (script::BindingHost): `ShowProfileManager`
/// shows the menus, `MenuLoadLevel` chooses a level in the level flow, `HUDLaunchMissionComplete` pushes the
/// mission-complete mode, `HuCreate` and `AddFlag` keep the level scripts' humans and flags, `ScreenQueueEffect` starts
/// a fade, `ShowRumbleModeInterface` pushes the Rumble menu (mode 0x11), and music and movies go
/// to FrontEndServices.
///
/// Research: docs/research/boot.md#main, docs/research/frontend.md#mode-flow, docs/research/scripting.md
class StartUpFlow final : public script::BindingHost {
  public:
    /// The modes over `device`, sheets from `loadSheet` and `strings` (which the preloads fill), run on `stack`, with
    /// scripts read through `scripts` (empty: no script system; the level flow then shows the menus itself). `legal`
    /// picks the legal screen's picture and the language; its `europe` flag also hides PM_Extras. Every argument must
    /// outlive the flow; `log` gets the modes', the services' and the scripts' lines. `loadLevel` loads a chosen level
    /// for gameplay (mode 1); empty: no gameplay, and the level flow brings the menus back instead.
    StartUpFlow(graphics::RenderDevice& device, GameModeStack& stack, const ProfileManagerMode::SheetLoader& loadSheet,
                gui::GlobalStrings& strings, LegalScreenSettings legal,
                const std::function<void(std::string_view)>& log, script::ScriptSource scripts = {},
                GameplayMode::LevelLoader loadLevel = {});

    /// What `main` does from the subsystems' start on: makes the Lua state (`Game_InitializeSubsystems`), plays the
    /// start-up movies (skipped: FrontEndServices), pushes the level flow, asks for the memory-card boot check, pushes
    /// the memory-card mode, then the legal screen, which runs first. Coney leaves out the controller check and its
    /// error mode (the pads are always read).
    void start();

    /// The services the modes share.
    [[nodiscard]] FrontEndServices& services() { return m_services; }
    /// The script system.
    [[nodiscard]] script::ScriptSystem& scripts() { return m_scripts; }
    /// The game state (language, level table).
    [[nodiscard]] GameState& state() { return m_state; }
    /// The configuration the stub bindings recorded.
    [[nodiscard]] const script::RecordedCalls& recorded() const { return m_recorded; }
    /// The screen fade.
    [[nodiscard]] const graphics::ScreenFade& fade() const { return m_fade; }
    /// Mode 5.
    [[nodiscard]] LegalScreenMode& legal() { return m_legal; }
    /// Mode 6.
    [[nodiscard]] MemoryCardMode& memoryCard() { return m_memoryCard; }
    /// Mode 8.
    [[nodiscard]] LevelFlowMode& levelFlow() { return m_levelFlow; }
    /// Mode 0x12.
    [[nodiscard]] ProfileManagerMode& profileManager() { return m_profileManager; }
    /// Mode 0xb.
    [[nodiscard]] MissionCompleteMode& missionComplete() { return m_missionComplete; }
    /// Mode 1.
    [[nodiscard]] GameplayMode& gameplay() { return m_gameplay; }
    /// Mode 0x11.
    [[nodiscard]] RumbleMenuMode& rumbleMenu() { return m_rumbleMenu; }
    /// The humans the level scripts created.
    [[nodiscard]] const CreatedHumans& humans() const { return m_humans; }
    /// The level's world flags.
    [[nodiscard]] const world_objects::WorldFlags& flags() const { return m_flags; }

    void showProfileManager(std::string_view onRumble, std::string_view onStartGame) override;
    void showRumbleModeInterface(std::string_view onCancel, std::string_view onStart, double players) override;
    void menuLoadLevel(std::string_view level) override;
    void playMovie(std::string_view name) override;
    void playMusic(std::string_view track) override;
    void stopMusic() override;
    void queueScreenEffect(int type, double seconds) override;
    void launchMissionComplete(int kind) override;

  private:
    GameModeStack& m_stack;
    std::function<void(std::string_view)> m_log;
    GameState m_state;
    script::RecordedCalls m_recorded;
    CreatedHumans m_humans;
    world_objects::WorldFlags m_flags;
    script::MessageHandlers m_messages;
    world_objects::VolumeBoxes m_boxes;
    script::AnimCallbacks m_animCallbacks;
    gui::RumbleData m_rumbleData; // the Rumble menu's lists, which its chunks build
    FrontEndServices m_services;
    graphics::ScreenFade m_fade;
    script::BindingContext m_context;
    bool m_hasScripts;
    script::ScriptSystem m_scripts; // after everything its bindings refer to
    ProfileManagerMode m_profileManager;
    GameplayMode m_gameplay;
    LevelFlowMode m_levelFlow;
    MissionCompleteMode m_missionComplete;
    RumbleMenuMode m_rumbleMenu;
    MemoryCardMode m_memoryCard;
    LegalScreenMode m_legal;
};

} // namespace coney
