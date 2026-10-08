// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "effects/particles.h"
#include "gui/global_strings.h"
#include "gui/rumble_mode_gui/rumble_data.h"
#include "hud/hud.h"
#include "scripting/anim_callbacks.h"
#include "scripting/message_handlers.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"
#include "world_objects/volume_boxes.h"

namespace coney {

/// Where a level starts player 1: what the level's own script created for the checkpoint. No data file holds a player
/// start; the level script makes player 1 with `HuCreate` while `InitLevel` runs it, at a literal position or at a
/// flag, and may teleport him onto a flag before play starts (docs/research/characters.md#level-starts).
struct LevelStart {
    std::string level;  ///< The level (`level99`).
    int checkpoint = 1; ///< The checkpoint the script read with `GetCheckPoint()`.
    /// Player 1 as the scripts left him (HumanCreation::teleported set when a `TeleportToFlag` moved him); nothing when
    /// the scripts made no player 1 with a position.
    std::optional<HumanCreation> player;
};

/// What running a level's scripts for its start did, for the log and the tests.
struct LevelScriptRun {
    LevelStart start;
    std::uint64_t scriptErrors = 0; ///< Script errors in the run (ScriptSystem::errors()).
    std::uint64_t skippedCalls = 0; ///< Calls of bindings Coney lacks in the run (ScriptSystem::skippedCalls()).
    std::size_t humans = 0;         ///< Humans the scripts created.
    std::size_t flags = 0;          ///< World flags the scripts and InitLevel made.
    /// What the preloads' recording stubs kept (`CfgSetTurnRates` and the other configuration Coney applies itself).
    script::RecordedCalls recorded;
};

/// The names of the two flags `InitLevel` adds after the level script, at the origin with heading 0
/// (docs/research/flags.md#sources).
inline constexpr std::string_view kCrimeSceneFlag = "CrimeScene";
inline constexpr std::string_view kGangCallFlag = "GangCall";

/// InitLevel's script steps for `level` in `scripts`, whose state must exist: forgets the humans and flags of the
/// level before, runs `global.lua` then `<level>.lua` (ScriptSystem::enterLevel()), whose `HuCreate` and `AddFlag`
/// calls fill `humans` and `flags`; with `records`, adds the level's placed objects (`<level>_objs.txt`, a missing or
/// unreadable file logged and skipped) to them, and its emitter lines to `particles` when given; adds InitLevel's two
/// flags, then calls the start callback the script set
/// (`SetStartGameCallback`, kept in `state` and cleared once called), and returns player 1's start for the checkpoint
/// in `state` (`W_GameState + 0x33a`).
///
/// **Coney's choice:** the original calls the start callback at InitLevel's end, after the level's loading and preload
/// (step 13); Coney calls it right after the script steps, before the loading, because Coney places the player from
/// what the scripts made and nothing the callback reads is loaded by those steps in Coney.
///
/// Research: docs/research/level-loading.md#initlevel, docs/research/flags.md#player-starts
[[nodiscard]] LevelStart runLevelScript(script::ScriptSystem& scripts, GameState& state, CreatedHumans& humans,
                                        world_objects::WorldFlags& flags, std::string_view level,
                                        world_objects::SpawnRecords* records = nullptr,
                                        effects::ParticleSystems* particles = nullptr);

/// What a level run alone (runLevelScriptAlone()) starts with that the menus would otherwise have set.
struct LevelScriptOptions {
    /// The game's random table, read from the player's disc (GameRandom); empty: Coney's stand-in generator.
    std::span<const std::uint32_t> randomTable;
    /// The Rumble menu's set-up an arena reads (`GetRumbleModeData`); nothing leaves it all 0, unless rumbleArena is
    /// set.
    std::optional<RumbleSetup> rumble;
    /// When set (and rumble is not), the set-up is the one the Rumble menu leaves by default with this arena's level
    /// number: the menu's chunks run after the preloads (gui::rumbleMenuDefaults()).
    std::optional<int> rumbleArena;
    /// The game's sound, given to the bindings before the preloads run, so their configuration (the music tracks, the
    /// interface cues, the voices and the sound matrix) reaches it; null: none (it may still be given later).
    script::SoundHost* sound = nullptr;
};

/// What the bindings ask of the game while a level's scripts run without the menus: there are no menus, modes or
/// audio, so most requests are dropped. The two that change the level are kept for whoever runs the level to act on
/// between frames (the subsystem tests' harnesses): `MenuLoadLevel` (a hub
/// mission's start, `runNextMission`) and `HUDLaunchMissionComplete` (a mission's end). The mission's end
/// (`HUDLaunchMissionComplete`, `HUDLaunchMissionFailed`) is also logged, so a level played on its own says how it
/// ended. A movie is not played.
class QuietBindingHost final : public script::BindingHost {
  public:
    /// Where the mission's end is logged (none when empty).
    void setLog(std::function<void(std::string_view)> log) { m_log = std::move(log); }
    /// Keeps and logs the kind; a launch made by the mission-complete work itself (`runNextMission`'s kind 4, while
    /// setUnlocking() holds) is not a new end.
    void launchMissionComplete(int kind) override;
    /// Keeps and logs the reason.
    void launchMissionFailed(std::string_view reason) override;
    /// The kind `HUDLaunchMissionComplete` passed, once it has been called.
    [[nodiscard]] const std::optional<int>& missionComplete() const { return m_complete; }
    /// The reason `HUDLaunchMissionFailed` passed, once it has been called.
    [[nodiscard]] const std::optional<std::string>& missionFailed() const { return m_failed; }
    void showProfileManager(std::string_view /*onRumble*/, std::string_view /*onStartGame*/) override {}
    void showRumbleModeInterface(std::string_view /*onCancel*/, std::string_view /*onStart*/,
                                 double /*players*/) override {}
    void menuLoadLevel(std::string_view level) override { m_nextLevel = std::string(level); }
    void playMovie(std::string_view /*name*/) override {}
    void playMusic(std::string_view /*track*/) override {}
    void stopMusic() override {}
    void queueScreenEffect(int /*type*/, double /*seconds*/) override {}

