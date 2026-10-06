// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "gamemodes/front_end_services.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/legal_screen_mode.h"
#include "gamemodes/level_flow_mode.h"
#include "gamemodes/memory_card_mode.h"
#include "gamemodes/mission_complete_mode.h"
#include "gamemodes/mission_failed_mode.h"
#include "gamemodes/pause_mode.h"
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
#include "warriors/profile_store.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"
#include "world_objects/volume_boxes.h"

namespace coney {

/// The game's start-up path from the movies to the main menu, as `main` sets it up: the script system, the game state,
/// the modes it pushes (8, 6, 5), the ones the front end pushes (0x12, then 0xb and 1 for a story game), with the
/// services they share. `coney --disc` and the disc test build it the same way.
///
/// It is also what the script bindings reach outside the script system (script::BindingHost): `ShowProfileManager`
/// shows the menus, `MenuLoadLevel` chooses a level in the level flow, `HUDLaunchMissionComplete` pushes the
/// mission-complete mode, `HuCreate` and `AddFlag` keep the level scripts' humans and flags, `ScreenQueueEffect` starts
/// a fade, `ShowRumbleModeInterface` pushes the Rumble menu (mode 0x11), `SSMC_StartLoadSequence` and
/// `SSMC_StartDeleteSequence` push the memory-card mode (6) to read the profiles again or after a delete, and music
/// and movies go to FrontEndServices.
///
/// Research: docs/research/boot.md#main, docs/research/frontend.md#mode-flow, docs/research/scripting.md
class StartUpFlow final : public script::BindingHost {
  public:
    /// The modes over `device`, sheets from `loadSheet` and `strings` (which the preloads fill), run on `stack`, with
    /// scripts read through `scripts` (empty: no script system; the level flow then shows the menus itself). `legal`
    /// picks the legal screen's picture and the language; its `europe` flag also hides PM_Extras. Every argument must
    /// outlive the flow; `log` gets the modes', the services' and the scripts' lines. `loadLevel` loads a chosen level
    /// for gameplay (mode 1); empty: no gameplay, and the level flow brings the menus back instead. `profileFolder`
    /// holds the saved profiles (DiskProfileStore); nothing: they last the run (SessionProfileStore), as in test mode.
    /// `cardCheckingMs` is how long the memory-card mode shows its "checking" message:
    /// MemoryCardMode::kCheckingMessageMs in the game, 0 (the default) in the frame-scripted tests (MemoryCardMode
    /// gives why).
    StartUpFlow(graphics::RenderDevice& device, GameModeStack& stack, const ProfileManagerMode::SheetLoader& loadSheet,
                gui::GlobalStrings& strings, LegalScreenSettings legal,
                const std::function<void(std::string_view)>& log, script::ScriptSource scripts = {},
                GameplayMode::LevelLoader loadLevel = {},
                const std::optional<std::filesystem::path>& profileFolder = std::nullopt,
                std::uint64_t cardCheckingMs = 0);

    /// What `main` does from the subsystems' start on: makes the Lua state (`Game_InitializeSubsystems`), pushes the
    /// level flow, asks for the memory-card boot check, pushes the memory-card mode, then the legal screen, and asks
    /// for the start-up movies, which the movie player pushes over them all so they play first (the original plays them
    /// before the pushes). Coney leaves out the controller check and its error mode (the pads are always read).
    void start();

    /// The services the modes share.
    [[nodiscard]] FrontEndServices& services() { return m_services; }
    /// The script system.
    [[nodiscard]] script::ScriptSystem& scripts() { return m_scripts; }
    /// What its bindings work on (main gives it the game's sound).
    [[nodiscard]] script::BindingContext& context() { return m_context; }
    /// The game state (language, level table).
    [[nodiscard]] GameState& state() { return m_state; }
    /// The configuration the stub bindings recorded.
    [[nodiscard]] const script::RecordedCalls& recorded() const { return m_recorded; }
    /// The save system: the profiles on disk, or for the run only.
    [[nodiscard]] ProfileStore& profiles() { return *m_profiles; }
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
    /// Mode 0xa.
    [[nodiscard]] PauseMode& pause() { return m_pause; }
    /// Mode 0xc.
    [[nodiscard]] MissionFailedMode& missionFailed() { return m_missionFailed; }
    /// Loads the sheets of sprite-sheet table records (the pause menu's background, the mission-failed title) through
    /// `loadRecord` from now on; until then they are not drawn.
    void setSheetRecordLoader(PauseRecordLoader loadRecord) { m_loadRecord = std::move(loadRecord); }
    /// Gives the pause and mission-failed modes the game's hooks (sound, objectives, radars).
    void setPauseHooks(const PauseHooks& hooks);
    /// Mode 0x11.
    [[nodiscard]] RumbleMenuMode& rumbleMenu() { return m_rumbleMenu; }
    /// The humans the level scripts created.
    [[nodiscard]] const CreatedHumans& humans() const { return m_humans; }
    /// The level's world flags.
    [[nodiscard]] const world_objects::WorldFlags& flags() const { return m_flags; }
    /// The object types the scripts configured (`CfgObj`).
    [[nodiscard]] const world_objects::ObjectTypes& objectTypes() const { return m_objectTypes; }
    /// The dynamic objects' spawn records (`ObjSpawn`).
    [[nodiscard]] world_objects::SpawnRecords& spawnRecords() { return m_spawnRecords; }
    [[nodiscard]] const world_objects::SpawnRecords& spawnRecords() const { return m_spawnRecords; }

    void showProfileManager(std::string_view onRumble, std::string_view onStartGame) override;
    void showRumbleModeInterface(std::string_view onCancel, std::string_view onStart, double players) override;
    void menuLoadLevel(std::string_view level) override;
    void playMovie(std::string_view name) override;
    void playMusic(std::string_view track) override;
    void stopMusic() override;
    void queueScreenEffect(int type, double seconds) override;
    void launchMissionComplete(int kind) override;
    void startLoadSequence() override;
    void startDeleteSequence() override;
    void launchMissionFailed(std::string_view reason) override;

  private:
    // The sheet of sprite-sheet table record `record`, through the loader set, or an error without one.
    [[nodiscard]] std::expected<graphics::SpriteSheet, Error> loadRecord(std::uint32_t record) const;

    GameModeStack& m_stack;
    std::function<void(std::string_view)> m_log;
    GameState m_state;
    script::RecordedCalls m_recorded;
    CreatedHumans m_humans;
    world_objects::WorldFlags m_flags;
    script::MessageHandlers m_messages;
    world_objects::VolumeBoxes m_boxes;
    script::AnimCallbacks m_animCallbacks;
    world_objects::ObjectTypes m_objectTypes;
    world_objects::SpawnRecords m_spawnRecords;
    gui::RumbleData m_rumbleData; // the Rumble menu's lists, which its chunks build
    FrontEndServices m_services;
    std::unique_ptr<ProfileStore> m_profiles;
    graphics::ScreenFade m_fade;
    script::BindingContext m_context;
    bool m_hasScripts;
    script::ScriptSystem m_scripts; // after everything its bindings refer to
    ProfileManagerMode m_profileManager;
    GameplayMode m_gameplay;
    LevelFlowMode m_levelFlow;
    MissionCompleteMode m_missionComplete;
    PauseRecordLoader m_loadRecord; // the sheet-table records' loader, which the two modes below call through
    PauseMode m_pause;
    MissionFailedMode m_missionFailed;
    RumbleMenuMode m_rumbleMenu;
    MemoryCardMode m_memoryCard;
    LegalScreenMode m_legal;
};

} // namespace coney