    /// Whether a script has ended the mission (`HUDLaunchMissionComplete`) since the last clearMissionComplete().
    [[nodiscard]] bool missionCompleteRequested() const { return m_missionComplete; }
    /// Forgets the end of the mission.
    void clearMissionComplete() { m_missionComplete = false; }
    /// Marks the mission-complete work (`UnlockAndLoad`) as running, so the launch it makes itself is not a new end.
    void setUnlocking(bool on) { m_unlocking = on; }
    /// The level a script asked for (`MenuLoadLevel`) and not yet taken; nothing when none.
    [[nodiscard]] const std::optional<std::string>& nextLevel() const { return m_nextLevel; }
    /// Forgets the level asked for.
    void clearNextLevel() { m_nextLevel.reset(); }

  private:
    std::function<void(std::string_view)> m_log;
    std::optional<int> m_complete;
    std::optional<std::string> m_failed;
    bool m_missionComplete = false;
    bool m_unlocking = false;
    std::optional<std::string> m_nextLevel;
};

/// The story's way into a level without the menus, for the tests of one subsystem (the game itself starts a level in
/// platform::GameSession): what the level's scripts work on (a
/// game state, the strings and configuration the preloads fill, the humans, flags and binding context, with the
/// bindings' requests dropped) and a script system that has run what the original runs before a level's script, in its
/// order: the preloads (the legal screen's, which fill the level table), then a fresh Lua state (the front end's
/// unload), then `SetCheckPoint(checkpoint)` and the level's index, as `runNextMission` leaves them. Gameplay
/// (GameplayMode over these) then enters the level as the level flow would. Scripts are read through `source`; `log`
/// gets the scripts' lines.
///
/// Research: docs/research/level-loading.md#story-into-level99, docs/research/scripting.md#life-of-the-lua-state
class LevelScripts {
  public:
    LevelScripts(const script::ScriptSource& source, std::string_view level, int checkpoint,
                 const std::function<void(std::string_view)>& log, const LevelScriptOptions& options = {});
    LevelScripts(const LevelScripts&) = delete;
    LevelScripts& operator=(const LevelScripts&) = delete;
    LevelScripts(LevelScripts&&) = delete;
    LevelScripts& operator=(LevelScripts&&) = delete;
    ~LevelScripts() = default;

    [[nodiscard]] script::ScriptSystem& scripts() { return m_scripts; }
    [[nodiscard]] script::BindingContext& context() { return m_context; }
    [[nodiscard]] GameState& state() { return m_state; }
    [[nodiscard]] CreatedHumans& humans() { return m_humans; }
    [[nodiscard]] world_objects::WorldFlags& flags() { return m_flags; }
    [[nodiscard]] world_objects::ObjectTypes& objectTypes() { return m_objectTypes; }
    [[nodiscard]] world_objects::SpawnRecords& spawnRecords() { return m_spawnRecords; }
    [[nodiscard]] script::RecordedCalls& recorded() { return m_recorded; }
    /// The HUD the scripts' HUD bindings act on, for the play mode to draw (PlayLevelMode::useHud()).
    [[nodiscard]] hud::Hud& hud() { return m_hud; }
    /// The bindings' host: what the scripts asked of the game that the runner of the level acts on.
    [[nodiscard]] QuietBindingHost& host() { return m_host; }

    /// Ends the mission as the mission-complete mode (0xb) does: the scripts' `UnlockAndLoad` (the unlocks, then
    /// `runNextMission(1)`, which sets the checkpoint and asks for the next level through `host()`), then both players'
    /// mission money banked. False when the scripts have no `UnlockAndLoad` (the level's scripts did not load).
    ///
    /// Research: docs/research/scripting.md#run-next-mission
    bool completeMission();
    /// Hands what a story keeps from one level to the next (the saved progress: the unlocks, the bank, the script
    /// flags and numbers) to `next`, a fresh LevelScripts for the next level.
    void carryProgressTo(LevelScripts& next) const;

  private:
    GameState m_state;
    gui::GlobalStrings m_strings;
    script::RecordedCalls m_recorded;
    CreatedHumans m_humans;
    world_objects::WorldFlags m_flags;
    script::MessageHandlers m_messages;
    world_objects::VolumeBoxes m_boxes;
    script::AnimCallbacks m_animCallbacks;
    world_objects::ObjectTypes m_objectTypes;
    world_objects::SpawnRecords m_spawnRecords;
    QuietBindingHost m_host;
    gui::RumbleData m_rumbleData;
    hud::Hud m_hud;
    script::BindingContext m_context;
    script::ScriptSystem m_scripts; // after everything its bindings refer to
};

/// Sets the player's turning as the preload scripts configure it (`CfgSetTurnRates`, `CfgTurnRate` in
/// config_preload2.lua, recorded by their stubs): the values the original plays with
/// (docs/research/characters.md#movement-constants). The settings are global; a level played on its own applies them
/// once its scripts are ready.
void applyTurnConfig(const script::RecordedCalls& recorded);

/// The story's way into `level` at `checkpoint` without the menus, run once: a script system of its own
/// running what the original runs before a level's script, in its order: the preloads (the legal screen's, which fill
/// the level table), then a fresh Lua state (the front end's unload), then `SetCheckPoint(checkpoint)`, the level's
/// index and runLevelScript(); then the scripts' frames of the first second of play (**Coney's choice**, so that what
/// the start schedules, such as the hub's walk 100 ms in, happens before the play mode places the player). Scripts are
/// read through `source`; `log` gets the scripts' lines. Script errors are counted, not fatal: the start is whatever
/// the scripts got to.
///
/// Research: docs/research/level-loading.md#story-into-level99, docs/research/scripting.md#life-of-the-lua-state
[[nodiscard]] LevelScriptRun runLevelScriptAlone(const script::ScriptSource& source, std::string_view level,
                                                 int checkpoint, const std::function<void(std::string_view)>& log,
                                                 const LevelScriptOptions& options = {});

} // namespace coney
